#include <utils.hpp>
#include <device/io/map.hpp>
#include <cstring>
#include <device/io.hpp>
#include <macro-def.hpp>
#include <difftest/dut.hpp>
#ifndef NPC_STANDALONE
#include <sim_top.hpp>
#endif

void *sdram_io_base = nullptr;

#define IFDBG if (sim_config.config_debugOutput)

static void sdram_io_handler(addr_t offset, int len, bool isWrite) {
    // TODO
}

bool device_sdram_init() {
    sdram_io_base = device_io_map_newSpace(SDRAM_LEN);
    if (sdram_io_base) {
        memset(sdram_io_base, 0, SDRAM_LEN);
    }
    device_io_addMMIOMap("sdram", SDRAM_ADDR, sdram_io_base, SDRAM_LEN, sdram_io_handler);

    return true;
}

word_t device_sdram_read(addr_t addr, int len) {
    word_t result;

    const addr_t baseAddr = SDRAM_ADDR;
    const addr_t baseLen = SDRAM_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "sdram: invalid memory read address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "sdram: invalid memory read length: %d\n",
        len
    );

    uint8_t *sdramMemory = (uint8_t *) sdram_io_base;
    result = sdramMemory[addr - baseAddr];
    if (len >= 2) {
        result |= sdramMemory[addr - baseAddr + 1] << 8;
    }
    if (len >= 4) {
        result |= sdramMemory[addr - baseAddr + 2] << 16;
        result |= sdramMemory[addr - baseAddr + 3] << 24;
    }

    trace_record_dtrace(0, "sdram", false, addr, len, result, "dpi", "SDRAM");

    return result;
}

void device_sdram_write(addr_t addr, int len, word_t data) {
    const addr_t baseAddr = SDRAM_ADDR;
    const addr_t baseLen = SDRAM_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "sdram: invalid memory write address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "sdram: invalid memory write length: %d\n",
        len
    );

    uint8_t *sdramMemory = (uint8_t *) sdram_io_base;
    if (len >= 1) {
        sdramMemory[addr - baseAddr] = data & 0xff;
    }
    if (len >= 2) {
        sdramMemory[addr - baseAddr + 1] = (data >> 8) & 0xff;
    }
    if (len >= 4) {
        sdramMemory[addr - baseAddr + 2] = (data >> 16) & 0xff;
        sdramMemory[addr - baseAddr + 3] = (data >> 24) & 0xff;
    }

#ifndef NPC_STANDALONE
    if (isDifftestActive()) {
        difftest_dut_syncMemoryToRef(addr, sdramMemory + (addr - baseAddr), (size_t) len);
    }
#endif

    trace_record_dtrace(0, "sdram", true, addr, len, data, "dpi", "SDRAM");

}
