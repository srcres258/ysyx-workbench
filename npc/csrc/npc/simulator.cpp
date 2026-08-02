#include "simulator_impl.hpp"
#include "npc/simulator.hpp"
#include "npc/state.hpp"
#include <sim_top.hpp>
#include <utils.hpp>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>
#include <iomanip>
#include <cmath>
#include <print>
#include <utils.hpp>
#include <sdb.hpp>
#include <difftest/dut.hpp>
#include <device.hpp>
#include <utils/Stage.hpp>
#include <utils/timer.hpp>
#include <tui/tui_events.hpp>
#include <tui/tui_control.hpp>
#include <tui/terminal.hpp>
#include <tui/renderer.hpp>
#include <tui/npc_snapshot.hpp>
#include <tui/tui_config.hpp>
#include <tui/layout.hpp>
#include <tui/panel.hpp>
#include <tui/tui_actions.hpp>
#include <tui/tui_overlay.hpp>
#include <perf.hpp>
#ifndef NPC_STANDALONE
#include <nvboard.h>
#else
#include <device/vga.hpp>
#include <device/keyboard.hpp>
#endif
#include <unistd.h>
#include <sys/select.h>

// ── NVBoard internal symbols (file-scope so unqualified lookup works) ──
#ifndef NPC_STANDALONE
extern void nvboard_uart_update(void);
extern unsigned char *vga_blank_n_ptr;
extern void vga_update(void);
extern void nvboard_bind_all_pins(VysyxSoCFull* top);
extern void uart_set_divisor(unsigned short);
#endif

// ── Active-instance bridge ──
static SimulatorImpl s_impl;
SimulatorImpl* getActiveSimulator() { return &s_impl; }

void setActiveSimulator(SimulatorImpl* impl) {
    if (impl && getActiveSimulator() && impl != getActiveSimulator()) {
        std::fprintf(
            stderr,
            "[npc] ERROR: only one active simulator per process is "
            "supported in this pass\n"
        );
        std::abort();
    }
    (void)impl;
}

// ── Authoritative config & state storage (Task 3) ──
SimConfig sim_config = {
    .config_itrace = false,
    .config_mtrace = false,
    .config_ftrace = false,
    .config_dtrace = false,
    .config_etrace = false,
    .config_difftest = false,
    .config_device = false,
    .config_wave = false,
    .config_debugOutput = false,
    .config_nvboard = false,
    .config_vga = false,
    .config_mrom = false,
    .config_tui = false,
    .config_perf = false,
    .config_traceFormat = std::move(std::string(DEFAULT_TRACE_FORMAT)),
    .config_traceDataMode = std::move(std::string(DEFAULT_TRACE_DATA_MODE)),
    .config_tuiConfigFilePath =
        std::move(std::string(DEFAULT_TUI_CONFIG_FILE_PATH)),
    .config_tuiGenerateConfig = false,
    .config_tuiGenerateFullConfig = false,
    .config_tuiForceOverwriteConfig = false,
    .config_tuiPrintConfigSchema = false,
    .config_tuiPrintDefaultConfig = false,
    .config_difftestPort = DEFAULT_DIFFTEST_PORT,
    .config_difftestStartMode =
        std::move(std::string(DEFAULT_DIFFTEST_START_MODE)),
    .config_difftestStartPC = DEFAULT_DIFFTEST_START_PC,
    .config_difftestPayloadBinFilePath =
        std::move(std::string(DEFAULT_DIFFTEST_PAYLOAD_BIN_FILE_PATH)),
    .config_difftestPayloadLoadAddr = DEFAULT_DIFFTEST_PAYLOAD_LOAD_ADDR,
    .config_difftestMemMode =
        std::move(std::string(DEFAULT_DIFFTEST_MEM_MODE)),
    .config_itraceOutFilePath =
        std::move(std::string(DEFAULT_ITRACE_OUT_FILE_PATH)),
    .config_itraceJsonlOutFilePath =
        std::move(std::string(DEFAULT_ITRACE_JSONL_OUT_FILE_PATH)),
    .config_mtraceOutFilePath =
        std::move(std::string(DEFAULT_MTRACE_OUT_FILE_PATH)),
    .config_mtraceJsonlOutFilePath =
        std::move(std::string(DEFAULT_MTRACE_JSONL_OUT_FILE_PATH)),
    .config_ftraceOutFilePath =
        std::move(std::string(DEFAULT_FTRACE_OUT_FILE_PATH)),
    .config_dtraceOutFilePath =
        std::move(std::string(DEFAULT_DTRACE_OUT_FILE_PATH)),
    .config_dtraceJsonlOutFilePath =
        std::move(std::string(DEFAULT_DTRACE_JSONL_OUT_FILE_PATH)),
    .config_etraceOutFilePath =
        std::move(std::string(DEFAULT_ETRACE_OUT_FILE_PATH)),
    .config_etraceJsonlOutFilePath =
        std::move(std::string(DEFAULT_ETRACE_JSONL_OUT_FILE_PATH)),
    .config_flashBinFilePath =
        std::move(std::string(DEFAULT_FLASH_BIN_FILE_PATH)),
    .config_flashElfFilePath =
        std::move(std::string(DEFAULT_FLASH_ELF_FILE_PATH)),
    .config_mromBinFilePath =
        std::move(std::string(DEFAULT_MROM_BIN_FILE_PATH)),
    .config_difftestSoFilePath =
        std::move(std::string(DEFAULT_DIFFTEST_SO_FILE_PATH)),
    .config_waveFilePath =
        std::move(std::string(DEFAULT_WAVE_FILE_PATH))
};

