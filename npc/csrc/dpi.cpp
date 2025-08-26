#include <iostream>
#include <iomanip>
#include <cstdint>
#include <sim_top.hpp>
#include <memory.hpp>

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
