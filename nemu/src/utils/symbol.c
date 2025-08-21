#include <common.h>
#include <libelf.h>
#include <fcntl.h>
#include <gelf.h>
#include <unistd.h>

#include <utils/symbol.h>

size_t load_function_symbols_from_elf(Symbol *dest, Elf *elf) {
    Elf_Scn     *scn;
    GElf_Shdr   shdr;
    Elf_Data    *data;
    GElf_Sym    sym;
    size_t      i, ii, count;

    scn = NULL;
    i = 0;
    while ((scn = elf_nextscn(elf, scn))) {
        gelf_getshdr(scn, &shdr);
        if (shdr.sh_type == SHT_SYMTAB || shdr.sh_type == SHT_DYNSYM) {
            data = elf_getdata(scn, NULL);
            count = shdr.sh_size / shdr.sh_entsize;
            // sym.st_info == 18的时候是func类型
            for (ii = 0; ii < count; ii++) {
                gelf_getsym(data, ii, &sym);
                // 只记录函数符号表
                if (ELF32_ST_TYPE(sym.st_info) != STT_FUNC) {
                    continue;
                }
                printf("Found function: %016lx %4ld %s\n", sym.st_value, sym.st_size,
                        elf_strptr(elf, shdr.sh_link, sym.st_name));
                dest[i].addr = sym.st_value;
                strcpy(dest[i].name, elf_strptr(elf, shdr.sh_link, sym.st_name));
                dest[i].size = sym.st_size;
                i++;
            }
        }
    }

    return i;
}