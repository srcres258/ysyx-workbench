#ifndef __TUI_CONTROL_HPP__
#define __TUI_CONTROL_HPP__ 1

#include <cstdint>

namespace tui {

/**
 * @brief Narrow control seam for the TUI frontend.
 *
 * These functions are the ONLY way the TUI layer may influence the
 * simulation loop.  Panels and frontend code MUST NOT touch Verilator
 * internals, simExecInfo, sim_halt, or sim_state directly.
 *
 * Conventions:
 *   - "step" APIs run exactly N units and then return (state → STOP if
 *     still RUNNING).  Zero is a no-op.
 *   - requestSimContinue() free-runs until halt, abort, or pause.
 *   - requestSimPause() sets a flag that the simulation loop checks
 *     after each instruction.  Safe to call from a signal handler.
 *   - requestSimReset() asserts the hardware reset for the given number
 *     of clock periods (default 15, matching the current simReset).
 */

/// Step the simulation by @p n instructions.  n=0 → no-op.
void requestSimStepInst(uint64_t n);

/// Step the simulation by @p n clock periods.  n=0 → no-op.
void requestSimStepClock(uint64_t n);

/// Free-run until halt, abort, or a pause request.
void requestSimContinue();

/// Request the simulation to pause at the next safe boundary.
void requestSimPause();

/// Assert hardware reset for @p cycles clock periods.
void requestSimReset(int cycles = 15);

/// True if a pause was requested and not yet acknowledged.
bool isPauseRequested();

/// Clear the pause flag (called by the execution loop after stopping).
void clearPauseRequest();

/// Reset pause and was-paused flags for lifecycle repeatability.
void resetPauseState();

} // namespace tui

#endif /* __TUI_CONTROL_HPP__ */