SimState sim_state = {
    .state = SIM_RUNNING,
    .haltPC = 0,
    .itrace_iringbuf = nullptr,
    .itrace_jsonl_ofs = std::ofstream{},
    .mtrace_jsonl_ofs = std::ofstream{},
    .dtrace_jsonl_ofs = std::ofstream{},
    .etrace_jsonl_ofs = std::ofstream{}
};

// ── Authoritative lifecycle globals ──
VerilatedContext *verContext = nullptr;

#ifdef NPC_STANDALONE
Vysyx_25070190 *top = nullptr;
#else
VysyxSoCFull *top = nullptr;
#endif

VerilatedFstC *tfp         = nullptr;
ExecInfo       simExecInfo = { .pc = 0x00000000, .inst = 0 };

volatile bool  sim_halt            = false;
uint64_t       execCount           = 0;
uint64_t       execCountClockPeriod = 0;
bool           s_difftestActive    = false;

// ── Signal handler ──
static void sigint_handler(int) {
    sim_halt = true;
}

void install_signal_handlers() {
    std::signal(SIGINT,  sigint_handler);
    std::signal(SIGTERM, sigint_handler);
}

// ── DPI module accessor ──
#ifdef NPC_STANDALONE
Vysyx_25070190_GeneralDPIAdapter *getDPIModule() {
    return top->ysyx_25070190->dpi;
}
#else
VysyxSoCFull_GeneralDPIAdapter *getDPIModule() {
    return top->ysyxSoCFull->asic->cpu->cpu->dpi;
}
#endif

// ── Execution counter accessors ──
uint64_t getExecCount()           { return execCount; }
uint64_t getExecCountClockPeriod() { return execCountClockPeriod; }
bool     isDifftestActive()       { return s_difftestActive; }

// ═══════════════════════════════════════════════════════════════════
//  Internal lifecycle helpers  (was static functions in sim.cpp)
// ═══════════════════════════════════════════════════════════════════

