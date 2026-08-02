#ifndef __DEVICE__VGA_HPP__
#define __DEVICE__VGA_HPP__ 1

#include <cstdint>

// VGA MMIO address map (NEMU-compatible, used by AM npc platform gpu.c)
#define VGA_CTL_BASE    0xa0000100UL
#define VGA_FB_BASE     0xa1000000UL

// Screen resolution (matches NEMU default: 400×300)
#define VGA_SCREEN_W    400
#define VGA_SCREEN_H    300
#define VGA_FB_COUNT    (VGA_SCREEN_W * VGA_SCREEN_H)
#define VGA_FB_SIZE     (VGA_FB_COUNT * 4)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize VGA device: allocate framebuffer and init control registers.
 *        When showWindow is true, also create the SDL2 window and texture.
 */
void vga_init(bool showWindow);

/**
 * @brief Check sync register. If set, render the framebuffer via SDL2 and
 *        clear the sync register. Called periodically from the simulation loop.
 */
void vga_update();

/**
 * @brief Clean up VGA resources (SDL2 window, texture, renderer).
 */
void vga_cleanup();

/**
 * @brief Handle read from VGA address space (called from DPI-C pmem_read).
 *
 * @param addr  byte address in [VGA_CTL_BASE, VGA_CTL_BASE+8) or [VGA_FB_BASE, VGA_FB_BASE+VGA_FB_SIZE)
 * @return 32-bit word read from the address
 */
uint32_t vga_read(uint32_t addr);

/**
 * @brief Handle write to VGA address space (called from DPI-C pmem_write).
 *
 * @param addr  byte address
 * @param data  32-bit word to write
 * @param strb  byte strobe mask (bit 0=byte0, bit 1=byte1, etc.)
 */
void vga_write(uint32_t addr, uint32_t data, uint8_t strb);

/**
 * @brief Check if the given address falls within VGA MMIO space.
 */
static inline bool vga_is_in_range(uint32_t addr) {
    return (addr >= VGA_CTL_BASE && addr < VGA_CTL_BASE + 8)
        || (addr >= VGA_FB_BASE  && addr < VGA_FB_BASE  + VGA_FB_SIZE);
}

#ifdef __cplusplus
}
#endif

#endif /* __DEVICE__VGA_HPP__ */
