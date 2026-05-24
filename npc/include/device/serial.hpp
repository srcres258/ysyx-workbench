#ifndef __DEVICE__SERIAL_HPP__
#define __DEVICE__SERIAL_HPP__ 1

#include <cstdint>

/**
 * @brief Serial port MMIO address (NEMU-compatible).
 *
 * The AM npc platform writes characters to this address via outb().
 * The standalone NPC DPI-C memory backend intercepts writes here and
 * prints the byte to stdout, mimicking NEMU's serial device behaviour.
 */
#define SERIAL_MMIO_BASE 0xa00003f8UL

/**
 * @brief Check if the given address falls within serial port MMIO space.
 *
 * The serial port occupies 4 bytes at 0xa00003f8-0xa00003fb (one word-aligned slot).
 */
static inline bool serial_is_in_range(uint32_t addr) {
    return addr >= SERIAL_MMIO_BASE && addr < SERIAL_MMIO_BASE + 4;
}

#endif /* __DEVICE__SERIAL_HPP__ */
