#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <macro-def.hpp>
#include <device/vga.hpp>

#define PMEM_SIZE (128 * 1024 * 1024)  // 128 MB physical memory
#define PMEM_BASE 0x80000000UL
static uint8_t pmem[PMEM_SIZE];

static inline bool addr_valid(uint32_t addr) {
    return addr >= PMEM_BASE && addr < PMEM_BASE + PMEM_SIZE - 3;
}

extern "C" {

int dpi_pmem_read(int addr) {
    uint32_t a = (uint32_t)addr;
    if (vga_is_in_range(a)) return (int)vga_read(a);
    if (!addr_valid(a)) return 0;
    uint32_t val;
    memcpy(&val, &pmem[a - PMEM_BASE], 4);
    return (int)val;
}

void dpi_pmem_write(int addr, int data, char strb) {
    uint32_t a = (uint32_t)addr;
    if (vga_is_in_range(a)) { vga_write(a, (uint32_t)data, (uint8_t)strb); return; }
    if (!addr_valid(a)) return;
    uint32_t wdata = (uint32_t)data;
    uint8_t  wstrb = (uint8_t)strb;
    uint32_t off = a - PMEM_BASE;
    if (wstrb & 0x1) pmem[off + 0] = (wdata >>  0) & 0xFF;
    if (wstrb & 0x2) pmem[off + 1] = (wdata >>  8) & 0xFF;
    if (wstrb & 0x4) pmem[off + 2] = (wdata >> 16) & 0xFF;
    if (wstrb & 0x8) pmem[off + 3] = (wdata >> 24) & 0xFF;
}

void dpi_set_pmem_word(int word_addr, int data) {
    uint32_t addr = (uint32_t)word_addr * 4;
    if (addr >= PMEM_SIZE - 3) return;
    pmem[addr + 0] = (data >>  0) & 0xFF;
    pmem[addr + 1] = (data >>  8) & 0xFF;
    pmem[addr + 2] = (data >> 16) & 0xFF;
    pmem[addr + 3] = (data >> 24) & 0xFF;
}

} // extern "C"

void standalone_mem_loadBin(const char *path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "[standalone] Failed to open binary: " << path << std::endl;
        return;
    }
    ifs.read((char *)pmem, PMEM_SIZE);
    size_t bytes = ifs.gcount();
    std::cout << "[standalone] Loaded " << bytes << " bytes from "
              << path << " into physical memory (0x80000000)" << std::endl;
}
