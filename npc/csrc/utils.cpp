#include <dlfcn.h>
#include <capstone/capstone.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <array>
#include <format>
#include <iostream>
#include <sstream>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <utils.hpp>
#include <sim_top.hpp>

#ifdef NPC_STANDALONE
#include <device/keyboard.hpp>
#include <device/rtc.hpp>
#include <device/serial.hpp>
#include <device/vga.hpp>
#endif

// ----------- state -----------

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

static bool traceFormatWantsHuman() {
    return sim_config.config_traceFormat == "human" || sim_config.config_traceFormat == "both";
}

static bool traceFormatWantsJsonl() {
    return sim_config.config_traceFormat == "jsonl" || sim_config.config_traceFormat == "both";
}

/**
 * @brief 初始化用于 itrace 的环形缓冲区。
 */
void sim_state_itrace_iringbuf_init() {
    sim_state.itrace_iringbuf = new RingBuffer(ITRACE_IRINGBUF_SIZE);
}

/**
 * @brief 释放用于 itrace 的环形缓冲区。
 */
void sim_state_itrace_iringbuf_destroy() {
    if (sim_state.itrace_iringbuf) {
        delete sim_state.itrace_iringbuf;
        sim_state.itrace_iringbuf = nullptr;
    }
}

/**
 * @brief 输出用于 itrace 的环形缓冲区中的内容。
 */
void sim_state_itrace_iringbuf_dump() {
    try {
        auto *iringbuf = sim_state.itrace_iringbuf;
        std::string str = iringbuf->read(iringbuf->availableData());
        std::cout << "itrace_iringbuf data:" << std::endl;
        std::cout << str << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Failed to get iringbuf data: " << e.what() << std::endl;
    }
}

/**
 * @brief 初始化所有用于 trace 记录的文件输出流（ofstream）。
 */
void sim_state_ofstream_init() {
    if (sim_config.config_itrace && traceFormatWantsHuman()) {
        sim_state.itrace_ofs.open(sim_config.config_itraceOutFilePath);
    }
    if (sim_config.config_itrace && traceFormatWantsJsonl()) {
        sim_state.itrace_jsonl_ofs.open(sim_config.config_itraceJsonlOutFilePath);
    }
    if (sim_config.config_mtrace && traceFormatWantsHuman()) {
        sim_state.mtrace_ofs.open(sim_config.config_mtraceOutFilePath);
    }
    if (sim_config.config_mtrace && traceFormatWantsJsonl()) {
        sim_state.mtrace_jsonl_ofs.open(sim_config.config_mtraceJsonlOutFilePath);
    }
    if (sim_config.config_ftrace && traceFormatWantsHuman()) {
        sim_state.ftrace_ofs.open(sim_config.config_ftraceOutFilePath);
    }
    if (sim_config.config_dtrace && traceFormatWantsHuman()) {
        sim_state.dtrace_ofs.open(sim_config.config_dtraceOutFilePath);
    }
    if (sim_config.config_dtrace && traceFormatWantsJsonl()) {
        sim_state.dtrace_jsonl_ofs.open(sim_config.config_dtraceJsonlOutFilePath);
    }
    if (sim_config.config_etrace && traceFormatWantsHuman()) {
        sim_state.etrace_ofs.open(sim_config.config_etraceOutFilePath);
    }
    if (sim_config.config_etrace && traceFormatWantsJsonl()) {
        sim_state.etrace_jsonl_ofs.open(sim_config.config_etraceJsonlOutFilePath);
    }
}

/**
 * @brief 关闭所有用于 trace 记录的文件输出流（ofstream）。
 */
