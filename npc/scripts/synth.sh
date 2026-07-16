#!/usr/bin/env bash
# ============================================================================
# NPC Synthesis Wrapper — orchestrates yosys-sta for ysyx_25070190
# ============================================================================
# Invoked by:  make -C npc synth
#
# Required env vars (set by npc/Makefile):
#   NPC_HOME         — absolute path to npc/
#   YOSYS_STA_HOME   — absolute path to yosys-sta/
#   DESIGN           — top module name            (default: ysyx_25070190)
#   CLK_FREQ_MHZ     — target clock in MHz        (default: 100)
#   CLK_PORT_NAME    — clock port name in RTL     (default: clock)
#   OUTPUT_DIR       — where final reports land   (default: npc/build/synth)
#   SDC_FILE         — SDC constraint file        (default: npc/constr/synth.sdc)
#   SYNTH_SEARCH     — enable binary freq search  (default: off)
#   SYNTH_LOW_MHZ    — search lower bound         (default: 1)
#   SYNTH_HIGH_MHZ   — search upper bound         (default: 500)
#   YOSYS_STA_AUTO_INIT — bootstrap yosys-sta via `make init` if missing
#                         (default: on)
#
# Exit codes:
#   0 — synthesis completed successfully
#   1 — pre-flight check failed (missing tool, missing file, bad config)
#   2 — yosys synthesis failed
#   3 — iEDA STA failed
# ============================================================================
set -euo pipefail

# ── helpers ────────────────────────────────────────────────────────
die()    { echo "[Synth] ERROR: $*" >&2; exit 1; }
warn()   { echo "[Synth] WARNING: $*" >&2; }
info()   { echo "[Synth] $*"; }

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Required command '$1' not found in PATH"
}

require_file() {
  [ -f "$1" ] || die "Required file not found: $1"
}

require_dir() {
  [ -d "$1" ] || die "Required directory not found: $1"
}

# ── defaults ───────────────────────────────────────────────────────
NPC_HOME="${NPC_HOME:-}"
YOSYS_STA_HOME="${YOSYS_STA_HOME:-}"
DESIGN="${DESIGN:-ysyx_25070190}"
CLK_FREQ_MHZ="${CLK_FREQ_MHZ:-100}"
CLK_PORT_NAME="${CLK_PORT_NAME:-clock}"
OUTPUT_DIR="${OUTPUT_DIR:-${NPC_HOME}/build/synth}"
SDC_FILE="${SDC_FILE:-${NPC_HOME}/constr/synth.sdc}"
RTL_GEN_DIR="${NPC_HOME}/vsrc/generated"
STUB_DIR="${NPC_HOME}/vsrc/synth"
SCRIPT_DIR="${NPC_HOME}/scripts"

# Frequency search knobs
SYNTH_SEARCH="${SYNTH_SEARCH:-off}"
SYNTH_LOW_MHZ="${SYNTH_LOW_MHZ:-1}"
SYNTH_HIGH_MHZ="${SYNTH_HIGH_MHZ:-500}"
YOSYS_STA_AUTO_INIT="${YOSYS_STA_AUTO_INIT:-on}"

