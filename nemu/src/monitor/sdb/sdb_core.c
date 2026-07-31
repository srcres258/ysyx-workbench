#include "sdb_core.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  TK_EOF = 0,
  TK_INT,
  TK_CHAR,
  TK_BOOL,
  TK_IDENT,
  TK_REG,
  TK_LPAREN,
  TK_RPAREN,
  TK_LBRACK,
  TK_RBRACK,
  TK_QMARK,
  TK_COLON,
  TK_COMMA,
  TK_PLUS,
  TK_MINUS,
  TK_STAR,
  TK_SLASH,
  TK_PERCENT,
  TK_BANG,
  TK_TILDE,
  TK_AMP,
  TK_PIPE,
  TK_CARET,
  TK_LT,
  TK_GT,
  TK_EQEQ,
  TK_NEQ,
  TK_LTE,
  TK_GTE,
  TK_ANDAND,
  TK_OROR,
  TK_SHL,
  TK_SHR,
} TokenType;

typedef struct {
  TokenType type;
  size_t begin;
  size_t end;
  char *text;
  uint64_t u64;
  bool b;
} Token;

typedef enum {
  AST_INT,
  AST_CHAR,
  AST_BOOL,
  AST_REG,
  AST_SYM,
  AST_UNARY,
  AST_BINARY,
  AST_COND,
  AST_MEM,
} AstKind;

typedef enum {
  UOP_POS,
  UOP_NEG,
  UOP_LNOT,
  UOP_BNOT,
  UOP_DEREF,
} UnaryOp;

typedef enum {
  BOP_MUL,
  BOP_DIV,
  BOP_MOD,
  BOP_ADD,
  BOP_SUB,
  BOP_SHL,
  BOP_SHR,
  BOP_LT,
  BOP_LTE,
  BOP_GT,
  BOP_GTE,
  BOP_EQ,
  BOP_NEQ,
  BOP_BAND,
  BOP_BXOR,
  BOP_BOR,
  BOP_ANDAND,
  BOP_OROR,
} BinaryOp;

typedef enum {
  MEM_SPACE_DEFAULT = 0,
  MEM_SPACE_MEM,
  MEM_SPACE_PMEM,
} MemSpace;

typedef struct SdbExpr {
  AstKind kind;
  size_t begin;
  size_t end;
  unsigned width;
  bool is_signed;
  union {
    uint64_t u64;
    bool b;
    char *text;
    struct {
      UnaryOp op;
      struct SdbExpr *child;
    } unary;
    struct {
      BinaryOp op;
      struct SdbExpr *lhs;
      struct SdbExpr *rhs;
    } binary;
    struct {
      struct SdbExpr *cond;
      struct SdbExpr *then_expr;
      struct SdbExpr *else_expr;
    } cond;
    struct {
      MemSpace space;
      struct SdbExpr *addr;
      unsigned read_width;
      bool read_signed;
      bool force_mmio;
    } mem;
  } u;
} SdbExpr;

static char *sdb_strdup_range(const char *s, size_t begin, size_t end) {
  if (end < begin) {
    return NULL;
  }
  size_t len = end - begin;
  char *r = (char *)malloc(len + 1);
  if (!r) {
    return NULL;
  }
  memcpy(r, s + begin, len);
  r[len] = '\0';
  return r;
}

void sdb_error_clear(SdbError *err) {
  if (!err) {
    return;
  }
  memset(err, 0, sizeof(*err));
}

void sdb_error_set(
  SdbError *err, SdbErrorCode code, size_t begin, size_t end,
  const char *message, const char *platform
) {
  if (!err) {
    return;
  }
  err->code = code;
  err->begin = begin;
  err->end = end;
  if (message) {
    strncpy(err->message, message, sizeof(err->message) - 1);
    err->message[sizeof(err->message) - 1] = '\0';
  } else {
    err->message[0] = '\0';
  }
  if (platform) {
    strncpy(err->platform, platform, sizeof(err->platform) - 1);
    err->platform[sizeof(err->platform) - 1] = '\0';
  } else {
    err->platform[0] = '\0';
  }
}

void sdb_error_setf(
  SdbError *err, SdbErrorCode code, size_t begin, size_t end,
  const char *platform, const char *fmt, ...
) {
  if (!err) {
    return;
  }
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  sdb_error_set(err, code, begin, end, buf, platform);
}

SdbValue sdb_value_make(uint64_t bits, unsigned width, bool is_signed) {
  if (width == 0 || width > 64) {
    width = 64;
  }
  SdbValue v = { .bits = sdb_value_truncate(bits, width), .width = width, .is_signed = is_signed };
  return v;
}

uint64_t sdb_value_mask(unsigned width) {
  if (width >= 64) {
    return UINT64_MAX;
  }
  if (width == 0) {
    return 0;
  }
  return (1ULL << width) - 1ULL;
}

uint64_t sdb_value_truncate(uint64_t bits, unsigned width) {
  return bits & sdb_value_mask(width);
}

bool sdb_value_truthy(SdbValue value) {
  return sdb_value_truncate(value.bits, value.width ? value.width : 64) != 0;
}

int64_t sdb_value_as_i64(SdbValue value) {
  unsigned width = value.width ? value.width : 64;
  uint64_t bits = sdb_value_truncate(value.bits, width);
  if (width >= 64) {
    return (int64_t)bits;
  }
  uint64_t sign = 1ULL << (width - 1);
  if (bits & sign) {
    return (int64_t)(bits | ~sdb_value_mask(width));
  }
  return (int64_t)bits;
}

uint64_t sdb_value_as_u64(SdbValue value) {
  return sdb_value_truncate(value.bits, value.width ? value.width : 64);
}

