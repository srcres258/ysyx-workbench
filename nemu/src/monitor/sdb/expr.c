/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

enum {
  TK_NOTYPE = 256, TK_EQ, TK_NUM,

  TK_NEG_MUL, // 负数乘法，用于当第二个乘数为负时将负号合并入乘号
  TK_NEG_DIV  // 负数除法，用于当除数为负时将负号合并入除号
};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {
  {" +", TK_NOTYPE},    // spaces
  {"\\+", '+'},         // plus
  {"-", '-'},           // minus
  {"\\*", '*'},         // multiply
  {"/", '/'},           // divide
  {"\\(", '('},         // left parenthesis
  {"\\)", ')'},         // right parenthesis
  {"==", TK_EQ},        // equal
  {"(0[xX]?)?[0-9]+", TK_NUM},   // number
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

#define LEN_TOKENS 256

static Token tokens[LEN_TOKENS] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static void record_token(int i, char *substr_start, int substr_len) {
  tokens[nr_token].type = rules[i].token_type;
  strncpy(tokens[nr_token].str, substr_start, substr_len);
  tokens[nr_token].str[substr_len] = '\0';
  nr_token++;
}

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        if (nr_token >= LEN_TOKENS) {
          panic("Regex tokens buffer overflow");
        }

        if (rules[i].token_type != TK_NOTYPE) { // Ignore spaces.
          // 如果这个符号是 + 或 -
          if (rules[i].token_type == '+' || rules[i].token_type == '-') {
            // 如果没有前一个符号，或者前一个符号为 ( 左括号
            if (nr_token == 0 || tokens[nr_token - 1].type == '(') {
              // 先补充前导0，以形成 0 + ... 或 0 - ... 这种符合运算算法的等价表示形式
              tokens[nr_token].type = TK_NUM;
              strcpy(tokens[nr_token].str, "0");
              nr_token++;

              // 再记录当前符号
              record_token(i, substr_start, substr_len);
            // 如果有前一个符号且前一个符号为 * 或 /
            } else if (nr_token > 0 && (tokens[nr_token - 1].type == '*' || tokens[nr_token - 1].type == '/')) {
              // 如果当前符号是负号的话，将这个负号与前面的 * 或 / 合并成负数版本（正号的话就不管了）
              if (rules[i].token_type == '-') {
                if (tokens[nr_token - 1].type == '*') { // * 号
                  tokens[nr_token - 1].type = TK_NEG_MUL;
                } else { // / 号
                  tokens[nr_token - 1].type = TK_NEG_DIV;
                }
              }
            } else {
              // 都没问题了，直接记录
              record_token(i, substr_start, substr_len);
            }
          } else {
            record_token(i, substr_start, substr_len);
          }
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

static bool check_parentheses(int p, int q) {
  int par_level, i;

  if (p >= q) {
    return false;
  }
  if (p < 0 || p >= nr_token) {
    return false;
  }
  if (q < 0 || q >= nr_token) {
    return false;
  }
  if (tokens[p].type != '(') {
    return false;
  }
  if (tokens[q].type != ')') {
    return false;
  }
  par_level = 1;
  for (i = p + 1; i < q; i++) {
    switch (tokens[i].type) {
      case '(':
        par_level++;
        break;
      case ')':
        par_level--;
        break;
    }
    if (par_level <= 0) {
      return false;
    }
  }
  return par_level == 1;
}

static int find_op_index(int p, int q) { // 寻找主运算符索引
  /*
    + - 运算比 * / 运算更低级，所以先考虑 + -，
    实在没找到再看 * /。
  */
  bool found_pm;
  int i;
  int last_pm; // 最后一个 + - 号的索引位置
  int last_td; // 最后一个 * / 号的索引位置
  /*
    当前括号层级（必须当层级为0时才统计上述两种符号的索引，
    因为括号内的总是优先计算，故不可能在括号内有主运算符）
  */
  int par_level;

  if (p >= q) {
    return -1;
  }

  found_pm = false;
  last_pm = -1;
  last_td = -1;
  par_level = 0;
  printf("Going to find main operator between %d and %d.\n", p, q);
  for (i = p; i <= q; i++) {
    switch (tokens[i].type) {
      case '(':
        printf("Met '(', par_level++\n");
        par_level++;
        break;
      case ')':
        printf("Met ')', par_level--\n");
        par_level--;
        break;
      case '+':
      case '-':
        if (par_level == 0) {
          if (!found_pm) {
            found_pm = true;
          }
          last_pm = i;
        }
        break;
      case '*':
      case '/':
      case TK_NEG_MUL:
      case TK_NEG_DIV:
        if (par_level == 0) {
          last_td = i;
        }
        break;
    }
  }
  if (last_pm < 0 && last_td < 0) {
    printf("Finding operator index failed between %d and %d.\n", p, q);
    return -1;
  }

  return found_pm ? last_pm : last_td;
}

// 注意由于求值可能为负数，所以返回值类型得用 int64_t（带符号整数）而非 word_t
// （原始代码给的类型是 word_t，是不对的）
static int64_t eval(int p, int q, bool *success) {
  bool stat;
  int64_t num, val1, val2;
  int op;

  if (p > q) {
    /* Bad expression. */

    printf("Bad expression, p is %d, q is %d.\n", p, q);
    *success = false;
    return 0;
  } else if (p == q) {
    /*
      Single token.

      For now this token should be a number.
      Return the value of the number.
    */

    if (tokens[p].type != TK_NUM) {
      printf("Single token is not a number.\n");
      *success = false;
      return 0;
    }
    num = strtoul(tokens[p].str, NULL, 0);
    *success = true;
    return num;
  } else if (check_parentheses(p, q)) {
    /*
      The expression is surrounded by a matched pair of parentheses.
      
      If that is the case, just throw away the parentheses and
      evaluate the expression inside them.
    */

    return eval(p + 1, q - 1, success);
  } else {
    op = find_op_index(p, q);
    if (op < 0) {
      // No operator found.
      printf("No operator found between %d and %d.\n", p, q);
      *success = false;
      return 0;
    }
    printf("Going to evaluate main operator %c at position %d.\n", tokens[op].type, op);
    val1 = eval(p, op - 1, &stat);
    if (!stat) {
      printf("Evaluation failed for val1.\n");
      *success = false;
      return 0;
    }
    val2 = eval(op + 1, q, &stat);
    if (!stat) {
      printf("Evaluation failed for val2.\n");
      *success = false;
      return 0;
    }

    switch (tokens[op].type) {
      case '+':
        printf("Calculating: %ld + %ld\n", val1, val2);
        *success = true;
        return val1 + val2;
      case '-':
        printf("Calculating: %ld - %ld\n", val1, val2);
        *success = true;
        return val1 - val2;
      case '*':
        printf("Calculating: %ld * %ld\n", val1, val2);
        *success = true;
        return val1 * val2;
      case '/':
        printf("Calculating: %ld / %ld\n", val1, val2);
        if (val2 == 0) {
          printf("Divide by zero.\n");
          *success = false;
          return 0;
        }
        *success = true;
        return val1 / val2;
      case TK_NEG_MUL:
        printf("Calculating: -(%ld * %ld)\n", val1, val2);
        *success = true;
        return -(val1 * val2);
      case TK_NEG_DIV:
        printf("Calculating: -(%ld / %ld)\n", val1, val2);
        if (val2 == 0) {
          printf("Divide by zero.\n");
          *success = false;
          return 0;
        }
        *success = true;
        return -(val1 / val2);
      default:
        // Found a token that is not of any operator type.
        printf("Found a token that is not of any operator type.\n");
        *success = false;
        return 0;
    }
  }
}

int64_t expr(char *e, bool *success) {
  bool stat;
  int64_t result;

  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  result = eval(0, nr_token - 1, &stat);
  if (!stat) {
    // Failed to evaluate the expression.
    printf("Failed to evaluate the expression.\n");
    *success = false;
    return 0;
  }

  *success = true;
  return result;
}
