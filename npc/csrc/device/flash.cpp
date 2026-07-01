#include <iostream>
#include <fstream>
#include <macro-def.hpp>
#include <utils.hpp>
#include <device/io.hpp>
#include <device/io/map.hpp>
#include <device/flash.hpp>

void *flash_io_base = nullptr;

#define IFDBG if (sim_config.config_debugOutput)

static bool initFlashMemory(std::string filename, size_t *fileSize) {
    size_t size;

    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    IFDBG std::cout << "Loading flash from file: " << filename << std::endl;
    size = f.tellg();
    IFDBG std::cout << "File size is " << size << " bytes." << std::endl;
    if (size > FLASH_LEN) {
        std::cerr << "Flash file is too large, size is " << size <<
            " bytes, but maximum is only " << FLASH_LEN << " bytes." << std::endl;
        return false;
    }
    f.seekg(0, std::ios::beg);
    if (f.read((char *) flash_io_base, size)) {
        IFDBG std::cout << "Successfully loaded flash from file." << std::endl;
    } else {
        std::cerr << "Failed to read flash from file!" << std::endl;
        return false;
    }

    if (fileSize) {
        *fileSize = size;
    }
    return true;
}

static void flash_io_handler(addr_t offset, int len, bool isWrite) {
    // Nothing to do here.
}

bool device_flash_init() {
    flash_io_base = device_io_map_newSpace(FLASH_LEN);
    device_io_addMMIOMap("flash", FLASH_ADDR, flash_io_base, FLASH_LEN, flash_io_handler);

    IFDBG std::cout << "Initializing flash from bin file..." << std::endl;
    std::string filename(sim_config.config_flashBinFilePath);
    size_t fileSize;
    if (!initFlashMemory(filename, &fileSize)) {
        std::cerr << "Failed to initialize flash from bin file: " << filename << std::endl;
        return false;
    }
    IFDBG std::cout << "Finished initializing flash, size is " << fileSize << " bytes." << std::endl;

    return true;
}

word_t device_flash_read(addr_t addr, int len) {
    word_t result;

    const addr_t baseAddr = FLASH_ADDR;
    const addr_t baseLen = FLASH_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "flash: invalid memory read address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "flash: invalid memory read length: %d\n",
        len
    );

    uint8_t *flashMemory = (uint8_t *) flash_io_base;
    result = flashMemory[addr - baseAddr];
    if (len >= 2) {
        result |= flashMemory[addr - baseAddr + 1] << 8;
    }
    if (len >= 4) {
        result |= flashMemory[addr - baseAddr + 2] << 16;
        result |= flashMemory[addr - baseAddr + 3] << 24;
    }

    trace_record_dtrace(0, "flash", false, addr, len, result, "dpi", "FLASH");

    return result;
}
