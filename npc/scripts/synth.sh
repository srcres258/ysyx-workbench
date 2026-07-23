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

# QoR view selection: canonical_flat (authoritative, flattened) or
# hierarchy_attribution (analysis-only, hierarchy-preserved).
SYNTH_QOR_VIEW="${SYNTH_QOR_VIEW:-canonical_flat}"

abspath_path() {
  python3 - "$1" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).resolve())
PY
}

is_truthy() {
  case "$1" in
    1|true|TRUE|on|ON|yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

bootstrap_yosys_sta() {
  local need_init=0
  [ -x "${YOSYS_STA_HOME}/bin/iEDA" ] || need_init=1
  [ -d "${YOSYS_STA_HOME}/pdk/nangate45" ] || need_init=1

  if [ "${need_init}" -eq 0 ]; then
    return 0
  fi

  if ! is_truthy "${YOSYS_STA_AUTO_INIT}"; then
    die "yosys-sta is not initialized (missing bin/iEDA and/or pdk/nangate45). Run 'make -C ${YOSYS_STA_HOME} init' first, or re-run with YOSYS_STA_AUTO_INIT=on to bootstrap automatically"
  fi

  info "Bootstrapping yosys-sta (missing iEDA and/or nangate45 PDK)..."
  require_cmd wget
  require_cmd git
  if ! make -C "${YOSYS_STA_HOME}" init; then
    die "yosys-sta bootstrap failed. Run 'make -C ${YOSYS_STA_HOME} init' manually to inspect the error"
  fi

  [ -x "${YOSYS_STA_HOME}/bin/iEDA" ] || die "yosys-sta bootstrap did not produce ${YOSYS_STA_HOME}/bin/iEDA"
  [ -d "${YOSYS_STA_HOME}/pdk/nangate45" ] || die "yosys-sta bootstrap did not produce ${YOSYS_STA_HOME}/pdk/nangate45"
}

# ── pre-flight validation ──────────────────────────────────────────
info "=== NPC Synthesis: ${DESIGN} @ ${CLK_FREQ_MHZ}MHz (clock port: ${CLK_PORT_NAME}) ==="

[ -n "${NPC_HOME}" ]       || die "NPC_HOME is not set"
[ -n "${YOSYS_STA_HOME}" ] || die "YOSYS_STA_HOME is not set"

# Normalize paths so `make -C yosys-sta` cannot reinterpret them relative to
# yosys-sta/ itself. This is crucial for fresh builds and direct script use.
NPC_HOME="$(abspath_path "${NPC_HOME}")"
YOSYS_STA_HOME="$(abspath_path "${YOSYS_STA_HOME}")"
OUTPUT_DIR="$(abspath_path "${OUTPUT_DIR}")"
SDC_FILE="$(abspath_path "${SDC_FILE}")"
RTL_GEN_DIR="$(abspath_path "${RTL_GEN_DIR}")"
STUB_DIR="$(abspath_path "${STUB_DIR}")"
SCRIPT_DIR="$(abspath_path "${SCRIPT_DIR}")"

require_dir "${NPC_HOME}"
require_dir "${YOSYS_STA_HOME}"
require_dir "${RTL_GEN_DIR}"
require_dir "${STUB_DIR}"
require_file "${SDC_FILE}"

# Validate QoR view selection
case "${SYNTH_QOR_VIEW}" in
  canonical_flat)
    SYNTH_FLATTEN=1
    ;;
  hierarchy_attribution)
    SYNTH_FLATTEN=0
    ;;
  *)
    die "Unknown SYNTH_QOR_VIEW='${SYNTH_QOR_VIEW}' — must be 'canonical_flat' or 'hierarchy_attribution'"
    ;;
