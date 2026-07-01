#include <utils.hpp>
#include <device/io/map.hpp>
#include <device/io.hpp>
#include <macro-def.hpp>

void *psram_io_base = nullptr;

#define IFDBG if (sim_config.config_debugOutput)

static void psram_io_handler(addr_t offset, int len, bool isWrite) {
    // TODO
}

bool device_psram_init() {
    psram_io_base = device_io_map_newSpace(PSRAM_LEN);
    device_io_addMMIOMap("psram", PSRAM_ADDR, psram_io_base, PSRAM_LEN, psram_io_handler);

    return true;
}

word_t device_psram_read(addr_t addr, int len) {
    word_t result;

    const addr_t baseAddr = PSRAM_ADDR;
    const addr_t baseLen = PSRAM_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "psram: invalid memory read address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "psram: invalid memory read length: %d\n",
        len
    );

    uint8_t *psramMemory = (uint8_t *) psram_io_base;
    result = psramMemory[addr - baseAddr];
    if (len >= 2) {
        result |= psramMemory[addr - baseAddr + 1] << 8;
    }
    if (len >= 4) {
        result |= psramMemory[addr - baseAddr + 2] << 16;
        result |= psramMemory[addr - baseAddr + 3] << 24;
    }

    trace_record_dtrace(0, "psram", false, addr, len, result, "dpi", "PSRAM");

    return result;
}

void device_psram_write(addr_t addr, int len, word_t data) {
    const addr_t baseAddr = PSRAM_ADDR;
    const addr_t baseLen = PSRAM_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "psram: invalid memory write address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "psram: invalid memory write length: %d\n",
        len
    );

    uint8_t *psramMemory = (uint8_t *) psram_io_base;
    if (len >= 1) {
        psramMemory[addr - baseAddr] = data & 0xff;
    }
    if (len >= 2) {
        psramMemory[addr - baseAddr + 1] = (data >> 8) & 0xff;
    }
    if (len >= 4) {
        psramMemory[addr - baseAddr + 2] = (data >> 16) & 0xff;
        psramMemory[addr - baseAddr + 3] = (data >> 24) & 0xff;
    }

    trace_record_dtrace(0, "psram", true, addr, len, data, "dpi", "PSRAM");

}