namespace npc { namespace internal {

static void flushOutput() {
    fflush(stdout);
    fflush(stderr);
}

static void traceAndDiffTest() {
    if (sim_config.config_difftest) {
        if (!s_difftestActive && simExecInfo.pc >= sim_config.config_difftestStartPC) {
            s_difftestActive = true;
            std::println(
                "[difftest] 在 PC=0x{:08x} 处激活 DiffTest 比较 (startPC=0x{:08x})",
                simExecInfo.pc, sim_config.config_difftestStartPC
            );
            tui::g_eventFeed.push(
                execCount, tui::EventType::DIFFTEST_ACTIVATE,
                simExecInfo.pc, sim_config.config_difftestStartPC,
                "DiffTest comparison activated"
            );
            difftest_dut_syncCurrentProcessorState();
            difftest_dut_syncPayloadMemoryToRef();
            difftest_dut_clearSkipRef();
            return;
        }
        if (s_difftestActive) {
            if (sim_halt)
                return;
            difftest_dut_step(simExecInfo.pc, getDPIModule()->core_pc);
        }
    }
    sdb_evalAndUpdateWP();
}

static void execute(uint64_t n) {
    for (uint64_t i = n; i > 0; i--) {
        if (!simExecOnceImpl()) {
            sim_state.state = SIM_ABORT;
            tui::g_eventFeed.push(
                execCount, tui::EventType::ABORT,
                simExecInfo.pc, 0,
                "Simulation aborted — simExecOnce returned false"
            );
            break;
        }
        flushOutput();
        if (tui::isPauseRequested()) {
            sim_state.state = SIM_STOP;
            tui::clearPauseRequest();
            break;
        }
        if (sim_halt) {
            sim_state.state = SIM_END;
            sim_state.haltPC = getDPIModule()->core_pc;
        }
        traceAndDiffTest();
        if (sim_state.state != SIM_RUNNING) {
            break;
        }
        if (sim_config.config_device) {
            device_update();
        }
    }
}

void simStepClockImpl() {
    top->clock = 0;
    top->eval();
    if (tfp) {
        verContext->timeInc(1);
        tfp->dump(verContext->time());
    }
    top->clock = 1;
    top->eval();
    if (tfp) {
        verContext->timeInc(1);
        tfp->dump(verContext->time());
    }

#ifndef NPC_STANDALONE
    if (!top->reset && sim_config.config_nvboard) {
        nvboard_uart_update();
    }
    if (!top->reset && sim_config.config_nvboard) {
        if (*vga_blank_n_ptr) vga_update();
    }
#endif

#ifdef NPC_STANDALONE
    if (!top->reset) {
        keyboard_update();
        if (sim_config.config_vga) {
            vga_update();
        }
    }
#endif

    execCountClockPeriod++;
    if (!top->reset && sim_config.config_perf) {
        perf::getPerfMonitor().sampleCycle(getDPIModule());
    }
}

void simStepImpl() {
    bool executionBegun = false;
    do {
        if (sim_halt || tui::isPauseRequested())
            break;
        simStepClockImpl();
        if (top->reset || sim_halt || tui::isPauseRequested())
            break;
        if (getDPIModule()->core_executing && !executionBegun) {
            executionBegun = true;
        }
    } while (!executionBegun || (executionBegun && getDPIModule()->core_executing));
}

void simResetImpl(int n) {
    if (sim_config.config_perf) {
        perf::getPerfMonitor().onResetBegin();
    }
    top->reset = 1;
    while (n--) {
        simStepClockImpl();
    }
    top->reset = 0;
    if (sim_config.config_perf) {
        perf::getPerfMonitor().onResetEnd();
    }
}

bool simExecOnceImpl() {
    if (sim_config.config_debugOutput)
        std::cout << "处理器开始执行第 " << std::dec << execCount << " 条指令 (从 0 开始算)..." << std::endl;

    auto *dpi = getDPIModule();
    addr_t pc = dpi->core_pc;
    if (sim_config.config_debugOutput)
        std::cout << "当前PC(即将执行的指令位置): 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << pc << std::endl;

    simStepImpl();
    if (sim_config.config_debugOutput) {
        std::cout << "当前指令: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << simExecInfo.inst << std::endl;
    }

    if (sim_state.itrace_ofs.is_open()) {
        char pbuf[128];
        char *p = pbuf;
        p += snprintf(p, sizeof(pbuf), FMT_WORD ":", simExecInfo.pc);
        int ilen = 4;
        int i;
        uint8_t *inst = (uint8_t *) &simExecInfo.inst;
        for (i = ilen - 1; i >= 0; i--) {
            p += snprintf(p, 4, " %02x", inst[i]);
        }
        int ilen_max = 4;
        int space_len = ilen_max - ilen;
        space_len = std::max(space_len, 0);
        space_len = space_len * 3 + 1;
        memset(p, ' ', space_len);
        p += space_len;
        disasm_disassemble(p, pbuf + sizeof(pbuf) - p, simExecInfo.pc, inst, ilen);

        std::string str(pbuf);
        str += "\n";
        auto *iringbuf = sim_state.itrace_iringbuf;
        if (str.length() > iringbuf->availableSpace()) {
            iringbuf->discard(str.length(), true);
        }
        iringbuf->write(str);
        sim_state.itrace_ofs << str;
        std::flush(sim_state.itrace_ofs);
        trace_record_itrace(simExecInfo.pc, simExecInfo.inst);

        if (sim_config.config_debugOutput) {
            std::cout << str;
            std::flush(std::cout);
        }
    }

    execCount++;
    return true;
}

void simExecImpl(uint64_t n) {
    word_t halt_ret;
    switch (sim_state.state) {
        case SIM_END:
        case SIM_ABORT:
        case SIM_QUIT:
            if (sim_config.config_debugOutput)
                std::cout << "即将退出仿真！" << std::endl;
            return;
        default:
            sim_state.state = SIM_RUNNING;
    }

    execute(n);

    switch (sim_state.state) {
        case SIM_RUNNING:
            sim_state.state = SIM_STOP;
            break;
        case SIM_END:
        case SIM_ABORT:
            halt_ret = getDPIModule()->gpr_gprs_10;
            std::cout << "仿真: " << (sim_state.state == SIM_ABORT ?
                    ANSI_FMT("ABORT", ANSI_FG_RED) :
                    (halt_ret == 0 ? ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN) :
                        ANSI_FMT("HIT BAD TRAP", ANSI_FG_RED))) <<
                " at pc = 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << sim_state.haltPC << std::dec <<
                ", 结果: " << halt_ret << std::endl;
            std::cout << "仿真结束, 共执行 " << std::dec << execCount <<
                " 条指令, 耗时 " << std::dec << execCountClockPeriod << " 个时钟周期." << std::endl;
            if (execCountClockPeriod > 0) {
                double ipc = static_cast<double>(execCount) / static_cast<double>(execCountClockPeriod);
                std::cout << "IPC = " << std::fixed << std::setprecision(4) << ipc << std::endl;
            }
            if (sim_config.config_perf) {
                const char *perfStrictEnv = std::getenv("NPC_CONFIG_PERF_CHECK_STRICT");
                if (perfStrictEnv && std::strcmp(perfStrictEnv, "on") == 0) {
                    perf::getPerfMonitor().setStrict(true);
                }
                perf::getPerfMonitor().dumpSummary(std::cout);
                perf::getPerfMonitor().dumpJson("build/perf/perf.json");
            }

            if (sim_state.state == SIM_END) {
                tui::g_eventFeed.push(
                    execCount,
                    (halt_ret == 0) ? tui::EventType::TRAP_GOOD : tui::EventType::TRAP_BAD,
                    sim_state.haltPC, halt_ret,
                    (halt_ret == 0) ? "Good trap" : "Bad trap"
                );
            }
    }

