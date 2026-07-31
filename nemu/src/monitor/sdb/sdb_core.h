#ifndef NEMU_SDB_CORE_H
#define NEMU_SDB_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  SDB_ERR_NONE = 0,
  SDB_ERR_LEXICAL,
  SDB_ERR_UNEXPECTED_TOKEN,
  SDB_ERR_UNEXPECTED_EOF,
  SDB_ERR_UNMATCHED_PAREN,
  SDB_ERR_INVALID_LITERAL,
  SDB_ERR_UNKNOWN_REGISTER,
  SDB_ERR_UNKNOWN_SYMBOL,
  SDB_ERR_DIVIDE_BY_ZERO,
  SDB_ERR_MODULO_BY_ZERO,
  SDB_ERR_INVALID_SHIFT,
  SDB_ERR_MEMORY_FAULT,
  SDB_ERR_MMIO_SIDE_EFFECT,
  SDB_ERR_UNSUPPORTED,
  SDB_ERR_OVERFLOW,
  SDB_ERR_RESOURCE_EXHAUSTED,
  SDB_ERR_MALFORMED_COMMAND,
} SdbErrorCode;

typedef struct {
  SdbErrorCode code;
  size_t begin;
  size_t end;
  char message[192];
  char platform[128];
} SdbError;

typedef struct {
  uint64_t bits;
  unsigned width;
  bool is_signed;
} SdbValue;

typedef enum {
  SDB_ADDR_MEM = 0,
  SDB_ADDR_PMEM = 1,
} SdbAddressSpace;

typedef struct SdbTargetOps {
  void *userdata;
  unsigned xlen;
  bool allow_mmio_read;

  bool (*read_register)(void *userdata, const char *name, SdbValue *out, SdbError *err);
  bool (*write_register)(void *userdata, const char *name, SdbValue value, SdbError *err);
  bool (*read_memory)(void *userdata, SdbAddressSpace space, uint64_t addr, unsigned width,
                      bool is_signed, bool force_mmio, SdbValue *out, SdbError *err);
  bool (*write_memory)(void *userdata, SdbAddressSpace space, uint64_t addr, unsigned width,
                       SdbValue value, bool force_mmio, SdbError *err);
  bool (*resolve_symbol)(void *userdata, const char *name, uint64_t *addr, SdbError *err);
  bool (*lookup_symbol)(void *userdata, uint64_t addr, char *name, size_t name_len,
                        uint64_t *offset, SdbError *err);
  uint64_t (*get_pc)(void *userdata);
  bool (*set_pc)(void *userdata, uint64_t pc, SdbError *err);
} SdbTargetOps;

typedef struct SdbExpr SdbExpr;

typedef struct {
  SdbError error;
  SdbValue value;
  bool has_value;
} SdbEvalResult;

typedef struct {
  const char *text;
  size_t length;
  size_t position;
} SdbInput;

void sdb_error_clear(SdbError *err);
void sdb_error_set(SdbError *err, SdbErrorCode code, size_t begin, size_t end,
                   const char *message, const char *platform);
void sdb_error_setf(SdbError *err, SdbErrorCode code, size_t begin, size_t end,
                    const char *platform, const char *fmt, ...);

SdbValue sdb_value_make(uint64_t bits, unsigned width, bool is_signed);
uint64_t sdb_value_mask(unsigned width);
uint64_t sdb_value_truncate(uint64_t bits, unsigned width);
bool sdb_value_truthy(SdbValue value);
int64_t sdb_value_as_i64(SdbValue value);
uint64_t sdb_value_as_u64(SdbValue value);

SdbExpr *sdb_expr_parse(const char *text, SdbError *err);
void sdb_expr_free(SdbExpr *expr);
bool sdb_expr_eval(const SdbExpr *expr, const SdbTargetOps *ops, SdbEvalResult *result);
bool sdb_expr_eval_text(const char *text, const SdbTargetOps *ops, SdbEvalResult *result);

const char *sdb_error_code_name(SdbErrorCode code);

#endif
