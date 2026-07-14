#include <tui/panel.hpp>
#include <tui/layout.hpp>
#include <tui/renderer.hpp>
#include <tui/npc_snapshot.hpp>
#include <tui/tui_config.hpp>
#include <tui/tui_events.hpp>
#include <device/io/mmio.hpp>
#include <utils.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <iostream>

namespace tui {

// ============================================================================
// PanelRegistry
// ============================================================================

PanelRegistry &PanelRegistry::instance() {
    static PanelRegistry reg;
    return reg;
}

bool PanelRegistry::registerPanel(std::unique_ptr<Panel> panel) {
    if (!panel) return false;
    const char *panelId = panel->id();
    if (m_panels.find(panelId) != m_panels.end()) {
        return false; // duplicate
    }
    m_panels[panelId] = std::move(panel);
    return true;
}

Panel *PanelRegistry::get(const std::string &id) {
    auto it = m_panels.find(id);
    return (it != m_panels.end()) ? it->second.get() : nullptr;
}

const Panel *PanelRegistry::get(const std::string &id) const {
    auto it = m_panels.find(id);
    return (it != m_panels.end()) ? it->second.get() : nullptr;
}

bool PanelRegistry::has(const std::string &id) const {
    return m_panels.find(id) != m_panels.end();
}

std::vector<std::string> PanelRegistry::allIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_panels.size());
    for (const auto &kv : m_panels) {
        ids.push_back(kv.first);
    }
    return ids;
}

std::string PanelRegistry::displayNameFor(const std::string &id) const {
    const Panel *p = get(id);
    return p ? std::string(p->displayName()) : id;
}

bool PanelRegistry::validateLayout(const LayoutTree &tree) const {
    auto ids = tree.panelIds();
    bool ok = true;
    for (const auto &id : ids) {
        if (!has(id)) {
            std::cerr << "[tui] layout error: unknown panel ID \"" << id << "\"" << std::endl;
            ok = false;
        }
    }
    return ok;
}

// ============================================================================
// Shared helper — panel border drawing
// ============================================================================

static void drawPanelBorder(Canvas &canvas, const Rect &rect,
                              const char *title, bool focused) {
    Style borderStyle = focused
        ? Style{kColourCyan,  kColourNone, true,  false, false}
        : Style{kColourBlue,  kColourNone, false, false, false};
    Style titleStyle = focused
        ? Style{kColourCyan,  kColourNone, true,  false, false}
        : Style{kColourWhite, kColourNone, false, false, false};

    primDrawBox(canvas, rect.row, rect.col, rect.h, rect.w,
                title, titleStyle, borderStyle);
}

static addr_t chooseInstAnchorPc(const TuiFrameModel &fm) {
    if (fm.pcStr[0] != '\0') {
        return static_cast<addr_t>(std::strtoul(fm.pcStr, nullptr, 0));
    }

    if (fm.retiredPcRaw != 0) {
        return fm.retiredPcRaw;
    }

    return 0;
}

static bool isReadableInstWord(addr_t addr) {
    for (size_t i = 0; i < 4; i++) {
        if (!device_io_mmio_isAddrValid(addr + static_cast<addr_t>(i))) {
            return false;
        }
    }
    return true;
}

static void formatHexAddr(char *buf, size_t size, addr_t addr) {
    std::snprintf(buf, size, "0x%08x", static_cast<uint32_t>(addr));
}

// ============================================================================
// ABI register names (RISC-V standard)
// ============================================================================

static const char *kAbiNames[] = {
    "zero", "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
    "s0",   "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
    "a6",   "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
    "s8",   "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"
};
static_assert(sizeof(kAbiNames) / sizeof(kAbiNames[0]) == RISCV_GPR_NUM,
              "ABI name array must cover all GPRs");

// ============================================================================
// CorePanel
// ============================================================================

class CorePanel : public Panel {
public:
    const char *id()          const override { return "core"; }
    const char *displayName() const override { return "Core"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " Core ", focused);

        uint16_t r = rect.row + 1;
        uint16_t c = rect.col + 1;
        uint16_t maxW = (rect.w > 2) ? (rect.w - 2) : 0;
        if (maxW == 0 || r >= rect.row + rect.h) return;