    if (sim_config.config_difftest && !s_difftestActive
        && (sim_state.state == SIM_END || sim_state.state == SIM_STOP)) {
        std::println(
            stderr,
            "[difftest] 错误: DiffTest 已启用但从未激活! 配置的 startPC=0x{:08x} "
            "在仿真过程中从未到达 (共执行 {} 条指令).",
            sim_config.config_difftestStartPC, execCount
        );
        std::println(
            stderr,
            "[difftest] 请检查: (1) startPC 是否设置正确; (2) 程序是否确实会执行到该地址."
        );
        tui::g_eventFeed.push(
            execCount, tui::EventType::DIFFTEST_WARNING,
            sim_config.config_difftestStartPC, execCount,
            "DiffTest enabled but startPC never reached"
        );
    }
}

void simExecClockPeriodImpl(uint64_t n) {
    for (uint64_t i = n; i > 0; i--) {
        if (sim_config.config_debugOutput)
            std::cout << "处理器开始执行第 " << std::dec << execCountClockPeriod << " 个时钟周期 (从 0 开始算)..." << std::endl;
        simStepClockImpl();
    }
}

}} // namespace npc::internal

// ═══════════════════════════════════════════════════════════════════
//  npc::Simulator::Impl  —  minimal wrapper
// ═══════════════════════════════════════════════════════════════════

class npc::Simulator::Impl {
public:
    Impl() = default;
    ~Impl() = default;

    void shutdownResources() {
        if (m_shutdown) return;
        m_shutdown = true;

#ifndef NPC_STANDALONE
        if (sim_config.config_nvboard) {
            nvboard_quit();
        }
#else
        if (sim_config.config_vga) {
            vga_cleanup();
        }
#endif
        if (tfp) {
            tfp->close();
        }
        delete top;
        top = nullptr;

        if (sim_config.config_itrace) {
            sim_state_itrace_iringbuf_destroy();
        }

        sim_state_ofstream_finalise();

        if (tfp) {
            delete tfp;
            tfp = nullptr;
        }

        delete verContext;
        verContext = nullptr;
    }

private:
    bool m_shutdown = false;
};

// ═══════════════════════════════════════════════════════════════════
//  npc::Simulator  constructor / destructor / lifecycle methods
// ═══════════════════════════════════════════════════════════════════

npc::Simulator::Simulator() : impl_(std::make_unique<Impl>()) {
    s_impl.owner = this;
}

npc::Simulator::~Simulator() {
    s_impl.owner = nullptr;
    impl_->shutdownResources();
}

