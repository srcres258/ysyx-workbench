#include <sim_top.hpp>
#include <utils.hpp>
#include <npc/simulator.hpp>
#include "npc/simulator_impl.hpp"

// ═══════════════════════════════════════════════════════════════════
//  Compatibility wrappers  —  delegate to library-internal impl
//
//  The authoritative implementations have moved to
//  npc/csrc/npc/simulator.cpp as part of Task 5.
//  These free-function wrappers keep all existing consumers
//  (sdb.cpp, tui/tui_control.cpp, main.cpp) compiling while
//  the migration to npc::Simulator methods proceeds.
// ═══════════════════════════════════════════════════════════════════

void simStepClockPeriod() {
    npc::internal::simStepClockImpl();
}

void simStep() {
    npc::internal::simStepImpl();
}

void simReset(int n) {
    npc::internal::simResetImpl(n);
}

bool simExecOnce() {
    return npc::internal::simExecOnceImpl();
}

void simExec(uint64_t n) {
    npc::internal::simExecImpl(n);
}

void simExecClockPeriod(uint64_t n) {
    npc::internal::simExecClockPeriodImpl(n);
}

bool simulate(bool sdbEnabled) {
    auto* bridge = getActiveSimulator();
    if (!bridge || !bridge->owner) {
        std::fprintf(
            stderr,
            "[npc] simulate() called with no active Simulator\n"
        );
        return false;
    }
    return bridge->owner->run(sdbEnabled);
}