        // --- State badge ---
        ColourIndex stateColor = kColourGreen;
        if (std::strcmp(fm.stateStr, "STOP") == 0)  stateColor = kColourYellow;
        if (std::strcmp(fm.stateStr, "ABORT") == 0) stateColor = kColourRed;
        if (std::strcmp(fm.stateStr, "END") == 0)   stateColor = kColourCyan;
        if (std::strcmp(fm.stateStr, "QUIT") == 0)  stateColor = kColourMagenta;

        writeClipped(canvas, r, c, c, maxW, "State: ", styleFg(kColourWhite));
        writeClippedF(canvas, r, static_cast<uint16_t>(c + 7), c, maxW,
                      styleFgBold(stateColor), "[%s]", fm.stateStr);

        // Halt PC on same line if present
        if (fm.haltPcStr[0] && maxW > 50) {
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 25), c, maxW,
                          styleFg(kColourWhite), " @ %s", fm.haltPcStr);
        }
        r++;
        if (r >= rect.row + rect.h) return;

        // --- PC line ---
        writeClippedF(canvas, r, c, c, maxW, styleFg(kColourWhite),
                      "PC: %s", fm.pcStr);

        // Retired PC on same line if space
        if (maxW > 45) {
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 22), c, maxW,
                          styleFg(kColourWhite), " Retired: %s", fm.retiredPcStr);
        }
        r++;
        if (r >= rect.row + rect.h) return;

        // --- Instruction line ---
        if (fm.instStr[0]) {
            writeClippedF(canvas, r, c, c, maxW, styleFg(kColourWhite),
                          "Inst: %s", fm.instStr);
            if (fm.disasmStr[0] && maxW > 35) {
                writeClippedF(canvas, r, static_cast<uint16_t>(c + 16), c, maxW,
                              styleFg(kColourCyan), " %s", fm.disasmStr);
            }
            r++;
            if (r >= rect.row + rect.h) return;
        }

        // --- Performance summary line ---
        writeClippedF(canvas, r, c, c, maxW, styleFg(kColourWhite),
                      "Cycles: %lu   Exec: %lu   IPC: %.4f",
                      fm.execCountClock, fm.execCount, fm.ipc);
        r++;
        if (r >= rect.row + rect.h) return;

        // --- DiffTest status ---
        if (fm.difftestStr[0]) {
            ColourIndex dtColor = (std::strstr(fm.difftestStr, "active") != nullptr)
                ? kColourGreen : kColourBlue;
            writeClippedF(canvas, r, c, c, maxW, styleFg(dtColor), "%s", fm.difftestStr);
        }
    }
};

// ============================================================================
// RegsPanel — full 32-register display with ABI/xN toggle and highlighting
// ============================================================================

class RegsPanel : public Panel {
public:
    const char *id()          const override { return "regs"; }
    const char *displayName() const override { return "Registers"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " Registers ", focused);

        uint16_t r = rect.row + 1;
        uint16_t c = rect.col + 1;
        uint16_t innerW = (rect.w > 2) ? (rect.w - 2) : 0;
        uint16_t innerH = (rect.h > 2) ? (rect.h - 2) : 0;
        if (innerW < 10 || innerH == 0) return;

        // Determine columns: 8 cols if wide enough (≥80 inner), else 4
        int nCols = (innerW >= 80) ? 8 : 4;
        uint16_t cellW = innerW / static_cast<uint16_t>(nCols);
        if (cellW < 9) {
            nCols = std::max<int>(1, static_cast<int>(innerW / 9));
            cellW = innerW / static_cast<uint16_t>(nCols);
        }
        int nRows = (RISCV_GPR_NUM + nCols - 1) / nCols;

        bool useAbi  = g_tuiConfig.regs.abi_names;
        bool doHl    = g_tuiConfig.regs.highlight_changed;

        // Detect changes via local cache
        for (size_t i = 0; i < RISCV_GPR_NUM; i++) {
            if (m_initialized && doHl && m_prevGprs[i] != fm.gprs[i].value) {
                m_changed[i] = true;
            }
            m_prevGprs[i] = fm.gprs[i].value;
        }
        m_initialized = true;

