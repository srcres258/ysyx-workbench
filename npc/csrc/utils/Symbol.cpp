#include <gelf.h>
#include <print>
#include <cstring>
#include <utils/Symbol.hpp>

/**
 * @brief 从给定 ELF 文件中加载函数符号信息。
 * 
 * @param dest 容纳目的符号的 vector
 * @param elf ELF 文件
 * @return size_t 总共加载的符号数量
 */
size_t loadFunctionSymbolsFromElf(
    std::vector<Symbol> *dest,
    Elf *elf
) {
    Elf_Scn     *scn;
    GElf_Shdr   shdr;
    Elf_Data    *data;
    GElf_Sym    sym;
    size_t      i, ii, count;

    scn = nullptr;
    i = 0;
    while ((scn = elf_nextscn(elf, scn))) {
        gelf_getshdr(scn, &shdr);
        if (shdr.sh_type == SHT_SYMTAB || shdr.sh_type == SHT_DYNSYM) {
            data = elf_getdata(scn, nullptr);
            count = shdr.sh_size / shdr.sh_entsize;
            // sym.st_info == 18的时候是func类型
            for (ii = 0; ii < count; ii++) {
                gelf_getsym(data, ii, &sym);
                // 只记录函数类型的符号
                if (ELF32_ST_TYPE(sym.st_info) == STT_FUNC) {
                    std::println(
                        "Found function: {:#016x} {:4} {}",
                        sym.st_value, sym.st_size,
                        elf_strptr(elf, shdr.sh_link, sym.st_name)
                    );
                    char buf[256];
                    strcpy(buf, elf_strptr(elf, shdr.sh_link, sym.st_name));
                    Symbol symObj = {
                        .addr = (addr_t) sym.st_value,
                        .name = std::move(std::string(buf)),
                        .size = sym.st_size
                    };
                    dest->push_back(symObj);
                    i++;
                }
            }
        }
    }

    return i;
}
