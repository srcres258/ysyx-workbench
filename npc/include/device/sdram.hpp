#ifndef __DEVICE__SDRAM_HPP__
#define __DEVICE__SDRAM_HPP__

#include <common.hpp>

bool device_sdram_init();

word_t device_sdram_read(addr_t addr, int len);

void device_sdram_write(addr_t addr, int len, word_t data);

#endif /* __DEVICE__SDRAM_HPP__ */
