#include <npc/config.hpp>
#include <utils.hpp>
#include <common.hpp>
#include <string>

namespace npc {
namespace {

void translateConfig(const SimulatorConfig &src, SimConfig &dst) {
    // ---- trace switches ----
    dst.config_itrace = src.itraceEnabled;
    dst.config_mtrace = src.mtraceEnabled;
    dst.config_ftrace = src.ftraceEnabled;
    dst.config_dtrace = src.dtraceEnabled;
    dst.config_etrace = src.etraceEnabled;

    // ---- feature switches ----
    dst.config_difftest     = src.difftestEnabled;
    dst.config_device       = src.deviceEnabled;
    dst.config_wave         = src.waveEnabled;
    dst.config_debugOutput  = src.debugOutputEnabled;
    dst.config_nvboard      = src.nvboardEnabled;
    dst.config_vga          = src.vgaEnabled;
    dst.config_mrom         = src.mromEnabled;

    // ---- TUI switches ----
    dst.config_tui  = src.tuiEnabled;
    dst.config_perf = src.perfEnabled;

    // ---- trace format ----
    dst.config_traceFormat   = src.traceFormat;
    dst.config_traceDataMode = src.traceDataMode;

    // ---- TUI config ----
    dst.config_tuiConfigFilePath       = src.tuiConfigFilePath;
    dst.config_tuiGenerateConfig       = src.tuiGenerateConfig;
    dst.config_tuiGenerateFullConfig   = src.tuiGenerateFullConfig;
    dst.config_tuiForceOverwriteConfig = src.tuiForceOverwriteConfig;
    dst.config_tuiPrintConfigSchema    = src.tuiPrintConfigSchema;
    dst.config_tuiPrintDefaultConfig   = src.tuiPrintDefaultConfig;

    // ---- DiffTest ----
    dst.config_difftestPort                 = src.difftestPort;
    dst.config_difftestStartMode            = src.difftestStartMode;
    dst.config_difftestStartPC              = static_cast<addr_t>(src.difftestStartPC);
    dst.config_difftestPayloadBinFilePath   = src.difftestPayloadBinFilePath;
    dst.config_difftestPayloadLoadAddr      = static_cast<addr_t>(src.difftestPayloadLoadAddr);
    dst.config_difftestMemMode              = src.difftestMemMode;

    // ---- output file paths ----
    dst.config_itraceOutFilePath      = src.itraceOutFilePath;
    dst.config_itraceJsonlOutFilePath = src.itraceJsonlOutFilePath;
    dst.config_mtraceOutFilePath      = src.mtraceOutFilePath;
    dst.config_mtraceJsonlOutFilePath = src.mtraceJsonlOutFilePath;
    dst.config_ftraceOutFilePath      = src.ftraceOutFilePath;
    dst.config_dtraceOutFilePath      = src.dtraceOutFilePath;
    dst.config_dtraceJsonlOutFilePath = src.dtraceJsonlOutFilePath;
    dst.config_etraceOutFilePath      = src.etraceOutFilePath;
    dst.config_etraceJsonlOutFilePath = src.etraceJsonlOutFilePath;
    dst.config_flashBinFilePath       = src.flashBinFilePath;
    dst.config_flashElfFilePath       = src.flashElfFilePath;
    dst.config_mromBinFilePath        = src.mromBinFilePath;
    dst.config_difftestSoFilePath     = src.difftestSoFilePath;
    dst.config_waveFilePath           = src.waveFilePath;
}

void resetSimState(SimState &state) {
    state.state             = SIM_RUNNING;
    state.haltPC            = 0;
    state.itrace_iringbuf   = nullptr;
    // ofstreams are left in their default-constructed state;
    // the runner will reopen them via sim_state_ofstream_init().
}

} // anonymous namespace

void applySimulatorConfig(const SimulatorConfig &config) {
    translateConfig(config, ::sim_config);
    resetSimState(::sim_state);
}

} // namespace npc
