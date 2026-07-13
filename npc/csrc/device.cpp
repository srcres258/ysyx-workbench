#include <SDL2/SDL.h>
#include <macro-def.hpp>
#ifndef NPC_STANDALONE
#include <nvboard.h>
#endif
#include <utils/timer.hpp>
#include <utils.hpp>
#include <device/io/map.hpp>
#include <device.hpp>

#include <device/mrom.hpp>
#include <device/flash.hpp>
#include <device/psram.hpp>
#include <device/sdram.hpp>
#include <device/sram.hpp>

void device_update() {
#ifndef NPC_STANDALONE
    if (sim_config.config_nvboard) {
        nvboard_update();
    }
#endif
}

bool device_init() {
    device_io_map_init();

    if (!device_mrom_init())
        return false;
    if (!device_flash_init())
        return false;
    if (!device_psram_init())
        return false;
    if (!device_sdram_init())
        return false;
    if (!device_sram_init())
        return false;

    return true;
}
