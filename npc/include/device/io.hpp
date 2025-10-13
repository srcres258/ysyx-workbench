#ifndef __DEVICE__IO_HPP__
#define __DEVICE__IO_HPP__ 1

#include <string>
#include <common.hpp>

using io_callback_t = void (*)(addr_t offset, int len, bool isWrite);

void device_io_addMMIOMap(
    std::string name, addr_t addr, void *space,
    uint32_t len, io_callback_t callback
);

#endif /* __DEVICE__IO_HPP__ */
