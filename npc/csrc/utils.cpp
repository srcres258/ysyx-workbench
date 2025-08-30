#include <dlfcn.h>
#include <capstone/capstone.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <iostream>
#include <utils.hpp>

// ----------- state -----------

SimConfig sim_config = {
    .config_itrace = false,
    .config_mtrace = false,
    .config_ftrace = false,
    .config_difftest = false,
    .config_device = false,

    .config_difftestPort = DEFAULT_DIFFTEST_PORT,

    .config_itraceOutFilePath =
        std::move(std::string(DEFAULT_ITRACE_OUT_FILE_PATH)),
    .config_mtraceOutFilePath =
        std::move(std::string(DEFAULT_MTRACE_OUT_FILE_PATH)),
    .config_ftraceOutFilePath =
        std::move(std::string(DEFAULT_FTRACE_OUT_FILE_PATH)),
    .config_elfFilePath =
        std::move(std::string(DEFAULT_ELF_FILE_PATH)),
    .config_difftestSoFilePath =
        std::move(std::string(DEFAULT_DIFFTEST_SO_FILE_PATH))
};

SimState sim_state = {
    .state = SIM_RUNNING,
    .haltPC = 0,

    .itrace_iringbuf = nullptr
};

/**
 * @brief 初始化用于 itrace 的环形缓冲区。
 */
void sim_state_itrace_iringbuf_init() {
    sim_state.itrace_iringbuf = new RingBuffer(ITRACE_IRINGBUF_SIZE);
}

/**
 * @brief 释放用于 itrace 的环形缓冲区。
 */
void sim_state_itrace_iringbuf_destroy() {
    if (sim_state.itrace_iringbuf) {
        delete sim_state.itrace_iringbuf;
        sim_state.itrace_iringbuf = nullptr;
    }
}

/**
 * @brief 输出用于 itrace 的环形缓冲区中的内容。
 */
void sim_state_itrace_iringbuf_dump() {
    try {
        auto *iringbuf = sim_state.itrace_iringbuf;
        std::string str = iringbuf->read(iringbuf->availableData());
        std::cout << "itrace_iringbuf data:" << std::endl;
        std::cout << str << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Failed to get iringbuf data: " << e.what() << std::endl;
    }
}

/**
 * @brief 初始化所有用于 trace 记录的文件输出流（ofstream）。
 */
void sim_state_ofstream_init() {
    if (sim_config.config_itrace) {
        sim_state.itrace_ofs.open(sim_config.config_itraceOutFilePath);
    }
    if (sim_config.config_mtrace) {
        sim_state.mtrace_ofs.open(sim_config.config_mtraceOutFilePath);
    }
    if (sim_config.config_ftrace) {
        sim_state.ftrace_ofs.open(sim_config.config_ftraceOutFilePath);
    }
}

/**
 * @brief 关闭所有用于 trace 记录的文件输出流（ofstream）。
 */
void sim_state_ofstream_finalise() {
    if (sim_config.config_itrace) {
        sim_state.itrace_ofs.flush();
    }
    if (sim_config.config_mtrace) {
        sim_state.mtrace_ofs.flush();
    }
    if (sim_config.config_ftrace) {
        sim_state.ftrace_ofs.flush();
    }
}

/**
 * @brief 根据相关配置，加载程序中的函数符号信息。
 * 需提前确保 sim_config 中相关配置信息已正确填入。
 * 
 * @return true 加载成功
 * @return false 加载失败
 */
