#ifndef __UTILS_SYMBOL_H__
#define __UTILS_SYMBOL_H__

#include <common.h>
#include <libelf.h>
#include <stdint.h>

typedef struct {
  word_t addr;
  char name[256];
  word_t size;
} Symbol;

size_t load_function_symbols_from_elf(Symbol *dest, Elf *elf);

#endif