const char *sdb_error_code_name(SdbErrorCode code) {
  switch (code) {
    case SDB_ERR_NONE: return "none";
    case SDB_ERR_LEXICAL: return "lexical error";
    case SDB_ERR_UNEXPECTED_TOKEN: return "unexpected token";
    case SDB_ERR_UNEXPECTED_EOF: return "unexpected end";
    case SDB_ERR_UNMATCHED_PAREN: return "unmatched parenthesis";
    case SDB_ERR_INVALID_LITERAL: return "invalid literal";
    case SDB_ERR_UNKNOWN_REGISTER: return "unknown register";
    case SDB_ERR_UNKNOWN_SYMBOL: return "unknown symbol";
    case SDB_ERR_DIVIDE_BY_ZERO: return "divide by zero";
    case SDB_ERR_MODULO_BY_ZERO: return "modulo by zero";
    case SDB_ERR_INVALID_SHIFT: return "invalid shift amount";
    case SDB_ERR_MEMORY_FAULT: return "memory access fault";
    case SDB_ERR_MMIO_SIDE_EFFECT: return "MMIO side-effect refusal";
    case SDB_ERR_UNSUPPORTED: return "unsupported operation";
    case SDB_ERR_OVERFLOW: return "integer overflow";
    case SDB_ERR_RESOURCE_EXHAUSTED: return "resource exhausted";
    case SDB_ERR_MALFORMED_COMMAND: return "malformed command";
  }
  return "error";
}

static bool token_vec_push(Token **tokens, size_t *count, size_t *cap, Token tok, SdbError *err) {
  if (*count >= *cap) {
    size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
    if (new_cap < *cap || new_cap > 4096) {
      sdb_error_set(err, SDB_ERR_RESOURCE_EXHAUSTED, tok.begin, tok.end, "too many tokens", "lexer");
      return false;
    }
    Token *new_tokens = (Token *)realloc(*tokens, new_cap * sizeof(Token));
    if (!new_tokens) {
      sdb_error_set(err, SDB_ERR_RESOURCE_EXHAUSTED, tok.begin, tok.end, "token buffer exhausted", "lexer");
      return false;
    }
    *tokens = new_tokens;
    *cap = new_cap;
  }
  (*tokens)[(*count)++] = tok;
  return true;
}

