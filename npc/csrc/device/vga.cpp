#include <string>
#include <iostream>
#include <SDL2/SDL.h>
#include <macro-def.hpp>
#include <common.hpp>
#include <device/map.hpp>
#include <device/vga.hpp>

static inline uint32_t screenWidth() {
    return VGA_SCREEN_W;
}

static inline uint32_t screenHeight() {
    return VGA_SCREEN_H;
}

static inline uint32_t screenSize() {
    return screenWidth() * screenHeight() * sizeof(uint32_t);
}

static void *vga_ctl_base = nullptr;
static void *vga_fb_base = nullptr;

static SDL_Renderer *renderer = nullptr;
static SDL_Texture *texture = nullptr;

static void initScreen() {
    SDL_Window *window = nullptr;
    std::string title("VGA Display");
    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer(
        VGA_SCREEN_W * 2, VGA_SCREEN_H * 2, 0,
        &window, &renderer
    );
    SDL_SetWindowTitle(window, title.c_str());
    texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC,
        screenWidth(), screenHeight()
    );
    SDL_RenderPresent(renderer);
}

static void updateScreen() {
    SDL_UpdateTexture(
        texture, nullptr, vga_fb_base,
        screenWidth() * sizeof(uint32_t)
    );
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

static bool readSyncStat() {
    uint32_t *vgaCtrlAddr = (uint32_t *) vga_ctl_base;
    return vgaCtrlAddr[1];
}

static void resetSyncStat() {
    uint32_t *vgaCtrlAddr = (uint32_t *) vga_ctl_base;
    vgaCtrlAddr[1] = 0;
}

void device_vga_updateScreen() {
    if (readSyncStat()) {
        updateScreen();
        resetSyncStat();
    }
}

void device_vga_init() {
    vga_ctl_base = device_map_newSpace(8);
    uint32_t *vgaCtlAddr = (uint32_t *) vga_ctl_base;
    vgaCtlAddr[0] = (screenWidth() << 16) | screenHeight();
    device_map_addMMIOMap(
        "vga_ctl", VGA_CTL_MMIO_ADDR, vga_ctl_base,
        8, nullptr
    );

    vga_fb_base = device_map_newSpace(screenSize());
    device_map_addMMIOMap(
        "vga_fb", VGA_FB_MMIO_ADDR, vga_fb_base,
        screenSize(), nullptr
    );
    initScreen();
    memset(vga_fb_base, 0, screenSize());
}