void sim_state_ofstream_finalise() {
    if (sim_config.config_itrace && sim_state.itrace_ofs.is_open()) {
        sim_state.itrace_ofs.flush();
        sim_state.itrace_ofs.close();
    }
    if (sim_config.config_itrace && sim_state.itrace_jsonl_ofs.is_open()) {
        sim_state.itrace_jsonl_ofs.flush();
        sim_state.itrace_jsonl_ofs.close();
    }
    if (sim_config.config_mtrace && sim_state.mtrace_ofs.is_open()) {
        sim_state.mtrace_ofs.flush();
        sim_state.mtrace_ofs.close();
    }
    if (sim_config.config_mtrace && sim_state.mtrace_jsonl_ofs.is_open()) {
        sim_state.mtrace_jsonl_ofs.flush();
        sim_state.mtrace_jsonl_ofs.close();
    }
    if (sim_config.config_ftrace && sim_state.ftrace_ofs.is_open()) {
        sim_state.ftrace_ofs.flush();
        sim_state.ftrace_ofs.close();
    }
    if (sim_config.config_dtrace && sim_state.dtrace_ofs.is_open()) {
        sim_state.dtrace_ofs.flush();
        sim_state.dtrace_ofs.close();
    }
    if (sim_config.config_dtrace && sim_state.dtrace_jsonl_ofs.is_open()) {
        sim_state.dtrace_jsonl_ofs.flush();
        sim_state.dtrace_jsonl_ofs.close();
    }
    if (sim_config.config_etrace && sim_state.etrace_ofs.is_open()) {
        sim_state.etrace_ofs.flush();
        sim_state.etrace_ofs.close();
    }
    if (sim_config.config_etrace && sim_state.etrace_jsonl_ofs.is_open()) {
        sim_state.etrace_jsonl_ofs.flush();
        sim_state.etrace_jsonl_ofs.close();
    }
}

/**
 * @brief 根据相关配置，加载程序中的函数符号信息。
 * 需提前确保 sim_config 中相关配置信息已正确填入。
 *
 * @return true 加载成功
 * @return false 加载失败
 */
bool sim_state_ftrace_funcSyms_init() {
    int fd;
    Elf *elf;
    size_t size;

    // Before the first call to elf_begin() ,
    // a program must call elf_version() to coordinate versions.
    if (elf_version(EV_CURRENT) == EV_NONE) {
        std::cerr << "libelf version is missing! ELF file will not be loaded." << std::endl;
        return false;
    }

    std::string elfFilePath(sim_config.config_flashElfFilePath);
    fd = open(elfFilePath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "Failed to open ELF file: " << elfFilePath << std::endl;
        return false;
    }
    elf = elf_begin(fd, ELF_C_READ, nullptr);
    if (!elf) {
        std::cerr << "Failed to load ELF file: " << elfFilePath << std::endl;
        close(fd);
        return false;
    }
    // 确定文件类型是否是ELF文件
    if (elf_kind(elf) != ELF_K_ELF) {
        std::cerr << "Not an ELF file: " << elfFilePath << std::endl;
        elf_end(elf);
        close(fd);
        return false;
    }
    size = loadFunctionSymbolsFromElf(&sim_state.ftrace_funcSyms, elf);
    elf_end(elf);
    close(fd);
    std::cout << "Loaded " << size << " function symbols from ELF file: " <<
        elfFilePath << std::endl;

    return true;
}