bool npc::Simulator::initialize(
    const SimulatorConfig& config,
    int verilatorArgc,
    const char *verilatorArgv[]
) {
    applySimulatorConfig(config);

    install_signal_handlers();

    tui::initEventFeed();
    tui::g_eventFeed.push(
        0, tui::EventType::CONFIG, 0,
        static_cast<word_t>(sim_config.config_tui ? 1 : 0),
        sim_config.config_tui ? "TUI mode enabled" : "TUI mode disabled"
    );

    timer_initRand();
    disasm_init();

    if (sim_config.config_itrace) {
        sim_state_itrace_iringbuf_init();
    }
    if (sim_config.config_ftrace) {
        if (!sim_state_ftrace_funcSyms_init()) {
            std::cerr << "函数符号表加载失败，请确保 ELF 文件路径正确！" << std::endl;
            return false;
        }
    }
    sim_state_ofstream_init();

    verContext = new VerilatedContext;
    verContext->commandArgs(verilatorArgc, verilatorArgv);

#ifdef NPC_STANDALONE
    top = new Vysyx_25070190(verContext);
    const char *imgVal = std::getenv("IMG");
    if (imgVal) {
        standalone_mem_loadBin(imgVal);
    } else {
        std::cerr << "[standalone] IMG env var not set, no binary loaded!" << std::endl;
    }
#else
    top = new VysyxSoCFull(verContext);
#endif

    if (sim_config.config_wave) {
        tfp = new VerilatedFstC;
        verContext->traceEverOn(true);
        top->trace(tfp, 0);
        tfp->open(sim_config.config_waveFilePath.c_str());
    }

    if (sim_config.config_device) {
#ifndef NPC_STANDALONE
        if (sim_config.config_nvboard) {
            fprintf(
                stderr, "[NPC] Entering NVBoard init path (config_device=%d, config_nvboard=%d)\n",
                sim_config.config_device, sim_config.config_nvboard
            );
            nvboard_bind_all_pins(top);
            nvboard_init();
            fprintf(stderr, "[NPC] nvboard_init() returned\n");
            uart_set_divisor(80);
            if (sim_config.config_debugOutput)
                std::cout << "NVBoard 已初始化." << std::endl;
        }
#endif
    }

    if (sim_config.config_device) {
        if (sim_config.config_debugOutput)
            std::cout << "正在加载外部设备..." << std::endl;
        if (!device_init()) {
            std::cerr << "外部设备加载失败! 退出..." << std::endl;
            if (tfp) { tfp->close(); delete tfp; tfp = nullptr; }
            delete top; top = nullptr;
            delete verContext; verContext = nullptr;
            return false;
        }
    }

    if (sim_config.config_difftest) {
        if (sim_config.config_debugOutput)
            std::cout << "正在加载 DiffTest..." << std::endl;
        std::cout << "[sim] DiffTest 起始模式: " << sim_config.config_difftestStartMode
                  << ", 起始 PC: 0x" << std::hex << sim_config.config_difftestStartPC
                  << ", 内存模式: " << sim_config.config_difftestMemMode
                  << std::dec << std::endl;
        if (sim_config.config_difftestStartMode == "payload") {
            std::cout << "[sim] Payload BIN: " << sim_config.config_difftestPayloadBinFilePath
                      << ", 加载地址: 0x" << std::hex
                      << sim_config.config_difftestPayloadLoadAddr
                      << std::dec << std::endl;
        }
        difftest_dut_init(
            sim_config.config_difftestSoFilePath.c_str(),
            sim_config.config_difftestPort
        );

        if (!sim_config.config_difftestPayloadBinFilePath.empty()) {
            std::println("[sim] 正在将 Payload 二进制加载到后备存储...");
            if (!difftest_dut_loadPayloadToBackingStore(
                sim_config.config_difftestPayloadBinFilePath.c_str(),
                sim_config.config_difftestPayloadLoadAddr
            )) {
                std::println(stderr, "[sim] 致命: Payload 加载失败, 退出!");
                if (tfp) { tfp->close(); delete tfp; tfp = nullptr; }
                delete top; top = nullptr;
                delete verContext; verContext = nullptr;
                return false;
            }
            {
                addr_t execRegionBase = 0;
                if (
                    sim_config.config_difftestStartPC >= PSRAM_ADDR &&
                    sim_config.config_difftestStartPC < PSRAM_ADDR + PSRAM_LEN
                ) {
                    execRegionBase = PSRAM_ADDR;
                } else if (
                    sim_config.config_difftestStartPC >= SDRAM_ADDR &&
                    sim_config.config_difftestStartPC < SDRAM_ADDR + SDRAM_LEN
                ) {
                    execRegionBase = SDRAM_ADDR;
                } else if (
                    sim_config.config_difftestStartPC >= SRAM_ADDR &&
                    sim_config.config_difftestStartPC < SRAM_ADDR + SRAM_LEN
                ) {
                    execRegionBase = SRAM_ADDR;
                }
                addr_t loadRegionBase = 0;
                if (
                    sim_config.config_difftestPayloadLoadAddr >= PSRAM_ADDR &&
                    sim_config.config_difftestPayloadLoadAddr < PSRAM_ADDR + PSRAM_LEN
                ) {
                    loadRegionBase = PSRAM_ADDR;
                } else if (
                    sim_config.config_difftestPayloadLoadAddr >= SDRAM_ADDR &&
                    sim_config.config_difftestPayloadLoadAddr < SDRAM_ADDR + SDRAM_LEN
                ) {
                    loadRegionBase = SDRAM_ADDR;
                } else if (
                    sim_config.config_difftestPayloadLoadAddr >= SRAM_ADDR &&
                    sim_config.config_difftestPayloadLoadAddr < SRAM_ADDR + SRAM_LEN
                ) {
                    loadRegionBase = SRAM_ADDR;
                }
                if (execRegionBase != 0 && execRegionBase != loadRegionBase) {
                    std::println(
                        "[sim] startPC=0x{:08x} 所在执行区域与 loadAddr=0x{:08x} 不同, "
                        "同步加载 Payload 到执行区域基址 0x{:08x}...",
                        sim_config.config_difftestStartPC,
                        sim_config.config_difftestPayloadLoadAddr,
                        execRegionBase
                    );
                    if (!difftest_dut_loadPayloadToBackingStore(
                        sim_config.config_difftestPayloadBinFilePath.c_str(),
                        execRegionBase
                    )) {
                        std::println(stderr, "[sim] 致命: 执行区域 Payload 加载失败, 退出!");
                        if (tfp) { tfp->close(); delete tfp; tfp = nullptr; }
                        delete top; top = nullptr;
                        delete verContext; verContext = nullptr;
                        return false;
                    }
                }
            }
        } else {
            if (
                (sim_config.config_difftestStartPC >= PSRAM_ADDR &&
                    sim_config.config_difftestStartPC < PSRAM_ADDR + PSRAM_LEN) ||
                (sim_config.config_difftestStartPC >= SDRAM_ADDR &&
                    sim_config.config_difftestStartPC < SDRAM_ADDR + SDRAM_LEN) ||
                (sim_config.config_difftestStartPC >= SRAM_ADDR &&
                    sim_config.config_difftestStartPC < SRAM_ADDR + SRAM_LEN)
            ) {
                std::println(
                    stderr,
                    "[sim] 警告: startPC=0x{:08x} 在 PSRAM/SDRAM/SRAM 内但未指定 Payload BIN 文件. "
                    "Activation 时内存同步会将空数据发到 REF, 可能导致 INVALID OPCODE 崩溃.",
                    sim_config.config_difftestStartPC
                );
            }
        }
    }

    if (sim_config.config_debugOutput)
        std::cout << "正在重置处理器..." << std::endl;
    internal::simResetImpl(15);

#ifdef NPC_STANDALONE
    vga_init(sim_config.config_vga);
#endif

    return true;
}