static bool is_ident_start(char c) {
  return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_char(char c) {
  return isalnum((unsigned char)c) || c == '_';
}

static bool parse_char_literal(const char *s, size_t len, size_t *consumed, uint64_t *value, SdbError *err, size_t begin) {
  if (len < 2 || s[0] != '\'') {
    sdb_error_set(err, SDB_ERR_INVALID_LITERAL, begin, begin + len, "invalid character literal", "lexer");
    return false;
  }
  size_t i = 1;
  unsigned char ch;
  if (i >= len) {
    sdb_error_set(err, SDB_ERR_UNEXPECTED_EOF, begin, begin + len, "unterminated character literal", "lexer");
    return false;
  }
  if (s[i] == '\\') {
    ++i;
    if (i >= len) {
      sdb_error_set(err, SDB_ERR_UNEXPECTED_EOF, begin, begin + len, "unterminated escape sequence", "lexer");
      return false;
    }
    switch (s[i]) {
      case '\\': ch = '\\'; break;
      case '\'': ch = '\''; break;
      case '"': ch = '"'; break;
      case 'n': ch = '\n'; break;
      case 't': ch = '\t'; break;
      case 'r': ch = '\r'; break;
      case '0': ch = '\0'; break;
      case 'b': ch = '\b'; break;
      case 'f': ch = '\f'; break;
      case 'v': ch = '\v'; break;
      case 'x': {
        ++i;
        unsigned v = 0;
        unsigned digits = 0;
        while (i < len && isxdigit((unsigned char)s[i]) && digits < 2) {
          v = v * 16 + (unsigned)(isdigit((unsigned char)s[i]) ? s[i] - '0' : (tolower((unsigned char)s[i]) - 'a' + 10));
          ++i;
          ++digits;
        }
        if (digits == 0) {
          sdb_error_set(err, SDB_ERR_INVALID_LITERAL, begin, begin + i, "invalid hex escape", "lexer");
          return false;
        }
        if (i >= len || s[i] != '\'') {
          sdb_error_set(err, SDB_ERR_UNEXPECTED_EOF, begin, begin + i, "unterminated character literal", "lexer");
          return false;
        }
        *consumed = i + 1;
        *value = v & 0xffu;
        return true;
      }
      default:
        ch = (unsigned char)s[i];
        break;
    }
    ++i;
  } else {
    ch = (unsigned char)s[i++];
  }
  if (i >= len || s[i] != '\'') {
    sdb_error_set(err, SDB_ERR_INVALID_LITERAL, begin, begin + i, "unterminated character literal", "lexer");
    return false;
  }
  *consumed = i + 1;
  *value = ch;
  return true;
}

static bool parse_number_literal(const char *s, size_t len, size_t *consumed, uint64_t *value, SdbError *err, size_t begin) {
  size_t i = 0;
  int base = 10;
  bool seen_digit = false;
  char buf[128];
  size_t out = 0;

  if (len >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    i = 2;
  } else if (len >= 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
    base = 2;
    i = 2;
  } else if (len >= 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) {
    base = 8;
    i = 2;
  } else if (len > 1 && s[0] == '0' && isdigit((unsigned char)s[1])) {
    base = 8;
    i = 1;
  }

  while (i < len) {
    char c = s[i];
    if (c == '_') {
      ++i;
      continue;
    }
    bool ok = false;
    if (base == 2) {
      ok = (c == '0' || c == '1');
    } else if (base == 8) {
      ok = (c >= '0' && c <= '7');
    } else if (base == 10) {
      ok = isdigit((unsigned char)c);
    } else {
      ok = isxdigit((unsigned char)c);
    }
    if (!ok) {
      break;
    }
    seen_digit = true;
    if (out + 1 >= sizeof(buf)) {
      sdb_error_set(err, SDB_ERR_RESOURCE_EXHAUSTED, begin, begin + i + 1, "numeric literal too long", "lexer");
      return false;
    }
    buf[out++] = c;
    ++i;
  }

  if (!seen_digit) {
    sdb_error_set(err, SDB_ERR_INVALID_LITERAL, begin, begin + i, "invalid numeric literal", "lexer");
    return false;
  }
  buf[out] = '\0';
  errno = 0;
  char *endptr = NULL;
  unsigned long long v = strtoull(buf, &endptr, base);
  if (errno == ERANGE || endptr == NULL || *endptr != '\0') {
    sdb_error_set(err, SDB_ERR_OVERFLOW, begin, begin + i, "numeric literal overflow", "lexer");
    return false;
  }
  *consumed = i;
  *value = (uint64_t)v;
  return true;
}

static bool lex_input(const char *text, Token **out_tokens, size_t *out_count, SdbError *err) {
  Token *tokens = NULL;
  size_t count = 0, cap = 0;
  size_t i = 0;
  size_t len = strlen(text);

  while (i < len) {
    unsigned char c = (unsigned char)text[i];
    if (isspace(c)) {
      ++i;
      continue;
    }

    Token tok = {0};
    tok.begin = i;
    tok.end = i + 1;

    if (c == '\'') {
      size_t consumed = 0;
      uint64_t v = 0;
      if (!parse_char_literal(text + i, len - i, &consumed, &v, err, i)) {
        free(tokens);
        return false;
      }
      tok.type = TK_CHAR;
      tok.u64 = v;
      tok.end = i + consumed;
      if (!token_vec_push(&tokens, &count, &cap, tok, err)) {
        free(tokens);
        return false;
      }
      i += consumed;
      continue;
    }

    if (isdigit(c)) {
      size_t consumed = 0;
      uint64_t v = 0;
      if (!parse_number_literal(text + i, len - i, &consumed, &v, err, i)) {
        free(tokens);
        return false;
      }
      tok.type = TK_INT;
      tok.u64 = v;
      tok.end = i + consumed;
      if (!token_vec_push(&tokens, &count, &cap, tok, err)) {
        free(tokens);
        return false;
      }
      i += consumed;
      continue;
    }

    if (c == '$') {
      size_t j = i + 1;
      if (j >= len || (!is_ident_start(text[j]) && !isdigit((unsigned char)text[j]))) {
        sdb_error_set(err, SDB_ERR_LEXICAL, i, j, "invalid register token", "lexer");
        free(tokens);
        return false;
      }
      while (j < len && is_ident_char(text[j])) {
        ++j;
      }
      tok.type = TK_REG;
      tok.text = sdb_strdup_range(text, i + 1, j);
      if (!tok.text) {
        sdb_error_set(err, SDB_ERR_RESOURCE_EXHAUSTED, i, j, "out of memory", "lexer");
        free(tokens);
        return false;
      }
      tok.end = j;
      if (!token_vec_push(&tokens, &count, &cap, tok, err)) {
        free(tok.text);
        free(tokens);
        return false;
      }
      i = j;
      continue;
    }

    if (is_ident_start((char)c)) {
      size_t j = i + 1;
      while (j < len && is_ident_char(text[j])) {
        ++j;
      }
      tok.type = TK_IDENT;
      tok.text = sdb_strdup_range(text, i, j);
      if (!tok.text) {
        sdb_error_set(err, SDB_ERR_RESOURCE_EXHAUSTED, i, j, "out of memory", "lexer");
        free(tokens);
        return false;
      }
      if (strcmp(tok.text, "true") == 0 || strcmp(tok.text, "false") == 0) {
        tok.type = TK_BOOL;
        tok.b = strcmp(tok.text, "true") == 0;
      }
      tok.end = j;
      if (!token_vec_push(&tokens, &count, &cap, tok, err)) {
        free(tok.text);
        free(tokens);
        return false;
      }
      i = j;
      continue;
    }

    #define TWO(ch1, ch2, ttype) do { \
      if (i + 1 < len && text[i] == (ch1) && text[i + 1] == (ch2)) { \
        tok.type = (ttype); tok.end = i + 2; \
        if (!token_vec_push(&tokens, &count, &cap, tok, err)) { free(tokens); return false; } \
        i += 2; \
        goto lex_next; \
      } \
    } while (0)

    TWO('&', '&', TK_ANDAND);
    TWO('|', '|', TK_OROR);
    TWO('=', '=', TK_EQEQ);
    TWO('!', '=', TK_NEQ);
    TWO('<', '=', TK_LTE);
    TWO('>', '=', TK_GTE);
    TWO('<', '<', TK_SHL);
    TWO('>', '>', TK_SHR);
    #undef TWO

    switch (c) {
      case '(': tok.type = TK_LPAREN; break;
      case ')': tok.type = TK_RPAREN; break;
      case '[': tok.type = TK_LBRACK; break;
      case ']': tok.type = TK_RBRACK; break;
      case '?': tok.type = TK_QMARK; break;
      case ':': tok.type = TK_COLON; break;
      case ',': tok.type = TK_COMMA; break;
      case '+': tok.type = TK_PLUS; break;
      case '-': tok.type = TK_MINUS; break;
      case '*': tok.type = TK_STAR; break;
      case '/': tok.type = TK_SLASH; break;
      case '%': tok.type = TK_PERCENT; break;
      case '!': tok.type = TK_BANG; break;
      case '~': tok.type = TK_TILDE; break;
      case '&': tok.type = TK_AMP; break;
      case '|': tok.type = TK_PIPE; break;
      case '^': tok.type = TK_CARET; break;
      case '<': tok.type = TK_LT; break;
      case '>': tok.type = TK_GT; break;
      default:
        sdb_error_setf(err, SDB_ERR_LEXICAL, i, i + 1, "lexer", "unexpected character '%c'", c);
        free(tokens);
        return false;
    }
    if (!token_vec_push(&tokens, &count, &cap, tok, err)) {
      free(tokens);
      return false;
    }
    ++i;
  lex_next:
    ;
  }

  Token eof = { .type = TK_EOF, .begin = len, .end = len };
  if (!token_vec_push(&tokens, &count, &cap, eof, err)) {
    free(tokens);
    return false;
  }

  *out_tokens = tokens;
  *out_count = count;
  return true;
}