namespace {

static constexpr uint64_t kTraceFlushBatch = 1024;
static uint64_t g_traceSeq = 0;
static uint64_t g_itraceWrites = 0;
static uint64_t g_mtraceWrites = 0;
static uint64_t g_dtraceWrites = 0;
static uint64_t g_etraceWrites = 0;

enum class MemoryClass {
    Ram,
    Rom,
    Flash,
    Framebuffer,
    MmioRegister,
    Timer,
    Uart,
    Keyboard,
    Unknown,
};

enum class CachePolicy {
    Cacheable,
    ReadOnlyCacheable,
    WriteThroughPreferred,
    Uncacheable,
    Unknown,
};

struct MemoryRegionDescriptor {
    const char *id;
    const char *name;
    addr_t base;
    addr_t end;
    MemoryClass memoryClass;
    CachePolicy cachePolicy;
    bool readable;
    bool writable;
    bool executable;
    bool volatileAccess;
    bool readHasSideEffect;
    bool writeHasSideEffect;
    bool idempotentRead;
    bool idempotentWrite;
    const char *reason;
};

static std::string toLowerCopy(std::string_view s) {
    std::string out(s.begin(), s.end());
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

static bool traceDataModeKeepValue(bool isWrite) {
    if (sim_config.config_traceDataMode == "none") {
        return false;
    }
    if (sim_config.config_traceDataMode == "stores") {
        return isWrite;
    }
    return true;
}

static const char *memoryClassToString(MemoryClass memoryClass) {
    switch (memoryClass) {
        case MemoryClass::Ram: return "ram";
        case MemoryClass::Rom: return "rom";
        case MemoryClass::Flash: return "flash";
        case MemoryClass::Framebuffer: return "framebuffer";
        case MemoryClass::MmioRegister: return "mmio_register";
        case MemoryClass::Timer: return "timer";
        case MemoryClass::Uart: return "uart";
        case MemoryClass::Keyboard: return "keyboard";
        case MemoryClass::Unknown: return "unknown";
    }
    return "unknown";
}

static const char *cachePolicyToString(CachePolicy cachePolicy) {
    switch (cachePolicy) {
        case CachePolicy::Cacheable: return "cacheable";
        case CachePolicy::ReadOnlyCacheable: return "read_only_cacheable";
        case CachePolicy::WriteThroughPreferred: return "write_through_preferred";
        case CachePolicy::Uncacheable: return "uncacheable";
        case CachePolicy::Unknown: return "unknown";
    }
    return "unknown";
}

static double cachePolicyToEligibility(CachePolicy cachePolicy) {
    switch (cachePolicy) {
        case CachePolicy::Cacheable:
        case CachePolicy::ReadOnlyCacheable:
            return 1.0;
        case CachePolicy::WriteThroughPreferred:
            return 0.5;
        case CachePolicy::Uncacheable:
        case CachePolicy::Unknown:
            return 0.0;
    }
    return 0.0;
}

static std::string jsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(ch));
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

class JsonObjectBuilder {
public:
    void addString(std::string_view key, std::string_view value) {
        addKey(key);
        out += '"';
        out += jsonEscape(value);
        out += '"';
    }

    void addStringOrNull(std::string_view key, std::string_view value, bool keepValue) {
        if (keepValue) {
            addString(key, value);
        } else {
            addNull(key);
        }
    }

    void addBool(std::string_view key, bool value) {
        addKey(key);
        out += value ? "true" : "false";
    }

    void addInt(std::string_view key, uint64_t value) {
        addKey(key);
        out += std::to_string(value);
    }

    void addSigned(std::string_view key, int64_t value) {
        addKey(key);
        out += std::to_string(value);
    }

    void addDouble(std::string_view key, double value) {
        addKey(key);
        out += std::format("{:.6g}", value);
    }

    void addHex(std::string_view key, uint64_t value) {
        addKey(key);
        out += std::format("\"0x{:x}\"", value);
    }

    void addNull(std::string_view key) {
        addKey(key);
        out += "null";
    }

    std::string finish() && {
        out.push_back('}');
        return std::move(out);
    }

private:
    void addKey(std::string_view key) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        out.push_back('"');
        out += jsonEscape(key);
        out += '"';
        out.push_back(':');
    }

    std::string out = "{";
    bool first = true;
};

static void writeHumanLine(std::ofstream &ofs, const std::string &line, uint64_t &counter) {
    ofs << line << '\n';
    counter++;
    if ((counter % kTraceFlushBatch) == 0) {
        ofs.flush();
    }
}

static void writeJsonLine(std::ofstream &ofs, const std::string &line, uint64_t &counter) {
    ofs << line << '\n';
    counter++;
    if ((counter % kTraceFlushBatch) == 0) {
        ofs.flush();
    }
}

static std::string hexValue(uint64_t value) {
    return std::format("0x{:x}", value);
}

static MemoryRegionDescriptor makeUnknownRegion(addr_t addr) {
    return MemoryRegionDescriptor{
        .id = "unknown",
        .name = "UNKNOWN",
        .base = addr,
        .end = addr,
        .memoryClass = MemoryClass::Unknown,
        .cachePolicy = CachePolicy::Unknown,
        .readable = false,
        .writable = false,
        .executable = false,
        .volatileAccess = false,
        .readHasSideEffect = false,
        .writeHasSideEffect = false,
        .idempotentRead = false,
        .idempotentWrite = false,
        .reason = "unmapped or unknown address",
    };
}

#ifdef NPC_STANDALONE
static constexpr addr_t kStandalonePmemBase = 0x80000000UL;
static constexpr addr_t kStandalonePmemEnd = kStandalonePmemBase + (128UL * 1024UL * 1024UL) - 1;

