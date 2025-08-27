#ifndef __COMMON_HPP__
#define __COMMON_HPP__ 1

#include <cstdint>
#include <cinttypes>

#ifdef CONFIG_RV64
typedef uint64_t word_t;
typedef int64_t sword_t;
#define FMT_WORD "0x%16" PRIx64
#else
typedef uint32_t word_t;
typedef int32_t sword_t;
#define FMT_WORD "0x%08" PRIx32
#endif

typedef word_t addr_t;
#define FMT_ADDR "0x%08" PRIx32
typedef uint16_t ioaddr_t;

#endif /* __COMMON_HPP__ */
