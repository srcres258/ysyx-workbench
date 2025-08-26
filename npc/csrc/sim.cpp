#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sim_top.hpp>
#include <utils.hpp>
#include <memory.hpp>
#include <sdb.hpp>

VProcessorCore *top = nullptr;
bool sim_halt = false;

static uint32_t execCount = 0;

/**
 * @brief 执行一步仿真，执行一个时钟周期。
 */
void simStep() {
    top->clock = 0;
    top->eval();
    top->clock = 1;
    top->eval();
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
 * @brief 打印寄存器信息。
 */
void simDumpRegisters() {
    std::cout << "寄存器信息:" << std::endl;
    std::cout << "zero: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_0 << std::endl;
    std::cout << "  ra: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_1 << std::endl;
    std::cout << "  sp: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_2 << std::endl;
    std::cout << "  gp: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_3 << std::endl;
    std::cout << "  tp: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_4 << std::endl;
    std::cout << "  t0: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_5 << std::endl;
    std::cout << "  t1: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_6 << std::endl;
    std::cout << "  t2: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_7 << std::endl;
    std::cout << "  fp: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_8 << std::endl;
    std::cout << "  s1: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_9 << std::endl;
    std::cout << "  a0: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_10 << std::endl;
    std::cout << "  a1: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_11 << std::endl;
    std::cout << "  a2: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_12 << std::endl;
    std::cout << "  a3: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_13 << std::endl;
    std::cout << "  a4: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_14 << std::endl;
    std::cout << "  a5: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_15 << std::endl;
    std::cout << "  a6: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_16 << std::endl;
    std::cout << "  a7: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_17 << std::endl;
    std::cout << "  s2: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_18 << std::endl;
    std::cout << "  s3: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_19 << std::endl;
    std::cout << "  s4: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_20 << std::endl;
    std::cout << "  s5: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_21 << std::endl;
    std::cout << "  s6: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_22 << std::endl;
    std::cout << "  s7: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_23 << std::endl;
    std::cout << "  s8: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_24 << std::endl;
    std::cout << "  s9: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_25 << std::endl;
    std::cout << " s10: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_26 << std::endl;
    std::cout << " s11: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_27 << std::endl;
    std::cout << "  t3: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_28 << std::endl;
    std::cout << "  t4: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_29 << std::endl;
    std::cout << "  t5: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_30 << std::endl;
    std::cout << "  t6: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_registers_31 << std::endl;
    std::cout << "  pc: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_pc << std::endl;
}

/**
 * @brief 执行一个时钟周期的仿真。
 */
bool simExecOnce() {
    addr_t addr;
    word_t data;

    std::cout << "处理器第 " << std::dec << execCount << " 次执行 (从 0 开始算)..." << std::endl;

    std::cout << "当前PC: 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_pc << std::endl;

    // 从内存中读指令
    std::cout << "正在从内存中读指令..." << std::endl;
    addr = top->io_instAddr;
    if (addr >= MEMORY_OFFSET) {
        data = readMemory(addr);
        std::cout << "地址: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << addr <<
            ", 指令: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << data << std::endl;
        top->io_instData = data;
    } else {
        std::cerr << "地址尚未初始化，仿真无法继续，只能异常退出！" << std::endl;
        return false;
    }

    // 解析指令
    std::cout << "正在解析该条指令..." << std::endl;
    simStep();

    // 若数据写使能激活，向内存中写数据
    if (top->io_writeEnable) {
        std::cout << "写使能激活，正在向内存中写数据..." << std::endl;
        addr = top->io_address;
        if (addr >= MEMORY_OFFSET) {
            data = top->io_writeData;
            std::cout << "地址: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << addr <<
                ", 数据: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << data << std::endl;
            writeMemory(addr, data);
        } else {
            std::cerr << "地址尚未初始化，跳过..." << std::endl;
        }
    } else {
        std::cout << "写使能未激活." << std::endl;
    }

    execCount++;

    return true;
}

/**
 * @brief （每执行一步后）进行 trace 和 difftest 。
 */
static void traceAndDiffTest() {
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
        }
        traceAndDiffTest();
        if (sim_state.state != SIM_RUNNING) {
            break;
        }
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
            halt_ret = top->io_registers_0;
            std::cout << "仿真: " <<
                (sim_state.state == SIM_ABORT ?
                    ANSI_FMT("ABORT", ANSI_FG_RED) :
                    ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN)) <<
                " at pc = 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << top->io_pc << std::dec <<
                ", 结果: " << halt_ret << std::endl;
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

    top = new VProcessorCore;

    std::cout << "正在重置..." << std::endl;
    simReset(1);

    std::cout << "正在启动仿真..." << std::endl;
    if (sdbEnabled) {
        sdb_mainLoop();
    } else {
        simExec(-1);
    }

    std::cout << "仿真结束." << std::endl;
    halt_ret = top->io_registers_0;
    delete top;

    return halt_ret == 0;
}