static const std::array<MemoryRegionDescriptor, 6> kTraceRegions = {{
    {"pmem", "PMEM", kStandalonePmemBase, kStandalonePmemEnd, MemoryClass::Ram, CachePolicy::Cacheable,
        true, true, true, false, false, false, true, true, "flat standalone physical memory"},
    {"rtc", "RTC", RTC_MMIO_BASE, RTC_MMIO_BASE + 7, MemoryClass::Timer, CachePolicy::Uncacheable,
        true, false, false, true, false, false, false, true, "volatile timer register; repeated reads are time-dependent"},
    {"keyboard", "Keyboard", KEYBOARD_MMIO_BASE, KEYBOARD_MMIO_BASE + 3, MemoryClass::Keyboard, CachePolicy::Uncacheable,
        true, false, false, true, true, false, false, true, "input FIFO and status register; reads consume events"},
    {"serial", "UART", SERIAL_MMIO_BASE, SERIAL_MMIO_BASE + 3, MemoryClass::Uart, CachePolicy::Uncacheable,
        false, true, false, true, false, true, true, false, "serial output port; writes have visible side effects"},
    {"vga_ctl", "VGA_CTL", VGA_CTL_BASE, VGA_CTL_BASE + 7, MemoryClass::MmioRegister, CachePolicy::Uncacheable,
        true, true, false, true, false, true, true, false, "VGA control registers; writes affect device state"},
    {"vga_fb", "VGA_FB", VGA_FB_BASE, VGA_FB_BASE + VGA_FB_SIZE - 1, MemoryClass::Framebuffer, CachePolicy::WriteThroughPreferred,
        true, true, false, false, false, true, true, true, "framebuffer memory; write-through or explicit flush is safer"},
}};
#else
static const std::array<MemoryRegionDescriptor, 11> kTraceRegions = {{
    {"mrom", "MROM", MROM_ADDR, MROM_ADDR + MROM_LEN - 1, MemoryClass::Rom, CachePolicy::ReadOnlyCacheable,
        true, false, true, false, false, false, true, true, "mask ROM; read-only backing store"},
    {"flash", "FLASH", FLASH_ADDR, FLASH_ADDR + FLASH_LEN - 1, MemoryClass::Flash, CachePolicy::ReadOnlyCacheable,
        true, false, true, false, false, false, true, true, "execute-in-place flash; reads are stable, writes are not modeled here"},
    {"psram", "PSRAM", PSRAM_ADDR, PSRAM_ADDR + PSRAM_LEN - 1, MemoryClass::Ram, CachePolicy::Cacheable,
        true, true, true, false, false, false, true, true, "ordinary mutable memory"},
    {"sdram", "SDRAM", SDRAM_ADDR, SDRAM_ADDR + SDRAM_LEN - 1, MemoryClass::Ram, CachePolicy::Cacheable,
        true, true, true, false, false, false, true, true, "ordinary mutable memory"},
    {"sram", "SRAM", SRAM_ADDR, SRAM_ADDR + SRAM_LEN - 1, MemoryClass::Ram, CachePolicy::Cacheable,
        true, true, true, false, false, false, true, true, "on-chip RAM; cacheable ordinary memory"},
    {"uart", "UART", 0x10000000UL, 0x10000000UL + 0x1000UL - 1, MemoryClass::Uart, CachePolicy::Uncacheable,
        true, true, false, true, true, true, false, false, "UART MMIO register block; writes have side effects"},
    {"spi", "SPI", 0x10001000UL, 0x10001000UL + 0x1000UL - 1, MemoryClass::MmioRegister, CachePolicy::Uncacheable,
        true, true, false, true, false, true, true, false, "SPI controller registers; writes control external flash state"},
    {"gpio", "GPIO", 0x10002000UL, 0x10002000UL + 0x10UL - 1, MemoryClass::MmioRegister, CachePolicy::Uncacheable,
        true, true, false, true, false, true, true, false, "GPIO control registers"},
    {"keyboard", "Keyboard", 0x10011000UL, 0x10011000UL + 0x8UL - 1, MemoryClass::Keyboard, CachePolicy::Uncacheable,
        true, false, false, true, true, false, false, true, "keyboard controller FIFO; reads consume state"},
    {"vga", "VGA", 0x21000000UL, 0x21000000UL + 0x200000UL - 1, MemoryClass::Framebuffer, CachePolicy::WriteThroughPreferred,
        true, true, false, false, false, true, true, true, "VGA controller / framebuffer window; treat conservatively"},
    {"clint", "CLINT", 0x02000000UL, 0x02000000UL + 0x10000UL - 1, MemoryClass::Timer, CachePolicy::Uncacheable,
        true, true, false, true, true, true, false, false, "machine timer/compare registers; time-dependent"},
}};
#endif