static SdbExpr *new_expr(AstKind kind, size_t begin, size_t end) {
  SdbExpr *expr = (SdbExpr *)calloc(1, sizeof(SdbExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = kind;
  expr->begin = begin;
  expr->end = end;
  expr->width = 64;
  expr->is_signed = false;
  return expr;
}

static void free_expr_tree(SdbExpr *expr) {
  if (!expr) {
    return;
  }
  switch (expr->kind) {
    case AST_SYM:
    case AST_REG:
      free(expr->u.text);
      break;
    case AST_UNARY:
      free_expr_tree(expr->u.unary.child);
      break;
    case AST_BINARY:
      free_expr_tree(expr->u.binary.lhs);
      free_expr_tree(expr->u.binary.rhs);
      break;
    case AST_COND:
      free_expr_tree(expr->u.cond.cond);
      free_expr_tree(expr->u.cond.then_expr);
      free_expr_tree(expr->u.cond.else_expr);
      break;
    case AST_MEM:
      free_expr_tree(expr->u.mem.addr);
      break;
    default:
      break;
  }
  free(expr);
}

typedef struct {
  Token *tokens;
  size_t count;
  size_t pos;
  const char *source;
  SdbError *err;
} Parser;

static Token *peek(Parser *p) {
  if (p->pos >= p->count) {
    return &p->tokens[p->count - 1];
  }
  return &p->tokens[p->pos];
}

static Token *prev(Parser *p) {
  if (p->pos == 0) {
    return &p->tokens[0];
  }
  return &p->tokens[p->pos - 1];
}

static bool match(Parser *p, TokenType type) {
  if (peek(p)->type != type) {
    return false;
  }
  ++p->pos;
  return true;
}

static bool expect(Parser *p, TokenType type, const char *msg) {
  if (match(p, type)) {
    return true;
  }
  Token *t = peek(p);
  sdb_error_set(
    p->err, t->type == TK_EOF ? SDB_ERR_UNEXPECTED_EOF : SDB_ERR_UNEXPECTED_TOKEN,
    t->begin, t->end, msg, "parser"
  );
  return false;
}

static SdbExpr *parse_expression(Parser *p);

static bool token_is_mem_keyword(Token *t, unsigned *width, bool *is_signed, MemSpace *space) {
  if (t->type != TK_IDENT) {
    return false;
  }
  if (strcmp(t->text, "mem") == 0) {
    *width = 0;
    *is_signed = false;
    *space = MEM_SPACE_MEM;
    return true;
  }
  if (strcmp(t->text, "pmem") == 0) {
    *width = 0;
    *is_signed = false;
    *space = MEM_SPACE_PMEM;
    return true;
  }
  struct { const char *name; unsigned width; bool is_signed; } table[] = {
    {"u8", 8, false}, {"u16", 16, false}, {"u32", 32, false}, {"u64", 64, false},
    {"i8", 8, true}, {"i16", 16, true}, {"i32", 32, true}, {"i64", 64, true},
    {"pmem8", 8, false}, {"pmem16", 16, false}, {"pmem32", 32, false}, {"pmem64", 64, false},
  };
  for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); ++i) {
    if (strcmp(t->text, table[i].name) == 0) {
      *width = table[i].width;
      *is_signed = table[i].is_signed;
      *space = (strncmp(table[i].name, "pmem", 4) == 0) ? MEM_SPACE_PMEM : MEM_SPACE_MEM;
      return true;
    }
  }
  return false;
}

static SdbExpr *parse_primary(Parser *p) {
  Token *t = peek(p);
  if (t->type == TK_INT) {
    ++p->pos;
    SdbExpr *expr = new_expr(AST_INT, t->begin, t->end);
    if (!expr) {
      sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, t->end, "out of memory", "parser");
      return NULL;
    }
    expr->u.u64 = t->u64;
    return expr;
  }
  if (t->type == TK_CHAR) {
    ++p->pos;
    SdbExpr *expr = new_expr(AST_CHAR, t->begin, t->end);
    if (!expr) {
      sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, t->end, "out of memory", "parser");
      return NULL;
    }
    expr->u.u64 = t->u64;
    expr->width = 8;
    return expr;
  }
  if (t->type == TK_BOOL) {
    ++p->pos;
    SdbExpr *expr = new_expr(AST_BOOL, t->begin, t->end);
    if (!expr) {
      sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, t->end, "out of memory", "parser");
      return NULL;
    }
    expr->u.b = t->b;
    expr->width = 1;
    return expr;
  }
  if (t->type == TK_REG) {
    ++p->pos;
    SdbExpr *expr = new_expr(AST_REG, t->begin, t->end);
    if (!expr) {
      sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, t->end, "out of memory", "parser");
      return NULL;
    }
    expr->u.text = t->text;
    t->text = NULL;
    return expr;
  }
  if (t->type == TK_IDENT) {
    ++p->pos;
    SdbExpr *expr = new_expr(AST_SYM, t->begin, t->end);
    if (!expr) {
      sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, t->end, "out of memory", "parser");
      return NULL;
    }
    expr->u.text = t->text;
    t->text = NULL;
    return expr;
  }
  if (match(p, TK_LPAREN)) {
    SdbExpr *inside = parse_expression(p);
    if (!inside) {
      return NULL;
    }
    if (!expect(p, TK_RPAREN, "expected ')'")) {
      free_expr_tree(inside);
      return NULL;
    }
    inside->begin = t->begin;
    inside->end = prev(p)->end;
    return inside;
  }
  sdb_error_set(
    p->err, t->type == TK_EOF ? SDB_ERR_UNEXPECTED_EOF : SDB_ERR_UNEXPECTED_TOKEN,
    t->begin, t->end, "expected primary expression", "parser"
  );
  return NULL;
}

