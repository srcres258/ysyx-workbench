#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cstdio>

#include <isa.hpp>
#include <sim_top.hpp>
#include <device/io/mmio.hpp>
#include <sdb.hpp>
#include "npc/simulator_impl.hpp"

#ifdef NPC_STANDALONE
extern "C" int dpi_pmem_read(int addr);
extern "C" void dpi_pmem_write(int addr, int data, char strb);
#endif

// ── Simulator-backed adapter helper ──
// All register/memory access paths go through the active simulator bridge.
// Returns true if the simulator is active and ready; false otherwise.
static inline bool ensure_sim_active(SdbError *err) {
  if (getActiveSimulator()) {
    return true;
  }
  sdb_error_set(
    err, SDB_ERR_UNSUPPORTED, 0, 0,
    "no active simulator instance — SDB unavailable", "npc"
  );
  return false;
}

static bool read_reg_impl(void *userdata, const char *name, SdbValue *out, SdbError *err) {
  (void) userdata;
  if (!ensure_sim_active(err))
    return false;
  bool success = false;
  if (std::strcmp(name, "pc") == 0) {
    *out = sdb_value_make(getDPIModule()->core_pc, sizeof(word_t) * 8, false);
    return true;
  }
  word_t value = isaRegStr2Val(name, &success);
  if (!success) {
    sdb_error_set(
      err, SDB_ERR_UNKNOWN_REGISTER, 0, 0, "unknown register", "npc"
    );
    return false;
  }
  *out = sdb_value_make(value, sizeof(word_t) * 8, false);
  return true;
}

static bool write_reg_impl(void *userdata, const char *name, SdbValue value, SdbError *err) {
  (void) userdata;
  if (!ensure_sim_active(err))
    return false;
  auto *dpi = getDPIModule();
  const uint64_t bits = sdb_value_as_u64(value);
  if (std::strcmp(name, "pc") == 0) {
    dpi->core_pc = static_cast<addr_t>(bits);
    return true;
  }
  if (
    std::strcmp(name, "x0") == 0 || std::strcmp(name, "0") == 0 ||
    std::strcmp(name, "$0") == 0
  ) {
    return true;
  }
  auto assign_gpr = [&](size_t idx) -> bool {
    switch (idx) {
      case 0: dpi->gpr_gprs_0 = bits; return true;
      case 1: dpi->gpr_gprs_1 = bits; return true;
      case 2: dpi->gpr_gprs_2 = bits; return true;
      case 3: dpi->gpr_gprs_3 = bits; return true;
      case 4: dpi->gpr_gprs_4 = bits; return true;
      case 5: dpi->gpr_gprs_5 = bits; return true;
      case 6: dpi->gpr_gprs_6 = bits; return true;
      case 7: dpi->gpr_gprs_7 = bits; return true;
      case 8: dpi->gpr_gprs_8 = bits; return true;
      case 9: dpi->gpr_gprs_9 = bits; return true;
      case 10: dpi->gpr_gprs_10 = bits; return true;
      case 11: dpi->gpr_gprs_11 = bits; return true;
      case 12: dpi->gpr_gprs_12 = bits; return true;
      case 13: dpi->gpr_gprs_13 = bits; return true;
      case 14: dpi->gpr_gprs_14 = bits; return true;
      case 15: dpi->gpr_gprs_15 = bits; return true;
      case 16: dpi->gpr_gprs_16 = bits; return true;
      case 17: dpi->gpr_gprs_17 = bits; return true;
      case 18: dpi->gpr_gprs_18 = bits; return true;
      case 19: dpi->gpr_gprs_19 = bits; return true;
      case 20: dpi->gpr_gprs_20 = bits; return true;
      case 21: dpi->gpr_gprs_21 = bits; return true;
      case 22: dpi->gpr_gprs_22 = bits; return true;
      case 23: dpi->gpr_gprs_23 = bits; return true;
      case 24: dpi->gpr_gprs_24 = bits; return true;
      case 25: dpi->gpr_gprs_25 = bits; return true;
      case 26: dpi->gpr_gprs_26 = bits; return true;
      case 27: dpi->gpr_gprs_27 = bits; return true;
      case 28: dpi->gpr_gprs_28 = bits; return true;
      case 29: dpi->gpr_gprs_29 = bits; return true;
      case 30: dpi->gpr_gprs_30 = bits; return true;
      case 31: dpi->gpr_gprs_31 = bits; return true;
      default: return false;
    }
  };
  bool ok = false;
  if (name[0] == 'x' && std::strlen(name) > 1) {
    char *end = nullptr;
    long idx = std::strtol(name + 1, &end, 10);
    if (end && *end == '\0' && idx >= 0 && idx < 32) {
      ok = assign_gpr(static_cast<size_t>(idx));
    }
  }
  if (!ok) {
    for (size_t i = 0; i < RISCV_GPR_NUM; ++i) {
      const char *reg_name = isaRegName(i);
      if (reg_name && std::strcmp(name, reg_name) == 0) {
        ok = assign_gpr(i);
        break;
      }
    }
  }
  if (!ok) {
    sdb_error_set(
      err, SDB_ERR_UNKNOWN_REGISTER, 0, 0,
      "unknown or unsupported register", "npc"
    );
    return false;
  }
  return true;
}