static const MemoryRegionDescriptor &lookupRegion(addr_t addr, std::string_view device, std::string_view region) {
    std::string deviceLower = toLowerCopy(device);
    std::string regionLower = toLowerCopy(region);

    for (const auto &desc : kTraceRegions) {
        if (!deviceLower.empty() && (deviceLower == desc.id || deviceLower == toLowerCopy(desc.name))) {
            if (addr >= desc.base && addr <= desc.end) {
                return desc;
            }
        }
        if (!regionLower.empty() && (regionLower == desc.id || regionLower == toLowerCopy(desc.name))) {
            if (addr >= desc.base && addr <= desc.end) {
                return desc;
            }
        }
    }

    for (const auto &desc : kTraceRegions) {
        if (addr >= desc.base && addr <= desc.end) {
            return desc;
        }
    }

    static MemoryRegionDescriptor unknown = makeUnknownRegion(0);
    unknown = makeUnknownRegion(addr);
    return unknown;
}

static std::string buildTraceJson(
    const char *traceType, const char *eventLevel, const char *source, uint64_t seq,
    uint64_t cycle, uint64_t instret, addr_t pc, const char *accessKind, const char *direction,
    const char *initiator, addr_t addr, int len, std::string_view dataField,
    std::string_view byteMaskField, const MemoryRegionDescriptor &region,
    uint64_t requestCycle, uint64_t responseCycle, int64_t latencyCycles,
    int beatIndex, int beatCount, std::string_view device, bool dataEnabled
) {
    JsonObjectBuilder j;
    j.addString("schema", "npc.mtrace");
    j.addInt("schema_version", 1);
    j.addString("record_type", "access");
    j.addString("trace_type", traceType);
    j.addString("event_level", eventLevel);
    j.addInt("seq", seq);
    j.addInt("access_id", seq);
    j.addInt("transaction_id", seq);
    j.addInt("cycle", cycle);
    j.addInt("instret", instret);
    j.addString("pc", hexValue(pc));
    j.addInt("pc_u64", pc);
    j.addString("access_kind", accessKind);
    j.addString("direction", direction);
    j.addString("initiator", initiator);
    j.addString("device", device);
    j.addString("address", hexValue(addr));
    j.addInt("address_u64", addr);
    j.addInt("size_bytes", static_cast<uint64_t>(len));
    if (!byteMaskField.empty()) {
        j.addString("byte_mask", byteMaskField);
    } else {
        j.addNull("byte_mask");
    }
    if (dataEnabled) {
        j.addString("data", dataField);
    } else {
        j.addNull("data");
    }
    if (requestCycle == 0) {
        j.addNull("request_cycle");
    } else {
        j.addInt("request_cycle", requestCycle);
    }
    if (responseCycle == 0) {
        j.addNull("response_cycle");
    } else {
        j.addInt("response_cycle", responseCycle);
    }
    if (latencyCycles < 0) {
        j.addNull("latency_cycles");
    } else {
        j.addSigned("latency_cycles", latencyCycles);
    }
    j.addString("region_id", region.id);
    j.addString("region_name", region.name);
    j.addString("region_base", hexValue(region.base));
    j.addString("region_end", hexValue(region.end));
    if (addr >= region.base) {
        j.addInt("region_offset", addr - region.base);
    } else {
        j.addNull("region_offset");
    }
    j.addString("memory_class", memoryClassToString(region.memoryClass));
    j.addString("cache_policy", cachePolicyToString(region.cachePolicy));
    j.addDouble("cache_eligibility", cachePolicyToEligibility(region.cachePolicy));
    j.addBool("volatile", region.volatileAccess);
    j.addBool("side_effect", region.readHasSideEffect || region.writeHasSideEffect);
    j.addBool("idempotent_read", region.idempotentRead);
    j.addBool("idempotent_write", region.idempotentWrite);
    j.addString("cacheability_reason", region.reason);
    j.addInt("beat_index", static_cast<uint64_t>(beatIndex));
    j.addInt("beat_count", static_cast<uint64_t>(beatCount));
    j.addNull("burst_id");
    j.addString("source", source);
    j.addString("status", "ok");
    (void) len;
    return std::move(j).finish();
}

