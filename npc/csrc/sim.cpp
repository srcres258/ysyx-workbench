#include <verilated_fst_c.h>
#ifndef NPC_STANDALONE
#include <nvboard.h>
#else
#include <device/vga.hpp>
#include <device/keyboard.hpp>
#endif
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <sys/select.h>
#include <print>
#include <utils.hpp>
#include <sdb.hpp>
#include <difftest/dut.hpp>
#include <device.hpp>
#include <utils/Stage.hpp>
#include <utils/timer.hpp>
#include <sim_top.hpp>
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

ExecInfo simExecInfo = {
    .pc = 0x00000000,
    .inst = 0
};

#ifdef NPC_STANDALONE
Vysyx_25070190 *top = nullptr;
#else
VysyxSoCFull *top = nullptr;
#endif
static VerilatedFstC *tfp = nullptr;
volatile bool sim_halt = false;

static void sigint_handler(int) {
    sim_halt = true;
}

static void install_signal_handlers() {
    std::signal(SIGINT,  sigint_handler);
    std::signal(SIGTERM, sigint_handler);
}

static uint64_t execCount = 0;
static uint64_t execCountClockPeriod = 0;
static bool s_difftestActive = false;

uint64_t getExecCount() { return execCount; }
uint64_t getExecCountClockPeriod() { return execCountClockPeriod; }
bool isDifftestActive() { return s_difftestActive; }

/**
 * @brief 获取 DPI 模块, 以便读取被仿真模块的信号.
 */
#ifdef NPC_STANDALONE
Vysyx_25070190_GeneralDPIAdapter *getDPIModule() {
    return top->ysyx_25070190->dpi;
}
#else
VysyxSoCFull_GeneralDPIAdapter *getDPIModule() {
    return top->ysyxSoCFull->asic->cpu->cpu->dpi;
}
#endif

/**
 * @brief 执行一步仿真 (执行一个时钟周期).
 */
void simStepClockPeriod() {
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
    // 每时钟周期更新一次 UART 采样 (仅在 NVBoard 启用且非 reset 期间)
    if (!top->reset && sim_config.config_nvboard) {
        extern void nvboard_uart_update(void);
        nvboard_uart_update();
    }
    if (!top->reset && sim_config.config_nvboard) {
        extern uint8_t *vga_blank_n_ptr;
        extern void vga_update();
        if (*vga_blank_n_ptr) vga_update();
    }
#endif

#ifdef NPC_STANDALONE
    if (!top->reset) {
        keyboard_update();
        vga_update();
    }
#endif

    execCountClockPeriod++;
}

/**
 * @brief 执行一步仿真 (执行一条指令).
 */
void simStep() {
    bool executionBegun = false;
    do {
        if (sim_halt || tui::isPauseRequested()) {
            break;
        }

        simStepClockPeriod();

        if (top->reset || sim_halt || tui::isPauseRequested()) {
            break;
        }

        if (getDPIModule()->core_executing && !executionBegun) {
            executionBegun = true;
        }
    } while (!executionBegun || (executionBegun && getDPIModule()->core_executing));
}

/**
 * @brief 重置仿真环境。
 * 
 * @param n 需要进行的时钟周期数
 */
void simReset(int n) {
    top->reset = 1;
    while (n--) {
        simStepClockPeriod();
    }
    top->reset = 0;
}

/**
 * @brief 执行一条指令的仿真。
 */
bool simExecOnce() {
    if (sim_config.config_debugOutput)
        std::cout << "处理器开始执行第 " << std::dec << execCount << " 条指令 (从 0 开始算)..." << std::endl;

    auto *dpi = getDPIModule();
    // NOTE: dpi->core_pc is the NEXT PC (to-be-fetched), NOT the retired PC.
    // The retired PC is simExecInfo.pc (set by dpi_onRetireTrace from WB stage).
    // DiffTest compare is post-retire in traceAndDiffTest(), so NO pre-sync here.
    addr_t pc = dpi->core_pc;
    if (sim_config.config_debugOutput)
        std::cout << "当前PC(即将执行的指令位置): 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << pc << std::endl;

    // 执行下一步
    simStep();
    if (sim_config.config_debugOutput) {
        std::cout << "当前指令: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << simExecInfo.inst << std::endl;
    }

    if (sim_config.config_itrace) {
        char pbuf[128];
        char *p = pbuf;
        p += snprintf(p, sizeof(pbuf), FMT_WORD ":", simExecInfo.pc);
        int ilen = 4; // TODO: 等实现 RV32C 指令集后需修改此处（RV32C单条指令长度为2）
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

        if (sim_config.config_debugOutput) {
            std::cout << str;
            std::flush(std::cout);
        }
    }

    execCount++;

    return true;
}

