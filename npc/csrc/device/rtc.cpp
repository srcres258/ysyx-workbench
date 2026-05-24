#include <device/rtc.hpp>
#include <utils/timer.hpp>

uint32_t rtc_read(uint32_t addr) {
    uint64_t us = timer_getTimeElapsedUSec();
    if ((addr & 4) == 0) {
        return (uint32_t)us;
    } else {
        return (uint32_t)(us >> 32);
    }
}

void rtc_write(uint32_t addr, uint32_t data, uint8_t strb) {
    (void)addr;
    (void)data;
    (void)strb;
}
