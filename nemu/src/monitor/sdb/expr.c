#include <isa.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>
#include <string.h>
#include <utils.h>
#include <utils/symbol.h>

#include "sdb.h"

static bool sdb_nemu_read_register(void *userdata, const char *name, SdbValue *out, SdbError *err) {
  (void)userdata;
  bool success = false;
  if (strcmp(name, "pc") == 0) {
    *out = sdb_value_make(cpu.pc, sizeof(word_t) * 8, false);
    return true;
  }
  word_t value = isa_reg_str2val(name, &success);
  if (!success) {
    sdb_error_set(err, SDB_ERR_UNKNOWN_REGISTER, 0, 0, "unknown register", "nemu");
    return false;
  }
  *out = sdb_value_make(value, sizeof(word_t) * 8, false);
  return true;
}

static bool sdb_nemu_read_memory(
  void *userdata, SdbAddressSpace space, uint64_t addr, unsigned width,
  bool is_signed, bool force_mmio, SdbValue *out, SdbError *err
) {
  (void)userdata;
  (void)space;
  int len = (int)(width / 8);
  if (len != 1 && len != 2 && len != 4 && len != 8) {
    sdb_error_set(err, SDB_ERR_INVALID_LITERAL, 0, 0, "unsupported memory width", "nemu");
    return false;
  }
  if (!force_mmio && !in_pmem((paddr_t)addr)) {
#ifdef CONFIG_DEVICE
    sdb_error_set(err, SDB_ERR_MMIO_SIDE_EFFECT, 0, 0, "refusing to read MMIO without force flag", "nemu");
    return false;
#else
    sdb_error_set(err, SDB_ERR_MEMORY_FAULT, 0, 0, "memory access fault", "nemu");
    return false;
#endif
  }
  word_t value = vaddr_read_mtrace((vaddr_t)addr, len, false);
  *out = sdb_value_make((uint64_t)value, width, is_signed);
  return true;
}

static bool sdb_nemu_write_register(void *userdata, const char *name, SdbValue value, SdbError *err) {
  (void)userdata;
  (void)value;
  sdb_error_set(err, SDB_ERR_UNSUPPORTED, 0, 0, "register write is unsupported in NEMU wrapper", "nemu");
  return false;
}

static bool sdb_nemu_write_memory(
  void *userdata, SdbAddressSpace space, uint64_t addr, unsigned width,
  SdbValue value, bool force_mmio, SdbError *err
) {
  (void)userdata;
  (void)space;
  (void)value;
  (void)force_mmio;
  (void)addr;
  (void)width;
  sdb_error_set(err, SDB_ERR_UNSUPPORTED, 0, 0, "memory write is unsupported in NEMU wrapper", "nemu");
  return false;
}

static bool sdb_nemu_resolve_symbol(void *userdata, const char *name, uint64_t *addr, SdbError *err) {
  (void)userdata;
  if (!name || !addr) {
    sdb_error_set(err, SDB_ERR_UNKNOWN_SYMBOL, 0, 0, "unknown symbol", "nemu");
    return false;
  }
#ifdef CONFIG_FTRACE
  for (size_t i = 0; i < nemu_state.ftrace_func_syms_size; ++i) {
    if (strcmp(nemu_state.ftrace_func_syms[i].name, name) == 0) {
      *addr = nemu_state.ftrace_func_syms[i].addr;
      return true;
    }
  }
#endif
  sdb_error_set(err, SDB_ERR_UNKNOWN_SYMBOL, 0, 0, "unknown symbol", "nemu");
  return false;
}

static bool sdb_nemu_lookup_symbol(
  void *userdata, uint64_t addr, char *name, size_t name_len,
  uint64_t *offset, SdbError *err
) {
  (void)userdata;
  (void)addr;
  (void)name;
  (void)name_len;
  (void)offset;
  (void)err;
  return false;
}

static uint64_t sdb_nemu_get_pc(void *userdata) {
  (void)userdata;
  return cpu.pc;
}

static bool sdb_nemu_set_pc(void *userdata, uint64_t pc, SdbError *err) {
  (void)userdata;
  (void)pc;
  sdb_error_set(err, SDB_ERR_UNSUPPORTED, 0, 0, "PC write unsupported in NEMU wrapper", "nemu");
  return false;
}

static const SdbTargetOps sdb_nemu_ops = {
  .userdata = NULL,
  .xlen = sizeof(word_t) * 8,
  .allow_mmio_read = false,
  .read_register = sdb_nemu_read_register,
  .write_register = sdb_nemu_write_register,
  .read_memory = sdb_nemu_read_memory,
  .write_memory = sdb_nemu_write_memory,
  .resolve_symbol = sdb_nemu_resolve_symbol,
  .lookup_symbol = sdb_nemu_lookup_symbol,
  .get_pc = sdb_nemu_get_pc,
  .set_pc = sdb_nemu_set_pc,
};

const SdbTargetOps *sdb_nemu_target_ops(void) {
  return &sdb_nemu_ops;
}

word_t expr(char *e, bool *success) {
  SdbEvalResult result = {0};
  if (!sdb_expr_eval_text(e, &sdb_nemu_ops, &result)) {
    if (success) {
      *success = false;
    }
    return 0;
  }
  if (success) {
    *success = true;
  }
  return (word_t)sdb_value_as_u64(result.value);
}
