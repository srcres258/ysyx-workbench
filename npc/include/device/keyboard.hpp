#ifndef __DEVICE__KEYBOARD_HPP__
#define __DEVICE__KEYBOARD_HPP__ 1

#include <cstdint>

/**
 * @brief Keyboard MMIO base address (NEMU-compatible, 0xa0000060).
 *
 * The AM npc platform reads a 32-bit keycode from this address.
 * Format: bit 15 = keydown mask (0x8000), bits 14:0 = AM keycode.
 * NEMU_KEY_NONE (0) means no key event available.
 */
#define KEYBOARD_MMIO_BASE 0xa0000060UL

/**
 * @brief Check if the given address falls within keyboard MMIO space.
 *
 * The keyboard occupies 4 bytes at 0xa0000060-0xa0000063.
 */
static inline bool keyboard_is_in_range(uint32_t addr) {
    return addr >= KEYBOARD_MMIO_BASE && addr < KEYBOARD_MMIO_BASE + 4;
}

/**
 * @brief Poll SDL2 events and enqueue keyboard events into the key queue.
 *
 * Must be called periodically from the simulation loop (every clock cycle
 * when not in reset). Shared SDL2 context with VGA module.
 */
void keyboard_update();

/**
 * @brief Handle read from keyboard address space.
 *
 * Dequeues one key event from the FIFO queue. Returns NEMU_KEY_NONE (0)
 * if the queue is empty.
 *
 * @param addr  byte address (must be in [KEYBOARD_MMIO_BASE, KEYBOARD_MMIO_BASE+4))
 * @return 32-bit keycode with KEYDOWN_MASK
 */
uint32_t keyboard_read(uint32_t addr);

/**
 * @brief Handle write to keyboard address space (no-op).
 */
void keyboard_write(uint32_t addr, uint32_t data, uint8_t strb);

#endif /* __DEVICE__KEYBOARD_HPP__ */
