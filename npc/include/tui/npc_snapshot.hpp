#ifndef __NPC_SNAPSHOT_HPP__
#define __NPC_SNAPSHOT_HPP__ 1

#include <cstdint>
#include <array>
#include <vector>
#include <common.hpp>
#include <processor.hpp>
#include <utils.hpp>
#include <tui/tui_events.hpp>

namespace tui {

/**
 * @brief Complete simulation snapshot at a single point in time.
 *
 * This is the SINGLE backend snapshot object that captures everything
 * the TUI frontend needs to render one frame.  All fields are populated
 * synchronously from one call to makeNpcSnapshot().
 */
struct NpcSnapshot {
    addr_t        retiredPc;
    word_t        retiredInst;
    addr_t        nextPc;
    ProcessorState procState;
    SimStateEnum  simState;
    addr_t        haltPc;
    bool          simHalt;
    uint64_t      execCount;
    uint64_t      execCountClock;
    bool          difftestActive;

    static constexpr size_t kMaxRecentEvents = 8;
    Event         recentEvents[kMaxRecentEvents];
    size_t        numRecentEvents;
};

/**
 * @brief Frontend-facing frame model derived from NpcSnapshot.
 *
 * Contains pre-formatted string data suitable for direct consumption
 * by TUI panels.  This is a pure data transfer object — no rendering,
 * no terminal logic, no layout.
 */
struct TuiFrameModel {
    // ---- Register entries (pre-formatted for display) ----
    struct RegEntry {
        char  name[16];
        char  valueStr[16];   // hex: "0xHHHHHHHH"
        word_t value;
        bool   changed;       // set by makeFrameModel when a prev snapshot is given
    };
    std::array<RegEntry, RISCV_GPR_NUM> gprs;

    // Only the 7 defined CSRs
    static constexpr size_t kNumDefinedCSRs = 7;
    RegEntry csrs[kNumDefinedCSRs];

    // ---- PC / instruction strings ----
    char pcStr[16];          // "0xHHHHHHHH"  (nextPc)
    char retiredPcStr[16];   // "0xHHHHHHHH"
    char instStr[16];        // hex instruction
    char disasmStr[64];      // disassembly (may be empty if disasm unavailable)

    // ---- Raw values for trace-panel fallback formatting ----
    word_t retiredPcRaw;     // retired PC as raw word_t (for byte formatting)
    word_t retiredInstRaw;   // retired instruction as raw word_t (for byte formatting)

    // ---- Performance ----
    uint64_t execCount;
    uint64_t execCountClock;
    double   ipc;            // computed: execCount / execCountClock (0 if no clocks)

    // ---- State strings ----
    char stateStr[32];       // "RUNNING" / "STOP" / "END" / "ABORT" / "QUIT"
    char haltPcStr[16];      // empty if not halted
    char difftestStr[32];    // e.g. "DiffTest: active"

    // ---- Recent event summary ----
    Event  recentEvents[NpcSnapshot::kMaxRecentEvents];
    size_t numRecentEvents;
};

/**
 * @brief A single formatted trace line for the TracePanel.
 *
 * Produced by drainTraceRingBuffer() from the itrace ring buffer.
 */
struct TraceEntry {
    char line[256];
};

/**
 * @brief Drain accumulated trace lines from the itrace ring buffer.
 *
 * Reads all currently available data from `sim_state.itrace_iringbuf`,
 * splits by newlines, and returns up to @p maxLines formatted entries.
 * Partial lines are accumulated internally and completed on subsequent
 * calls.
 *
 * Returns an empty vector when itrace is disabled or the ring buffer
 * is empty.
 *
 * @note This is a consuming read — call it once per frame.
 */
std::vector<TraceEntry> drainTraceRingBuffer(size_t maxLines);

/**
 * @brief Take a complete snapshot of the current simulation state.
 *
 * Must be called when the simulator is in a consistent state
 * (post-eval, DUT idle).
 */
NpcSnapshot makeNpcSnapshot();

/**
 * @brief Build a TuiFrameModel from an NpcSnapshot.
 *
 * @param snap  Current snapshot.
 * @param prev  Optional previous snapshot for register-change detection.
 *              Pass nullptr for the first frame.
 */
TuiFrameModel makeFrameModel(const NpcSnapshot &snap,
                             const NpcSnapshot *prev = nullptr);

} // namespace tui

#endif /* __NPC_SNAPSHOT_HPP__ */
