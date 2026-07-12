#include <cstdint>
#include <tui/tui_control.hpp>
#include <tui/tui_events.hpp>
#include <sim_top.hpp>
#include <utils.hpp>

namespace tui {

static volatile bool s_pauseRequested = false;
static bool s_wasPaused = false;

void requestSimStepInst(uint64_t n) {
    if (n == 0) return;
    simExec(n);
}

void requestSimStepClock(uint64_t n) {
    if (n == 0) return;
    simExecClockPeriod(n);
}

void requestSimContinue() {
    clearPauseRequest();
    if (sim_state.state == SIM_END || sim_state.state == SIM_ABORT
        || sim_state.state == SIM_QUIT) {
        return;
    }
    if (s_wasPaused) {
        s_wasPaused = false;
        tui::g_eventFeed.push(getExecCount(),
            tui::EventType::RESUME,
            simExecInfo.pc, 0,
            "Simulation resumed from pause"
        );
    }
    sim_state.state = SIM_RUNNING;
}

void requestSimPause() {
    s_pauseRequested = true;
    s_wasPaused = true;
    tui::g_eventFeed.push(getExecCount(),
        tui::EventType::PAUSE,
        simExecInfo.pc, 0,
        "Pause requested"
    );
}

void requestSimReset(int cycles) {
    s_pauseRequested = false;
    s_wasPaused = false;
    simReset(cycles);
    tui::g_eventFeed.push(getExecCount(),
        tui::EventType::RESET,
        0, static_cast<word_t>(cycles),
        "Simulation reset"
    );
}

bool isPauseRequested() {
    return s_pauseRequested;
}

void clearPauseRequest() {
    s_pauseRequested = false;
}

} // namespace tui