static SdbExpr *parse_unary(Parser *p) {
  Token *t = peek(p);
  if (t->type == TK_PLUS || t->type == TK_MINUS || t->type == TK_BANG || t->type == TK_TILDE || t->type == TK_STAR) {
    ++p->pos;
    SdbExpr *child = parse_unary(p);
    if (!child) {
      return NULL;
    }
    if (t->type == TK_STAR) {
      SdbExpr *expr = new_expr(AST_MEM, t->begin, child->end);
      if (!expr) {
        free_expr_tree(child);
        sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, child->end, "out of memory", "parser");
        return NULL;
      }
      expr->u.mem.space = MEM_SPACE_DEFAULT;
      expr->u.mem.addr = child;
      expr->u.mem.read_width = 0;
      expr->u.mem.read_signed = false;
      expr->u.mem.force_mmio = false;
      return expr;
    }
    SdbExpr *expr = new_expr(AST_UNARY, t->begin, child->end);
    if (!expr) {
      free_expr_tree(child);
      sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, child->end, "out of memory", "parser");
      return NULL;
    }
    expr->u.unary.child = child;
    expr->u.unary.op = (t->type == TK_PLUS) ? UOP_POS : (t->type == TK_MINUS) ? UOP_NEG : (t->type == TK_BANG) ? UOP_LNOT : UOP_BNOT;
    return expr;
  }

  if (t->type == TK_IDENT) {
    unsigned width = 0;
    bool is_signed = false;
    MemSpace space = MEM_SPACE_DEFAULT;
    if (token_is_mem_keyword(t, &width, &is_signed, &space)) {
      if (p->pos + 1 < p->count && p->tokens[p->pos + 1].type == TK_LBRACK) {
        ++p->pos;
        ++p->pos;
        SdbExpr *addr = parse_expression(p);
        if (!addr) {
          return NULL;
        }
        if (!expect(p, TK_RBRACK, "expected ']'")) {
          free_expr_tree(addr);
          return NULL;
        }
        SdbExpr *expr = new_expr(AST_MEM, t->begin, prev(p)->end);
        if (!expr) {
          free_expr_tree(addr);
          sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, t->begin, prev(p)->end, "out of memory", "parser");
          return NULL;
        }
        expr->u.mem.addr = addr;
        expr->u.mem.space = space;
        expr->u.mem.read_width = width ? width : 0;
        expr->u.mem.read_signed = is_signed;
        expr->u.mem.force_mmio = false;
        return expr;
      }
    }
  }

  return parse_primary(p);
}

static BinaryOp token_to_binary(TokenType t) {
  switch (t) {
    case TK_STAR: return BOP_MUL;
    case TK_SLASH: return BOP_DIV;
    case TK_PERCENT: return BOP_MOD;
    case TK_PLUS: return BOP_ADD;
    case TK_MINUS: return BOP_SUB;
    case TK_SHL: return BOP_SHL;
    case TK_SHR: return BOP_SHR;
    case TK_LT: return BOP_LT;
    case TK_LTE: return BOP_LTE;
    case TK_GT: return BOP_GT;
    case TK_GTE: return BOP_GTE;
    case TK_EQEQ: return BOP_EQ;
    case TK_NEQ: return BOP_NEQ;
    case TK_AMP: return BOP_BAND;
    case TK_CARET: return BOP_BXOR;
    case TK_PIPE: return BOP_BOR;
    case TK_ANDAND: return BOP_ANDAND;
    case TK_OROR: return BOP_OROR;
    default: return BOP_ADD;
  }
}

static int precedence(BinaryOp op) {
  switch (op) {
    case BOP_MUL:
    case BOP_DIV:
    case BOP_MOD: return 12;
    case BOP_ADD:
    case BOP_SUB: return 11;
    case BOP_SHL:
    case BOP_SHR: return 10;
    case BOP_LT:
    case BOP_LTE:
    case BOP_GT:
    case BOP_GTE: return 9;
    case BOP_EQ:
    case BOP_NEQ: return 8;
    case BOP_BAND: return 7;
    case BOP_BXOR: return 6;
    case BOP_BOR: return 5;
    case BOP_ANDAND: return 4;
    case BOP_OROR: return 3;
  }
  return 0;
}