        // Render each register
        for (int row = 0; row < nRows; row++) {
            if (r + static_cast<uint16_t>(row) >= rect.row + rect.h) break;

            for (int col = 0; col < nCols; col++) {
                int idx = row + col * nRows;
                if (idx >= RISCV_GPR_NUM) break;

                uint16_t xpos = c + static_cast<uint16_t>(col) * cellW;

                // Name: ABI or xN
                const char *name = useAbi ? kAbiNames[idx] : fm.gprs[idx].name;

                // Style: highlight if changed
                bool changed = doHl && m_changed[idx];
                Style nameStyle = changed
                    ? styleFgBold(kColourGreen)
                    : styleFgBold(kColourYellow);
                Style valStyle = changed
                    ? styleFgBold(kColourGreen)
                    : styleFg(kColourWhite);

                // Format: "name: value" or compact "name:value"
                char cellBuf[32];
                std::snprintf(cellBuf, sizeof(cellBuf),
                              "%s:%s", name, fm.gprs[idx].valueStr);
                writeClipped(canvas, r + static_cast<uint16_t>(row), xpos,
                             xpos, cellW, cellBuf, valStyle);

                // Overwrite the name portion with nameStyle
                // (simple approach: draw name first, then value)
                size_t nameLen = std::strlen(name);
                if (nameLen + 1 < cellW) {
                    // Write name with highlight color
                    writeClipped(canvas, r + static_cast<uint16_t>(row), xpos,
                                 xpos, cellW, name, nameStyle);
                    // Write colon
                    Style colonStyle = changed
                        ? styleFgBold(kColourGreen) : styleFg(kColourWhite);
                    writeClipped(canvas, r + static_cast<uint16_t>(row),
                                 xpos + static_cast<uint16_t>(nameLen),
                                 xpos, cellW, ":", colonStyle);
                }
            }
        }

        // Clear highlight state — changes show for one frame
        for (size_t i = 0; i < RISCV_GPR_NUM; i++) {
            m_changed[i] = false;
        }
    }

private:
    word_t m_prevGprs[RISCV_GPR_NUM] = {};
    bool   m_changed[RISCV_GPR_NUM]   = {};
    bool   m_initialized              = false;
};

// ============================================================================
// CsrPanel — 7 defined CSRs with change highlighting
// ============================================================================

class CsrPanel : public Panel {
public:
    const char *id()          const override { return "csr"; }
    const char *displayName() const override { return "CSR"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " CSR ", focused);

        uint16_t r = rect.row + 1;
        uint16_t c = rect.col + 1;
        uint16_t innerW = (rect.w > 2) ? (rect.w - 2) : 0;
        if (innerW < 10) return;

        // Detect changes via local cache
        for (size_t i = 0; i < TuiFrameModel::kNumDefinedCSRs; i++) {
            if (m_initialized && m_prevCsrs[i] != fm.csrs[i].value) {
                m_changed[i] = true;
            }
            m_prevCsrs[i] = fm.csrs[i].value;
        }
        m_initialized = true;

        for (size_t i = 0; i < TuiFrameModel::kNumDefinedCSRs; i++) {
            if (r >= rect.row + rect.h - 1) break;

            bool changed = m_changed[i];
            Style nameStyle = changed
                ? styleFgBold(kColourYellow)
                : styleFg(kColourWhite);
            Style valStyle = changed
                ? styleFgBold(kColourYellow)
                : styleFg(kColourWhite);

            // Name column
            writeClippedF(canvas, r, c, c, innerW, nameStyle,
                          "%-10s", fm.csrs[i].name);

            // Value
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 11), c, innerW,
                          valStyle, "%s", fm.csrs[i].valueStr);

            // Binary bit breakdown for mstatus (index 0) if space permits
            if (i == 0 && innerW >= 50) {
                // Show 32-bit binary view for mstatus
                writeClippedF(canvas, r, static_cast<uint16_t>(c + 24), c, innerW,
                              styleFg(kColourBlue), " [");
                // Show top bits compactly
                word_t val = fm.csrs[i].value;
                char bitBuf[40];
                int pos = 0;
                for (int b = 31; b >= 0 && pos < 38; b--) {
                    if (b != 31 && (b + 1) % 8 == 0 && pos > 0) {
                        bitBuf[pos++] = ' ';
                    }
                    bitBuf[pos++] = (val & (1u << b)) ? '1' : '0';
                    if (pos >= 38) break;
                }
                bitBuf[pos] = '\0';
                writeClippedF(canvas, r, static_cast<uint16_t>(c + 27), c, innerW,
                              styleFg(kColourBlue), "%s]", bitBuf);
            }
            r++;
        }

        // Clear highlight state
        for (size_t i = 0; i < TuiFrameModel::kNumDefinedCSRs; i++) {
            m_changed[i] = false;
        }
    }

