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
#include <device/sram.hpp>
#include <sim_top.hpp>
#include <tui/tui_events.hpp>
#include "../npc/simulator_impl.hpp"

// ── Convenience: resolve active simulator (singleton, Task 2/6) ──
static inline SimulatorImpl& sim() {
    return *getActiveSimulator();
}

using ref_difftest_memcpy_f_t = void (*)(
    addr_t addr, void *buf, size_t n, bool direction
);
using ref_difftest_regcpy_f_t = void (*)(
    void *dut, bool direction
);
using ref_difftest_exec_f_t = void (*)(uint64_t n);
using ref_difftest_raise_intr_f_t = void (*)(word_t NO);
using ref_difftest_set_mem_map_f_t = void (*)(
    const DiffTestMemRegion *regions, size_t nr_regions
);
using ref_difftest_get_mem_map_f_t = size_t (*)(
    DiffTestMemRegion *regions, size_t max_regions
);
using ref_difftest_set_reset_vector_f_t = void (*)(uint64_t reset_vector);
using ref_difftest_init_f_t = void (*)(int port);

// ── Ownership moved to SimulatorImpl (Task 6):
//     function ptrs, pendingSkipRefPcs, skipDutNrInst, device mem bases ──

void difftest_dut_skipRef(addr_t pc, DiffTestSkipReason reason) {
    auto &s = sim();
    s.skipDutNrInst = 0;
    if (s.pendingSkipRefPcs.empty() || s.pendingSkipRefPcs.back().pc != pc) {
        s.pendingSkipRefPcs.push_back({ .pc = pc, .reason = reason });
    }
}

void difftest_dut_skipDut(int nr_ref, int nr_dut) {
    auto &s = sim();
    int i;
    
    s.skipDutNrInst += nr_dut;

    for (i = nr_ref; i --> 0;) {
        s.ref_difftest_exec(1);
    }
}

static void loadRefSymbols(void *dlHandle) {
    auto &s = sim();
    std::cout << "正在加载 difftest_memcpy ..." << std::endl;
    s.ref_difftest_memcpy = (ref_difftest_memcpy_f_t)
        dlsym(dlHandle, "difftest_memcpy");
    assert(s.ref_difftest_memcpy);

    std::cout << "正在加载 difftest_regcpy ..." << std::endl;
    s.ref_difftest_regcpy = (ref_difftest_regcpy_f_t)
        dlsym(dlHandle, "difftest_regcpy");
    assert(s.ref_difftest_regcpy);

    std::cout << "正在加载 difftest_exec ..." << std::endl;
    s.ref_difftest_exec = (ref_difftest_exec_f_t)
        dlsym(dlHandle, "difftest_exec");
    assert(s.ref_difftest_exec);

    std::cout << "正在加载 difftest_raise_intr ..." << std::endl;
    s.ref_difftest_raise_intr = (ref_difftest_raise_intr_f_t)
        dlsym(dlHandle, "difftest_raise_intr");
    assert(s.ref_difftest_raise_intr);

    std::cout << "正在加载 difftest_set_mem_map ..." << std::endl;
    s.ref_difftest_set_mem_map = (ref_difftest_set_mem_map_f_t)
        dlsym(dlHandle, "difftest_set_mem_map");
    assert(s.ref_difftest_set_mem_map);

    std::cout << "正在加载 difftest_get_mem_map ..." << std::endl;
    s.ref_difftest_get_mem_map = (ref_difftest_get_mem_map_f_t)
        dlsym(dlHandle, "difftest_get_mem_map");
    assert(s.ref_difftest_get_mem_map);

    std::cout << "正在加载 difftest_set_reset_vector ..." << std::endl;
    s.ref_difftest_set_reset_vector = (ref_difftest_set_reset_vector_f_t)
        dlsym(dlHandle, "difftest_set_reset_vector");
    assert(s.ref_difftest_set_reset_vector);

    std::cout << "正在加载 difftest_init ..." << std::endl;
    s.ref_difftest_init = (ref_difftest_init_f_t)
        dlsym(dlHandle, "difftest_init");
    assert(s.ref_difftest_init);
}

