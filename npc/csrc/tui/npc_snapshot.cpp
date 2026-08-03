#include <cstdio>
#include <cstring>
#include <tui/npc_snapshot.hpp>
#include <tui/tui_events.hpp>
#include <isa.hpp>
#include <macro-def.hpp>

namespace tui {

NpcSnapshot makeNpcSnapshot(const SnapshotInput &input) {
    NpcSnapshot snap {};

    snap.retiredPc   = input.retiredPc;
    snap.retiredInst = input.retiredInst;
    snap.nextPc      = input.nextPc;
    snap.procState   = input.procState;
    snap.simState    = input.simState;
    snap.haltPc      = input.haltPc;
    snap.simHalt     = input.simHalt;
    snap.callFrames  = input.callFrames;

    snap.execCount      = input.execCount;
    snap.execCountClock = input.execCountClock;
    snap.difftestActive = input.difftestActive;

    if (input.perfEnabled) {
        snap.perfCounters = input.perfCounters;
    }

    snap.numRecentEvents = input.eventFeed ?
        input.eventFeed->getRecentEvents(
            snap.recentEvents,
            NpcSnapshot::kMaxRecentEvents
        ) : 0;

    auto appendInstMark = [&snap](
        const char *name, addr_t pc,
        bool valid, bool hasPc
    ) {
        if (snap.numInstMarks >= NpcSnapshot::kMaxInstMarks)
            return;
        auto &mark = snap.instMarks[snap.numInstMarks++];
        std::snprintf(mark.name, sizeof(mark.name), "%s", name);
        mark.pc    = pc;
        mark.valid = valid;
        mark.hasPc = hasPc;
    };

    // IF  stage
    appendInstMark(
        "IF",  input.nextPc,
        input.stages[0].valid, input.stages[0].hasPc
    );
    // ID  stage — computed later from DPI, not passed upstream
    appendInstMark(
        "ID",  0,
        input.stages[1].valid, input.stages[1].hasPc
    );
    // EX  stage
    appendInstMark("EX",  input.stages[2].pc,
        input.stages[2].valid, input.stages[2].hasPc
    );
    // MEM stage
    appendInstMark(
        "MEM", input.stages[3].pc,
        input.stages[3].valid, input.stages[3].hasPc
    );
    // WB  stage
    appendInstMark(
        "WB",  input.stages[4].pc,
        input.stages[4].valid, input.stages[4].hasPc
    );

    return snap;
}

static const char *stateString(SimStateEnum s) {
    switch (s) {
        case SIM_RUNNING: return "RUNNING";
        case SIM_STOP:    return "STOP";
        case SIM_END:     return "END";
        case SIM_ABORT:   return "ABORT";
        case SIM_QUIT:    return "QUIT";
        default:          return "UNKNOWN";
    }
}

TuiFrameModel makeFrameModel(
    const NpcSnapshot &snap,
    const NpcSnapshot *prev
) {
    TuiFrameModel fm{};
    const auto &st = snap.procState;

    // ---- GPRs ----
    for (size_t i = 0; i < RISCV_GPR_NUM; i++) {
        auto &entry = fm.gprs[i];
        std::snprintf(entry.name, sizeof(entry.name), "x%zu", i);
        std::snprintf(
            entry.valueStr, sizeof(entry.valueStr),
            "0x%08x", st.gpr[i]
        );
        entry.value = st.gpr[i];
        entry.changed = prev && (st.gpr[i] != prev->procState.gpr[i]);
    }

    // ---- Defined CSRs ----
    static constexpr word_t csrIds[TuiFrameModel::kNumDefinedCSRs] = {
        CSR_MSTATUS, CSR_MTVEC, CSR_MEPC, CSR_MCAUSE,
        CSR_MTVAL, CSR_MVENDORID, CSR_MARCHID
    };
    static constexpr const char *csrNames[TuiFrameModel::kNumDefinedCSRs] = {
        "mstatus", "mtvec", "mepc", "mcause",
        "mtval", "mvendorid", "marchid"
    };
    for (size_t i = 0; i < TuiFrameModel::kNumDefinedCSRs; i++) {
        auto &entry = fm.csrs[i];
        word_t val = st.csr[csrIds[i]];
        std::snprintf(entry.name, sizeof(entry.name), "%s", csrNames[i]);
        std::snprintf(
            entry.valueStr, sizeof(entry.valueStr),
            "0x%08x", val
        );
        entry.value = val;
        entry.changed = prev && (val != prev->procState.csr[csrIds[i]]);
    }

    // ---- PC strings ----
    std::snprintf(fm.pcStr, sizeof(fm.pcStr), "0x%08x", snap.nextPc);
    std::snprintf(
        fm.retiredPcStr, sizeof(fm.retiredPcStr),
        "0x%08x", snap.retiredPc
    );
    std::snprintf(
        fm.instStr, sizeof(fm.instStr),
        "0x%08x", snap.retiredInst
    );

    fm.retiredPcRaw   = snap.retiredPc;
    fm.retiredInstRaw = snap.retiredInst;
    fm.pcRaw          = snap.nextPc;
    fm.spRaw          = st.gpr[2];

    if (snap.execCount > 0) {
        uint8_t instBytes[4];
        instBytes[0] = static_cast<uint8_t>(snap.retiredInst & 0xff);
        instBytes[1] = static_cast<uint8_t>((snap.retiredInst >> 8) & 0xff);
        instBytes[2] = static_cast<uint8_t>((snap.retiredInst >> 16) & 0xff);
        instBytes[3] = static_cast<uint8_t>((snap.retiredInst >> 24) & 0xff);
        disasm_disassemble(
            fm.disasmStr, sizeof(fm.disasmStr),
            snap.retiredPc, instBytes, 4
        );
    }

    // ---- Performance ----
    fm.execCount      = snap.execCount;
    fm.execCountClock = snap.execCountClock;
    fm.ipc = (snap.execCountClock > 0) ?
        static_cast<double>(snap.execCount) /
            static_cast<double>(snap.execCountClock) :
        0.0;

    // ---- Perf counter snapshot ----
    fm.perfValid = false;
    if (!snap.perfCounters.empty()) {
        for (
            size_t i = 0;
            i < TuiFrameModel::kNumPerfCounters && i < snap.perfCounters.size();
            i++
        ) {
            fm.perfValues[i] = snap.perfCounters[i].value;
        }

        uint64_t coreCycle   = fm.perfValues[perf::Idx::CORE_CYCLE];
        uint64_t coreInstret = fm.perfValues[perf::Idx::CORE_INSTRET];
        uint64_t coreStall   = fm.perfValues[perf::Idx::CORE_STALL_CYCLE];

        fm.perfIpc = (coreCycle > 0) ?
            static_cast<double>(coreInstret) / static_cast<double>(coreCycle) :
            0.0;
        fm.perfCpi = (coreInstret > 0) ?
            static_cast<double>(coreCycle) / static_cast<double>(coreInstret) :
            0.0;
        fm.perfStallPct = (coreCycle > 0) ?
            static_cast<double>(coreStall) /
                static_cast<double>(coreCycle) * 100.0 :
            0.0;
        fm.perfValid = true;
    }

    // ---- State strings ----
    std::snprintf(
        fm.stateStr,
        sizeof(fm.stateStr),
        "%s",
        stateString(snap.simState)
    );
    if (snap.simState == SIM_END || snap.simState == SIM_ABORT) {
        std::snprintf(
            fm.haltPcStr, sizeof(fm.haltPcStr),
            "0x%08x", snap.haltPc
        );
    } else {
        fm.haltPcStr[0] = '\0';
    }
    std::snprintf(
        fm.difftestStr,
        sizeof(fm.difftestStr),
        "DiffTest: %s",
        snap.difftestActive ? "active" : "inactive"
    );

    // ---- Recent events ----
    fm.numRecentEvents = snap.numRecentEvents;
    std::memcpy(
        fm.recentEvents, snap.recentEvents,
        snap.numRecentEvents * sizeof(Event)
    );

    fm.numInstMarks = snap.numInstMarks;
    std::memcpy(
        fm.instMarks, snap.instMarks,
        snap.numInstMarks * sizeof(NpcSnapshot::InstMark)
    );

    fm.callFrames = snap.callFrames;

    return fm;
}

std::vector<TraceEntry> drainTraceRingBuffer(
    RingBuffer *iringbuf,
    bool itraceEnabled,
    size_t maxLines
) {
    std::vector<TraceEntry> result;
    if (!itraceEnabled || !iringbuf)
        return result;
    if (iringbuf->availableData() == 0)
        return result;

    static std::string s_partial;
    s_partial += iringbuf->read(iringbuf->availableData());

    size_t pos = 0;
    size_t nl;
    while (
        (nl = s_partial.find('\n', pos)) != std::string::npos &&
        result.size() < maxLines
    ) {
        TraceEntry entry{};
        std::string view = s_partial.substr(pos, nl - pos);
        if (!view.empty()) {
            size_t len = std::min(view.size(), sizeof(entry.line) - 1);
            std::memcpy(entry.line, view.c_str(), len);
            entry.line[len] = '\0';
            result.push_back(entry);
        }
        pos = nl + 1;
    }
    s_partial.erase(0, pos);

    return result;
}

} // namespace tui