private:
    word_t m_prevCsrs[TuiFrameModel::kNumDefinedCSRs] = {};
    bool   m_changed[TuiFrameModel::kNumDefinedCSRs]   = {};
    bool   m_initialized = false;
};

// ============================================================================
// PerfPanel — performance counters grouped by domain (from PerfMonitor snapshot)
// ============================================================================

class PerfPanel : public Panel {
public:
    const char *id()          const override { return "perf"; }
    const char *displayName() const override { return "Performance"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " Performance ", focused);

        uint16_t r = rect.row + 1;
        const uint16_t c = rect.col + 1;
        const uint16_t innerW = (rect.w > 2) ? (rect.w - 2) : 0;
        if (innerW < 8 || r >= rect.row + rect.h) return;

        const uint16_t endRow = rect.row + rect.h;

        // ── No perf data ──
        if (!fm.perfValid) {
            if (fm.execCountClock == 0) {
                writeClippedF(canvas, r, c, c, innerW, styleFg(kColourBlue),
                              "Waiting for simulation data...");
            } else {
                writeClippedF(canvas, r, c, c, innerW, styleFg(kColourBlue),
                              "Perf counters: inactive");
            }
            return;
        }

        const auto &pv = fm.perfValues;

        // ── Core counters + derived metrics ──
        {
            uint64_t cyc = pv[perf::Idx::CORE_CYCLE];
            uint64_t ins = pv[perf::Idx::CORE_INSTRET];
            uint64_t busy = pv[perf::Idx::CORE_BUSY_CYCLE];
            uint64_t stall= pv[perf::Idx::CORE_STALL_CYCLE];
            writeClippedF(canvas, r, c, c, innerW, styleFg(kColourGreen),
                          "Core: c=%lu i=%lu b=%lu s=%lu", cyc, ins, busy, stall);
            r++; if (r >= endRow) return;
        }

        {
            const char *cpiFmt = (fm.perfCpi > 0.0) ? "%.2f" : "-";
            const char *ipcFmt = (fm.perfIpc > 0.0) ? "%.3f" : "-";
            const char *stFmt  = (fm.perfStallPct > 0.0) ? "%.1f%%" : "-";
            writeClippedF(canvas, r, c, c, innerW, styleFg(kColourYellow),
                          "CPI=");
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 4), c, innerW,
                          styleFg(kColourYellow), cpiFmt, fm.perfCpi);
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 11), c, innerW,
                          styleFg(kColourYellow), " IPC=");
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 16), c, innerW,
                          styleFg(kColourYellow), ipcFmt, fm.perfIpc);
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 23), c, innerW,
                          styleFg(kColourYellow), " St%=");
            writeClippedF(canvas, r, static_cast<uint16_t>(c + 28), c, innerW,
                          styleFg(kColourYellow), stFmt, fm.perfStallPct);
            r++; if (r >= endRow) return;
        }

        // ── Inst Class ──
        {
            writeClippedF(canvas, r, c, c, innerW, styleFg(kColourWhite),
                          "ICls: al=%lu ld=%lu st=%lu br=%lu",
                          pv[perf::Idx::INST_CLASS_ALU_COUNT],
                          pv[perf::Idx::INST_CLASS_LOAD_COUNT],
                          pv[perf::Idx::INST_CLASS_STORE_COUNT],
                          pv[perf::Idx::INST_CLASS_BRANCH_COUNT]);
            r++; if (r >= endRow) return;
            writeClippedF(canvas, r, c, c, innerW, styleFg(kColourWhite),
                          "      jl=%lu jr=%lu cs=%lu md=%lu",
                          pv[perf::Idx::INST_CLASS_JAL_COUNT],
                          pv[perf::Idx::INST_CLASS_JALR_COUNT],
                          pv[perf::Idx::INST_CLASS_CSR_COUNT],
                          pv[perf::Idx::INST_CLASS_MULDIV_COUNT]);
            r++; if (r >= endRow) return;
        }

        // ── State ──
        {
            writeClippedF(canvas, r, c, c, innerW, styleFg(kColourCyan),
                          "Pipe: IF=%lu DE=%lu EX=%lu ME=%lu WB=%lu",
                          pv[perf::Idx::STATE_FETCH_CYCLE],
                          pv[perf::Idx::STATE_DECODE_CYCLE],
                          pv[perf::Idx::STATE_EXECUTE_CYCLE],
                          pv[perf::Idx::STATE_MEMORY_CYCLE],
                          pv[perf::Idx::STATE_WRITEBACK_CYCLE]);
            r++; if (r >= endRow) return;
        }

        // ── Stall + Mem + Trap (condensed into remaining rows) ──
        {
            writeClippedF(canvas, r, c, c, innerW, styleFg(kColourMagenta),
                          "Stall: iw=%lu mw=%lu bk=%lu sm=%lu mb=%lu",
                          pv[perf::Idx::STALL_IFETCH_WAIT_RESP_CYCLE],
                          pv[perf::Idx::STALL_MEM_WAIT_RESP_CYCLE],
                          pv[perf::Idx::STALL_MEM_REQ_BLOCKED_CYCLE],
                          pv[perf::Idx::STALL_STRUCT_SHARED_MEM_CYCLE],
                          pv[perf::Idx::STALL_MULDIV_BUSY_CYCLE]);
            r++; if (r >= endRow) return;
        }

        {
            writeClippedF(canvas, r, c, c, innerW, styleFg(kColourWhite),
                          "Mem: ld=%lu st=%lu mm=%lu  |  Trap: %lu",
                          pv[perf::Idx::MEM_LOAD_REQ_COUNT],
                          pv[perf::Idx::MEM_STORE_REQ_COUNT],
                          pv[perf::Idx::MEM_MMIO_REQ_COUNT],
                          pv[perf::Idx::TRAP_EXCEPTION_COUNT]);
        }
    }
};