static SdbExpr *parse_binary(Parser *p, int min_prec) {
  SdbExpr *lhs = parse_unary(p);
  if (!lhs) {
    return NULL;
  }
  while (1) {
    Token *t = peek(p);
    BinaryOp op;
    bool is_op = false;
    switch (t->type) {
      case TK_STAR: case TK_SLASH: case TK_PERCENT: case TK_PLUS: case TK_MINUS:
      case TK_SHL: case TK_SHR: case TK_LT: case TK_LTE: case TK_GT: case TK_GTE:
      case TK_EQEQ: case TK_NEQ: case TK_AMP: case TK_CARET: case TK_PIPE:
      case TK_ANDAND: case TK_OROR:
        op = token_to_binary(t->type);
        is_op = true;
        break;
      default:
        break;
    }
    if (!is_op || precedence(op) < min_prec) {
      break;
    }
    ++p->pos;
    int next_prec = precedence(op) + 1;
    SdbExpr *rhs = parse_binary(p, next_prec);
    if (!rhs) {
      free_expr_tree(lhs);
      return NULL;
    }
    SdbExpr *expr = new_expr(AST_BINARY, lhs->begin, rhs->end);
    if (!expr) {
      free_expr_tree(lhs);
      free_expr_tree(rhs);
      sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, lhs->begin, rhs->end, "out of memory", "parser");
      return NULL;
    }
    expr->u.binary.op = op;
    expr->u.binary.lhs = lhs;
    expr->u.binary.rhs = rhs;
    lhs = expr;
  }
  return lhs;
}

static SdbExpr *parse_conditional(Parser *p) {
  SdbExpr *cond = parse_binary(p, 3);
  if (!cond) {
    return NULL;
  }
  if (!match(p, TK_QMARK)) {
    return cond;
  }
  SdbExpr *then_expr = parse_expression(p);
  if (!then_expr) {
    free_expr_tree(cond);
    return NULL;
  }
  if (!expect(p, TK_COLON, "expected ':' in conditional expression")) {
    free_expr_tree(cond);
    free_expr_tree(then_expr);
    return NULL;
  }
  SdbExpr *else_expr = parse_conditional(p);
  if (!else_expr) {
    free_expr_tree(cond);
    free_expr_tree(then_expr);
    return NULL;
  }
  SdbExpr *expr = new_expr(AST_COND, cond->begin, else_expr->end);
  if (!expr) {
    free_expr_tree(cond);
    free_expr_tree(then_expr);
    free_expr_tree(else_expr);
    sdb_error_set(p->err, SDB_ERR_RESOURCE_EXHAUSTED, cond->begin, else_expr->end, "out of memory", "parser");
    return NULL;
  }
  expr->u.cond.cond = cond;
  expr->u.cond.then_expr = then_expr;
  expr->u.cond.else_expr = else_expr;
  return expr;
}

static SdbExpr *parse_expression(Parser *p) {
  return parse_conditional(p);
}

SdbExpr *sdb_expr_parse(const char *text, SdbError *err) {
  if (err) {
    sdb_error_clear(err);
  }
  if (!text) {
    sdb_error_set(err, SDB_ERR_INVALID_LITERAL, 0, 0, "null expression", "parser");
    return NULL;
  }
  Token *tokens = NULL;
  size_t count = 0;
  if (!lex_input(text, &tokens, &count, err)) {
    return NULL;
  }
  Parser p = { .tokens = tokens, .count = count, .pos = 0, .source = text, .err = err };
  SdbExpr *expr = parse_expression(&p);
  if (expr && peek(&p)->type != TK_EOF) {
    Token *t = peek(&p);
    sdb_error_set(err, SDB_ERR_UNEXPECTED_TOKEN, t->begin, t->end, "unexpected token after expression", "parser");
    free_expr_tree(expr);
    expr = NULL;
  }
  for (size_t i = 0; i < count; ++i) {
    free(tokens[i].text);
  }
  free(tokens);
  return expr;
}

void sdb_expr_free(SdbExpr *expr) {
  free_expr_tree(expr);
}

static SdbValue eval_expr(const SdbExpr *expr, const SdbTargetOps *ops, SdbEvalResult *result);

static bool eval_as_bool(const SdbExpr *expr, const SdbTargetOps *ops, bool *ok, SdbError *err) {
  SdbEvalResult r = {0};
  if (!sdb_expr_eval(expr, ops, &r)) {
    if (err) {
      *err = r.error;
    }
    *ok = false;
    return false;
  }
  *ok = true;
  return sdb_value_truthy(r.value);
}

static SdbValue eval_binary_arith(BinaryOp op, SdbValue lhs, SdbValue rhs, SdbError *err, size_t begin, size_t end) {
  unsigned width = lhs.width > rhs.width ? lhs.width : rhs.width;
  if (width == 0) {
    width = 64;
  }
  uint64_t l = sdb_value_truncate(lhs.bits, width);
  uint64_t r = sdb_value_truncate(rhs.bits, width);
  switch (op) {
    case BOP_MUL: return sdb_value_make(l * r, width, lhs.is_signed || rhs.is_signed);
    case BOP_ADD: return sdb_value_make(l + r, width, lhs.is_signed || rhs.is_signed);
    case BOP_SUB: return sdb_value_make(l - r, width, lhs.is_signed || rhs.is_signed);
    case BOP_BAND: return sdb_value_make(l & r, width, false);
    case BOP_BXOR: return sdb_value_make(l ^ r, width, false);
    case BOP_BOR: return sdb_value_make(l | r, width, false);
    case BOP_SHL:
      if (r >= width) {
        sdb_error_set(err, SDB_ERR_INVALID_SHIFT, begin, end, "invalid shift amount", "eval");
        return sdb_value_make(0, width, false);
      }
      return sdb_value_make(l << r, width, false);
    case BOP_SHR:
      if (r >= width) {
        sdb_error_set(err, SDB_ERR_INVALID_SHIFT, begin, end, "invalid shift amount", "eval");
        return sdb_value_make(0, width, false);
      }
      if (lhs.is_signed) {
        int64_t sl = sdb_value_as_i64(lhs);
        return sdb_value_make((uint64_t)(sl >> r), width, true);
      }
      return sdb_value_make(l >> r, width, false);
    default:
      break;
  }
  return sdb_value_make(0, width, false);
}

