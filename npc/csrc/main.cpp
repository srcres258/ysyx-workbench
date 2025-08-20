#include <iostream>
#include <iomanip>
#include <fstream>
#include <verilated.h>

#include "VProcessorCore.h"

static VProcessorCore *top = nullptr;
static bool sim_halt = false;

/**
 * @brief 模拟内存地址偏移，处理器默认从该地址处开始执行（取第一条指令）
 */
const uint32_t MEMORY_OFFSET = 0x80000000;
/**
 * @brief 模拟计算机主存大小，单位为字节。
 */
const uint32_t MEMORY_SIZE = 1024 * 64;

/**
 * 模拟计算机主存，内含一段 RV32I 指令集的机器码，以小端序存放。
 */
static uint8_t memory[MEMORY_SIZE] = { 0 };

/**
 * @brief 从给定二进制文件（bin）加载内容到主存中。
 * 
 * @param filename 文件名
 * @return true 成功
 * @return false 失败
 */
static bool initMemory(const char *filename) {
    size_t size;

    std::ifstream f(filename, std::ios::binary | std::ios::ate);

    if (!f) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return false;
    }
    std::cout << "正在加载文件: " << filename << std::endl;
    size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (f.read((char *) memory, size)) {
        std::cout << "文件加载成功." << std::endl;
    } else {
        std::cerr << "文件加载失败!" << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief 从主存中读取内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @return uint32_t 读取到的内容
 */
static uint32_t readMemory(uint32_t addr) {
    uint32_t result;

    result = memory[addr - MEMORY_OFFSET];
    result |= memory[addr - MEMORY_OFFSET + 1] << 8;
    result |= memory[addr - MEMORY_OFFSET + 2] << 16;
    result |= memory[addr - MEMORY_OFFSET + 3] << 24;

    return result;
}

/**
 * @brief 向主存中写入内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @param data 将要写入的内容
 */
static void writeMemory(uint32_t addr, uint32_t data) {
    uint32_t val;

    val = data;
    memory[addr - MEMORY_OFFSET] = val & 0xFF;
    val >>= 8;
    memory[addr - MEMORY_OFFSET + 1] = val & 0xFF;
    val >>= 8;
    memory[addr - MEMORY_OFFSET + 2] = val & 0xFF;
    val >>= 8;
    memory[addr - MEMORY_OFFSET + 3] = val;
}

/**
 * @brief 打印寄存器信息。
 */
static void dumpRegisters() {
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

static void step() {
    top->clock = 0;
    top->eval();
    top->clock = 1;
    top->eval();
}

static void reset(int n) {
    top->reset = 1;
    while (n--) {
        step();
    }
    top->reset = 0;
}

extern "C" void dpi_halt(bool halt) {
    sim_halt = halt;

    if (sim_halt) {
        std::cout << "[sim] 仿真环境置仿真终止信号，处理器下一次执行前将结束仿真！" << std::endl;
    }
}

extern "C" void dpi_onAddressUpdate(uint32_t _address) {
    uint32_t addr, data;

    std::cout << "[sim] 仿真环境检测到处理器寻址地址发生改变，正在从内存中读数据并提供给处理器..." << std::endl;

    addr = top->io_address;
    std::cout << "[sim] 地址: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << addr << std::endl;
    if (addr >= MEMORY_OFFSET && addr < MEMORY_OFFSET + MEMORY_SIZE) {
        data = readMemory(addr);
        std::cout << "[sim] 数据: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << data << std::endl;
        top->io_readData = data;
    } else {
        std::cerr << "[sim] 地址尚未初始化，置缺省值..." << std::endl;
        top->io_readData = 0;
    }
}

int main(int argc, const char **argv) {
    uint32_t addr, data, i;

    Verilated::commandArgs(argc, argv);

    std::cout << "正在加载二进制文件到主存..." << std::endl;
    if (!initMemory("build/program.bin")) {
        std::cerr << "二进制文件加载失败，退出..." << std::endl;
        return 1;
    }

    top = new VProcessorCore;

    std::cout << "正在重置..." << std::endl;
    reset(1);

    std::cout << "正在启动仿真..." << std::endl;
    i = 0;
    // 若仿真未停止，则执行仿真
    while (!sim_halt) {
        std::cout << "处理器第 " << std::dec << i << " 次执行 (从 0 开始算)..." << std::endl;

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
            return 1;
        }

        // 解析指令
        std::cout << "正在解析该条指令..." << std::endl;
        step();

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

        dumpRegisters();

        // std::cout << "按 ENTER 键继续..." << std::endl;
        // std::cin.get();

        i++;
    }

    std::cout << "仿真结束." << std::endl;
    delete top;

    return 0;
}