// ============================================================================
// InstPanel — centered disassembly window with active-stage arrows
// ============================================================================

class InstPanel : public Panel {
public:
    const char *id()          const override { return "inst"; }
    const char *displayName() const override { return "Inst"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " Inst ", focused);

        uint16_t r = rect.row + 1;
        uint16_t c = rect.col + 1;
        uint16_t innerH = (rect.h > 2) ? (rect.h - 2) : 0;
        uint16_t innerW = (rect.w > 2) ? (rect.w - 2) : 0;
        if (innerH == 0 || innerW < 10) return;

        addr_t anchorPc = chooseInstAnchorPc(fm);
        anchorPc &= ~static_cast<addr_t>(0x3);

        // TODO(pipeline): re-enable in-flight instruction arrows once NPC becomes pipelined.
        // For the current multi-cycle core, the current PC line is the only one we mark.

        size_t centerOffset = innerH / 2;
        addr_t startPc = anchorPc;
        if (centerOffset > 0) {
            addr_t back = static_cast<addr_t>(centerOffset) * 4;
            startPc = (anchorPc >= back) ? (anchorPc - back) : 0;
        }

        for (size_t i = 0; i < innerH; i++) {
            addr_t linePc = startPc + static_cast<addr_t>(i) * 4;
            bool highlighted = (linePc == anchorPc);

            char disasm[64] = {0};
            uint8_t bytes[4] = {0, 0, 0, 0};
            bool readable = isReadableInstWord(linePc);
            if (readable) {
                word_t inst = device_io_mmio_read(linePc, 4);
                bytes[0] = static_cast<uint8_t>(inst & 0xff);
                bytes[1] = static_cast<uint8_t>((inst >> 8) & 0xff);
                bytes[2] = static_cast<uint8_t>((inst >> 16) & 0xff);
                bytes[3] = static_cast<uint8_t>((inst >> 24) & 0xff);
                if (!disasm_tryDisassemble(disasm, sizeof(disasm), linePc, bytes, 4)) {
                    std::snprintf(disasm, sizeof(disasm), "unavailable");
                }
            } else {
                std::snprintf(disasm, sizeof(disasm), "invalid");
            }

            Style lineStyle = highlighted ? styleFgBold(kColourGreen)
                                          : styleFg(kColourWhite);
            const char *arrow = highlighted ? "-> " : "   ";

            if (readable) {
                writeClippedF(canvas, static_cast<uint16_t>(r + i), c, c, innerW,
                              lineStyle,
                              "%s0x%08x: %02x %02x %02x %02x      %-24s",
                              arrow, linePc,
                              bytes[3], bytes[2], bytes[1], bytes[0], disasm);
            } else {
                writeClippedF(canvas, static_cast<uint16_t>(r + i), c, c, innerW,
                              styleFg(kColourBlue),
                              "%s0x%08x: ?? ?? ?? ??      %s",
                              arrow, linePc, disasm);
            }
        }
    }
};