bool npc::Simulator::run(bool sdbEnabled) {
    word_t halt_ret;
    bool success = true;

    if (sim_config.config_debugOutput)
        std::cout << "正在启动仿真..." << std::endl;

    if (sim_config.config_tui) {
        bool kbOk = true;
        const auto &kb = tui::g_tuiConfig.keybindings;
        kbOk &= tui::validateKeyBinding(kb.focus_next,       "focus_next");
        kbOk &= tui::validateKeyBinding(kb.focus_prev,       "focus_prev");
        kbOk &= tui::validateKeyBinding(kb.help_overlay,     "help_overlay");
        kbOk &= tui::validateKeyBinding(kb.maximize_toggle,  "maximize_toggle");
        kbOk &= tui::validateKeyBinding(kb.panel_picker,     "panel_picker");
        kbOk &= tui::validateKeyBinding(kb.pause_resume,     "pause_resume");
        kbOk &= tui::validateKeyBinding(kb.quit,             "quit");
        kbOk &= tui::validateKeyBinding(kb.reset,            "reset");
        kbOk &= tui::validateKeyBinding(kb.resize_down,      "resize_down");
        kbOk &= tui::validateKeyBinding(kb.resize_left,      "resize_left");
        kbOk &= tui::validateKeyBinding(kb.resize_right,     "resize_right");
        kbOk &= tui::validateKeyBinding(kb.resize_up,        "resize_up");
        kbOk &= tui::validateKeyBinding(kb.step_clock,       "step_clock");
        kbOk &= tui::validateKeyBinding(kb.step_instruction, "step_instruction");
        kbOk &= tui::validateKeyBinding(kb.tab_next,         "tab_next");
        if (!kbOk) {
            std::fprintf(stderr, "[tui] fatal: keybinding validation failed — aborting\n");
            success = false;
            goto run_cleanup;
        }

        tui::TerminalSession term;
        if (!term) {
            std::fprintf(stderr, "[tui] terminal session init failed — aborting\n");
            success = false;
            goto run_cleanup;
        }

        sdb_init();

        tui::LayoutTree layout;
        if (!layout.buildFromPreset(tui::g_tuiConfig.layout.preset)) {
            std::fprintf(
                stderr, "[tui] fatal: unknown layout preset \"%s\"\n",
                tui::g_tuiConfig.layout.preset.c_str()
            );
            success = false;
            goto run_cleanup;
        }

        tui::PanelRegistry::instance().registerBuiltins();

        if (!tui::PanelRegistry::instance().validateLayout(layout)) {
            std::fprintf(
                stderr, "[tui] fatal: layout references unknown panel IDs. "
                "Check your [layout] preset or panel registrations.\n"
            );
            success = false;
            goto run_cleanup;
        }

        tui::ActionMap actionMap;
        actionMap.buildFromConfig(tui::g_tuiConfig.keybindings);

        tui::Overlay overlay;

        auto sz = term.querySize();
        tui::Renderer renderer;
        renderer.resize(sz.rows, sz.cols);

        std::vector<std::string> pickerIds;

        bool running = true;
        while (running) {
            if (term.consumeResizeFlag()) {
                sz = term.querySize();
                renderer.resize(sz.rows, sz.cols);
            }

            if (sim_state.state == SIM_RUNNING) {
                constexpr int kRefreshBurst = 500;
                for (int bi = 0; bi < kRefreshBurst && sim_state.state == SIM_RUNNING; bi++) {
                    internal::execute(1);
                }
            }

            renderer.beginFrame();

            if (renderer.terminalTooSmall()) {
                renderer.endFrame();
                renderer.flush();
                usleep(50000);
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                struct timeval tv{0, 0};
                if (::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
                    auto ch = tui::readKeyChord();
                    if (ch && actionMap.lookup(*ch) == tui::Action::QUIT)
                        running = false;
                }
                continue;
            }

            uint16_t statusRows = tui::g_tuiConfig.ui.status_bar ? 1 : 0;
            layout.compute(sz.rows, sz.cols, statusRows);

            if (layout.terminalTooSmall()) {
                renderer.endFrame();
                renderer.flush();
                usleep(50000);
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                struct timeval tv{0, 0};
                if (::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
                    auto ch = tui::readKeyChord();
                    if (ch && actionMap.lookup(*ch) == tui::Action::QUIT)
                        running = false;
                }
                continue;
            }

            tui::NpcSnapshot snap;
            tui::TuiFrameModel fm;
            if (
                sim_state.state == SIM_RUNNING || sim_state.state == SIM_STOP ||
                sim_state.state == SIM_END || sim_state.state == SIM_ABORT
            ) {
                tui::SnapshotInput si{};
                auto *dpi = getDPIModule();

                si.retiredPc   = simExecInfo.pc;
                si.retiredInst = simExecInfo.inst;
                si.nextPc      = dpi->core_pc;
                si.procState   = getProcessorState();
                si.simState    = sim_state.state;
                si.haltPc      = sim_state.haltPC;
                si.simHalt     = sim_halt;
                si.execCount      = getExecCount();
                si.execCountClock = getExecCountClockPeriod();
                si.difftestActive = isDifftestActive();
                si.perfEnabled    = sim_config.config_perf;
                if (si.perfEnabled) {
                    si.perfCounters = perf::getPerfMonitor().view();
                }
                si.eventFeed = &tui::g_eventFeed;

                si.stages[0] = { dpi->core_pc, dpi->ifu_if_nextStage_valid != 0, true };
                si.stages[1] = { 0, dpi->idu_id_nextStage_valid != 0, false };
                si.stages[2] = { dpi->exu_exPc, dpi->exu_ex_nextStage_valid != 0, true };
                si.stages[3] = { dpi->memu_memPc, dpi->memu_mem_nextStage_valid != 0, true };
                si.stages[4] = { dpi->wbu_pc, dpi->wbu_wb_nextStage_valid != 0, true };

                {
                    auto stack = sim_state.ftrace_callStack;
                    si.callFrames.reserve(stack.size());
                    while (!stack.empty()) {
                        si.callFrames.push_back(stack.top());
                        stack.pop();
                    }
                }

                snap = tui::makeNpcSnapshot(si);
                fm   = tui::makeFrameModel(snap);
            }

            auto &reg = tui::PanelRegistry::instance();
            layout.forEachLeaf([&](
                const std::string &panelId,
                const tui::Rect &rect, bool focused
            ) {
                tui::Panel *panel = reg.get(panelId);
                if (panel) {
                    panel->render(renderer.canvas(), rect, fm, focused);
                }
            });

            layout.drawTabBars(renderer.canvas());

            if (statusRows > 0 && sz.rows > 0) {
                char leftBuf[128];
                char rightBuf[128];
                const char *simLabel = "STOP";
                if (sim_state.state == SIM_RUNNING) simLabel = "RUN";
                else if (sim_state.state == SIM_END)    simLabel = "END";
                else if (sim_state.state == SIM_ABORT)  simLabel = "ABORT";
                std::snprintf(
                    leftBuf, sizeof(leftBuf),
                    " Preset: %s | Sim: %s | Focus: %s%s",
                    tui::g_tuiConfig.layout.preset.c_str(),
                    simLabel,
                    layout.focusedPanel().c_str(),
                    layout.isMaximized() ? " [MAX]" : ""
                );
                std::snprintf(
                    rightBuf, sizeof(rightBuf),
                    "q:quit  h:overlay  tab:focus  m:max  space:pause  s:step"
                );
                renderer.drawStatusBar(
                    sz.rows - 1, leftBuf, rightBuf,
                    tui::styleBg(tui::kColourBlue),
                    tui::styleFgBold(tui::kColourWhite)
                );
            }

            overlay.render(renderer.canvas(), sz.rows, sz.cols);

            renderer.endFrame();
            renderer.flush();

            if (overlay.quitRequested()) {
                running = false;
                break;
            }

            if (term.consumeResizeFlag()) {
                sz = term.querySize();
                renderer.resize(sz.rows, sz.cols);
                continue;
            }

            {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                struct timeval tv;
                if (sim_state.state == SIM_RUNNING) {
                    tv.tv_sec  = 0;
                    tv.tv_usec = 10000;
                } else {
                    tv.tv_sec  = 1;
                    tv.tv_usec = 0;
                }
                int sel = ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
                if (sel <= 0) continue;
            }

            auto chord = tui::readKeyChord();
            if (!chord) {
                running = false;
                break;
            }

            if (overlay.active()) {
                if (chord->key == tui::SpecialKey::kEsc) {
                    overlay.close();
                } else if (chord->key == tui::SpecialKey::kEnter) {
                    std::string cmd = overlay.execute();
                    if (!cmd.empty()) {
                        overlay.dispatchCommand(cmd);
                    }
                    if (overlay.quitRequested()) {
                        running = false;
                        break;
                    }
                } else if (chord->key == tui::SpecialKey::kBackspace) {
                    overlay.backspace();
                } else if (chord->key == tui::SpecialKey::kLeft) {
                    overlay.moveCursorLeft();
                } else if (chord->key == tui::SpecialKey::kRight) {
                    overlay.moveCursorRight();
                } else if (chord->key == tui::SpecialKey::kUp) {
                    overlay.historyPrev();
                } else if (chord->key == tui::SpecialKey::kDown) {
                    overlay.historyNext();
                } else if (chord->key == tui::SpecialKey::kSpace) {
                    overlay.insertChar(' ');
                } else if (chord->key >= 32 && chord->key <= 126 && chord->mod == tui::kModNone) {
                    overlay.insertChar(static_cast<char>(chord->key));
                }
            } else {
                tui::Action act = actionMap.lookup(*chord);
                switch (act) {
                    case tui::Action::QUIT:
                        running = false;
                        break;
                    case tui::Action::FOCUS_NEXT:
                        layout.focusNext();
                        break;
                    case tui::Action::FOCUS_PREV:
                        layout.focusPrev();
                        break;
                    case tui::Action::TOGGLE_MAXIMIZE:
                        layout.toggleMaximize();
                        break;
                    case tui::Action::TAB_NEXT:
                        if (layout.isFocusedInTabbed())
                            layout.focusTabNext();
                        break;
                    case tui::Action::HELP_OVERLAY:
                        overlay.open();
                        overlay.appendOutput("── Keybindings ──");
                        for (const auto &[ch, a] : actionMap.allBindings()) {
                            char buf[64];
                            std::snprintf(
                                buf, sizeof(buf), "  %-16s → %s",
                                "key", tui::actionName(a)
                            );
                            overlay.appendOutput(buf);
                        }
                        overlay.appendOutput("── Overlay commands ──");
                        overlay.appendOutput("  c/si/sic/pause/reset/q  info r/w");
                        overlay.appendOutput("  x N EXPR  p EXPR  w EXPR  d N  help");
                        break;
                    case tui::Action::PAUSE_RESUME:
                        if (sim_state.state == SIM_RUNNING) {
                            tui::requestSimPause();
                        } else if (sim_state.state == SIM_STOP) {
                            tui::requestSimContinue();
                        }
                        break;
                    case tui::Action::STEP_INST:
                        if (sim_state.state == SIM_STOP) {
                            tui::requestSimStepInst(1);
                        }
                        break;
                    case tui::Action::STEP_CLOCK:
                        if (sim_state.state == SIM_STOP) {
                            tui::requestSimStepClock(1);
                        }
                        break;
                    case tui::Action::RESET:
                        tui::requestSimReset();
                        break;
                    case tui::Action::PANEL_PICKER: {
                        pickerIds = layout.panelIds();
                        if (!pickerIds.empty()) {
                            overlay.open();
                            overlay.appendOutput("── Panel Picker ──");
                            int idx = 0;
                            for (const auto &pid : pickerIds) {
                                char buf[128];
                                std::snprintf(
                                    buf, sizeof(buf), "  [%d] %s%s", idx,
                                    tui::PanelRegistry::instance().displayNameFor(pid).c_str(),
                                    (pid == layout.focusedPanel()) ? " (*)" : ""
                                );
                                overlay.appendOutput(buf);
                                idx++;
                            }
                            overlay.appendOutput("Type digit to focus panel, ESC to close.");
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            if (overlay.active() && !pickerIds.empty() &&
                chord->key >= '0' && chord->key <= '9' &&
                chord->mod == tui::kModNone) {
                int pickIdx = static_cast<int>(chord->key - '0');
                if (pickIdx >= 0 && static_cast<size_t>(pickIdx) < pickerIds.size()) {
                    layout.setFocus(pickerIds[static_cast<size_t>(pickIdx)]);
                    overlay.close();
                }
            }
            if (overlay.active() && chord->key == tui::SpecialKey::kEsc && !pickerIds.empty()) {
                pickerIds.clear();
            }
        }
    } else if (sdbEnabled) {
        sdb_init();
        sdb_mainLoop();
    } else {
        internal::simExecImpl(static_cast<uint64_t>(-1));
    }

    if (sim_config.config_debugOutput)
        std::cout << "仿真结束." << std::endl;
    halt_ret = getDPIModule()->gpr_gprs_10;

run_cleanup:
    impl_->shutdownResources();

    return success && (halt_ret == 0);
}

void npc::Simulator::reset(int cycles) {
    internal::simResetImpl(cycles);
}

void npc::Simulator::stepInstruction() {
    internal::simExecOnceImpl();
}

void npc::Simulator::stepClock() {
    internal::simStepClockImpl();
}

bool npc::Simulator::halted() const {
    return sim_halt;
}

npc::SimStatus npc::Simulator::state() const {
    switch (sim_state.state) {
        case SIM_RUNNING: return SimStatus::Running;
        case SIM_STOP:    return SimStatus::Stopped;
        case SIM_END:     return SimStatus::Ended;
        case SIM_ABORT:   return SimStatus::Aborted;
        case SIM_QUIT:    return SimStatus::Quit;
        default:          return SimStatus::Stopped;
    }
}

npc::ProcessorState npc::Simulator::processorState() const {
    return npc::getProcessorState();
}

std::uint64_t npc::Simulator::execCount() const {
    return ::execCount;
}

std::uint64_t npc::Simulator::execClockCount() const {
    return ::execCountClockPeriod;
}

// ── Public convenience ──
npc::ProcessorState npc::getProcessorState() {
    return ::getProcessorState();
}