void difftest_dut_init(const char *refSoFile, int port) {
    auto &s = sim();
    assert(refSoFile != nullptr);
    std::println("[difftest] DiffTest 已启用! 目标 REF: {}", refSoFile);

    std::println("[difftest] 正在打开 REF 动态链接库文件...");
    void *handle = dlopen(refSoFile, RTLD_LAZY);
    assert(handle != nullptr);

    std::println("[difftest] 正在从 REF 加载符号...");
    loadRefSymbols(handle);

    std::println("[difftest] REF 加载完毕! 正在初始化 REF...");
    s.ref_difftest_init(port);

    const DiffTestMemRegion memMap[] = {
#ifdef NPC_STANDALONE
        { PSRAM_ADDR, standalone_mem_getPmemSize(), DIFFTEST_MEM_REGION_RAM },
#else
        { SRAM_ADDR,  SRAM_LEN,  DIFFTEST_MEM_REGION_RAM },
        { MROM_ADDR,  MROM_LEN,  DIFFTEST_MEM_REGION_RAM },
        { FLASH_ADDR, FLASH_LEN, DIFFTEST_MEM_REGION_RAM },
        { PSRAM_ADDR, PSRAM_LEN, DIFFTEST_MEM_REGION_RAM },
        { SDRAM_ADDR, SDRAM_LEN, DIFFTEST_MEM_REGION_RAM },
#endif
    };

    s.ref_difftest_set_mem_map(memMap, sizeof(memMap) / sizeof(memMap[0]));
    s.ref_difftest_set_reset_vector(sim_config.config_difftestStartPC);

    if (s.ref_difftest_get_mem_map) {
        DiffTestMemRegion debugMap[sizeof(memMap) / sizeof(memMap[0])] = {};
        size_t mapCount = s.ref_difftest_get_mem_map(
            debugMap,
            sizeof(debugMap) / sizeof(debugMap[0])
        );
        Assert(mapCount == sizeof(memMap) / sizeof(memMap[0]));
    }

    std::println("[difftest] 正在将初始数据同步给 REF...");
#ifdef NPC_STANDALONE
    s.ref_difftest_memcpy(
        PSRAM_ADDR,
        standalone_mem_getPmemBase(),
        standalone_mem_getLoadedSize(),
        DIFFTEST_TO_REF
    );
#else
    if (s.mrom_io_base != nullptr) {
        s.ref_difftest_memcpy(
            MROM_ADDR,
            s.mrom_io_base,
            MROM_LEN,
            DIFFTEST_TO_REF
        );
    }
    if (s.flash_io_base != nullptr) {
        s.ref_difftest_memcpy(
            FLASH_ADDR,
            s.flash_io_base,
            FLASH_LEN,
            DIFFTEST_TO_REF
        );
    }
#endif
    difftest_dut_syncCurrentProcessorState();
}

static void checkregs(ProcessorState *refState, addr_t pc) {
    if (!isaCheckRegisters(refState)) {
        std::println("[difftest] 检测到 DUT 与 REF 的处理器状态不一致! 正在中止...");
        sim_state.state = SIM_ABORT;
        sim_state.haltPC = pc;
        tui::g_eventFeed.push(
            getExecCount(), tui::EventType::DIFFTEST_MISMATCH,
            pc, 0,
            "DUT/REF register mismatch detected"
        );
        std::cout << "----- REF registers -----" << std::endl;
        refState->dump();
        std::cout << "----- DUT registers -----" << std::endl;
        isaRegDisplay();
    }
}

void difftest_dut_step(addr_t pc, addr_t npc) {
    auto &s = sim();
    ProcessorState refState;

    if (s.skipDutNrInst > 0) {
        s.ref_difftest_regcpy(&refState, DIFFTEST_TO_DUT);
        if (refState.pc == npc) {
            s.skipDutNrInst = 0;
            checkregs(&refState, npc);
            return;
        }
        s.skipDutNrInst--;
        if (s.skipDutNrInst == 0) {
            panic(
                "can not catch up with ref.pc = " FMT_WORD " at pc = " FMT_WORD,
                refState.pc, pc
            );
        }
        return;
    }

    if (!s.pendingSkipRefPcs.empty() && s.pendingSkipRefPcs.front().pc == pc) {
        auto skipEvent = s.pendingSkipRefPcs.front();
        s.pendingSkipRefPcs.pop_front();
        ProcessorState dutState = getProcessorState();
        s.ref_difftest_regcpy(&dutState, DIFFTEST_TO_REF);
        if (sim_config.config_debugOutput) {
            std::println(
                "[difftest] skipRef at pc=0x{:08x}, reason={}",
                pc,
                (int) skipEvent.reason
            );
        }
        return;
    }

    s.ref_difftest_exec(1);
    s.ref_difftest_regcpy(&refState, DIFFTEST_TO_DUT);

    checkregs(&refState, pc);
}

void difftest_dut_syncCurrentProcessorState() {
    auto &s = sim();
    ProcessorState state = getProcessorState();
    s.ref_difftest_regcpy(&state, DIFFTEST_TO_REF);
}

void difftest_dut_syncMemoryToRef(addr_t addr, const void *buf, size_t len) {
    auto &s = sim();
#ifdef NPC_STANDALONE
    (void) addr;
    (void) buf;
    (void) len;
#else
    if (s.ref_difftest_memcpy == nullptr || len == 0) {
        return;
    }
    s.ref_difftest_memcpy(addr, const_cast<void *>(buf), len, DIFFTEST_TO_REF);
#endif
}

