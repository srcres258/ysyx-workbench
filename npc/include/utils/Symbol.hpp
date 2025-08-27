#ifndef __UTILS_SYMBOL_HPP__
#define __UTILS_SYMBOL_HPP__ 1

#include <libelf.h>
#include <cstdint>
#include <vector>
#include <string>
#include <common.hpp>

/**
 * @brief 位于程序段中的一个符号。
 */
struct Symbol {
    addr_t addr;
    std::string name;
    size_t size;
};

/**
 * @brief 从给定 ELF 文件中加载函数符号信息。
 * 
 * @param dest 容纳目的符号的 vector
 * @param elf ELF 文件
 * @return size_t 总共加载的符号数量
 */
size_t loadFunctionSymbolsFromElf(std::vector<Symbol> *dest, Elf *elf);

#endif /* __UTILS_SYMBOL_HPP__ */