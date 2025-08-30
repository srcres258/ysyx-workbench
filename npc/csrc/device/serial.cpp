#include <iostream>
#include <macro-def.hpp>
#include <utils.hpp>
#include <device/map.hpp>
#include <device/serial.hpp>

#define CH_OFFSET 0

static uint8_t *serial_base = nullptr;

static void serial_putc(char ch) {
    std::cerr.put(ch);
    std::flush(std::cerr);
}

static void serial_io_handler(uint32_t offset, int len, bool isWrite) {
    Assert(len == 1);
    switch (offset) {
        case CH_OFFSET:
            if (isWrite) {
                serial_putc(serial_base[0]);
            } else {
                panic("do not support read");
            }
            break;
        default:
            panic("do not support offser = %d", offset);
    }
}

void device_serial_init() {
    serial_base = device_map_newSpace(8);
    device_map_addMMIOMap(
        "serial", SERIAL_MMIO_ADDR, serial_base,
        8, serial_io_handler
    );
}
