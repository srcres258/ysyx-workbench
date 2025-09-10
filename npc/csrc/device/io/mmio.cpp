#include <utils.hpp>
#include <memory.hpp>
#include <device/map.hpp>
#include <device/mmio.hpp>

#define NR_MAPS 16

#define PMEM_LEFT MEMORY_OFFSET
#define PMEM_RIGHT (MEMORY_OFFSET + PHYS_MEMORY_SIZE - 1)

static IOMap maps[NR_MAPS] = {};
static int nr_maps = 0;

static IOMap *fetchMMIOMap(addr_t addr) {
    int mapId = device_map_findMapIdByAddr(maps, nr_maps, addr);
    return mapId == -1 ? nullptr : &maps[mapId];
}

static void reportMMIOOverlap(
    std::string name1, addr_t l1, addr_t r1,
    std::string name2, addr_t l2, addr_t r2
) {
    panic(
        "MMIO region %s@[" FMT_ADDR ", " FMT_ADDR "] is overlapped "
            "with %s@[" FMT_ADDR ", " FMT_ADDR "]",
        name1.c_str(), l1, r1, name2.c_str(), l2, r2
    );
}

void device_map_addMMIOMap(
    std::string name, addr_t addr, void *space,
    uint32_t len, io_callback_t callback
) {
    int i;
    IOMap *map;

    Assert(nr_maps < NR_MAPS);
    addr_t left = addr;
    addr_t right = addr + len - 1;
    if (isPhysMemoryAddr(left) || isPhysMemoryAddr(right)) {
        reportMMIOOverlap(name, left, right, "pmem", PMEM_LEFT, PMEM_RIGHT);
    }
    for (i = 0; i < nr_maps; i++) {
        map = &maps[i];
        if (left <= map->high && right >= map->low) {
            reportMMIOOverlap(name, left, right, map->name, map->low, map->high);
        }
    }

    IOMap newMap = {
        .name = name,
        .low = addr,
        .high = addr + len - 1,
        .space = space,
        .callback = callback
    };
    maps[nr_maps] = newMap;
    printf(
        "Add mmio map '%s' at [" FMT_ADDR ", " FMT_ADDR "]\n",
        newMap.name.c_str(), newMap.low, newMap.high
    );
    nr_maps++;
}

word_t device_mmio_read(addr_t addr, int len) {
    IOMap *map = fetchMMIOMap(addr);
    if (map == nullptr) {
        panic("device_mmio_read failed to find a device map at " FMT_ADDR ", read len 0x%08x\n", addr, len);
    }
    Assert(len == 1 || len == 2 || len == 4 || len == 8);
    return map->read(addr, len);
}

void device_mmio_write(addr_t addr, int len, word_t data) {
    IOMap *map = fetchMMIOMap(addr);
    if (map == nullptr) {
        panic("device_mmio_write failed to find a device map at " FMT_ADDR ", write len 0x%08x, data " FMT_WORD "\n", addr, len, data);
    }
    Assert(len == 1 || len == 2 || len == 4 || len == 8);
    map->write(addr, len, data);
}
