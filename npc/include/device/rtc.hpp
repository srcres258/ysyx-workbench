#ifndef __DEVICE__RTC_HPP__
#define __DEVICE__RTC_HPP__ 1

#include <cstdint>

/**
 * @brief RTC MMIO base address (NEMU-compatible, 0xa0000048).
 *
 * The AM npc platform reads a 64-bit microsecond counter from this address
 * as two 32-bit words (low word at +0, high word at +4).
 * The value is sourced from the same wall-clock timer used by CLINT MTIME
 * (timer_getTimeElapsedUSec() in utils/timer.hpp).
 */
#define RTC_MMIO_BASE 0xa0000048UL

/**
 * @brief Check if the given address falls within RTC MMIO space.
 *
 * The RTC occupies 8 bytes at 0xa0000048-0xa000004f.
 */
static inline bool rtc_is_in_range(uint32_t addr) {
    return addr >= RTC_MMIO_BASE && addr < RTC_MMIO_BASE + 8;
}

/**
 * @brief Handle read from RTC address space.
 *
 * @param addr  byte address in [RTC_MMIO_BASE, RTC_MMIO_BASE+8)
 * @return 32-bit word: low word if (addr & 4) == 0, high word otherwise
 */
uint32_t rtc_read(uint32_t addr);

/**
 * @brief Handle write to RTC address space (no-op: RTC is read-only).
 */
void rtc_write(uint32_t addr, uint32_t data, uint8_t strb);

#endif /* __DEVICE__RTC_HPP__ */