bool sim_state_ftrace_funcSyms_init() {
    int fd;
    Elf *elf;
    size_t size;

    // Before the first call to elf_begin() ,
    // a program must call elf_version() to coordinate versions.
    if (elf_version(EV_CURRENT) == EV_NONE) {
        std::cerr << "libelf version is missing! ELF file will not be loaded." << std::endl;
        return false;
    }

    fd = open(sim_config.config_elfFilePath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "Failed to open ELF file: " << sim_config.config_elfFilePath << std::endl;
        return false;
    }
    elf = elf_begin(fd, ELF_C_READ_MMAP, nullptr);
    if (!elf) {
        std::cerr << "Failed to load ELF file: " << sim_config.config_elfFilePath << std::endl;
        close(fd);
        return false;
    }
    // 确定文件类型是否是ELF文件
    if (elf_kind(elf) != ELF_K_ELF) {
        std::cerr << "Not an ELF file: " << sim_config.config_elfFilePath << std::endl;
        elf_end(elf);
        close(fd);
        return false;
    }
    size = loadFunctionSymbolsFromElf(&sim_state.ftrace_funcSyms, elf);
    elf_end(elf);
    close(fd);
    std::cout << "Loaded " << size << " function symbols from ELF file: " <<
        sim_config.config_elfFilePath << std::endl;

    return true;
}

// ----------- disasm -----------

using cs_disasm_f_t = size_t (*)(csh handle, const uint8_t *code,
    size_t code_size, uint64_t address, size_t count, cs_insn **insn);
using cs_free_f_t = void (*)(cs_insn *insn, size_t count);
using cs_open_f_t = cs_err (*)(cs_arch arch, cs_mode mode, csh *handle);

static cs_disasm_f_t cs_disasm_dl = nullptr;
static cs_free_f_t cs_free_dl = nullptr;
static cs_open_f_t cs_open_dl = nullptr;

static csh handle;

/**
 * @brief 初始化反汇编工具。在使用本反汇编工具前须调用此函数。
 */
void disasm_init() {
    void *dl_handle;

    dl_handle = dlopen("libcapstone.so.5", RTLD_LAZY);
    assert(dl_handle);

    cs_open_dl = (cs_open_f_t) dlsym(dl_handle, "cs_open");
    assert(cs_open_dl);

    cs_disasm_dl = (cs_disasm_f_t) dlsym(dl_handle, "cs_disasm");
    assert(cs_disasm_dl);

    cs_free_dl = (cs_free_f_t) dlsym(dl_handle, "cs_free");
    assert(cs_free_dl);

    cs_arch arch = CS_ARCH_RISCV;
    cs_mode mode = (cs_mode) (CS_MODE_RISCV32 | CS_MODE_RISCVC);
    int ret = cs_open_dl(arch, mode, &handle);
    assert(ret == CS_ERR_OK);
}

/**
 * @brief 使用反汇编工具反汇编一段代码。
 * 
 * @param str 输出目的字符串缓冲区
 * @param size 字符串缓冲区大小
 * @param pc 程序计数器
 * @param code 待反汇编的代码段
 * @param nbyte 待反汇编的代码段长度
 */
void disasm_disassemble(
    char *str, int size, uint64_t pc,
    uint8_t *code, int nbyte
) {
    cs_insn *insn;
    size_t count = cs_disasm_dl(handle, code, nbyte, pc, 0, &insn);
    assert(count == 1);
    int ret = snprintf(str, size, "%s", insn->mnemonic);
    if (insn->op_str[0] != '\0') {
        snprintf(str + ret, size - ret, "\t%s", insn->op_str);
    }
    cs_free_dl(insn, count);
}

// ----------- memory -----------

word_t memoryHostRead(const void *addr, int len) {
    switch (len) {
        case 1:
            return *((uint8_t *) addr);
        case 2:
            return *((uint16_t *) addr);
        case 4:
            return *((uint32_t *) addr);
        case 8:
            return *((uint64_t *) addr);
        default:
            panic();
            return 0;
    }
}

void memoryHostWrite(void *addr, int len, word_t data) {
    switch (len) {
        case 1:
            *((uint8_t *) addr) = data;
            return;
        case 2:
            *((uint16_t *) addr) = data;
            return;
        case 4:
            *((uint32_t *) addr) = data;
            return;
        case 8:
            *((uint64_t *) addr) = data;
            return;
        default:
            panic();
    }
}