// ============================================================================
// FuncPanel — active call stack (top of stack shown first)
// ============================================================================

class FuncPanel : public Panel {
public:
    const char *id()          const override { return "func"; }
    const char *displayName() const override { return "Func"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " Func ", focused);

        uint16_t r = rect.row + 1;
        uint16_t c = rect.col + 1;
        uint16_t innerH = (rect.h > 2) ? (rect.h - 2) : 0;
        uint16_t innerW = (rect.w > 2) ? (rect.w - 2) : 0;
        if (innerH == 0 || innerW < 16) return;

        if (fm.callFrames.empty()) {
            canvas.write(r, c, "(no call frames yet)", styleFg(kColourBlue));
            return;
        }

        {
            char spBuf[32];
            std::snprintf(spBuf, sizeof(spBuf), "current_sp=0x%08x  depth=%zu",
                          fm.spRaw, fm.callFrames.size());
            writeClipped(canvas, r, c, c, innerW, spBuf, styleFg(kColourCyan));
            r++;
        }

        size_t rows = std::min<size_t>(innerH, fm.callFrames.size());

        for (size_t i = 0; i < rows; i++) {
            const auto &frame = fm.callFrames[i];

            // Stack-top is the currently executing function by definition;
            // retAddr lives in the caller's code, not a function-end boundary.
            bool highlighted = (i == 0);

            char funcAddrBuf[16];
            char retAddrBuf[16];
            char callerSpBuf[16];
            formatHexAddr(funcAddrBuf, sizeof(funcAddrBuf), frame.funcAddr);
            formatHexAddr(retAddrBuf, sizeof(retAddrBuf), frame.retAddr);
            formatHexAddr(callerSpBuf, sizeof(callerSpBuf), frame.callerSp);

            Style lineStyle = highlighted ? styleFgBold(kColourGreen)
                                          : styleFg(kColourWhite);
            const char *arrow = highlighted ? "-> " : "   ";

            writeClippedF(canvas, static_cast<uint16_t>(r + i), c, c, innerW,
                          lineStyle, "%s%s@%s (ret=%s, caller_sp=%s)",
                          arrow, frame.funcName.c_str(), funcAddrBuf,
                          retAddrBuf, callerSpBuf);
        }
    }
};

// ============================================================================
// TracePanel — live trace display with ring‑buffer drain and fallback
// ============================================================================

class TracePanel : public Panel {
public:
    const char *id()          const override { return "trace"; }
    const char *displayName() const override { return "Trace"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " Trace ", focused);

        uint16_t r = rect.row + 1;
        uint16_t c = rect.col + 1;
        uint16_t innerH = (rect.h > 2) ? (rect.h - 2) : 0;
        uint16_t innerW = (rect.w > 2) ? (rect.w - 2) : 0;
        if (innerH == 0 || innerW < 10) return;

        size_t bufMax = maxLines();

        // ── Drain ring buffer (itrace enabled) ──
        if (sim_config.config_itrace) {
            auto entries = drainTraceRingBuffer(bufMax);
            for (auto &e : entries) {
                if (e.line[0]) {
                    m_lines.push_back(e.line);
                }
            }
        }

        // ── Fallback: record from snapshot (itrace disabled) ──
        if (!sim_config.config_itrace && fm.execCount > m_lastExecCount) {
            m_lastExecCount = fm.execCount;

            char buf[256];
            uint8_t const *inst = reinterpret_cast<uint8_t const *>(&fm.retiredInstRaw);
            if (fm.disasmStr[0]) {
                std::snprintf(buf, sizeof(buf),
                    "0x%08x: %02x %02x %02x %02x      %s",
                    fm.retiredPcRaw, inst[3], inst[2], inst[1], inst[0],
                    fm.disasmStr);
            } else {
                std::snprintf(buf, sizeof(buf),
                    "0x%08x: %02x %02x %02x %02x",
                    fm.retiredPcRaw, inst[3], inst[2], inst[1], inst[0]);
            }
            m_lines.push_back(buf);
        }

