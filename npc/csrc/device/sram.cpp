#include <utils.hpp>
#include <sim_top.hpp>
#include <device/io/map.hpp>
#include <device/io.hpp>
#include <macro-def.hpp>
#include <cstring>
#include "../npc/simulator_impl.hpp"

static inline void*& sram_io_base() {
    return getActiveSimulator()->sram_io_base;
}

#ifndef NPC_STANDALONE
static inline auto &dutSramMemory() {
    return top->ysyxSoCFull->asic->__PVT__axi4ram__DOT__mem_ext__DOT__Memory;
}
#endif

void device_sram_syncShadowFromDUT(addr_t addr, size_t len) {
#ifndef NPC_STANDALONE
    auto &dutMemory = dutSramMemory();
    uint8_t *shadow = (uint8_t *) sram_io_base();
    Assert(addr >= SRAM_ADDR && addr - SRAM_ADDR <= SRAM_LEN - len);
    for (size_t i = 0; i < len; ++i) {
        const size_t byteOffset = (size_t) (addr - SRAM_ADDR) + i;
        const size_t wordIndex = byteOffset / 4;
        const size_t lane = byteOffset % 4;
        shadow[byteOffset] = (dutMemory[wordIndex] >> (lane * 8)) & 0xff;
    }
#else
    (void) addr;
    (void) len;
#endif
}

void device_sram_syncDUTFromShadow(addr_t addr, size_t len) {
#ifndef NPC_STANDALONE
    auto &dutMemory = dutSramMemory();
    uint8_t *shadow = (uint8_t *) sram_io_base();
    Assert(addr >= SRAM_ADDR && addr - SRAM_ADDR <= SRAM_LEN - len);
    for (size_t i = 0; i < len; ++i) {
        const size_t byteOffset = (size_t) (addr - SRAM_ADDR) + i;
        const size_t wordIndex = byteOffset / 4;
        const size_t lane = byteOffset % 4;
        const uint32_t mask = 0xffu << (lane * 8);
        dutMemory[wordIndex] = (dutMemory[wordIndex] & ~mask) |
            (uint32_t(shadow[byteOffset]) << (lane * 8));
    }
#else
    (void) addr;
    (void) len;
#endif
}

static void sram_io_handler(addr_t offset, int len, bool isWrite) {
    if (isWrite) {
        device_sram_syncDUTFromShadow(SRAM_ADDR + offset, (size_t) len);
    } else {
        device_sram_syncShadowFromDUT(SRAM_ADDR + offset, (size_t) len);
    }
}

bool device_sram_init() {
    sram_io_base() = device_io_map_newSpace(SRAM_LEN);
    device_io_addMMIOMap(
        "sram",
        SRAM_ADDR,
        sram_io_base(),
        SRAM_LEN,
        sram_io_handler
    );

    if (sram_io_base()) {
        memset(sram_io_base(), 0, SRAM_LEN);
    }

    return true;
}

word_t device_sram_read(addr_t addr, int len) {
    word_t result;

    const addr_t baseAddr = SRAM_ADDR;
    const addr_t baseLen = SRAM_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "sram: invalid memory read address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "sram: invalid memory read length: %d\n",
        len
    );

    uint8_t *sramMemory = (uint8_t *) sram_io_base();
    result = sramMemory[addr - baseAddr];
    if (len >= 2) {
        result |= sramMemory[addr - baseAddr + 1] << 8;
    }
    if (len >= 4) {
        result |= sramMemory[addr - baseAddr + 2] << 16;
        result |= sramMemory[addr - baseAddr + 3] << 24;
    }

    trace_record_dtrace(0, "sram", false, addr, len, result, "dpi", "SRAM");

    return result;
}

void device_sram_write(addr_t addr, int len, word_t data) {
    const addr_t baseAddr = SRAM_ADDR;
    const addr_t baseLen = SRAM_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "sram: invalid memory write address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "sram: invalid memory write length: %d\n",
        len
    );

    uint8_t *sramMemory = (uint8_t *) sram_io_base();
    if (len >= 1) {
        sramMemory[addr - baseAddr] = data & 0xff;
    }
    if (len >= 2) {
        sramMemory[addr - baseAddr + 1] = (data >> 8) & 0xff;
    }
    if (len >= 4) {
        sramMemory[addr - baseAddr + 2] = (data >> 16) & 0xff;
        sramMemory[addr - baseAddr + 3] = (data >> 24) & 0xff;
    }

    trace_record_dtrace(0, "sram", true, addr, len, data, "dpi", "SRAM");
}