static SdbValue eval_binary_cmp(BinaryOp op, SdbValue lhs, SdbValue rhs) {
  bool signed_cmp = lhs.is_signed || rhs.is_signed;
  int res = 0;
  if (signed_cmp) {
    int64_t l = sdb_value_as_i64(lhs);
    int64_t r = sdb_value_as_i64(rhs);
    switch (op) {
      case BOP_LT: res = l < r; break;
      case BOP_LTE: res = l <= r; break;
      case BOP_GT: res = l > r; break;
      case BOP_GTE: res = l >= r; break;
      case BOP_EQ: res = l == r; break;
      case BOP_NEQ: res = l != r; break;
      default: break;
    }
  } else {
    uint64_t l = sdb_value_as_u64(lhs);
    uint64_t r = sdb_value_as_u64(rhs);
    switch (op) {
      case BOP_LT: res = l < r; break;
      case BOP_LTE: res = l <= r; break;
      case BOP_GT: res = l > r; break;
      case BOP_GTE: res = l >= r; break;
      case BOP_EQ: res = l == r; break;
      case BOP_NEQ: res = l != r; break;
      default: break;
    }
  }
  return sdb_value_make((uint64_t)res, 1, false);
}

static SdbValue eval_expr(const SdbExpr *expr, const SdbTargetOps *ops, SdbEvalResult *result) {
  if (!expr || !ops) {
    if (result) {
      sdb_error_set(&result->error, SDB_ERR_UNSUPPORTED, 0, 0, "missing expression or target ops", "eval");
      result->has_value = false;
    }
    return sdb_value_make(0, 64, false);
  }

  SdbValue out = sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
  switch (expr->kind) {
    case AST_INT:
      out = sdb_value_make(expr->u.u64, 64, false);
      break;
    case AST_CHAR:
      out = sdb_value_make(expr->u.u64, 8, false);
      break;
    case AST_BOOL:
      out = sdb_value_make(expr->u.b ? 1 : 0, 1, false);
      break;
    case AST_REG: {
      if (!ops->read_register) {
        if (result)
          sdb_error_set(&result->error, SDB_ERR_UNSUPPORTED, expr->begin, expr->end, "register read unsupported", "eval");
        break;
      }
      SdbValue regv = {0};
      if (!ops->read_register(ops->userdata, expr->u.text, &regv, result ? &result->error : NULL)) {
        if (result)
          result->has_value = false;
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      out = regv;
      break;
    }
    case AST_SYM: {
      uint64_t addr = 0;
      if (!ops->resolve_symbol) {
        if (result)
          sdb_error_set(&result->error, SDB_ERR_UNSUPPORTED, expr->begin, expr->end, "symbol lookup unsupported", "eval");
        break;
      }
      if (!ops->resolve_symbol(ops->userdata, expr->u.text, &addr, result ? &result->error : NULL)) {
        if (result)
          result->has_value = false;
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      out = sdb_value_make(addr, ops->xlen ? ops->xlen : 64, false);
      break;
    }
    case AST_UNARY: {
      SdbEvalResult child = {0};
      if (!sdb_expr_eval(expr->u.unary.child, ops, &child)) {
        if (result)
          result->error = child.error;
        if (result)
          result->has_value = false;
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      switch (expr->u.unary.op) {
        case UOP_POS:
          out = child.value;
          break;
        case UOP_NEG:
          out = sdb_value_make((uint64_t)(-(int64_t)sdb_value_as_i64(child.value)), child.value.width, true);
          break;
        case UOP_LNOT:
          out = sdb_value_make(!sdb_value_truthy(child.value), 1, false);
          break;
        case UOP_BNOT:
          out = sdb_value_make(~sdb_value_as_u64(child.value), child.value.width, false);
          break;
        case UOP_DEREF: {
          if (!ops->read_memory) {
            if (result)
              sdb_error_set(&result->error, SDB_ERR_UNSUPPORTED, expr->begin, expr->end, "memory read unsupported", "eval");
            return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
          }
          uint64_t addr = sdb_value_as_u64(child.value);
          unsigned width = ops->xlen ? ops->xlen : 64;
          SdbValue memv = {0};
          if (!ops->read_memory(ops->userdata, SDB_ADDR_MEM, addr, width, false, false, &memv, result ? &result->error : NULL)) {
            if (result) result->has_value = false;
            return sdb_value_make(0, width, false);
          }
          out = memv;
          break;
        }
      }
      break;
    }
    case AST_MEM: {
      SdbEvalResult addr_res = {0};
      if (!sdb_expr_eval(expr->u.mem.addr, ops, &addr_res)) {
        if (result)
          result->error = addr_res.error;
        if (result)
          result->has_value = false;
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      if (!ops->read_memory) {
        if (result)
          sdb_error_set(&result->error, SDB_ERR_UNSUPPORTED, expr->begin, expr->end, "memory read unsupported", "eval");
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      unsigned width = expr->u.mem.read_width ? expr->u.mem.read_width : (ops->xlen ? ops->xlen : 64);
      SdbAddressSpace space = expr->u.mem.space == MEM_SPACE_PMEM ? SDB_ADDR_PMEM : SDB_ADDR_MEM;
      if (!ops->read_memory(
        ops->userdata, space, sdb_value_as_u64(addr_res.value), width, expr->u.mem.read_signed,
        expr->u.mem.force_mmio, &out, result ? &result->error : NULL
      )) {
        if (result)
          result->has_value = false;
        return sdb_value_make(0, width, false);
      }
      break;
    }
    case AST_BINARY: {
      if (expr->u.binary.op == BOP_ANDAND || expr->u.binary.op == BOP_OROR) {
        bool l_ok = false;
        bool l = eval_as_bool(expr->u.binary.lhs, ops, &l_ok, result ? &result->error : NULL);
        if (!l_ok) {
          if (result)
            result->has_value = false;
          return sdb_value_make(0, 1, false);
        }
        if (expr->u.binary.op == BOP_ANDAND) {
          if (!l) {
            out = sdb_value_make(0, 1, false);
            break;
          }
          bool r_ok = false;
          bool r = eval_as_bool(expr->u.binary.rhs, ops, &r_ok, result ? &result->error : NULL);
          if (!r_ok) {
            if (result)
              result->has_value = false;
            return sdb_value_make(0, 1, false);
          }
          out = sdb_value_make(r, 1, false);
        } else {
          if (l) {
            out = sdb_value_make(1, 1, false);
            break;
          }
          bool r_ok = false;
          bool r = eval_as_bool(expr->u.binary.rhs, ops, &r_ok, result ? &result->error : NULL);
          if (!r_ok) {
            if (result)
              result->has_value = false;
            return sdb_value_make(0, 1, false);
          }
          out = sdb_value_make(r, 1, false);
        }
        break;
      }

      SdbEvalResult lhs_res = {0};
      SdbEvalResult rhs_res = {0};
      if (!sdb_expr_eval(expr->u.binary.lhs, ops, &lhs_res)) {
        if (result)
          result->error = lhs_res.error;
        if (result)
          result->has_value = false;
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      if (!sdb_expr_eval(expr->u.binary.rhs, ops, &rhs_res)) {
        if (result)
          result->error = rhs_res.error;
        if (result)
          result->has_value = false;
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      SdbValue lhs = lhs_res.value;
      SdbValue rhs = rhs_res.value;
      switch (expr->u.binary.op) {
        case BOP_MUL:
        case BOP_ADD:
        case BOP_SUB:
        case BOP_SHL:
        case BOP_SHR:
        case BOP_BAND:
        case BOP_BXOR:
        case BOP_BOR:
          out = eval_binary_arith(expr->u.binary.op, lhs, rhs, result ? &result->error : NULL, expr->begin, expr->end);
          break;
        case BOP_DIV: {
          uint64_t divisor = sdb_value_as_u64(rhs);
          if (divisor == 0) {
            if (result)
              sdb_error_set(
                &result->error, SDB_ERR_DIVIDE_BY_ZERO, expr->u.binary.rhs->begin, expr->u.binary.rhs->end,
                "divide by zero", "eval"
              );
            if (result)
              result->has_value = false;
            return sdb_value_make(0, lhs.width, lhs.is_signed);
          }
          out = sdb_value_make(sdb_value_as_u64(lhs) / divisor, lhs.width > rhs.width ? lhs.width : rhs.width, lhs.is_signed || rhs.is_signed);
          break;
        }
        case BOP_MOD: {
          uint64_t divisor = sdb_value_as_u64(rhs);
          if (divisor == 0) {
            if (result) sdb_error_set(&result->error, SDB_ERR_MODULO_BY_ZERO, expr->u.binary.rhs->begin, expr->u.binary.rhs->end,
                                      "modulo by zero", "eval");
            if (result) result->has_value = false;
            return sdb_value_make(0, lhs.width, lhs.is_signed);
          }
          out = sdb_value_make(sdb_value_as_u64(lhs) % divisor, lhs.width > rhs.width ? lhs.width : rhs.width, lhs.is_signed || rhs.is_signed);
          break;
        }
        case BOP_LT: case BOP_LTE: case BOP_GT: case BOP_GTE: case BOP_EQ: case BOP_NEQ:
          out = eval_binary_cmp(expr->u.binary.op, lhs, rhs);
          break;
        default:
          if (result)
            sdb_error_set(&result->error, SDB_ERR_UNSUPPORTED, expr->begin, expr->end, "unsupported operator", "eval");
          if (result)
            result->has_value = false;
          return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      break;
    }
    case AST_COND: {
      bool ok = false;
      bool cond = eval_as_bool(expr->u.cond.cond, ops, &ok, result ? &result->error : NULL);
      if (!ok) {
        if (result)
          result->has_value = false;
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      if (cond) {
        if (!sdb_expr_eval(expr->u.cond.then_expr, ops, result)) {
          return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
        }
        return result->value;
      }
      if (!sdb_expr_eval(expr->u.cond.else_expr, ops, result)) {
        return sdb_value_make(0, ops->xlen ? ops->xlen : 64, false);
      }
      return result->value;
    }
  }

  if (result) {
    result->value = out;
    result->has_value = true;
    if (result->error.code == SDB_ERR_NONE) {
      sdb_error_clear(&result->error);
    }
  }
  return out;
}

bool sdb_expr_eval(const SdbExpr *expr, const SdbTargetOps *ops, SdbEvalResult *result) {
  if (!result) {
    return false;
  }
  sdb_error_clear(&result->error);
  result->has_value = false;
  if (!expr || !ops) {
    sdb_error_set(&result->error, SDB_ERR_UNSUPPORTED, 0, 0, "missing expression or target ops", "eval");
    return false;
  }
  (void)eval_expr(expr, ops, result);
  return result->error.code == SDB_ERR_NONE && result->has_value;
}

bool sdb_expr_eval_text(const char *text, const SdbTargetOps *ops, SdbEvalResult *result) {
  SdbError err;
  SdbExpr *expr = sdb_expr_parse(text, &err);
  if (!expr) {
    if (result) {
      result->error = err;
      result->has_value = false;
    }
    return false;
  }
  bool ok = sdb_expr_eval(expr, ops, result);
  sdb_expr_free(expr);
  return ok;
}
