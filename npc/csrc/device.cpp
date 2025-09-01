#include <SDL2/SDL.h>
#include <macro-def.hpp>
#include <utils/timer.hpp>
#include <device/map.hpp>
#include <utils.hpp>
#include <device.hpp>

#include <device/serial.hpp>
#include <device/rtc.hpp>
#include <device/vga.hpp>
#include <device/keyboard.hpp>

static uint64_t lastUpdateTime = 0;

void device_update() {
    uint64_t now = timer_getTimeElapsedUSec();
    if (now - lastUpdateTime < 1000000 / TIMER_HZ) {
        return;
    }
    lastUpdateTime = now;

    device_vga_updateScreen();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                sim_state.state = SIM_QUIT;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                uint8_t k = event.key.keysym.scancode;
                bool isKeyDown = event.key.type == SDL_KEYDOWN;
                device_keyboard_sendKey(k, isKeyDown);
                break;
        }
    }
}

void device_init() {
    device_map_init();

    device_serial_init();
    device_rtc_init();
    device_vga_init();
    device_keyboard_init();
}