esac
export SYNTH_FLATTEN

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
require_dir "${YOSYS_STA_HOME}/pdk/nangate45" || \
die "nangate45 PDK not found at ${YOSYS_STA_HOME}/pdk/nangate45"

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
VIEW_OUT_DIR="${OUTPUT_DIR}/${SYNTH_QOR_VIEW}"
RESULT_DIR="${VIEW_OUT_DIR}/${DESIGN}-${CLK_FREQ_MHZ}MHz"

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
  info "Launching yosys-sta (PDK: nangate45, view: ${SYNTH_QOR_VIEW})..."

  set +e
  make -C "${YOSYS_STA_HOME}" syn sta \
    DESIGN="${DESIGN}" \
    SDC_FILE="${SDC_FILE}" \
    CLK_FREQ_MHZ="${CLK_FREQ_MHZ}" \
    CLK_PORT_NAME="${CLK_PORT_NAME}" \
    RTL_FILES="${RTL_FILE_LIST}" \
    O="${VIEW_OUT_DIR}" \
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
  "synth_stat.json"
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

# Hard-failure: synth_stat.json must exist and be non-empty (machine-readable artifact)
SYNTH_STAT_JSON="${RESULT_DIR}/synth_stat.json"
if [ ! -f "${SYNTH_STAT_JSON}" ]; then
  die "Required JSON stats artifact not found: ${SYNTH_STAT_JSON}"
fi
if [ ! -s "${SYNTH_STAT_JSON}" ]; then
  die "JSON stats artifact is empty: ${SYNTH_STAT_JSON}"
fi
info "JSON stats artifact present and non-empty ✓"

# ── verify timing classification artifacts (raw data for downstream processing) ──
# These companion files provide raw data for reg2reg/in2reg/reg2out/in2out
# classification, hold analysis, path groups, high-fanout detection, and
# clock skew.  They are auto-generated by iSTA alongside the .rpt.
COMPANION_REPORTS=(
  "${DESIGN}.fanout"       # high-fanout nets → high-fanout category
  "${DESIGN}.cap"           # capacitance violations → path detail enrichment
  "${DESIGN}.trans"         # transition violations → path detail enrichment
  "${DESIGN}_setup.skew"    # setup clock skew → path-group-level clock analysis
  "${DESIGN}_hold.skew"     # hold clock skew → hold category
  "${DESIGN}.pwr"           # power report (iPA)
  "${DESIGN}_instance.pwr"  # per-instance power (iPA)
  "${DESIGN}_instance.csv"  # per-instance power CSV (iPA)
)

MISSING_COMPANION=()
for r in "${COMPANION_REPORTS[@]}"; do
  if [ ! -f "${RESULT_DIR}/${r}" ]; then
    MISSING_COMPANION+=("${r}")
  elif [ ! -s "${RESULT_DIR}/${r}" ]; then
    warn "Timing artifact is empty: ${RESULT_DIR}/${r}"
  fi
done