/**
 * @brief （每执行一步后）进行 trace 和 difftest 。
 */
static void traceAndDiffTest() {
    if (sim_config.config_difftest) {
        if (!s_difftestActive && simExecInfo.pc >= sim_config.config_difftestStartPC) {
            s_difftestActive = true;
            std::println("[difftest] 在 PC=0x{:08x} 处激活 DiffTest 比较 (startPC=0x{:08x})",
                         simExecInfo.pc, sim_config.config_difftestStartPC);
            tui::g_eventFeed.push(execCount, tui::EventType::DIFFTEST_ACTIVATE,
                                  simExecInfo.pc, sim_config.config_difftestStartPC,
                                  "DiffTest comparison activated");
            difftest_dut_syncCurrentProcessorState();
            difftest_dut_syncPayloadMemoryToRef();
            difftest_dut_clearSkipRef();
            return;
        }
        if (s_difftestActive) {
            difftest_dut_step(simExecInfo.pc, getDPIModule()->core_pc);
        }
    }
    sdb_evalAndUpdateWP();
}

/**
 * @brief 刷新到终端的输出流 (stdout 和 stderr) 的缓冲区, 以便待显示的信息能够及时呈现.
 */
static void flushOutput() {
    fflush(stdout);
    fflush(stderr);
}

/**
 * @brief 进行真正的硬件仿真动作。
 * 
 * @param n 需要进行仿真的时钟周期数
 */
static void execute(uint64_t n) {
    uint64_t i;

    for (i = n; i > 0; i--) {
        if (!simExecOnce()) {
            sim_state.state = SIM_ABORT;
            tui::g_eventFeed.push(execCount, tui::EventType::ABORT,
                                  simExecInfo.pc, 0,
                                  "Simulation aborted — simExecOnce returned false");
            break;
        }
        flushOutput();
        // Pause check must come BEFORE sim_halt: pause is soft (SIM_STOP),
        // sim_halt from DPI is terminal (SIM_END / TRAP).
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

    if (sim_config.config_itrace) {
        sim_state_itrace_iringbuf_dump();
    }
}

/**
 * @brief 执行若干次时钟周期的仿真.
 * 
 * @param n 需要进行仿真的时钟周期数
 */
void simExecClockPeriod(uint64_t n) {
    uint64_t i;

    for (i = n; i > 0; i--) {
        if (sim_config.config_debugOutput)
            std::cout << "处理器开始执行第 " << std::dec << execCountClockPeriod << " 个时钟周期 (从 0 开始算)..." << std::endl;
        simStepClockPeriod();
    }
}

/**
 * @brief 执行若干条指令的仿真.
 * 
 * @param n 需要进行仿真的指令数
 */
void simExec(uint64_t n) {
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
            halt_ret = getDPIModule()->gpr_gprs_10; // a0 寄存器是 x10
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

            if (sim_state.state == SIM_END) {
                tui::g_eventFeed.push(execCount,
                    (halt_ret == 0) ? tui::EventType::TRAP_GOOD : tui::EventType::TRAP_BAD,
                    sim_state.haltPC, halt_ret,
                    (halt_ret == 0) ? "Good trap" : "Bad trap");
            }
    }

    // 未达检测: difftest 已配置但 startPC 从未到达
    if (sim_config.config_difftest && !s_difftestActive
        && (sim_state.state == SIM_END || sim_state.state == SIM_STOP)) {
        std::println(stderr,
            "[difftest] 错误: DiffTest 已启用但从未激活! 配置的 startPC=0x{:08x} "
            "在仿真过程中从未到达 (共执行 {} 条指令).",
            sim_config.config_difftestStartPC, execCount);
        std::println(stderr,
            "[difftest] 请检查: (1) startPC 是否设置正确; (2) 程序是否确实会执行到该地址.");
        tui::g_eventFeed.push(execCount, tui::EventType::DIFFTEST_WARNING,
                              sim_config.config_difftestStartPC, execCount,
                              "DiffTest enabled but startPC never reached");
    }
}

/**
 * @brief 开始仿真主流程。
 * 
 * @param sdbEnabled 是否启用 SDB
 * @return true 成功
 * @return false 失败
 */
