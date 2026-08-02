#ifndef NPC_MEMORY_HPP
#define NPC_MEMORY_HPP

#include <cstddef>
#include <cstdint>

namespace npc {

struct MemoryRegion {
    std::uint32_t baseAddr;
    std::size_t   size;
};

} // namespace npc

#endif // NPC_MEMORY_HPP