static std::string buildMtraceJson(
    addr_t pc, bool isWrite, addr_t addr, int len, word_t data, uint8_t strobe, uint32_t resp
) {
    const auto &region = lookupRegion(addr, "", "");
    const uint64_t seq = ++g_traceSeq;
    const uint64_t cycle = getExecCountClockPeriod();
    const uint64_t instret = getExecCount();
    const bool dataEnabled = traceDataModeKeepValue(isWrite);
    std::string dataField = hexValue(static_cast<uint64_t>(data));
    std::string byteMask = std::format("0x{:x}", strobe);
    (void) resp;
    return buildTraceJson(
        "mtrace", "architecture_access", "mtrace", seq, cycle, instret, pc,
        isWrite ? "store" : "load", isWrite ? "write" : "read", "lsu",
        addr, len, dataField, byteMask, region, 0, 0, -1, 0, 1, "cpu", dataEnabled
    );
}

static std::string buildItraceJson(addr_t pc, word_t inst) {
    const auto &region = lookupRegion(pc, "", "");
    const uint64_t seq = ++g_traceSeq;
    const uint64_t cycle = getExecCountClockPeriod();
    const uint64_t instret = getExecCount();
    const bool dataEnabled = traceDataModeKeepValue(false);
    std::string dataField = hexValue(static_cast<uint64_t>(inst));
    return buildTraceJson(
        "mtrace", "architecture_access", "itrace", seq, cycle, instret, pc,
        "ifetch", "read", "ifu", pc, 4, dataField, "", region, 0, 0, -1, 0, 1, "cpu", dataEnabled
    );
}

static std::string buildDtraceJson(
    addr_t pc, const char *device, bool isWrite, addr_t addr, int len, word_t data,
    const char *bus, const char *regionName
) {
    const auto &region = lookupRegion(addr, device ? device : "", regionName ? regionName : "");
    const uint64_t seq = ++g_traceSeq;
    const uint64_t cycle = getExecCountClockPeriod();
    const uint64_t instret = getExecCount();
    const bool dataEnabled = traceDataModeKeepValue(isWrite);
    std::string dataField = hexValue(static_cast<uint64_t>(data));
    std::string eventLevel = (bus && std::string_view(bus) == "io-map") ? "bus_transaction" : "device_access";
    std::string source = bus ? bus : "device";
    std::string initiator = bus ? bus : "device";
    std::string deviceStr = device ? device : "unknown";
    (void) pc;
    return buildTraceJson(
        "dtrace", eventLevel.c_str(), source.c_str(), seq, cycle, instret, pc,
        isWrite ? "store" : "load", isWrite ? "write" : "read", initiator.c_str(),
        addr, len, dataField, "", region, 0, 0, -1, 0, 1, deviceStr, dataEnabled
    );
}

