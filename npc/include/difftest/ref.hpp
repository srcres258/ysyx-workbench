#ifndef __DIFFTEST__REF_HPP__
#define __DIFFTEST__REF_HPP__ 1

#include <cstddef>
#include <common.hpp>
#include <difftest-def.hpp>

__EXPORT_C void difftest_memcpy(addr_t addr, void *buf, size_t n);
__EXPORT_C void difftest_regcpy(void *dut, bool direction);
__EXPORT_C void difftest_exec(uint64_t n);
__EXPORT_C void difftest_raise_intr(word_t NO);
__EXPORT_C void difftest_init(int port);

#endif
