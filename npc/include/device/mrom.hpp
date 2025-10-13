#ifndef __DEVICE__MROM_HPP__
#define __DEVICE__MROM_HPP__ 1

#include <common.hpp>

bool device_mrom_init();

word_t device_mrom_read(addr_t addr, int len);

#endif /* __DEVICE__MROM_HPP__ */