static bool read_mem_impl(
  void *userdata, SdbAddressSpace space, uint64_t addr, unsigned width,
  bool is_signed, bool force_mmio, SdbValue *out, SdbError *err
) {
  (void) userdata;
  (void) is_signed;
  if (!ensure_sim_active(err))
    return false;
  const int len = static_cast<int>(width / 8);
  if (len != 1 && len != 2 && len != 4 && len != 8) {
    sdb_error_set(
      err, SDB_ERR_UNSUPPORTED, 0, 0, "unsupported memory width", "npc"
    );
    return false;
  }
  if (!device_io_mmio_isAddrValid(static_cast<addr_t>(addr))) {
#ifdef NPC_STANDALONE
    if (addr >= 0x80000000ULL) {
      uint64_t value = 0;
      for (int i = 0; i < len; ++i) {
        int word = dpi_pmem_read(static_cast<int>(addr + static_cast<uint64_t>(i & ~3)));
        const int shift = (i & 3) * 8;
        value |= static_cast<uint64_t>((word >> shift) & 0xff) << (i * 8);
      }
      *out = sdb_value_make(value, width, is_signed);
      return true;
    }
#endif
    if (!force_mmio) {
      sdb_error_set(
        err, SDB_ERR_MMIO_SIDE_EFFECT, 0, 0,
        "refusing to read MMIO without force flag", "npc"
      );
      return false;
    }
  }
  uint64_t value = 0;
  if (device_io_mmio_isAddrValid(static_cast<addr_t>(addr))) {
    value = device_io_mmio_read(static_cast<addr_t>(addr), len);
  } else {
#ifdef NPC_STANDALONE
    if (addr >= 0x80000000ULL) {
      value = static_cast<uint32_t>(dpi_pmem_read(static_cast<int>(addr)));
    } else {
      sdb_error_set(
        err, SDB_ERR_MEMORY_FAULT, 0, 0, "memory access fault", "npc"
      );
      return false;
    }
#else
    sdb_error_set(
      err, SDB_ERR_MEMORY_FAULT, 0, 0, "memory access fault", "npc"
    );
    return false;
#endif
  }
  *out = sdb_value_make(value, width, is_signed);
  return true;
}

static bool write_mem_impl(
  void *userdata, SdbAddressSpace space, uint64_t addr, unsigned width,
  SdbValue value, bool force_mmio, SdbError *err
) {
  (void) userdata;
  (void) space;
  if (!ensure_sim_active(err))
    return false;
  const int len = static_cast<int>(width / 8);
  if (len != 1 && len != 2 && len != 4 && len != 8) {
    sdb_error_set(
      err, SDB_ERR_UNSUPPORTED, 0, 0, "unsupported memory width", "npc"
    );
    return false;
  }
  if (!device_io_mmio_isAddrValid(static_cast<addr_t>(addr))) {
#ifdef NPC_STANDALONE
    if (addr >= 0x80000000ULL) {
      uint64_t bits = sdb_value_as_u64(value);
      for (int i = 0; i < len; ++i) {
        char strb = 1 << (i & 3);
        dpi_pmem_write(
          static_cast<int>(addr + static_cast<uint64_t>(i & ~3)),
          static_cast<int>(bits >> (8 * (i & 3))),
          strb
        );
      }
      return true;
    }
#endif
    if (!force_mmio) {
      sdb_error_set(
        err, SDB_ERR_MMIO_SIDE_EFFECT, 0, 0,
        "refusing to write MMIO without force flag", "npc"
      );
      return false;
    }
  }
  if (device_io_mmio_isAddrValid(static_cast<addr_t>(addr))) {
    device_io_mmio_write(
      static_cast<addr_t>(addr),
      len,
      static_cast<word_t>(sdb_value_as_u64(value))
    );
    return true;
  }
  sdb_error_set(
    err, SDB_ERR_MEMORY_FAULT, 0, 0, "memory access fault", "npc"
  );
  return false;
}

static bool resolve_symbol_impl(
  void *userdata, const char *name, uint64_t *addr, SdbError *err
) {
  (void) userdata;
  (void) name;
  (void) addr;
  sdb_error_set(err, SDB_ERR_UNKNOWN_SYMBOL, 0, 0, "unknown symbol", "npc");
  return false;
}

static bool lookup_symbol_impl(
  void *userdata, uint64_t addr, char *name, size_t name_len,
  uint64_t *offset, SdbError *err
) {
  (void) userdata;
  (void) addr;
  (void) name;
  (void) name_len;
  (void) offset;
  (void) err;
  return false;
}

static uint64_t get_pc_impl(void *userdata) {
  (void) userdata;
  if (!getActiveSimulator())
    return 0;
  return getDPIModule()->core_pc;
}

static bool set_pc_impl(void *userdata, uint64_t pc, SdbError *err) {
  (void) userdata;
  if (!ensure_sim_active(err))
    return false;
  getDPIModule()->core_pc = static_cast<addr_t>(pc);
  sdb_error_set(
    err, SDB_ERR_UNSUPPORTED, 0, 0,
    "pc write uses best-effort direct assignment", "npc"
  );
  return true;
}

static const SdbTargetOps sdb_npc_ops = {
  .userdata = NULL,
  .xlen = sizeof(word_t) * 8,
  .allow_mmio_read = false,
  .read_register = read_reg_impl,
  .write_register = write_reg_impl,
  .read_memory = read_mem_impl,
  .write_memory = write_mem_impl,
  .resolve_symbol = resolve_symbol_impl,
  .lookup_symbol = lookup_symbol_impl,
  .get_pc = get_pc_impl,
  .set_pc = set_pc_impl,
};

const SdbTargetOps *sdb_npc_target_ops(void) {
  return &sdb_npc_ops;
}
