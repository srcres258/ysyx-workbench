//
// npc/tests/test_support.cpp — Config helper implementation for the NPC test suite.
//
// Provides MakeDefaultConfig() which returns a SimulatorConfig with all noisy
// features disabled and output paths redirected to temp.  Used by the
// NpcSimulatorTest fixture and any standalone test that needs a safe baseline.
//

#include "test_fixture.h"

#include <string>

npc::SimulatorConfig NpcSimulatorTest::MakeDefaultConfig() {
    npc::SimulatorConfig cfg;

    // ── Disable all trace channels ──
    cfg.itraceEnabled = false;
    cfg.mtraceEnabled = false;
    cfg.ftraceEnabled = false;
    cfg.dtraceEnabled = false;
    cfg.etraceEnabled = false;

    // ── Disable interactive / UI / external features ──
    cfg.difftestEnabled    = false;
    cfg.deviceEnabled      = false;
    cfg.waveEnabled        = false;
    cfg.debugOutputEnabled = false;
    cfg.nvboardEnabled     = false;
    cfg.vgaEnabled         = false;
    cfg.mromEnabled        = false;
    cfg.tuiEnabled         = false;
    cfg.perfEnabled        = false; // Keep counters quiet for smoke tests

    // ── Redirect all output paths to temp ──
    // Tests run from npc/ as CWD; the default "build/" paths in SimulatorConfig
    // would pollute the real build directory.  /tmp is safe and isolated.
    // Note: with all trace/device features disabled, these paths are never
    // opened — the redirection is purely defensive.
    const char *base = "/tmp/npc-test";
    cfg.itraceOutFilePath      = std::string(base) + "/itrace.log";
    cfg.itraceJsonlOutFilePath = std::string(base) + "/itrace.jsonl";
    cfg.mtraceOutFilePath      = std::string(base) + "/mtrace.log";
    cfg.mtraceJsonlOutFilePath = std::string(base) + "/mtrace.jsonl";
    cfg.ftraceOutFilePath      = std::string(base) + "/ftrace.log";
    cfg.dtraceOutFilePath      = std::string(base) + "/dtrace.log";
    cfg.dtraceJsonlOutFilePath = std::string(base) + "/dtrace.jsonl";
    cfg.etraceOutFilePath      = std::string(base) + "/etrace.log";
    cfg.etraceJsonlOutFilePath = std::string(base) + "/etrace.jsonl";
    cfg.flashBinFilePath       = std::string(base) + "/flash.bin";
    cfg.flashElfFilePath       = std::string(base) + "/flash.elf";
    cfg.mromBinFilePath        = std::string(base) + "/mrom.bin";
    cfg.waveFilePath           = std::string(base) + "/sim.fst";

    return cfg;
}

// ── Linker stubs for runner-side symbols ─────────────────────────────────
// The simulator library (libnpc.a) references nvboard_bind_all_pins() when
// built in ysyxsoc mode — the call is guarded at runtime by
// config_device + config_nvboard (both false in MakeDefaultConfig()), but
// the linker still requires the symbol.  The runner resolves it via
// auto_bind.cpp; the test binary provides this no-op stub.

class VysyxSoCFull;

void nvboard_bind_all_pins(VysyxSoCFull *) {}