if [ ${#MISSING_COMPANION[@]} -gt 0 ]; then
  warn "Some companion timing artifacts are missing (downstream classifiers may have reduced data):"
  for m in "${MISSING_COMPANION[@]}"; do
    echo "  - ${RESULT_DIR}/${m}" >&2
  done
else
  info "All companion timing artifacts present ✓"
fi

# ── verify hierarchy-preserved area source ────────────────────────────
# synth_hierarchy.json is captured BEFORE flattening (yosys.tcl) and
# provides per-module area breakdown with submodule information.
# Its absence is non-fatal: the summary renderer falls back to the
# flat synth_stat.json.
SYNTH_HIERARCHY_JSON="${RESULT_DIR}/synth_hierarchy.json"
if [ -f "${SYNTH_HIERARCHY_JSON}" ]; then
  if [ -s "${SYNTH_HIERARCHY_JSON}" ]; then
    HIER_MODULE_COUNT=$(python3 -c "
import json, sys
try:
    data = json.load(open('${SYNTH_HIERARCHY_JSON}'))
    modules = data.get('modules', {})
    print(len(modules))
except Exception:
    print(0)
" 2>/dev/null || echo "0")
    info "Hierarchy-preserved area source present ✓ (${HIER_MODULE_COUNT} module(s) captured before flattening)"
  else
    warn "Hierarchy area source is empty: ${SYNTH_HIERARCHY_JSON} (hierarchy tree will fall back to flat stats)"
  fi
else
  warn "Hierarchy area source not found: ${SYNTH_HIERARCHY_JSON} (hierarchy tree will use flat fallback)"
fi

# ── .rpt content sanity check ────────────────────────────────────────
# The .rpt must contain at minimum the summary table header line.
# An empty or truncated .rpt indicates a silent iSTA failure.
RPT_FILE="${RESULT_DIR}/${DESIGN}.rpt"
if ! grep -q '| Endpoint' "${RPT_FILE}" 2>/dev/null; then
  die "Timing report ${RPT_FILE} appears corrupted or empty: missing summary table header ('| Endpoint'). The iSTA run may have silently failed — check ${RESULT_DIR}/sta.log"
fi
info "Timing report .rpt structure verified ✓"

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

# Write machine-readable summary JSON + human-readable text report
# Delegates to synth_summary.py which consumes the parsed area/timing model
# and emits schema-v3 JSON + text report + optimization hotspots.
SYNTH_AREA_BUDGET="${SYNTH_AREA_BUDGET_UM2:-23000}"
info "Rendering synth summary (area budget: ${SYNTH_AREA_BUDGET} µm²)..."

# Collect provenance for the v3 summary
PROV_YOSYS_VER="$(yosys -V 2>&1 | grep -oP 'Yosys\s+\K[\d.]+' | head -1 || echo 'N/A')"
PROV_IEDA_VER="N/A"
IEDA_BIN="${YOSYS_STA_HOME}/bin/iEDA"
if [ -x "${IEDA_BIN}" ]; then
  # iEDA doesn't have a -V flag; use file modification time as a version proxy
  PROV_IEDA_VER="$(date -r "${IEDA_BIN}" '+%Y-%m-%d' 2>/dev/null || echo 'N/A')"
fi
PROV_WORKBENCH_COMMIT="N/A"
if command -v git >/dev/null 2>&1; then
  WORKBENCH_GIT_DIR="${NPC_HOME}/.."
  PROV_WORKBENCH_COMMIT="$(git -C "${WORKBENCH_GIT_DIR}" rev-parse --short HEAD 2>/dev/null || echo 'N/A')"
fi
PROV_CPU_COMMIT="N/A"
PROV_LIBERTY="NangateOpenCellLibrary_ss_typical.lib (via nangate45 PDK)"
PROV_GENERATED_AT="$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo 'N/A')"

# Determine fanout source: full-netlist (from Task 5) or timing-sampled
PROV_FANOUT_SOURCE="full_netlist"

# For canonical_flat, write summary to the root output directory (backward compat).
# For hierarchy_attribution, write summary inside the view directory.
if [ "${SYNTH_QOR_VIEW}" = "canonical_flat" ]; then
  SUMMARY_OUT_DIR="${OUTPUT_DIR}"
else
  SUMMARY_OUT_DIR="${VIEW_OUT_DIR}"
fi

python3 "${SCRIPT_DIR}/synth_summary.py" \
  --design "${DESIGN}" \
  --target-mhz "${CLK_FREQ_MHZ}" \
  --output-dir "${SUMMARY_OUT_DIR}" \
  --result-dir "${RESULT_DIR}" \
  --area-budget "${SYNTH_AREA_BUDGET}" \
  --yosys-version "${PROV_YOSYS_VER}" \
  --ieda-version "${PROV_IEDA_VER}" \
  --pdk nangate45 \
  --liberty "${PROV_LIBERTY}" \
  --workbench-commit "${PROV_WORKBENCH_COMMIT}" \
  --cpu-commit "${PROV_CPU_COMMIT}" \
  --generated-at "${PROV_GENERATED_AT}" \
  --fanout-source "${PROV_FANOUT_SOURCE}" \
  --qor-view "${SYNTH_QOR_VIEW}" \
  --sdc-file "${SDC_FILE}" \
  --ieda-bin "${YOSYS_STA_HOME}/bin/iEDA" \
  --yosys-sta-home "${YOSYS_STA_HOME}" \
  || warn "Failed to write synth summary JSON/text/hotspots"

# ── verify timing classification report files (post rendering) ─────
# These are generated by synth_timing.py during summary rendering.
TIMING_CLASSIFICATION_REPORTS=(
  "constraint_coverage.rpt"
  "unconstrained_endpoints.rpt"
  "analysis_warnings.rpt"
  "high_fanout_nets.rpt"
  "ista_report_timing_capabilities.rpt"
  "data_startpoints_q.txt"
  "data_endpoints_d.txt"
  "timing_reg2reg_data.json"
  "timing_reg2reg_data.rpt"
)

MISSING_CLASSIFICATION=()
for r in "${TIMING_CLASSIFICATION_REPORTS[@]}"; do
  if [ ! -f "${RESULT_DIR}/${r}" ]; then
    MISSING_CLASSIFICATION+=("${r}")
  elif [ ! -s "${RESULT_DIR}/${r}" ]; then
    warn "Timing classification report is empty: ${RESULT_DIR}/${r}"
  fi
done

# Also verify v3 summary artifacts
V3_ARTIFACTS=("${OUTPUT_DIR}/synth_summary.json" "${OUTPUT_DIR}/optimization_hotspots.txt")
for a in "${V3_ARTIFACTS[@]}"; do
  if [ ! -f "${a}" ]; then
    MISSING_CLASSIFICATION+=("$(basename "${a}") (in output dir)")
  elif [ ! -s "${a}" ]; then
    warn "Summary artifact is empty: ${a}"
  fi
done

if [ ${#MISSING_CLASSIFICATION[@]} -gt 0 ]; then
  warn "Some timing classification reports are missing:"
  for m in "${MISSING_CLASSIFICATION[@]}"; do
    echo "  - ${RESULT_DIR}/${m}" >&2
  done
else
  info "All timing classification reports present ✓"
fi

# ── promote task-2 artifacts to root output dir (canonical_flat only) ─
# The capability report and pin inventories are canonical-flat-derived
# but are consumed by later pipeline steps from the root output directory.
if [ "${SYNTH_QOR_VIEW}" = "canonical_flat" ]; then
  TASK2_ARTIFACTS=(
    "ista_report_timing_capabilities.rpt"
    "data_startpoints_q.txt"
    "data_endpoints_d.txt"
    "timing_reg2reg_data.json"
    "timing_reg2reg_data.rpt"
  )
  for art in "${TASK2_ARTIFACTS[@]}"; do
    if [ -f "${RESULT_DIR}/${art}" ]; then
      cp "${RESULT_DIR}/${art}" "${OUTPUT_DIR}/${art}"
      info "Promoted ${art} → ${OUTPUT_DIR}/${art}"
    else
      warn "Task-2 artifact missing: ${RESULT_DIR}/${art}"
    fi
  done
fi

# ── area flow comparison report (when both views exist) ──────────────────
# Only meaningful in single-shot mode; search mode has its own multi-probe
# output and doesn't need a cross-view comparison.
if [ "${SYNTH_SEARCH}" != "on" ] && [ "${SYNTH_SEARCH}" != "1" ] && [ "${SYNTH_SEARCH}" != "true" ]; then
  OTHER_VIEW=""
  if [ "${SYNTH_QOR_VIEW}" = "canonical_flat" ]; then
    OTHER_VIEW="hierarchy_attribution"
  else
    OTHER_VIEW="canonical_flat"
  fi
  OTHER_RESULT_DIR="${OUTPUT_DIR}/${OTHER_VIEW}/${DESIGN}-${CLK_FREQ_MHZ}MHz"
  if [ -d "${OTHER_RESULT_DIR}" ]; then
    info "Both QoR views present — generating area flow comparison report..."
    python3 "${SCRIPT_DIR}/synth_summary.py" \
      --compare \
      --design "${DESIGN}" \
      --target-mhz "${CLK_FREQ_MHZ}" \
      --canonical-dir "${OUTPUT_DIR}/canonical_flat/${DESIGN}-${CLK_FREQ_MHZ}MHz" \
      --attribution-dir "${OUTPUT_DIR}/hierarchy_attribution/${DESIGN}-${CLK_FREQ_MHZ}MHz" \
      --output-dir "${OUTPUT_DIR}" \
      || warn "Failed to generate area flow comparison report"
  fi
fi

# ── touch sentinel for Makefile incremental check ──────────────────
touch "${OUTPUT_DIR}/.synth_done"

info "Synthesis complete."
exit 0
