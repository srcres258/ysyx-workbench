#include <utils.hpp>
#include <macro-def.hpp>
#include <difftest/dut.hpp>
#include <sim_top.hpp>
#include <memory.hpp>
#include <device/map.hpp>

#define IO_SPACE_MAX (32 * 1024 * 1024)

static uint8_t *ioSpace = nullptr;
static uint8_t *pSpace = nullptr;

uint8_t *device_map_newSpace(int size) {
    uint8_t *p = pSpace;
    // page aligned
    size = (size + (PAGE_SIZE - 1)) & ~PAGE_MASK;
    pSpace += size;
    Assert(pSpace - ioSpace < IO_SPACE_MAX);
    return p;
}

int device_map_findMapIdByAddr(const IOMap *maps, int size, addr_t addr) {
    int i;

    for (i = 0; i < size; i++) {
        if (maps[i].isInside(addr)) {
            difftest_dut_skipRef();
            return i;
        }
    }

    return -1;
}

static void checkBound(const IOMap *map, addr_t addr) {
    if (map) {
        Assert(
            addr <= map->high && addr >= map->low,
            "address (" FMT_ADDR ") is out of bound {%s} [" FMT_ADDR
                ", " FMT_ADDR "] at pc = " FMT_WORD,
            addr, map->name, map->low, map->high, top->io_pc
        );
    } else {
        panic(
            "address (" FMT_ADDR ") is null at pc = " FMT_WORD,
            addr, top->io_pc
        );
    }
}

static void invokeCallback(io_callback_t c, addr_t offset, int len, bool isWrite) {
    if (c) {
        c(offset, len, isWrite);
    }
}

bool IOMap::isInside(addr_t addr) const {
    return addr >= low && addr <= high;
}

word_t IOMap::read(addr_t addr, int len) const {
    Assert(len >= 1 && len <= 8);
    checkBound(this, addr);
    addr_t offset = addr - low;
    invokeCallback(callback, offset, len, false);
    word_t ret = memoryHostRead(space + offset, len);
    // TODO: dtrace
    return ret;
}

void IOMap::write(addr_t addr, int len, word_t data) const {
    Assert(len >= 1 && len <= 8);
    checkBound(this, addr);
    addr_t offset = addr - low;
    memoryHostWrite(space + offset, len, data);
    invokeCallback(callback, offset, len, true);
    // TODO: dtrace
}

void device_map_init() {
    ioSpace = new uint8_t[IO_SPACE_MAX];
    Assert(ioSpace);
    pSpace = ioSpace;
}
