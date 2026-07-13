#ifndef __DEVICE__SRAM_HPP__
#define __DEVICE__SRAM_HPP__ 1

#include <common.hpp>

extern void *sram_io_base;

bool device_sram_init();

word_t device_sram_read(addr_t addr, int len);

void device_sram_write(addr_t addr, int len, word_t data);

void device_sram_syncShadowFromDUT(addr_t addr, size_t len);

void device_sram_syncDUTFromShadow(addr_t addr, size_t len);

#endif /* __DEVICE__SRAM_HPP__ */
