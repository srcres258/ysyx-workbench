#include <iostream>
#include <fstream>
#include <macro-def.hpp>
#include <utils.hpp>
#include <device/io.hpp>
#include <device/io/map.hpp>
#include <device/mrom.hpp>

void *mrom_io_base = nullptr;

#define IFDBG if (sim_config.config_debugOutput)

static bool initMROMMemory(std::string filename, size_t *fileSize) {
    size_t size;

    std::ifstream f(filename, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    IFDBG std::cout << "Loading MROM from file: " << filename << std::endl;
    size = f.tellg();
    IFDBG std::cout << "File size is " << size << " bytes." << std::endl;
    if (size > MROM_LEN) {
        std::cerr << "MROM file is too large, size is " << size <<
            " bytes, but maximum is only " << MROM_LEN << " bytes." << std::endl;
        return false;
    }
    f.seekg(0, std::ios::beg);
    if (f.read((char *) mrom_io_base, size)) {
        IFDBG std::cout << "Successfully loaded MROM from file." << std::endl;
    } else {
        std::cerr << "Failed to read MROM from file!" << std::endl;
        return false;
    }

    if (fileSize) {
        *fileSize = size;
    }
    return true;
}

static void mrom_io_handler(addr_t offset, int len, bool isWrite) {
    // Nothing to do here.
}

bool device_mrom_init() {
    mrom_io_base = device_io_map_newSpace(MROM_LEN);
    device_io_addMMIOMap("mrom", MROM_ADDR, mrom_io_base, MROM_LEN, mrom_io_handler);

    if (sim_config.config_mrom) {
        IFDBG std::cout << "Initializing MROM from bin file..." << std::endl;
        std::string filename(sim_config.config_mromBinFilePath);
        size_t fileSize;
        if (!initMROMMemory(filename, &fileSize)) {
            std::cerr << "Failed to initialize MROM from bin file: " << filename << std::endl;
            return false;
        }
        IFDBG std::cout << "Finished initializing MROM, size is " << fileSize << " bytes." << std::endl;
    }

    return true;
}

word_t device_mrom_read(addr_t addr, int len) {
    word_t result;

    const addr_t baseAddr = MROM_ADDR;
    const addr_t baseLen = MROM_LEN;
    Assert(
        addr >= baseAddr && len >= 1 && len <= 4 &&
        addr - baseAddr <= baseLen - (addr_t) len,
        "MROM: invalid memory read address: " FMT_ADDR "\n",
        addr
    );
    Assert(
        len == 1 || len == 2 || len == 4,
        "MROM: invalid memory read length: %d\n",
        len
    );

    uint8_t *mromMemory = (uint8_t *) mrom_io_base;
    result = mromMemory[addr - baseAddr];
    if (len >= 2) {
        result |= mromMemory[addr - baseAddr + 1] << 8;
    }
    if (len >= 4) {
        result |= mromMemory[addr - baseAddr + 2] << 16;
        result |= mromMemory[addr - baseAddr + 3] << 24;
    }

    trace_record_dtrace(0, "mrom", false, addr, len, result, "dpi", "MROM");

    return result;
}
