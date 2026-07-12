#include <dlfcn.h>
#include <cstddef>
#include <cassert>
#include <print>
#include <cstring>
#include <deque>
#include <fstream>
#include <processor.hpp>
#include <isa.hpp>
#include <utils.hpp>
#include <macro-def.hpp>
#include <difftest/dut.hpp>
#include <sim_top.hpp>
#include <tui/tui_events.hpp>

using ref_difftest_memcpy_f_t = void (*)(
    addr_t addr, void *buf, size_t n, bool direction
);
using ref_difftest_regcpy_f_t = void (*)(
    void *dut, bool direction
);
using ref_difftest_exec_f_t = void (*)(uint64_t n);
using ref_difftest_raise_intr_f_t = void (*)(word_t NO);
using ref_difftest_init_f_t = void (*)(int port);

static ref_difftest_memcpy_f_t ref_difftest_memcpy = nullptr;
static ref_difftest_regcpy_f_t ref_difftest_regcpy = nullptr;
static ref_difftest_exec_f_t ref_difftest_exec = nullptr;
static ref_difftest_raise_intr_f_t ref_difftest_raise_intr = nullptr;
static ref_difftest_init_f_t ref_difftest_init = nullptr;

static std::deque<addr_t> pendingSkipRefPcs;
static int skipDutNrInst = 0;

void difftest_dut_skipRef(addr_t pc) {
    skipDutNrInst = 0;
    if (pendingSkipRefPcs.empty() || pendingSkipRefPcs.back() != pc) {
        pendingSkipRefPcs.push_back(pc);
    }
}

void difftest_dut_skipDut(int nr_ref, int nr_dut) {
    int i;
    
    skipDutNrInst += nr_dut;

    for (i = nr_ref; i --> 0;) {
        ref_difftest_exec(1);
    }
}

static void loadRefSymbols(void *dlHandle) {
    std::cout << "正在加载 difftest_memcpy ..." << std::endl;
    ref_difftest_memcpy = (ref_difftest_memcpy_f_t) dlsym(dlHandle, "difftest_memcpy");
    assert(ref_difftest_memcpy);

    std::cout << "正在加载 difftest_regcpy ..." << std::endl;
    ref_difftest_regcpy = (ref_difftest_regcpy_f_t) dlsym(dlHandle, "difftest_regcpy");
    assert(ref_difftest_regcpy);

    std::cout << "正在加载 difftest_exec ..." << std::endl;
    ref_difftest_exec = (ref_difftest_exec_f_t) dlsym(dlHandle, "difftest_exec");
    assert(ref_difftest_exec);

    std::cout << "正在加载 difftest_raise_intr ..." << std::endl;
    ref_difftest_raise_intr = (ref_difftest_raise_intr_f_t) dlsym(dlHandle, "difftest_raise_intr");
    assert(ref_difftest_raise_intr);

    std::cout << "正在加载 difftest_init ..." << std::endl;
    ref_difftest_init = (ref_difftest_init_f_t) dlsym(dlHandle, "difftest_init");
    assert(ref_difftest_init);
}

extern void *flash_io_base;
extern void *mrom_io_base;
extern void *psram_io_base;
extern void *sdram_io_base;

void difftest_dut_init(const char *refSoFile, int port) {
    assert(refSoFile != nullptr);
    std::println("[difftest] DiffTest 已启用! 目标 REF: {}", refSoFile);

    std::println("[difftest] 正在打开 REF 动态链接库文件...");
    void *handle = dlopen(refSoFile, RTLD_LAZY);
    assert(handle != nullptr);

    std::println("[difftest] 正在从 REF 加载符号...");
    loadRefSymbols(handle);

    std::println("[difftest] REF 加载完毕! 正在初始化 REF...");
    ref_difftest_init(port);

    std::println("[difftest] 正在将初始数据同步给 REF...");
    ref_difftest_memcpy(FLASH_ADDR, flash_io_base, FLASH_LEN, DIFFTEST_TO_REF);
    ref_difftest_memcpy(MROM_ADDR, mrom_io_base, MROM_LEN, DIFFTEST_TO_REF);
    difftest_dut_syncCurrentProcessorState();
}

static void checkregs(ProcessorState *refState, addr_t pc) {
    if (!isaCheckRegisters(refState)) {
        std::println("[difftest] 检测到 DUT 与 REF 的处理器状态不一致! 正在中止...");
        sim_state.state = SIM_ABORT;
        sim_state.haltPC = pc;
        tui::g_eventFeed.push(getExecCount(), tui::EventType::DIFFTEST_MISMATCH,
                              pc, 0,
                              "DUT/REF register mismatch detected");
        std::cout << "----- REF registers -----" << std::endl;
        refState->dump();
        std::cout << "----- DUT registers -----" << std::endl;
        isaRegDisplay();
    }
}

