#ifndef __DEVICE__MAP_HPP__
#define __DEVICE__MAP_HPP__ 1

#include <string>
#include <common.hpp>

using io_callback_t = void (*)(addr_t offset, int len, bool isWrite);

uint8_t *device_map_newSpace(int size);

struct IOMap {
    std::string name;
    addr_t low;
    addr_t high;
    void *space;
    io_callback_t callback;

    bool isInside(addr_t addr) const;

    word_t read(addr_t addr, int len) const;

    void write(addr_t addr, int len, word_t data) const;
};

int device_map_findMapIdByAddr(const IOMap *maps, int size, addr_t addr);

void device_map_addMMIOMap(
    std::string name, addr_t addr, void *space,
    uint32_t len, io_callback_t callback
);

void device_map_init();

#endif /* __DEVICE__MAP_HPP__ */
