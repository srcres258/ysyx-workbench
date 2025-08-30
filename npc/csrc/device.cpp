#include <device/map.hpp>
#include <device.hpp>

#include <device/serial.hpp>

void device_init() {
    device_map_init();

    device_serial_init();
}
