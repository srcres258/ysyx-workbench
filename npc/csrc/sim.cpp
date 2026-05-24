#include <verilated_fst_c.h>
#ifndef NPC_STANDALONE
#include <nvboard.h>
#else
#include <device/vga.hpp>
#endif
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <cmath>
#include <utils.hpp>
#include <sdb.hpp>
#include <difftest/dut.hpp>
#include <device.hpp>
#include <utils/Stage.hpp>
#include <utils/timer.hpp>
#include <sim_top.hpp>

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
bool sim_halt = false;

static uint64_t execCount = 0;
static uint64_t execCountClockPeriod = 0;

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
        simStepClockPeriod();

        if (top->reset) {
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
    simExecInfo.pc = dpi->core_pc;
    if (sim_config.config_debugOutput)
        std::cout << "当前PC: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << simExecInfo.pc << std::endl;

    // 若开启了 difftest, 执行前要先向 REF 同步处理器状态.
    if (sim_config.config_difftest) {
        difftest_dut_syncCurrentProcessorState();
    }

    // 执行下一步
    simStep();
    simExecInfo.inst = getDPIModule()->ifu_instData;
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
        difftest_dut_step(simExecInfo.pc, getDPIModule()->core_pc);
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
            break;
        }
        flushOutput();
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
            extern void nvboard_bind_all_pins(VysyxSoCFull* top);
            nvboard_bind_all_pins(top);
            nvboard_init();
            extern void uart_set_divisor(uint16_t d);
            uart_set_divisor(80);
            if (sim_config.config_debugOutput)
                std::cout << "NVBoard 已初始化." << std::endl;
        }
#endif
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
        difftest_dut_init(
            sim_config.config_difftestSoFilePath.c_str(),
            sim_config.config_difftestPort
        );
    }

    if (sim_config.config_debugOutput)
        std::cout << "正在启动仿真..." << std::endl;
    if (sdbEnabled) {
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
    delete top;

    if (sim_config.config_itrace) {
        sim_state_itrace_iringbuf_destroy();
    }

    sim_state_ofstream_finalise();

    if (tfp) {
        delete tfp;
    }

    return success && (halt_ret == 0);
}