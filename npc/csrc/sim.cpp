#include <verilated_fst_c.h>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <cmath>
#include <sim_top.hpp>
#include <utils.hpp>
#include <memory.hpp>
#include <sdb.hpp>
#include <difftest/dut.hpp>
#include <device.hpp>
#include <utils/Stage.hpp>

ExecInfo simExecInfo = {
    .pc = 0x00000000,
    .inst = 0
};

VProcessorCore *top = nullptr;
static VerilatedFstC *tfp = nullptr;
bool sim_halt = false;

static uint32_t execCount = 0;

/**
 * @brief 执行一步仿真，执行一个时钟周期。
 */
void simStep() {
    do {
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
    } while (top->ioDPI_stage != STAGE_IF);
}

/**
 * @brief 重置仿真环境。
 * 
 * @param n 需要进行的时钟周期数
 */
void simReset(int n) {
    top->reset = 1;
    while (n--) {
        simStep();
    }
    top->reset = 0;
}

/**
 * @brief 执行一个时钟周期的仿真。
 */
bool simExecOnce() {
    addr_t addr;
    word_t data;

    std::cout << "处理器第 " << std::dec << execCount << " 次执行 (从 0 开始算)..." << std::endl;

    simExecInfo.pc = top->io_pc;
    std::cout << "当前PC: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << simExecInfo.pc << std::endl;

    // 从内存中读指令
    simExecInfo.inst = 0x00000000;
    std::cout << "正在从内存中读指令..." << std::endl;
    addr = top->io_pc;
    if (addr >= MEMORY_OFFSET) {
        data = readMemory(addr, sizeof(word_t));
        std::cout << "地址: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << addr <<
            ", 指令: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << data << std::endl;
        top->io_instData = data;
        simExecInfo.inst = data;
    } else {
        std::cerr << "地址尚未初始化，仿真无法继续，只能异常退出！" << std::endl;
        return false;
    }

    // 解析指令
    std::cout << "正在解析该条指令..." << std::endl;
    simStep();

    execCount++;

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

        std::cout << str;
        std::flush(std::cout);
    }

    return true;
}

/**
 * @brief （每执行一步后）进行 trace 和 difftest 。
 */
static void traceAndDiffTest() {
    if (sim_config.config_difftest) {
        difftest_dut_step(simExecInfo.pc, top->io_pc);
    }
    sdb_evalAndUpdateWP();
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
        if (sim_halt) {
            sim_state.state = SIM_END;
            sim_state.haltPC = top->io_pc;
        }
        traceAndDiffTest();
        if (sim_state.state != SIM_RUNNING) {
            break;
        }
    }

    if (sim_config.config_itrace) {
        sim_state_itrace_iringbuf_dump();
    }
}

/**
 * @brief 执行若干时钟周期的仿真。
 * 
 * @param n 需要进行仿真的时钟周期数
 */
void simExec(uint64_t n) {
    word_t halt_ret;

    switch (sim_state.state) {
        case SIM_END:
        case SIM_ABORT:
        case SIM_QUIT:
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
            halt_ret = top->ioDPI_registers_0;
            std::cout << "仿真: " <<
                (sim_state.state == SIM_ABORT ?
                    ANSI_FMT("ABORT", ANSI_FG_RED) :
                    ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN)) <<
                " at pc = 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << sim_state.haltPC << std::dec <<
                ", 结果: " << halt_ret << std::endl;
    }
}

extern size_t binFileSize;

/**
 * @brief 开始仿真主流程。
 * 
 * @param sdbEnabled 是否启用 SDB
 * @return true 成功
 * @return false 失败
 */
bool simulate(bool sdbEnabled) {
    word_t halt_ret;

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

    top = new VProcessorCore(verContext);

    if (sim_config.config_wave) {
        tfp = new VerilatedFstC;
        verContext->traceEverOn(true);
        top->trace(tfp, 0);
        tfp->open(sim_config.config_waveFilePath.c_str());
    }

    std::cout << "正在重置处理器..." << std::endl;
    simReset(1);

    if (sim_config.config_difftest) {
        std::cout << "正在加载 DiffTest..." << std::endl;
        difftest_dut_init(
            sim_config.config_difftestSoFilePath.c_str(),
            binFileSize,
            sim_config.config_difftestPort
        );
    }

    if (sim_config.config_device) {
        std::cout << "正在加载外部设备..." << std::endl;
        device_init();
    }

    std::cout << "正在启动仿真..." << std::endl;
    if (sdbEnabled) {
        sdb_init();
        sdb_mainLoop();
    } else {
        simExec(-1);
    }

    std::cout << "仿真结束." << std::endl;
    halt_ret = top->ioDPI_registers_0;
    delete top;

    if (sim_config.config_itrace) {
        sim_state_itrace_iringbuf_destroy();
    }

    sim_state_ofstream_finalise();

    if (tfp) {
        delete tfp;
    }

    return halt_ret == 0;
}
