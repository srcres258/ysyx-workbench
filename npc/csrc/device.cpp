#include <SDL2/SDL.h>
#include <macro-def.hpp>
#include <utils/timer.hpp>
#include <utils.hpp>
#include <device/io/map.hpp>
#include <device.hpp>

#include <device/mrom.hpp>

void device_update() {
    // TODO: currently no device update needed.
}

bool device_init() {
    device_io_map_init();

    if (!device_mrom_init())
        return false;

    return true;
}