static std::string buildEtraceJson(
    addr_t pc, const char *trapKind, word_t cause, word_t mepc, word_t mtval, word_t target
) {
    const auto &region = lookupRegion(pc, "", "");
    const uint64_t seq = ++g_traceSeq;
    const uint64_t cycle = getExecCountClockPeriod();
    const uint64_t instret = getExecCount();
    JsonObjectBuilder j;
    j.addString("schema", "npc.mtrace");
    j.addInt("schema_version", 1);
    j.addString("record_type", "trap");
    j.addString("trace_type", "etrace");
    j.addString("event_level", "architecture_access");
    j.addInt("seq", seq);
    j.addInt("access_id", seq);
    j.addInt("transaction_id", seq);
    j.addInt("cycle", cycle);
    j.addInt("instret", instret);
    j.addString("pc", hexValue(pc));
    j.addInt("pc_u64", pc);
    j.addString("trap_kind", trapKind);
    j.addString("cause", hexValue(cause));
    j.addString("mepc", hexValue(mepc));
    j.addString("mtval", hexValue(mtval));
    j.addString("target", hexValue(target));
    j.addString("region_id", region.id);
    j.addString("region_name", region.name);
    j.addString("region_base", hexValue(region.base));
    j.addString("region_end", hexValue(region.end));
    j.addString("memory_class", memoryClassToString(region.memoryClass));
    j.addString("cache_policy", cachePolicyToString(region.cachePolicy));
    j.addDouble("cache_eligibility", cachePolicyToEligibility(region.cachePolicy));
    j.addBool("volatile", region.volatileAccess);
    j.addBool("side_effect", region.readHasSideEffect || region.writeHasSideEffect);
    j.addBool("idempotent_read", region.idempotentRead);
    j.addBool("idempotent_write", region.idempotentWrite);
    j.addString("cacheability_reason", region.reason);
    j.addInt("beat_index", 0);
    j.addInt("beat_count", 1);
    j.addNull("burst_id");
    j.addString("source", "etrace");
    j.addString("status", "ok");
    return std::move(j).finish();
}

} // namespace

static addr_t trace_current_pc() {
    auto *module = getDPIModule();
    return module ? module->core_pc : 0;
}

void trace_record_mtrace(
    addr_t pc, bool isWrite, addr_t addr, int len, word_t data, uint8_t strobe, uint32_t resp
) {
    if (!sim_config.config_mtrace) {
        return;
    }

    const addr_t tracePc = pc;
    const char *op = isWrite ? "store" : "load";
    std::string message = std::format(
        "0x{:08x}: mtrace {} addr=0x{:08x} len={} data=0x{:08x} strobe=0x{:x} resp={}",
        tracePc, op, addr, len, data, strobe, resp
    );
    if (traceFormatWantsHuman() && sim_state.mtrace_ofs.is_open()) {
        writeHumanLine(sim_state.mtrace_ofs, message, g_mtraceWrites);
    }
    if (traceFormatWantsJsonl() && sim_state.mtrace_jsonl_ofs.is_open()) {
        writeJsonLine(sim_state.mtrace_jsonl_ofs, buildMtraceJson(tracePc, isWrite, addr, len, data, strobe, resp), g_mtraceWrites);
    }
    if (sim_config.config_debugOutput) {
        std::cout << "[sim] mtrace: " << message << std::endl;
    }
}

void trace_record_itrace(addr_t pc, word_t inst) {
    if (!sim_config.config_itrace) {
        return;
    }

    if (sim_state.itrace_jsonl_ofs.is_open()) {
        writeJsonLine(sim_state.itrace_jsonl_ofs, buildItraceJson(pc, inst), g_itraceWrites);
    }
}

void trace_record_dtrace(
    addr_t pc, const char *device, bool isWrite, addr_t addr, int len, word_t data,
    const char *bus, const char *region
) {
    if (!sim_config.config_dtrace) {
        return;
    }

    const addr_t tracePc = pc ? pc : trace_current_pc();
    const char *op = isWrite ? "write" : "read";
    std::string message = std::format(
        "0x{:08x}: dtrace device={} {} addr=0x{:08x} len={} data=0x{:08x} bus={} region={}",
        tracePc, device, op, addr, len, data, bus, region
    );
    if (traceFormatWantsHuman() && sim_state.dtrace_ofs.is_open()) {
        writeHumanLine(sim_state.dtrace_ofs, message, g_dtraceWrites);
    }
    if (traceFormatWantsJsonl() && sim_state.dtrace_jsonl_ofs.is_open()) {
        writeJsonLine(sim_state.dtrace_jsonl_ofs, buildDtraceJson(tracePc, device, isWrite, addr, len, data, bus, region), g_dtraceWrites);
    }
    if (sim_config.config_debugOutput) {
        std::cout << "[sim] dtrace: " << message << std::endl;
    }
}

