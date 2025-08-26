#include <iostream>
#include <string>
#include <iomanip>
#include <sim_top.hpp>
#include <isa.hpp>

static const char *regs[] = {
    "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};
#define LEN_REGS (sizeof(regs) / sizeof(regs[0]))

/**
 * @brief 根据寄存器索引，读取寄存器的值。
 * 
 * @param idx 寄存器索引
 * @return word_t 寄存器的值；若寄存器不存在或读取失败则返回0
 */
word_t isaRegVal(size_t idx) {
    if (idx >= LEN_REGS) {
        return 0;
    }

    switch (idx) {
        case 0: return top->io_registers_0;
        case 1: return top->io_registers_1;
        case 2: return top->io_registers_2;
        case 3: return top->io_registers_3;
        case 4: return top->io_registers_4;
        case 5: return top->io_registers_5;
        case 6: return top->io_registers_6;
        case 7: return top->io_registers_7;
        case 8: return top->io_registers_8;
        case 9: return top->io_registers_9;
        case 10: return top->io_registers_10;
        case 11: return top->io_registers_11;
        case 12: return top->io_registers_12;
        case 13: return top->io_registers_13;
        case 14: return top->io_registers_14;
        case 15: return top->io_registers_15;
        case 16: return top->io_registers_16;
        case 17: return top->io_registers_17;
        case 18: return top->io_registers_18;
        case 19: return top->io_registers_19;
        case 20: return top->io_registers_20;
        case 21: return top->io_registers_21;
        case 22: return top->io_registers_22;
        case 23: return top->io_registers_23;
        case 24: return top->io_registers_24;
        case 25: return top->io_registers_25;
        case 26: return top->io_registers_26;
        case 27: return top->io_registers_27;
        case 28: return top->io_registers_28;
        case 29: return top->io_registers_29;
        case 30: return top->io_registers_30;
        case 31: return top->io_registers_31;
    }

    return 0; // never reach here
}

/**
 * @brief 根据寄存器索引，获取寄存器名称。
 * 
 * @param idx 寄存器索引
 * @return word_t 寄存器名称；若寄存器不存在或读取失败则返回空指针
 */
const char *isaRegName(size_t idx) {
    if (idx >= LEN_REGS) {
        return nullptr;
    }

    return regs[idx];
}

/**
 * @brief ISA：获取给定名称的寄存器中的值。
 * 
 * @param s 寄存器名称
 * @param success 操作是否成功
 * @return word_t 获取到的寄存器值；若获取失败则返回0
 */
word_t isaRegStr2Val(const char *s, bool *success) {
    int i;
    std::string str(s);

    for (i = 0; i < LEN_REGS; i++) {
        if (str.compare(regs[i]) == 0) {
            *success = true;
            return isaRegVal(i);
        }
    }

    *success = false;
    return 0;
}

/**
 * @brief ISA：将所有寄存器中的值打印到标准输出。
 */
void isaRegDisplay() {
    int i;

    std::cout << "Registers:" << std::endl;
    for (i = 0; i < LEN_REGS; i++) {
        std::cout << std::setfill(' ') << std::setw(4) << regs[i] <<
        ": 0x" << std::setfill('0') << std::setw(8) <<
        std::hex << isaRegVal(i) << std::dec << std::endl;
    }

    std::cout << "PC is currently at 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << top->io_pc << std::dec << std::endl;
}
