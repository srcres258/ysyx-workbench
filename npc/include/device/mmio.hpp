#ifndef __DEVICE__MMIO_HPP__
#define __DEVICE__MMIO_HPP__ 1

#include <common.hpp>

word_t device_mmio_read(addr_t addr, int len);

void device_mmio_write(addr_t addr, int len, word_t data);

#endif /* __DEVICE__MMIO_HPP__ */