void trace_record_etrace(
    addr_t pc, const char *trapKind, word_t cause, word_t mepc, word_t mtval, word_t target
) {
    if (!sim_config.config_etrace) {
        return;
    }

    const addr_t tracePc = pc ? pc : trace_current_pc();
    std::string message = std::format(
        "0x{:08x}: etrace trap={} cause=0x{:08x} mepc=0x{:08x} mtval=0x{:08x} target=0x{:08x}",
        tracePc, trapKind, cause, mepc, mtval, target
    );
    if (traceFormatWantsHuman() && sim_state.etrace_ofs.is_open()) {
        writeHumanLine(sim_state.etrace_ofs, message, g_etraceWrites);
    }
    if (traceFormatWantsJsonl() && sim_state.etrace_jsonl_ofs.is_open()) {
        writeJsonLine(sim_state.etrace_jsonl_ofs, buildEtraceJson(tracePc, trapKind, cause, mepc, mtval, target), g_etraceWrites);
    }
    if (sim_config.config_debugOutput) {
        std::cout << "[sim] etrace: " << message << std::endl;
    }
}

// ----------- disasm -----------

using cs_disasm_f_t = size_t (*)(csh handle, const uint8_t *code,
    size_t code_size, uint64_t address, size_t count, cs_insn **insn);
using cs_free_f_t = void (*)(cs_insn *insn, size_t count);
using cs_open_f_t = cs_err (*)(cs_arch arch, cs_mode mode, csh *handle);

static cs_disasm_f_t cs_disasm_dl = nullptr;
static cs_free_f_t cs_free_dl = nullptr;
static cs_open_f_t cs_open_dl = nullptr;

static csh handle;

/**
 * @brief 初始化反汇编工具。在使用本反汇编工具前须调用此函数。
 */
void disasm_init() {
    void *dl_handle;

    dl_handle = dlopen("libcapstone.so.5", RTLD_LAZY);
    assert(dl_handle);

    cs_open_dl = (cs_open_f_t) dlsym(dl_handle, "cs_open");
    assert(cs_open_dl);

    cs_disasm_dl = (cs_disasm_f_t) dlsym(dl_handle, "cs_disasm");
    assert(cs_disasm_dl);

    cs_free_dl = (cs_free_f_t) dlsym(dl_handle, "cs_free");
    assert(cs_free_dl);

    cs_arch arch = CS_ARCH_RISCV;
    cs_mode mode = (cs_mode) (CS_MODE_RISCV32 | CS_MODE_RISCVC);
    int ret = cs_open_dl(arch, mode, &handle);
    assert(ret == CS_ERR_OK);
}

/**
 * @brief 使用反汇编工具反汇编一段代码。
 *
 * @param str 输出目的字符串缓冲区
 * @param size 字符串缓冲区大小
 * @param pc 程序计数器
 * @param code 待反汇编的代码段
 * @param nbyte 待反汇编的代码段长度
 */
void disasm_disassemble(
    char *str, int size, uint64_t pc,
    uint8_t *code, int nbyte
) {
    bool ok = disasm_tryDisassemble(str, size, pc, code, nbyte);
    assert(ok);
}

bool disasm_tryDisassemble(
    char *str, int size, uint64_t pc,
    uint8_t *code, int nbyte
) {
    if (!str || size <= 0) {
        return false;
    }

    cs_insn *insn = nullptr;
    size_t count = cs_disasm_dl(handle, code, nbyte, pc, 0, &insn);
    if (count != 1 || insn == nullptr) {
        str[0] = '\0';
        if (insn != nullptr && count > 0) {
            cs_free_dl(insn, count);
        }
        return false;
    }

    int ret = snprintf(str, size, "%s", insn->mnemonic);
    if (insn->op_str[0] != '\0') {
        snprintf(str + ret, size - ret, " %s", insn->op_str);
    }
    cs_free_dl(insn, count);
    return true;
}

// ----------- memory -----------

word_t memoryHostRead(const void *addr, int len) {
    switch (len) {
        case 1:
            return *((uint8_t *) addr);
        case 2:
            return *((uint16_t *) addr);
        case 4:
            return *((uint32_t *) addr);
        case 8:
            return *((uint64_t *) addr);
        default:
            panic();
            return 0;
    }
}

void memoryHostWrite(void *addr, int len, word_t data) {
    switch (len) {
        case 1:
            *((uint8_t *) addr) = data;
            return;
        case 2:
            *((uint16_t *) addr) = data;
            return;
        case 4:
            *((uint32_t *) addr) = data;
            return;
        case 8:
            *((uint64_t *) addr) = data;
            return;
        default:
            panic();
    }
}