bool simulate(bool sdbEnabled) {
    word_t halt_ret;
    bool success = true;

    install_signal_handlers();

    tui::initEventFeed();
    tui::g_eventFeed.push(0, tui::EventType::CONFIG, 0,
                           static_cast<word_t>(sim_config.config_tui ? 1 : 0),
                           sim_config.config_tui ? "TUI mode enabled" : "TUI mode disabled");

    timer_initRand();
    disasm_init();
    if (sim_config.config_itrace) {
        sim_state_itrace_iringbuf_init();
    }
    if (sim_config.config_ftrace) {
        if (!sim_state_ftrace_funcSyms_init()) {
            std::cerr << "函数符号表加载失败，请确保 ELF 文件路径正确！" << std::endl;
            success = false;
            goto sim_cleanup;
        }
    }
    sim_state_ofstream_init();

#ifdef NPC_STANDALONE
    top = new Vysyx_25070190(verContext);
    {
        // Standalone: load program binary into DPI-C memory
        const char *imgVal = std::getenv("IMG");
        if (imgVal) {
            standalone_mem_loadBin(imgVal);
        } else {
            std::cerr << "[standalone] IMG env var not set, no binary loaded!" << std::endl;
        }
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
            fprintf(stderr, "[NPC] Entering NVBoard init path (config_device=%d, config_nvboard=%d)\n",
                    sim_config.config_device, sim_config.config_nvboard);
            extern void nvboard_bind_all_pins(VysyxSoCFull* top);
            nvboard_bind_all_pins(top);
            nvboard_init();
            fprintf(stderr, "[NPC] nvboard_init() returned\n");
            extern void uart_set_divisor(uint16_t d);
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
            success = false;
            goto sim_cleanup;
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

        // Payload 预加载: 将配置的 payload 二进制加载到后备存储
        // Verilog PSRAM/SDRAM 行为模型不通过 DPI 更新 C++ 缓冲, 所以必须在
        // activation 同步之前显式加载, 否则 syncPayloadMemoryToRef()
        // 会把空数据复制到 REF 导致 INVALID OPCODE.
        if (!sim_config.config_difftestPayloadBinFilePath.empty()) {
            std::println("[sim] 正在将 Payload 二进制加载到后备存储...");
            if (!difftest_dut_loadPayloadToBackingStore(
                    sim_config.config_difftestPayloadBinFilePath.c_str(),
                    sim_config.config_difftestPayloadLoadAddr)) {
                std::println(stderr, "[sim] 致命: Payload 加载失败, 退出!");
                success = false;
                goto sim_cleanup;
            }

            // 如果 startPC 所在的执行区域与 loadAddr 不在同一内存区域,
            // 也需要填充执行区域的后备存储.
            // 例如: loadAddr→SDRAM 但 startPC→PSRAM 的情况,
            // bootloader 在 reset 后会把代码从 FLASH 复制到 PSRAM 执行,
            // 但 Verilog PSRAM 模型不会更新 C++ 缓冲.
            // 不填充执行区域就会导致 REF 在 activation 时收到空的 PSRAM 数据.
            {
                addr_t execRegionBase = 0;
                if (sim_config.config_difftestStartPC >= PSRAM_ADDR &&
                    sim_config.config_difftestStartPC < PSRAM_ADDR + PSRAM_LEN) {
                    execRegionBase = PSRAM_ADDR;
                } else if (sim_config.config_difftestStartPC >= SDRAM_ADDR &&
                           sim_config.config_difftestStartPC < SDRAM_ADDR + SDRAM_LEN) {
                    execRegionBase = SDRAM_ADDR;
                } else if (sim_config.config_difftestStartPC >= SRAM_ADDR &&
                           sim_config.config_difftestStartPC < SRAM_ADDR + SRAM_LEN) {
                    execRegionBase = SRAM_ADDR;
                }
                // 计算 loadAddr 所在内存区域的基址, 用于与执行区域比较.
                // 当 loadAddr 位于 PSRAM 或 SDRAM 范围内时, 其区域基址为对应设备的基址;
                // 否则 loadRegionBase 保持 0, 此时也会触发执行区域加载 (兼容非标地址).
                addr_t loadRegionBase = 0;
                if (sim_config.config_difftestPayloadLoadAddr >= PSRAM_ADDR &&
                    sim_config.config_difftestPayloadLoadAddr < PSRAM_ADDR + PSRAM_LEN) {
                    loadRegionBase = PSRAM_ADDR;
                } else if (sim_config.config_difftestPayloadLoadAddr >= SDRAM_ADDR &&
                           sim_config.config_difftestPayloadLoadAddr < SDRAM_ADDR + SDRAM_LEN) {
                    loadRegionBase = SDRAM_ADDR;
                } else if (sim_config.config_difftestPayloadLoadAddr >= SRAM_ADDR &&
                           sim_config.config_difftestPayloadLoadAddr < SRAM_ADDR + SRAM_LEN) {
                    loadRegionBase = SRAM_ADDR;
                }
                if (execRegionBase != 0 && execRegionBase != loadRegionBase) {
                    std::println("[sim] startPC=0x{:08x} 所在执行区域与 loadAddr=0x{:08x} 不同, "
                                 "同步加载 Payload 到执行区域基址 0x{:08x}...",
                                 sim_config.config_difftestStartPC,
                                 sim_config.config_difftestPayloadLoadAddr,
                                 execRegionBase);
                    if (!difftest_dut_loadPayloadToBackingStore(
                            sim_config.config_difftestPayloadBinFilePath.c_str(),
                            execRegionBase)) {
                        std::println(stderr, "[sim] 致命: 执行区域 Payload 加载失败, 退出!");
                        success = false;
                        goto sim_cleanup;
                    }
                }
            }
        } else {
            if ((sim_config.config_difftestStartPC >= PSRAM_ADDR &&
                 sim_config.config_difftestStartPC < PSRAM_ADDR + PSRAM_LEN) ||
                (sim_config.config_difftestStartPC >= SDRAM_ADDR &&
                 sim_config.config_difftestStartPC < SDRAM_ADDR + SDRAM_LEN) ||
                (sim_config.config_difftestStartPC >= SRAM_ADDR &&
                 sim_config.config_difftestStartPC < SRAM_ADDR + SRAM_LEN)) {
                std::println(stderr,
                    "[sim] 警告: startPC=0x{:08x} 在 PSRAM/SDRAM/SRAM 内但未指定 Payload BIN 文件. "
                    "Activation 时内存同步会将空数据发到 REF, 可能导致 INVALID OPCODE 崩溃.",
                    sim_config.config_difftestStartPC);
            }
        }
    }

    if (sim_config.config_debugOutput)
        std::cout << "正在重置处理器..." << std::endl;
    // ysyxSoC 中, CPU 核心电路部分会在上电后的第 10 个时钟周期时自动强行触发一个 reset 信号.
    // 所以为保险起见, 这里对整体电路维持 reset 信号 15 个时钟周期,
    // 以确保电路各部分正常 reset 完成之后再开始工作,
    // 避免工作到中途遇到 reset 信号导致状态被重置.
    simReset(15);

#ifdef NPC_STANDALONE
    vga_init();
#endif

    if (sim_config.config_debugOutput)
        std::cout << "正在启动仿真..." << std::endl;
    if (sim_config.config_tui) {
        // ── Validate all keybinding strings (fail fast, no TTY needed) ──
        {
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
                std::fprintf(stderr,
                    "[tui] fatal: keybinding validation failed — aborting\n");
                success = false;
                goto sim_cleanup;
            }
        }

        tui::TerminalSession term;
        if (!term) {
            std::fprintf(stderr, "[tui] terminal session init failed — aborting\n");
            success = false;
            goto sim_cleanup;
        }

        // ── Init SDB backend for expression evaluation ──
        sdb_init();

        // ── Build layout tree from config preset ──
        tui::LayoutTree layout;
        if (!layout.buildFromPreset(tui::g_tuiConfig.layout.preset)) {
            std::fprintf(stderr,
                "[tui] fatal: unknown layout preset \"%s\"\n",
                tui::g_tuiConfig.layout.preset.c_str());
            success = false;
            goto sim_cleanup;
        }

        // ── Register built‑in panels ──
        tui::PanelRegistry::instance().registerBuiltins();

        // ── Validate that all panel IDs in the layout exist ──
        if (!tui::PanelRegistry::instance().validateLayout(layout)) {
            std::fprintf(stderr,
                "[tui] fatal: layout references unknown panel IDs. "
                "Check your [layout] preset or panel registrations.\n");
            success = false;
            goto sim_cleanup;
        }

        // ── Build action map ──
        tui::ActionMap actionMap;
        actionMap.buildFromConfig(tui::g_tuiConfig.keybindings);

        tui::Overlay overlay;

        auto sz = term.querySize();
        tui::Renderer renderer;
        renderer.resize(sz.rows, sz.cols);

        // ── Panel picker state ──
        std::vector<std::string> pickerIds;

        bool running = true;
        while (running) {
            // ── Handle terminal resize ──
            if (term.consumeResizeFlag()) {
                sz = term.querySize();
                renderer.resize(sz.rows, sz.cols);
            }

            // ── Drive simulation burst when running ──
            if (sim_state.state == SIM_RUNNING) {
                constexpr int kRefreshBurst = 500;
                for (int bi = 0; bi < kRefreshBurst && sim_state.state == SIM_RUNNING; bi++) {
                    execute(1);
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

            // ── Compute layout for current terminal size ──
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

            // ── Take snapshot & build frame model ──
            tui::NpcSnapshot snap;
            tui::TuiFrameModel fm;
            if (sim_state.state == SIM_RUNNING || sim_state.state == SIM_STOP ||
                sim_state.state == SIM_END || sim_state.state == SIM_ABORT) {
                snap = tui::makeNpcSnapshot();
                fm   = tui::makeFrameModel(snap);
            }
            // else: fm stays default‑initialised (all zeros / empty strings)

            // ── Render each panel through the layout system ──
            auto &reg = tui::PanelRegistry::instance();
            layout.forEachLeaf([&](const std::string &panelId,
                                    const tui::Rect &rect, bool focused) {
                tui::Panel *panel = reg.get(panelId);
                if (panel) {
                    panel->render(renderer.canvas(), rect, fm, focused);
                }
            });

            // ── Draw tab bars ──
            layout.drawTabBars(renderer.canvas());

            // ── Status bar ──
            if (statusRows > 0 && sz.rows > 0) {
                char leftBuf[128];
                char rightBuf[128];
                const char *simLabel = "STOP";
                if (sim_state.state == SIM_RUNNING) simLabel = "RUN";
                else if (sim_state.state == SIM_END)    simLabel = "END";
                else if (sim_state.state == SIM_ABORT)  simLabel = "ABORT";
                std::snprintf(leftBuf, sizeof(leftBuf),
                    " Preset: %s | Sim: %s | Focus: %s%s",
                    tui::g_tuiConfig.layout.preset.c_str(),
                    simLabel,
                    layout.focusedPanel().c_str(),
                    layout.isMaximized() ? " [MAX]" : "");
                std::snprintf(rightBuf, sizeof(rightBuf),
                    "q:quit  h:overlay  tab:focus  m:max  space:pause  s:step");
                renderer.drawStatusBar(sz.rows - 1, leftBuf, rightBuf,
                    tui::styleBg(tui::kColourBlue),
                    tui::styleFgBold(tui::kColourWhite));
            }

            overlay.render(renderer.canvas(), sz.rows, sz.cols);

            renderer.endFrame();
            renderer.flush();

            // ── Check for quit from overlay ──
            if (overlay.quitRequested()) {
                running = false;
                break;
            }

            // ── If a resize arrived mid-frame, discard this frame and rebuild
            //     with the new size on the next loop iteration. This prevents
            //     flushing a frame composed for stale dimensions.
            if (term.consumeResizeFlag()) {
                sz = term.querySize();
                renderer.resize(sz.rows, sz.cols);
                continue;
            }

            // ── Handle input (non‑blocking when running, blocking when stopped) ──
            {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                struct timeval tv;
                if (sim_state.state == SIM_RUNNING) {
                    tv.tv_sec  = 0;
                    tv.tv_usec = 10000;  // 10 ms poll
                } else {
                    tv.tv_sec  = 1;
                    tv.tv_usec = 0;
                }
                int sel = ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
                if (sel <= 0) {
                    // No input — next iteration
                    continue;
                }
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
                            std::snprintf(buf, sizeof(buf), "  %-16s → %s",
                                          "key", tui::actionName(a));
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
                                std::snprintf(buf, sizeof(buf), "  [%d] %s%s",
                                    idx,
                                    tui::PanelRegistry::instance().displayNameFor(pid).c_str(),
                                    (pid == layout.focusedPanel()) ? " (*)" : "");
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

            // ── Handle panel picker digit input if overlay is showing picker ──
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
        simExec(-1);
    }

    if (sim_config.config_debugOutput)
        std::cout << "仿真结束." << std::endl;
    halt_ret = getDPIModule()->gpr_gprs_10; // a0 寄存器是 x10

sim_cleanup:
#ifndef NPC_STANDALONE
    if (sim_config.config_nvboard) {
        nvboard_quit();
    }
#else
    vga_cleanup();
#endif
    if (tfp) {
        tfp->close();
    }
    delete top;

    if (sim_config.config_itrace) {
        sim_state_itrace_iringbuf_destroy();
    }

    sim_state_ofstream_finalise();

    if (tfp) {
        delete tfp;
        tfp = nullptr;
    }

    return success && (halt_ret == 0);
}
