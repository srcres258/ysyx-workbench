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
#include "npc/gpr_fields.hpp"

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
#define NPC_GPR_CASE(idx) case idx: dpi->gpr_gprs_##idx = bits; return true;
    switch (idx) {
      NPC_GPR_FIELDS_0_15(NPC_GPR_CASE)
#ifndef CONFIG_RVE
      NPC_GPR_FIELDS_16_31(NPC_GPR_CASE)
#endif
      default: return false;
    }
#undef NPC_GPR_CASE
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
