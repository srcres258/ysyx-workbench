#include <macro-def.hpp>
#include <utils/timer.hpp>
#include <device/map.hpp>
#include <device/rtc.hpp>

static void *rtc_base = nullptr;

static void rtc_io_handler(uint32_t offset, int len, bool isWrite) {
    uint32_t *rtcAddr;
    uint64_t us;

    rtcAddr = (uint32_t *) rtc_base;
    us = timer_getTimeElapsedUSec();
    rtcAddr[0] = (uint32_t) us;
    rtcAddr[1] = (uint32_t) (us >> 32);
}

void device_rtc_init() {
    rtc_base = device_map_newSpace(8);
    device_map_addMMIOMap(
        "rtc", RTC_MMIO_ADDR, rtc_base,
        8, rtc_io_handler
    );
}