void difftest_dut_clearSkipRef() {
    sim().pendingSkipRefPcs.clear();
}

void difftest_dut_syncPayloadMemoryToRef() {
#ifdef NPC_STANDALONE
    return;
#else
    auto &s = sim();
    const auto &cfg = sim_config;
    std::println(
        "[difftest] 正在将 payload 内存区域同步到 REF (mem_mode={})...", 
        cfg.config_difftestMemMode
    );

    auto syncRegion = [&](addr_t base, size_t len, const char *name, void *dutBase) {
        if (!dutBase) {
            std::println("[difftest] 警告: {} 设备未初始化，跳过同步", name);
            return;
        }
        std::println(
            "[difftest] 同步 {} [0x{:08x}, 0x{:08x}) {} 字节...",
            name, base, base + len, len
        );
        s.ref_difftest_memcpy(base, dutBase, len, DIFFTEST_TO_REF);
    };

    const bool copyPsram = cfg.config_difftestMemMode == "auto" ||
        cfg.config_difftestMemMode == "psram";
    const bool copySdram = cfg.config_difftestMemMode == "auto" ||
        cfg.config_difftestMemMode == "sdram";
    const bool copySram = cfg.config_difftestMemMode == "auto" ||
        cfg.config_difftestMemMode == "sram";

    if (copyPsram) {
        syncRegion(PSRAM_ADDR, PSRAM_LEN, "PSRAM", s.psram_io_base);
    }
    if (copySdram) {
        syncRegion(SDRAM_ADDR, SDRAM_LEN, "SDRAM", s.sdram_io_base);
    }
    if (copySram) {
        device_sram_syncShadowFromDUT(SRAM_ADDR, SRAM_LEN);
        syncRegion(SRAM_ADDR, SRAM_LEN, "SRAM", s.sram_io_base);
    }
#endif
}

bool difftest_dut_loadPayloadToBackingStore(const char *binFilePath, addr_t loadAddr) {
    auto &s = sim();
    if (binFilePath == nullptr || binFilePath[0] == '\0') {
        return true;
    }

    addr_t baseAddr;
    size_t maxAvail;
    void *backingStore;
    const char *regionName;
    bool isSramRegion = false;

    if (loadAddr >= PSRAM_ADDR && loadAddr < PSRAM_ADDR + PSRAM_LEN) {
        baseAddr = PSRAM_ADDR;
        maxAvail = PSRAM_ADDR + PSRAM_LEN - loadAddr;
        backingStore = s.psram_io_base;
        regionName = "PSRAM";
    } else if (loadAddr >= SDRAM_ADDR && loadAddr < SDRAM_ADDR + SDRAM_LEN) {
        baseAddr = SDRAM_ADDR;
        maxAvail = SDRAM_ADDR + SDRAM_LEN - loadAddr;
        backingStore = s.sdram_io_base;
        regionName = "SDRAM";
    } else if (loadAddr >= SRAM_ADDR && loadAddr < SRAM_ADDR + SRAM_LEN) {
        baseAddr = SRAM_ADDR;
        maxAvail = SRAM_ADDR + SRAM_LEN - loadAddr;
        backingStore = s.sram_io_base;
        regionName = "SRAM";
        isSramRegion = true;
    } else {
        std::println(
            "[difftest] 错误: Payload 加载地址 0x{:08x} 不在 PSRAM / SDRAM / "
                "SRAM 范围内!",
            loadAddr
        );
        return false;
    }

    if (!backingStore) {
        std::println(
            "[difftest] 错误: {} 后备存储未初始化, 无法加载 payload!",
            regionName
        );
        return false;
    }

    std::ifstream f(binFilePath, std::ios::binary | std::ios::ate);
    if (!f) {
        std::println("[difftest] 错误: 无法打开 payload 文件: {}", binFilePath);
        return false;
    }

    size_t fileSize = f.tellg();
    if (fileSize > maxAvail) {
        std::println(
            "[difftest] 错误: Payload 文件 {} 过大 ({} > {} max), 无法装入 {}!",
            binFilePath, fileSize, maxAvail, regionName
        );
        return false;
    }

    f.seekg(0, std::ios::beg);
    size_t offset = static_cast<size_t>(loadAddr - baseAddr);
    f.read(reinterpret_cast<char *>(backingStore) + offset, fileSize);
    if (f.fail()) {
        std::println("[difftest] 错误: 读取 payload 文件 {} 失败!", binFilePath);
        return false;
    }

    if (isSramRegion) {
        device_sram_syncDUTFromShadow(loadAddr, fileSize);
    }

    std::println(
        "[difftest] 成功加载 payload {} ({} 字节) 到 {} 地址 0x{:08x}",
        binFilePath, fileSize, regionName, loadAddr
    );
    return true;
}
