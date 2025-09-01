#include <ctime>
#include <random>
#include <utils/timer.hpp>

static_assert(CLOCKS_PER_SEC == 1000000, "CLOCKS_PER_SEC != 1000000");
static_assert(sizeof(clock_t) == 8, "sizeof(clock_t) != 8");

static uint64_t bootTime = 0;

static uint64_t getTimeInternal() {
    timespec now;
    uint64_t us;
    
    // 我们直接使用 clock_gettime 函数来获取时间。
    clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
    us = now.tv_sec * 1000000 + now.tv_nsec / 1000;

    return us;
}

uint64_t timer_getTimeElapsedUSec() {
    uint64_t now;

    if (bootTime == 0) {
        bootTime = getTimeInternal();
    }
    now = getTimeInternal();

    return now - bootTime;
}

void timer_initRand() {
    srand(getTimeInternal());
}