        // ── Evict oldest when over capacity ──
        while (m_lines.size() > bufMax) {
            m_lines.pop_front();
        }

        size_t totalLines = m_lines.size();
        if (totalLines == 0) {
            if (r < rect.row + rect.h - 1) {
                canvas.write(r, c, "(no trace data yet)", styleFg(kColourBlue));
            }
            return;
        }

        // ── Calculate viewport ──
        size_t visibleLines = innerH;
        if (visibleLines > totalLines) visibleLines = totalLines;

        size_t startIdx;
        if (m_followTail) {
            startIdx = (totalLines > visibleLines)
                ? (totalLines - visibleLines) : 0;
        } else {
            if (m_scrollOffset + visibleLines > totalLines) {
                m_scrollOffset = (totalLines > visibleLines)
                    ? (totalLines - visibleLines) : 0;
            }
            startIdx = totalLines - visibleLines - m_scrollOffset;
        }

        // ── Render visible lines ──
        for (size_t i = 0; i < visibleLines; i++) {
            if (r + static_cast<uint16_t>(i) >= rect.row + rect.h - 1) break;

            size_t idx = startIdx + i;
            if (idx >= totalLines) break;

            const std::string &line = m_lines[idx];
            if (line.size() > static_cast<size_t>(innerW)) {
                std::string trunc = line.substr(0, innerW);
                canvas.writeStr(r + static_cast<uint16_t>(i), c,
                                trunc, styleFg(kColourWhite));
            } else {
                canvas.writeStr(r + static_cast<uint16_t>(i), c,
                                line, styleFg(kColourWhite));
            }
        }

        // ── Scroll indicator when not following tail ──
        if (!m_followTail && totalLines > visibleLines && m_scrollOffset > 0) {
            uint16_t indicatorR = r;
            if (innerW > 6) {
                canvas.writeF(indicatorR, static_cast<uint16_t>(c + innerW - 6),
                              styleFgBold(kColourYellow), "[up]");
            }
        }
        if (m_followTail && totalLines > visibleLines) {
            uint16_t indicatorR = r;
            if (innerW > 8) {
                canvas.writeF(indicatorR, static_cast<uint16_t>(c + innerW - 8),
                              styleFgBold(kColourGreen), "[tail]");
            }
        }
    }

private:
    std::deque<std::string> m_lines;
    uint64_t m_lastExecCount = 0;

    bool   m_followTail   = true;
    size_t m_scrollOffset = 0;

    static size_t maxLines() {
        size_t n = static_cast<size_t>(g_tuiConfig.trace.buffer_size);
        return (n > 0) ? n : 1024;
    }
};

// ============================================================================
// EventsPanel — live event feed with follow‑tail / scrollback
// ============================================================================

class EventsPanel : public Panel {
public:
    const char *id()          const override { return "events"; }
    const char *displayName() const override { return "Events"; }

