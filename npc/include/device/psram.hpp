#ifndef __DEVICE__PSRAM_HPP__
#define __DEVICE__PSRAM_HPP__

#include <common.hpp>

bool device_psram_init();

word_t device_psram_read(addr_t addr, int len);

void device_psram_write(addr_t addr, int len, word_t data);

#endif /* __DEVICE__PSRAM_HPP__ */