void difftest_dut_step(addr_t pc, addr_t npc) {
    ProcessorState refState;

    if (skipDutNrInst > 0) {
        ref_difftest_regcpy(&refState, DIFFTEST_TO_DUT);
        if (refState.pc == npc) {
            skipDutNrInst = 0;
            checkregs(&refState, npc);
            return;
        }
        skipDutNrInst--;
        if (skipDutNrInst == 0) {
            panic(
                "can not catch up with ref.pc = " FMT_WORD
                " at pc = " FMT_WORD,
                refState.pc, pc
            );
        }
        return;
    }

    if (!pendingSkipRefPcs.empty() && pendingSkipRefPcs.front() == pc) {
        pendingSkipRefPcs.pop_front();
        // to skip the checking of an instruction,
        // just copy the reg state to reference design
        ProcessorState dutState = getProcessorState();
        ref_difftest_regcpy(&dutState, DIFFTEST_TO_REF);
        return;
    }

    ref_difftest_exec(1);
    ref_difftest_regcpy(&refState, DIFFTEST_TO_DUT);

    checkregs(&refState, pc);
}

void difftest_dut_syncCurrentProcessorState() {
    ProcessorState state = getProcessorState();
    ref_difftest_regcpy(&state, DIFFTEST_TO_REF);
}

void difftest_dut_clearSkipRef() {
    pendingSkipRefPcs.clear();
}

void difftest_dut_syncPayloadMemoryToRef() {
    const auto &cfg = sim_config;
    std::println("[difftest] 正在将 payload 内存区域同步到 REF (mem_mode={})...", cfg.config_difftestMemMode);

    auto syncRegion = [](addr_t base, size_t len, const char *name, void *dutBase) {
        if (!dutBase) {
            std::println("[difftest] 警告: {} 设备未初始化，跳过同步", name);
            return;
        }
        std::println("[difftest] 同步 {} [0x{:08x}, 0x{:08x}) {} 字节...", name, base, base + len, len);
        ref_difftest_memcpy(base, dutBase, len, DIFFTEST_TO_REF);
    };

    const bool copyPsram = cfg.config_difftestMemMode == "auto" || cfg.config_difftestMemMode == "psram";
    const bool copySdram = cfg.config_difftestMemMode == "auto" || cfg.config_difftestMemMode == "sdram";

    if (copyPsram) {
        syncRegion(PSRAM_ADDR, PSRAM_LEN, "PSRAM", psram_io_base);
    }
    if (copySdram) {
        syncRegion(SDRAM_ADDR, SDRAM_LEN, "SDRAM", sdram_io_base);
    }
}

bool difftest_dut_loadPayloadToBackingStore(const char *binFilePath, addr_t loadAddr) {
    if (binFilePath == nullptr || binFilePath[0] == '\0') {
        return true;  // 无文件, 静默成功
    }

    // Determine which backing store to use based on load address
    addr_t baseAddr;
    size_t maxAvail;
    void *backingStore;
    const char *regionName;

    if (loadAddr >= PSRAM_ADDR && loadAddr < PSRAM_ADDR + PSRAM_LEN) {
        baseAddr = PSRAM_ADDR;
        maxAvail = PSRAM_ADDR + PSRAM_LEN - loadAddr;
        backingStore = psram_io_base;
        regionName = "PSRAM";
    } else if (loadAddr >= SDRAM_ADDR && loadAddr < SDRAM_ADDR + SDRAM_LEN) {
        baseAddr = SDRAM_ADDR;
        maxAvail = SDRAM_ADDR + SDRAM_LEN - loadAddr;
        backingStore = sdram_io_base;
        regionName = "SDRAM";
    } else {
        std::println("[difftest] 错误: Payload 加载地址 0x{:08x} 不在 PSRAM 或 SDRAM 范围内!", loadAddr);
        return false;
    }

    if (!backingStore) {
        std::println("[difftest] 错误: {} 后备存储未初始化, 无法加载 payload!", regionName);
        return false;
    }

    std::ifstream f(binFilePath, std::ios::binary | std::ios::ate);
    if (!f) {
        std::println("[difftest] 错误: 无法打开 payload 文件: {}", binFilePath);
        return false;
    }

    size_t fileSize = f.tellg();
    if (fileSize > maxAvail) {
        std::println("[difftest] 错误: Payload 文件 {} 过大 ({} > {} max), 无法装入 {}!",
                     binFilePath, fileSize, maxAvail, regionName);
        return false;
    }

    f.seekg(0, std::ios::beg);
    size_t offset = static_cast<size_t>(loadAddr - baseAddr);
    f.read(reinterpret_cast<char *>(backingStore) + offset, fileSize);
    if (f.fail()) {
        std::println("[difftest] 错误: 读取 payload 文件 {} 失败!", binFilePath);
        return false;
    }

    std::println("[difftest] 成功加载 payload {} ({} 字节) 到 {} 地址 0x{:08x}",
                 binFilePath, fileSize, regionName, loadAddr);
    return true;
}