    void render(Canvas &canvas, const Rect &rect,
                const TuiFrameModel &fm, bool focused) override {
        drawPanelBorder(canvas, rect, " Events ", focused);

        uint16_t r = rect.row + 1;
        uint16_t c = rect.col + 1;
        uint16_t innerH = (rect.h > 2) ? (rect.h - 2) : 0;
        uint16_t innerW = (rect.w > 2) ? (rect.w - 2) : 0;
        if (innerH == 0 || innerW < 10) return;

        // ── Poll event feed for new entries ──
        size_t feedSize = g_eventFeed.size();
        if (feedSize > m_lastSeenCount) {
            size_t toRead = std::min(feedSize, static_cast<size_t>(128));
            Event fresh[128];
            size_t nFresh = g_eventFeed.getRecentEvents(fresh, toRead);

            // Rebuild local buffer chronologically (oldest first)
            m_events.clear();
            for (size_t i = nFresh; i > 0; i--) {
                m_events.push_back(fresh[i - 1]);
            }
            m_lastSeenCount = feedSize;
        }

        size_t maxEv = maxEvents();
        while (m_events.size() > maxEv) {
            m_events.pop_front();
        }

        size_t totalEvents = m_events.size();
        if (totalEvents == 0) {
            if (r < rect.row + rect.h - 1) {
                canvas.write(r, c, "(no events)", styleFg(kColourBlue));
            }
            return;
        }

        // ── Calculate viewport ──
        size_t visibleLines = innerH;
        if (visibleLines > totalEvents) visibleLines = totalEvents;

        size_t startIdx;
        if (m_followTail) {
            startIdx = (totalEvents > visibleLines)
                ? (totalEvents - visibleLines) : 0;
        } else {
            if (m_scrollOffset + visibleLines > totalEvents) {
                m_scrollOffset = (totalEvents > visibleLines)
                    ? (totalEvents - visibleLines) : 0;
            }
            startIdx = totalEvents - visibleLines - m_scrollOffset;
        }

        // ── Render events ──
        for (size_t i = 0; i < visibleLines; i++) {
            if (r + static_cast<uint16_t>(i) >= rect.row + rect.h - 1) break;

            size_t idx = startIdx + i;
            if (idx >= totalEvents) break;

            const Event &ev = m_events[idx];

            ColourIndex evColor = kColourWhite;
            const char *prefix = "[*]";
            switch (ev.type) {
                case EventType::HALT:               prefix = "[H]"; evColor = kColourRed;    break;
                case EventType::TRAP_GOOD:          prefix = "[T]"; evColor = kColourGreen;  break;
                case EventType::TRAP_BAD:           prefix = "[!]"; evColor = kColourRed;    break;
                case EventType::ABORT:              prefix = "[A]"; evColor = kColourRed;    break;
                case EventType::WATCHPOINT:         prefix = "[W]"; evColor = kColourYellow; break;
                case EventType::DIFFTEST_MISMATCH:  prefix = "[D]"; evColor = kColourRed;    break;
                case EventType::DIFFTEST_ACTIVATE:  prefix = "[D]"; evColor = kColourCyan;   break;
                case EventType::DIFFTEST_WARNING:   prefix = "[W]"; evColor = kColourYellow; break;
                case EventType::PAUSE:              prefix = "[P]"; evColor = kColourYellow; break;
                case EventType::RESUME:             prefix = "[R]"; evColor = kColourGreen;  break;
                case EventType::RESET:              prefix = "[R]"; evColor = kColourCyan;   break;
                case EventType::CONFIG:             prefix = "[C]"; evColor = kColourBlue;   break;
                default: break;
            }
            canvas.write(r + static_cast<uint16_t>(i), c, prefix,
                         styleFgBold(evColor));

            size_t descW = (innerW > 4) ? (static_cast<size_t>(innerW) - 4) : 0;
            if (descW > 0) {
                char desc[128];
                std::snprintf(desc, sizeof(desc), "%.*s",
                              static_cast<int>(std::min(descW, size_t(100))),
                              ev.description);
                canvas.write(r + static_cast<uint16_t>(i),
                             static_cast<uint16_t>(c + 4), desc,
                             styleFg(kColourWhite));
            }
        }

        // ── Scroll indicator ──
        if (!m_followTail && totalEvents > visibleLines && m_scrollOffset > 0) {
            uint16_t indicatorR = r;
            if (innerW > 6) {
                canvas.writeF(indicatorR, static_cast<uint16_t>(c + innerW - 6),
                              styleFgBold(kColourYellow), "[up]");
            }
        }
        if (m_followTail && totalEvents > visibleLines) {
            uint16_t indicatorR = r;
            if (innerW > 8) {
                canvas.writeF(indicatorR, static_cast<uint16_t>(c + innerW - 8),
                              styleFgBold(kColourGreen), "[tail]");
            }
        }
    }

private:
    std::deque<Event> m_events;
    size_t m_lastSeenCount = 0;

    bool   m_followTail   = true;
    size_t m_scrollOffset = 0;

    static size_t maxEvents() { return 1024; }
};

// ============================================================================
// registerBuiltins
// ============================================================================

void PanelRegistry::registerBuiltins() {
    registerPanel(std::make_unique<CorePanel>());
    registerPanel(std::make_unique<RegsPanel>());
    registerPanel(std::make_unique<CsrPanel>());
    registerPanel(std::make_unique<InstPanel>());
    registerPanel(std::make_unique<FuncPanel>());
    registerPanel(std::make_unique<TracePanel>());
    registerPanel(std::make_unique<EventsPanel>());
    registerPanel(std::make_unique<PerfPanel>());
}

} // namespace tui
