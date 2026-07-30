#include <SDL2/SDL.h>
#include <cstring>
#include <device/vga.hpp>
#include <utils/timer.hpp>

static SDL_Window   *vga_window   = nullptr;
static SDL_Renderer *vga_renderer = nullptr;
static SDL_Texture  *vga_texture  = nullptr;

static uint32_t vga_ctl[2];
static uint32_t vga_fb[VGA_FB_COUNT];

void vga_init() {
    vga_ctl[0] = (VGA_SCREEN_W << 16) | VGA_SCREEN_H;
    vga_ctl[1] = 0;
    std::memset(vga_fb, 0, sizeof(vga_fb));

    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer(
        VGA_SCREEN_W * 2, VGA_SCREEN_H * 2, 0,
        &vga_window, &vga_renderer
    );
    SDL_SetWindowTitle(vga_window, "NPC-Standalone-VGA");
    vga_texture = SDL_CreateTexture(
        vga_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, VGA_SCREEN_W, VGA_SCREEN_H
    );
    SDL_RenderPresent(vga_renderer);
}

/**
 * @brief Update the VGA display if the sync register is non-zero.
 *
 * Rate-limited to approximately TIMER_HZ (60) updates per second,
 * matching NEMU's device_update() behaviour.  Without rate limiting,
 * the __am_gpu_init() fill loop (120000 per-pixel syncs) would
 * trigger 120000 SDL renders on NPC standalone, making the simulation
 * unusably slow.
 */
void vga_update() {
    if (vga_ctl[1] == 0) return;

    static uint64_t lastRender = 0;
    uint64_t now = timer_getTimeElapsedUSec();
    // Render at most once every ~16.7 ms (60 Hz)
    if (now - lastRender < 1000000 / 60) {
        return;
    }
    lastRender = now;

    SDL_UpdateTexture(vga_texture, nullptr, vga_fb, VGA_SCREEN_W * sizeof(uint32_t));
    SDL_RenderClear(vga_renderer);
    SDL_RenderCopy(vga_renderer, vga_texture, nullptr, nullptr);
    SDL_RenderPresent(vga_renderer);
    vga_ctl[1] = 0;
}

void vga_cleanup() {
    if (vga_texture)  { SDL_DestroyTexture(vga_texture);  vga_texture  = nullptr; }
    if (vga_renderer) { SDL_DestroyRenderer(vga_renderer); vga_renderer = nullptr; }
    if (vga_window)   { SDL_DestroyWindow(vga_window);     vga_window   = nullptr; }
    SDL_Quit();
}

uint32_t vga_read(uint32_t addr) {
    if (addr >= VGA_CTL_BASE && addr < VGA_CTL_BASE + 8) {
        auto byteOff = addr & 3u;
        auto wordIdx  = (addr - VGA_CTL_BASE) / 4;
        return vga_ctl[wordIdx] >> (byteOff * 8);
    }
    if (addr >= VGA_FB_BASE && addr < VGA_FB_BASE + VGA_FB_SIZE) {
        auto byteOff  = addr & 3u;
        auto base     = addr & ~3u;
        auto pixelIdx = (base - VGA_FB_BASE) / 4;
        return vga_fb[pixelIdx] >> (byteOff * 8);
    }
    return 0;
}

/**
 * @brief Write selected bytes of a 32-bit word with AXI4 byte-strobe.
 *
 * AXI4 WSTRB[i] gates WDATA byte i (bits [8i+7 : 8i]).
 * For full-word writes (strb=0xF), this is equivalent to
 *   *reinterpret_cast<uint32_t*>(&dst) = data
 * on a little-endian host, matching NEMU's host_write() behaviour.
 */
static inline void maskedWrite(uint32_t &dst, uint32_t data, uint8_t strb) {
    for (int i = 0; i < 4; i++) {
        if (strb & (1u << i)) {
            uint8_t *bp = (uint8_t *)&dst + i;
            *bp = (uint8_t)(data >> (i * 8));
        }
    }
}

void vga_write(uint32_t addr, uint32_t data, uint8_t strb) {
    if (addr >= VGA_CTL_BASE && addr < VGA_CTL_BASE + 8) {
        auto wordIdx = (addr - VGA_CTL_BASE) / 4;
        if (wordIdx < 2) {
            maskedWrite(vga_ctl[wordIdx], data, strb);
        }
        return;
    }
    if (addr >= VGA_FB_BASE && addr < VGA_FB_BASE + VGA_FB_SIZE) {
        auto pixelIdx = (addr - VGA_FB_BASE) / 4;
        if (pixelIdx >= VGA_FB_COUNT)
            return;
        maskedWrite(vga_fb[pixelIdx], data, strb);
    }
}
