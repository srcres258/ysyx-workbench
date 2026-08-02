#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <utils.hpp>
#include <macro-def.hpp>
#include <difftest/dut.hpp>
#include <sim_top.hpp>
#include <device/vga.hpp>
#include <device/serial.hpp>
#include <device/rtc.hpp>
#include <device/keyboard.hpp>

#define PMEM_SIZE (128 * 1024 * 1024)  // 128 MB physical memory
#define PMEM_BASE 0x80000000UL
static uint8_t pmem[PMEM_SIZE];
static size_t pmem_loaded_size = 0;

static inline bool addr_valid(uint32_t addr) {
    return addr >= PMEM_BASE && addr < PMEM_BASE + PMEM_SIZE - 3;
}

extern "C" {

int dpi_pmem_read(int addr) {
    uint32_t a = (uint32_t)addr;
    if (rtc_is_in_range(a)) {
        difftest_dut_skipRef(getDPIModule()->core_pc, DIFFTEST_SKIP_REASON_MMIO);
        int value = (int) rtc_read(a);
        trace_record_dtrace(0, "rtc", false, a, 4, (word_t) value, "standalone", "RTC");
        return value;
    }
    if (keyboard_is_in_range(a)) {
        difftest_dut_skipRef(getDPIModule()->core_pc, DIFFTEST_SKIP_REASON_MMIO);
        int value = (int) keyboard_read(a);
        trace_record_dtrace(0, "keyboard", false, a, 4, (word_t) value, "standalone", "KBD");
        return value;
    }
    if (vga_is_in_range(a)) {
        difftest_dut_skipRef(getDPIModule()->core_pc, DIFFTEST_SKIP_REASON_MMIO);
        int value = (int) vga_read(a);
        trace_record_dtrace(0, "vga", false, a, 4, (word_t) value, "standalone", "VGA");
        return value;
    }
    uint32_t word_addr = a & ~0x3u;
    if (!addr_valid(word_addr))
        return 0;
    uint32_t val;
    memcpy(&val, &pmem[word_addr - PMEM_BASE], 4);
    return (int) val;
}

void dpi_pmem_write(int addr, int data, char strb) {
    uint32_t a = (uint32_t)addr;
    if (serial_is_in_range(a)) {
        difftest_dut_skipRef(getDPIModule()->core_pc, DIFFTEST_SKIP_REASON_MMIO);
        uint32_t wdata = (uint32_t)data;
        uint8_t  wstrb = (uint8_t)strb;
        for (int i = 0; i < 4; i++) {
            if (wstrb & (1u << i)) {
                std::cout << (char) ((wdata >> (i * 8)) & 0xFF);
            }
        }
        std::cout << std::flush;
        trace_record_dtrace(0, "serial", true, a, 4, (word_t) wdata, "standalone", "UART");
        return;
    }
    if (rtc_is_in_range(a))      { difftest_dut_skipRef(getDPIModule()->core_pc, DIFFTEST_SKIP_REASON_MMIO); rtc_write(a, (uint32_t)data, (uint8_t)strb); trace_record_dtrace(0, "rtc", true, a, 4, (word_t) data, "standalone", "RTC"); return; }
    if (keyboard_is_in_range(a)) { difftest_dut_skipRef(getDPIModule()->core_pc, DIFFTEST_SKIP_REASON_MMIO); keyboard_write(a, (uint32_t)data, (uint8_t)strb); trace_record_dtrace(0, "keyboard", true, a, 4, (word_t) data, "standalone", "KBD"); return; }
    if (vga_is_in_range(a))      { difftest_dut_skipRef(getDPIModule()->core_pc, DIFFTEST_SKIP_REASON_MMIO); vga_write(a, (uint32_t)data, (uint8_t)strb); trace_record_dtrace(0, "vga", true, a, 4, (word_t) data, "standalone", "VGA"); return; }
    uint32_t word_addr = a & ~0x3u;
    if (!addr_valid(word_addr)) return;
    uint32_t wdata = (uint32_t)data;
    uint8_t  wstrb = (uint8_t)strb;
    uint32_t off = word_addr - PMEM_BASE;
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
    ifs.read((char *) pmem, PMEM_SIZE);
    size_t bytes = ifs.gcount();
    pmem_loaded_size = bytes;
    std::cout << "[standalone] Loaded " << bytes << " bytes from "
              << path << " into physical memory (0x80000000)" << std::endl;
}

uint8_t *standalone_mem_getPmemBase() {
    return pmem;
}

size_t standalone_mem_getLoadedSize() {
    return pmem_loaded_size;
}

size_t standalone_mem_getPmemSize() {
    return PMEM_SIZE;
}
