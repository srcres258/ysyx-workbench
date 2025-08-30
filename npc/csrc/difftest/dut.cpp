#include <dlfcn.h>
#include <cstddef>
#include <cassert>
#include <print>
#include <cstring>
#include <difftest/dut.hpp>
#include <memory.hpp>
#include <processor.hpp>
#include <isa.hpp>
#include <utils.hpp>

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

static bool isSkipRef = false;
static int skipDutNrInst = 0;

void difftest_dut_skipRef() {
    isSkipRef = true;
    skipDutNrInst = 0;
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

void difftest_dut_init(const char *refSoFile, size_t imgSize, int port) {
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
    ref_difftest_memcpy(MEMORY_OFFSET, memory, imgSize, DIFFTEST_TO_REF);
    ProcessorState state = getProcessorState();
    ref_difftest_regcpy(&state, DIFFTEST_TO_REF);
}

static void checkregs(ProcessorState *refState, addr_t pc) {
    if (!isaCheckRegisters(refState)) {
        std::println("[difftest] 检测到 DUT 与 REF 的处理器状态不一致! 正在中止...");
        sim_state.state = SIM_ABORT;
        sim_state.haltPC = pc;
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

    if (isSkipRef) {
        // to skip the checking of an instruction,
        // just copy the reg state to reference design
        ProcessorState dutState = getProcessorState();
        ref_difftest_regcpy(&dutState, DIFFTEST_TO_REF);
        isSkipRef = false;
        return;
    }

    ref_difftest_exec(1);
    ref_difftest_regcpy(&refState, DIFFTEST_TO_DUT);

    checkregs(&refState, pc);
}
