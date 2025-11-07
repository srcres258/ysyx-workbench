#ifndef __DEVICE__FLASH_HPP__
#define __DEVICE__FLASH_HPP__ 1

#include <common.hpp>

bool device_flash_init();

word_t device_flash_read(addr_t addr, int len);

#endif
