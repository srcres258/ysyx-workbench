#include <utils.hpp>
#include <macro-def.hpp>
#include <difftest/dut.hpp>
#include <sim_top.hpp>
#include <device/io/map.hpp>

// IO space size: 32 MB
#define IO_SPACE_MAX (32 * 1024 * 1024)

static uint8_t *ioSpace = nullptr;
static uint8_t *pSpace = nullptr;

uint8_t *device_io_map_newSpace(int size) {
    uint8_t *p = pSpace;
    // ensure memory page alignment
    int sizeAligned = (size + (MEMORY_PAGE_SIZE - 1)) & MEMORY_PAGE_MASK;
    pSpace += sizeAligned;
    Assert(pSpace - ioSpace < IO_SPACE_MAX);

    return p;
}

int device_io_map_findMapIdByAddr(const IOMap *maps, int size, addr_t addr) {
    int i;

    for (i = 0; i < size; i++) {
        if (maps[i].isInside(addr)) {
            if (sim_config.config_difftest) {
                difftest_dut_skipRef();
            }
            return i;
        }
    }

    return -1;
}

static auto *dpi() {
    return getDPIModule();
}

static void checkBound(const IOMap *map, addr_t addr) {
    if (map) {
        Assert(
            addr <= map->high && addr >= map->low,
            "address (" FMT_ADDR ") is out of bound {%s} [" FMT_ADDR
                ", " FMT_ADDR "] at pc = " FMT_WORD,
            addr, map->name, map->low, map->high, dpi()->core_pc
        );
    } else {
        panic(
            "address (" FMT_ADDR ") is null at pc = " FMT_WORD,
            addr, dpi()->core_pc
        );
    }
}

static void invokeCallback(io_callback_t c, addr_t offset, int len, bool isWrite) {
    if (c) {
        c(offset, len, isWrite);
    }
}

static void dtraceRecord(
    addr_t addr, int len, word_t data,
    const IOMap *map, std::string type
) {
    auto content = std::format(
        "0x{:08x}: Device {}: {} at 0x{:08x}, len {}, data 0x{:08x}",
        dpi()->core_pc, map->name, type, addr, len, data
    );
    sim_state.dtrace_ofs << content << std::endl;
    if (sim_config.config_debugOutput) {
        std::cout << "[sim] dtrace: " << content << std::endl;
    }
}

bool IOMap::isInside(addr_t addr) const {
    return addr >= low && addr < high;
}

word_t IOMap::read(addr_t addr, int len) const {
    Assert(len >= 1 && len <= 8);
    checkBound(this, addr);
    addr_t offset = addr - low;
    invokeCallback(callback, offset, len, false);
    word_t ret = memoryHostRead(ioSpace + offset, len);
    if (sim_config.config_dtrace) {
        dtraceRecord(addr, len, ret, this, "read");
    }

    return ret;
}

void IOMap::write(addr_t addr, int len, word_t data) const {
    Assert(len >= 1 && len <= 8);
    checkBound(this, addr);
    addr_t offset = addr - low;
    memoryHostWrite(ioSpace + offset, len, data);
    invokeCallback(callback, offset, len, true);
    if (sim_config.config_dtrace) {
        dtraceRecord(addr, len, data, this, "write");
    }
}

void device_io_map_init() {
    ioSpace = new uint8_t[IO_SPACE_MAX];
    Assert(ioSpace);
    pSpace = ioSpace;
}