is_truthy() {
  case "$1" in
    1|true|TRUE|on|ON|yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

bootstrap_yosys_sta() {
  local need_init=0
  [ -x "${YOSYS_STA_HOME}/bin/iEDA" ] || need_init=1
  [ -d "${YOSYS_STA_HOME}/pdk/icsprout55" ] || need_init=1

  if [ "${need_init}" -eq 0 ]; then
    return 0
  fi

  if ! is_truthy "${YOSYS_STA_AUTO_INIT}"; then
    die "yosys-sta is not initialized (missing bin/iEDA and/or pdk/icsprout55). Run 'make -C ${YOSYS_STA_HOME} init' first, or re-run with YOSYS_STA_AUTO_INIT=on to bootstrap automatically"
  fi

  info "Bootstrapping yosys-sta (missing iEDA and/or icsprout55 PDK)..."
  require_cmd wget
  require_cmd git
  if ! make -C "${YOSYS_STA_HOME}" init; then
    die "yosys-sta bootstrap failed. Run 'make -C ${YOSYS_STA_HOME} init' manually to inspect the error"
  fi

  [ -x "${YOSYS_STA_HOME}/bin/iEDA" ] || die "yosys-sta bootstrap did not produce ${YOSYS_STA_HOME}/bin/iEDA"
  [ -d "${YOSYS_STA_HOME}/pdk/icsprout55" ] || die "yosys-sta bootstrap did not produce ${YOSYS_STA_HOME}/pdk/icsprout55"
}

# ── pre-flight validation ──────────────────────────────────────────
info "=== NPC Synthesis: ${DESIGN} @ ${CLK_FREQ_MHZ}MHz (clock port: ${CLK_PORT_NAME}) ==="

[ -n "${NPC_HOME}" ]       || die "NPC_HOME is not set"
[ -n "${YOSYS_STA_HOME}" ] || die "YOSYS_STA_HOME is not set"
require_dir "${NPC_HOME}"
require_dir "${YOSYS_STA_HOME}"
require_dir "${RTL_GEN_DIR}"
require_dir "${STUB_DIR}"
require_file "${SDC_FILE}"

# Validate toolchain
require_cmd yosys
require_cmd make
bootstrap_yosys_sta

# Validate yosys version (need >= 0.48)
YOSYS_VER=$(yosys -V 2>&1 | grep -oP '[\d]+\.[\d]+' | head -1 || echo "0.0")
YOSYS_MAJOR=$(echo "${YOSYS_VER}" | cut -d. -f1)
YOSYS_MINOR=$(echo "${YOSYS_VER}" | cut -d. -f2)
if [ "${YOSYS_MAJOR}" -eq 0 ] && [ "${YOSYS_MINOR}" -lt 48 ]; then
  die "yosys version ${YOSYS_VER} is too old (need >= 0.48)"
fi
info "yosys version: ${YOSYS_VER}"

# Validate PDK availability
require_dir "${YOSYS_STA_HOME}/pdk/icsprout55" || \
  die "icsprout55 PDK not found at ${YOSYS_STA_HOME}/pdk/icsprout55"

# ── collect RTL sources ────────────────────────────────────────────
info "Collecting RTL sources from ${RTL_GEN_DIR}..."

RTL_FILES=()
while IFS= read -r -d '' f; do
  RTL_FILES+=("${f}")
done < <(find "${RTL_GEN_DIR}" \( -name "*.sv" -o -name "*.v" \) \
  -not -name "GeneralDPIAdapter.sv" \
  -not -name "StandaloneMemDPI.sv" \
  -not -path "*/verification/*" \
  -print0 2>/dev/null)

if [ ${#RTL_FILES[@]} -eq 0 ]; then
  die "No RTL source files found in ${RTL_GEN_DIR} — run 'make chisel-gen' first"
fi

# Add synthesis stubs (DPI replacements)
STUB_COUNT=0
while IFS= read -r -d '' f; do
  RTL_FILES+=("${f}")
  STUB_COUNT=$((STUB_COUNT + 1))
done < <(find "${STUB_DIR}" \( -name "*.sv" -o -name "*.v" \) -print0 2>/dev/null)

info "Found ${#RTL_FILES[@]} RTL files (${STUB_COUNT} synthesis stub(s))"

# Verify the top module DESIGN exists in the RTL
TOP_FOUND=false
for f in "${RTL_FILES[@]}"; do
  if grep -q "module[[:space:]]\+${DESIGN}[[:space:];(#]" "${f}" 2>/dev/null; then
    TOP_FOUND=true
    break
  fi
done
if [ "${TOP_FOUND}" != "true" ]; then
  die "Top module '${DESIGN}' not found in any RTL file"
fi
info "Top module '${DESIGN}' found ✓"

# Build space-separated file list for yosys-sta
RTL_FILE_LIST="${RTL_FILES[*]}"

# ── invoke yosys-sta ───────────────────────────────────────────────
mkdir -p "${OUTPUT_DIR}"

SYNTH_MAKE_LOG="${OUTPUT_DIR}/synth_make.log"
RESULT_DIR="${OUTPUT_DIR}/${DESIGN}-${CLK_FREQ_MHZ}MHz"

if [ "${SYNTH_SEARCH}" = "on" ] || [ "${SYNTH_SEARCH}" = "1" ] || [ "${SYNTH_SEARCH}" = "true" ]; then
  # ── frequency search mode ────────────────────────────────────────
  info "Frequency search enabled: scanning [${SYNTH_LOW_MHZ}, ${SYNTH_HIGH_MHZ}] MHz..."
  SEARCH_OUT="${OUTPUT_DIR}/search"
  mkdir -p "${SEARCH_OUT}"

  python3 "${SCRIPT_DIR}/synth_search.py" \
    --yosys-sta-home "${YOSYS_STA_HOME}" \
    --output-dir "${SEARCH_OUT}" \
    --design "${DESIGN}" \
    --sdc-file "${SDC_FILE}" \
    --clk-port "${CLK_PORT_NAME}" \
    --rtl-files "${RTL_FILE_LIST}" \
    --low "${SYNTH_LOW_MHZ}" \
    --high "${SYNTH_HIGH_MHZ}" \
    --resume "${CLK_FREQ_MHZ}" \
    >"${SEARCH_OUT}/search_result.txt" 2>&1

  SEARCH_EXIT=$?
  if [ ${SEARCH_EXIT} -ne 0 ]; then
    die "Frequency search failed (exit ${SEARCH_EXIT}). See ${SEARCH_OUT}/search_result.txt"
  fi

  # Extract results
  SEARCH_MHZ=$(grep -oP '^MAX_FREQ_MHZ=\K\d+' "${SEARCH_OUT}/search_result.txt" 2>/dev/null || echo "")
  PROBE_MHZ=$(grep -oP '^PROBE_MHZ=\K\d+' "${SEARCH_OUT}/search_result.txt" 2>/dev/null || echo "${SEARCH_MHZ}")
  if [ -z "${SEARCH_MHZ}" ]; then
    die "Frequency search produced no MAX_FREQ_MHZ result"
  fi
  info "Search converged: max frequency = ${SEARCH_MHZ} MHz (probe: ${PROBE_MHZ} MHz)"

  # Best probe directory — resolve the yosys-sta nested output:
  # yosys-sta Makefile creates $(O)/$(DESIGN)-$(CLK_FREQ_MHZ)MHz/,
  # so results are one level deeper than the probe dir.
  BEST_RESULT="${SEARCH_OUT}/probe_${PROBE_MHZ}MHz/${DESIGN}-${PROBE_MHZ}MHz"
  if [ -d "${BEST_RESULT}" ]; then
    # Copy best result contents to the canonical output directory
    info "Copying best result from ${BEST_RESULT} → ${RESULT_DIR}"
    rm -rf "${RESULT_DIR}"
    mkdir -p "${RESULT_DIR}"
    cp -r "${BEST_RESULT}"/* "${RESULT_DIR}/"
  fi

  FINAL_MHZ="${SEARCH_MHZ}"

else
  # ── single-shot mode ─────────────────────────────────────────────
  info "Launching yosys-sta (PDK: icsprout55)..."

  set +e
  make -C "${YOSYS_STA_HOME}" syn sta \
    DESIGN="${DESIGN}" \
    SDC_FILE="${SDC_FILE}" \
    CLK_FREQ_MHZ="${CLK_FREQ_MHZ}" \
    CLK_PORT_NAME="${CLK_PORT_NAME}" \
    RTL_FILES="${RTL_FILE_LIST}" \
    O="${OUTPUT_DIR}" \
    >"${SYNTH_MAKE_LOG}" 2>&1
  SYNTH_EXIT=$?
  set -e

  if [ ${SYNTH_EXIT} -ne 0 ]; then
    echo "" >&2
    echo "============================================================" >&2
    echo " SYNTHESIS FAILED (exit code ${SYNTH_EXIT})" >&2
    echo "============================================================" >&2
    echo "" >&2

    YOSYS_LOG="${RESULT_DIR}/yosys.log"
    STA_LOG="${RESULT_DIR}/sta.log"

    if [ -f "${YOSYS_LOG}" ]; then
      if ! grep -q "create_clock" "${SYNTH_MAKE_LOG}" 2>/dev/null; then
        echo "→ Hint: yosys may have failed before STA. Check ${YOSYS_LOG}" >&2
      fi
      if grep -qi "ERROR" "${YOSYS_LOG}" 2>/dev/null; then
        echo "→ yosys reported errors (see ${YOSYS_LOG}):" >&2
        grep -i "ERROR" "${YOSYS_LOG}" 2>/dev/null | head -5 >&2
      fi
    fi

    if [ -f "${STA_LOG}" ]; then
      if grep -qi "ERROR\|FATAL" "${STA_LOG}" 2>/dev/null; then
        echo "→ iEDA/STA reported errors (see ${STA_LOG}):" >&2
        grep -iE "ERROR|FATAL" "${STA_LOG}" 2>/dev/null | head -5 >&2
      fi
    fi

    if [ -f "${RESULT_DIR}/sta.log" ]; then
      if grep -qi "port.*not found\|no such port\|can.*find.*port" "${RESULT_DIR}/sta.log" 2>/dev/null; then
        echo "" >&2
        echo "→ CLOCK PORT MISMATCH: The SDC references port '${CLK_PORT_NAME}'" >&2
        echo "  but it was not found in the design. Verify the clock port name" >&2
        echo "  matches the RTL (hint: check CLK_PORT_NAME env var)." >&2
        grep -i "port.*not found\|no such port" "${RESULT_DIR}/sta.log" 2>/dev/null | head -3 >&2
      fi
    fi

    echo "" >&2
    echo "Full build log: ${SYNTH_MAKE_LOG}" >&2
    exit ${SYNTH_EXIT}
  fi

  FINAL_MHZ=""  # will be derived from WNS below
fi

# ── post-synthesis: verify reports ─────────────────────────────────
info "Synthesis completed. Verifying output reports..."

REQUIRED_REPORTS=(
  "${DESIGN}.netlist.v"
  "synth_stat.txt"
  "synth_check.txt"
  "yosys.log"
  "${DESIGN}.rpt"
  "sta.log"
)

MISSING=()
for r in "${REQUIRED_REPORTS[@]}"; do
  if [ ! -f "${RESULT_DIR}/${r}" ]; then
    MISSING+=("${r}")
  fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
  warn "Some expected reports are missing:"
  for m in "${MISSING[@]}"; do
    echo "  - ${RESULT_DIR}/${m}" >&2
  done
else
  info "All expected reports present ✓"
fi

# ── extract key metrics (using report_parser.py for fail-closed parsing) ──
REPORT_PARSER="${SCRIPT_DIR}/report_parser.py"

if [ ! -f "${REPORT_PARSER}" ]; then
  die "Report parser not found: ${REPORT_PARSER}"
fi

info ""
info "============================================================"
info " SYNTHESIS SUMMARY: ${DESIGN} @ ${CLK_FREQ_MHZ}MHz target"
info "============================================================"

# Parse area via report_parser (fail-closed)
echo ""
echo "--- Area ---"
AREA_PARSE_OUT=""
if python3 "${REPORT_PARSER}" stat "${RESULT_DIR}/synth_stat.txt" --design "${DESIGN}" 2>/dev/null; then
  AREA_PARSE_OUT=$(python3 "${REPORT_PARSER}" stat "${RESULT_DIR}/synth_stat.txt" --design "${DESIGN}" 2>&1)
  echo "${AREA_PARSE_OUT}"
else
  RC=$?
  warn "Failed to parse area from synth_stat.txt (exit ${RC})"
  AREA_PARSE_OUT=""
fi

# Parse timing via report_parser (fail-closed)
echo ""
echo "--- Timing ---"
TIMING_PARSE_OUT=""
if python3 "${REPORT_PARSER}" sta "${RESULT_DIR}/${DESIGN}.rpt" 2>/dev/null; then
  TIMING_PARSE_OUT=$(python3 "${REPORT_PARSER}" sta "${RESULT_DIR}/${DESIGN}.rpt" 2>&1)
  echo "${TIMING_PARSE_OUT}"
else
  RC=$?
  warn "Failed to parse STA timing from ${DESIGN}.rpt (exit ${RC})"
  TIMING_PARSE_OUT=""
fi

# Derive maximum achievable frequency from actual WNS
if [ -n "${TIMING_PARSE_OUT}" ]; then
  PARSED_WNS=$(echo "${TIMING_PARSE_OUT}" | grep -oP '^WNS:\s+\K[\d.-]+' || echo "")
  if [ -n "${PARSED_WNS}" ]; then
    if [ -z "${FINAL_MHZ}" ]; then
      FINAL_PY_OUT=$(python3 -c "
import sys; sys.path.insert(0, '${SCRIPT_DIR}')
from report_parser import derive_max_frequency
try:
    max_mhz = derive_max_frequency(${PARSED_WNS}, ${CLK_FREQ_MHZ})
    print(max_mhz)
except Exception as e:
    print(f'derive_max_frequency failed: {e}', file=sys.stderr)
    sys.exit(1)
")
      FINAL_MHZ="${FINAL_PY_OUT:-0}"
    fi

    echo ""
    if [ "$(python3 -c "print(1 if ${PARSED_WNS} >= 0 else 0)" 2>/dev/null || echo 0)" = "1" ]; then
      info "Setup timing MET — WNS = ${PARSED_WNS} ns (pass)"
    else
      echo ""
      info "Setup timing NOT MET — WNS = ${PARSED_WNS} ns (violation)"
    fi
    info "Derived maximum frequency: ${FINAL_MHZ} MHz (from actual WNS slack)"
  fi
fi

echo ""
info "Report directory: ${RESULT_DIR}/"
info "============================================================"

# Write machine-readable summary JSON
SUMMARY_JSON="${OUTPUT_DIR}/synth_summary.json"
python3 -c "
import json, sys
from pathlib import Path
sys.path.insert(0, '${SCRIPT_DIR}')
from report_parser import parse_sta_report, parse_synth_stat, derive_max_frequency

summary = {
    'design': '${DESIGN}',
    'target_mhz': ${CLK_FREQ_MHZ},
    'final_mhz': ${FINAL_MHZ:-0},
}

sta_rpt = Path('${RESULT_DIR}/${DESIGN}.rpt')
if sta_rpt.is_file():
    try:
        timing = parse_sta_report(sta_rpt)
        summary['wns_ns'] = timing['wns']
        summary['tns_ns'] = timing['tns']
        if ${FINAL_MHZ:-0} == 0:
            summary['final_mhz'] = derive_max_frequency(timing['wns'], ${CLK_FREQ_MHZ})
    except Exception as e:
        summary['timing_parse_error'] = str(e)

stat_txt = Path('${RESULT_DIR}/synth_stat.txt')
if stat_txt.is_file():
    try:
        area = parse_synth_stat(stat_txt, '${DESIGN}')
        summary['cell_count'] = area['cell_count']
        summary['area_um2'] = area['area_um2']
    except Exception as e:
        summary['area_parse_error'] = str(e)

with open('${SUMMARY_JSON}', 'w') as f:
    json.dump(summary, f, indent=2)
print(f'Summary written to ${SUMMARY_JSON}')
" || warn "Failed to write summary JSON"

# ── touch sentinel for Makefile incremental check ──────────────────
touch "${OUTPUT_DIR}/.synth_done"

info "Synthesis complete."
exit 0
