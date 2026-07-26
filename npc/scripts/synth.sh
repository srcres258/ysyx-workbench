#!/usr/bin/env bash
# ============================================================================
# NPC Synthesis Wrapper — orchestrates yosys-sta for ysyx_25070190
# ============================================================================
# Invoked by:  make -C npc synth
#
# Required env vars (set by npc/Makefile):
#   NPC_HOME         — absolute path to npc/
#   YOSYS_STA_HOME   — absolute path to yosys-sta/
#   IEDA_BIN         — absolute path to iEDA binary (defaults to
#                      ${YOSYS_STA_HOME}/bin/iEDA)
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
IEDA_BIN="${IEDA_BIN:-${YOSYS_STA_HOME}/bin/iEDA}"
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

# Experiment selector: empty=canonical single-shot, exp_a_upstream_default=historical flow
SYNTH_EXPERIMENT="${SYNTH_EXPERIMENT:-}"

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
  [ -x "${IEDA_BIN}" ] || need_init=1
  [ -d "${YOSYS_STA_HOME}/pdk/nangate45" ] || need_init=1

  if [ "${need_init}" -eq 0 ]; then
    return 0
  fi

  if ! is_truthy "${YOSYS_STA_AUTO_INIT}"; then
    die "yosys-sta is not initialized (missing iEDA binary and/or pdk/nangate45). Run 'make -C ${YOSYS_STA_HOME} init' first, or re-run with YOSYS_STA_AUTO_INIT=on to bootstrap automatically"
  fi

  info "Bootstrapping yosys-sta (missing iEDA and/or nangate45 PDK)..."
  require_cmd wget
  require_cmd git
  if ! make -C "${YOSYS_STA_HOME}" init; then
    die "yosys-sta bootstrap failed. Run 'make -C ${YOSYS_STA_HOME} init' manually to inspect the error"
  fi

  [ -x "${IEDA_BIN}" ] || die "yosys-sta bootstrap did not produce ${IEDA_BIN}"
  [ -d "${YOSYS_STA_HOME}/pdk/nangate45" ] || die "yosys-sta bootstrap did not produce ${YOSYS_STA_HOME}/pdk/nangate45"
}

# ── identity helpers ────────────────────────────────────────────────
# Compute SHA256 of a file, returns "N/A" if file missing or sha256sum unavailable
compute_file_sha256() {
  local f="$1"
  if [ ! -f "${f}" ]; then
    echo "N/A"
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${f}" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "${f}" | awk '{print $1}'
  else
    python3 -c "import hashlib; print(hashlib.sha256(open('${f}','rb').read()).hexdigest())" 2>/dev/null || echo "N/A"
  fi
}

# Collect the input identity manifest and write it to input_identity.json.
# This runs AFTER RTL collection but BEFORE yosys-sta so the RTL hash
# captures the generator output used for this specific run.
collect_input_identity() {
  local identity_json="${OUTPUT_DIR}/input_identity.json"
  mkdir -p "${OUTPUT_DIR}"

  info "Collecting input identity manifest..."

  # ── git commits ──
  local workbench_commit="N/A"
  local cpu_commit="N/A"
  if command -v git >/dev/null 2>&1; then
    local workbench_git_dir="${NPC_HOME}/.."
    workbench_commit=$(git -C "${workbench_git_dir}" rev-parse HEAD 2>/dev/null || echo "N/A")
    cpu_commit=$(git -C "${NPC_HOME}" rev-parse HEAD 2>/dev/null || echo "N/A")
  fi

  # ── generated Verilog SHA256: combined hash of all RTL source files (sorted) ──
  # Hash each file individually, then hash the concatenation for a single identity digest.
  local rtl_hashes_json="{}"
  local combined_parts=""
  local sorted_rtl_files=()
  while IFS= read -r -d '' f; do
    sorted_rtl_files+=("${f}")
  done < <(for f in "${RTL_FILES[@]}"; do echo "${f}"; done | sort -z | tr '\n' '\0')
  # Re-read sorted
  unset sorted_rtl_files
  local sorted_rtl_files=()
  local rtl_files_sorted
  rtl_files_sorted=$(printf '%s\n' "${RTL_FILES[@]}" | sort)
  while IFS= read -r f; do
    [ -z "${f}" ] && continue
    sorted_rtl_files+=("${f}")
  done <<< "${rtl_files_sorted}"

  local rtl_hash_parts=""
  if [ ${#sorted_rtl_files[@]} -gt 0 ]; then
    # Build per-file hash dict and combined digest
    local py_script
    py_script=$(cat <<'PYEOF'
import hashlib, json, sys
files = sys.argv[1:]
hashes = {}
combined = hashlib.sha256()
for f in sorted(files):
    h = hashlib.sha256(open(f, 'rb').read()).hexdigest()
    hashes[f] = h
    combined.update(f.encode())
    combined.update(h.encode())
rtl_hashes = json.dumps(hashes, indent=2)
combined_hex = combined.hexdigest()
# Write a JSON fragment for later assembly
out = {
    "rtl_source_hashes": hashes,
    "generated_verilog_sha256": combined_hex
}
print(json.dumps(out))
PYEOF
)
    local rtl_hash_result
    rtl_hash_result=$(python3 -c "${py_script}" "${sorted_rtl_files[@]}" 2>/dev/null || echo '{"rtl_source_hashes":{},"generated_verilog_sha256":"N/A"}')
    local generated_verilog_sha256
    generated_verilog_sha256=$(echo "${rtl_hash_result}" | python3 -c "import sys,json; print(json.load(sys.stdin)['generated_verilog_sha256'])" 2>/dev/null || echo "N/A")
    rtl_hashes_json=$(echo "${rtl_hash_result}" | python3 -c "import sys,json; print(json.dumps(json.load(sys.stdin)['rtl_source_hashes']))" 2>/dev/null || echo "{}")
  else
    generated_verilog_sha256="N/A"
  fi

  # ── Liberty SHA256 ──
  local liberty_sha256="N/A"
  if [ -d "${YOSYS_STA_HOME}/pdk/nangate45/lib" ]; then
    local liberty_file
    liberty_file=$(find "${YOSYS_STA_HOME}/pdk/nangate45/lib" -name "*.lib" -type f 2>/dev/null | head -1)
    if [ -n "${liberty_file}" ] && [ -f "${liberty_file}" ]; then
      liberty_sha256=$(compute_file_sha256 "${liberty_file}")
    fi
  fi

  # ── SDC SHA256 ──
  local sdc_sha256
  sdc_sha256=$(compute_file_sha256 "${SDC_FILE}")

  # ── Liberty file path (for human reference) ──
  local liberty_path="N/A"
  if [ -d "${YOSYS_STA_HOME}/pdk/nangate45/lib" ]; then
    liberty_path=$(find "${YOSYS_STA_HOME}/pdk/nangate45/lib" -name "*.lib" -type f 2>/dev/null | head -1 || echo "N/A")
  fi

  # ── tool versions ──
  local yosys_version
  yosys_version=$(yosys -V 2>&1 | grep -oP 'Yosys\s+\K[\d.]+' | head -1 || echo "N/A")
  if [ -z "${yosys_version}" ]; then
    yosys_version="N/A"
  fi

  local abc_version="PENDING"
  local ista_version="N/A"
  local ieda_bin="${IEDA_BIN}"
  if [ -x "${ieda_bin}" ]; then
    ista_version=$(date -r "${ieda_bin}" '+%Y-%m-%d' 2>/dev/null || echo "N/A")
  fi

  # ── config flags snapshot ──
  local config_flags
  config_flags=$(python3 -c "
import json
flags = {
    'SYNTH_STRATEGY': '${SYNTH_STRATEGY:-DELAY 4}',
    'SYNTH_QOR_VIEW': '${SYNTH_QOR_VIEW}',
    'SYNTH_FLATTEN': '${SYNTH_FLATTEN:-1}',
    'SYNTH_EXPERIMENT': '${SYNTH_EXPERIMENT:-}',
    'SYNTH_SEARCH': '${SYNTH_SEARCH:-off}',
    'CLK_FREQ_MHZ': '${CLK_FREQ_MHZ}',
    'CLK_PORT_NAME': '${CLK_PORT_NAME}',
    'SYNTH_AREA_BUDGET_UM2': '${SYNTH_AREA_BUDGET_UM2:-23000}',
    'SYNTH_LOW_MHZ': '${SYNTH_LOW_MHZ:-1}',
    'SYNTH_HIGH_MHZ': '${SYNTH_HIGH_MHZ:-500}',
    'YOSYS_STA_AUTO_INIT': '${YOSYS_STA_AUTO_INIT:-on}',
}
print(json.dumps(flags, indent=2))
" 2>/dev/null || echo "{}")

  # ── synthesis defines ──
  local synthesis_defines
  synthesis_defines=$(python3 -c "
import json
defines = {}
print(json.dumps(defines, indent=2))
" 2>/dev/null || echo "{}")

  # ── write identity JSON ──
  python3 -c "
import json, sys

# Build the identity manifest
identity = {
    'workbench_commit': '${workbench_commit}',
    'cpu_commit': '${cpu_commit}',
    'generated_verilog_sha256': '${generated_verilog_sha256}',
    'liberty_sha256': '${liberty_sha256}',
    'sdc_sha256': '${sdc_sha256}',
    'top_module': '${DESIGN}',
    'target_clock_mhz': ${CLK_FREQ_MHZ},
    'synthesis_defines': json.loads('''${synthesis_defines}'''),
    'config_flags': json.loads('''${config_flags}'''),
    'yosys_version': '${yosys_version}',
    'abc_version': '${abc_version}',
    'ista_version': '${ista_version}',
    'experiment': '${SYNTH_EXPERIMENT:-}',
    'liberty_path': '${liberty_path}',
    'sdc_path': '${SDC_FILE}',
}

# Merge RTL source hashes
rtl_hashes = json.loads('''${rtl_hashes_json}''')
identity['rtl_source_hashes'] = rtl_hashes

with open('${identity_json}', 'w') as f:
    json.dump(identity, f, indent=2, sort_keys=True)
print('[Synth] Input identity manifest written: ${identity_json}')
" 2>/dev/null || warn "Failed to write input identity manifest"

  # Store key fields for later use (hash gate)
  echo "${generated_verilog_sha256}" > "${OUTPUT_DIR}/.input_identity_generated_verilog_sha256"
  echo "${liberty_sha256}" > "${OUTPUT_DIR}/.input_identity_liberty_sha256"
  echo "${sdc_sha256}" > "${OUTPUT_DIR}/.input_identity_sdc_sha256"

  info "Input identity manifest collected ✓"
  info "  generated_verilog_sha256: ${generated_verilog_sha256}"
  info "  liberty_sha256: ${liberty_sha256}"
  info "  sdc_sha256: ${sdc_sha256}"
}

# Update input_identity.json with post-synthesis fields (ABC version, etc.)
enrich_input_identity() {
  local identity_json="${OUTPUT_DIR}/input_identity.json"
  if [ ! -f "${identity_json}" ]; then
    return 0
  fi

  local yosys_log="${RESULT_DIR}/yosys.log"

  # Extract ABC version from yosys log
  local abc_version="N/A"
  if [ -f "${yosys_log}" ]; then
    abc_version=$(grep -oP 'ABC \(version [^)]+\)' "${yosys_log}" 2>/dev/null | head -1 || echo "N/A")
    if [ -z "${abc_version}" ]; then
      abc_version=$(grep -oP 'UC Berkeley, ABC [\d.]+' "${yosys_log}" 2>/dev/null | head -1 || echo "N/A")
    fi
  fi

  if [ "${abc_version}" != "N/A" ] && [ "${abc_version}" != "PENDING" ]; then
    python3 -c "
import json
with open('${identity_json}', 'r') as f:
    data = json.load(f)
data['abc_version'] = '${abc_version}'
with open('${identity_json}', 'w') as f:
    json.dump(data, f, indent=2, sort_keys=True)
" 2>/dev/null || true
    info "Enriched input_identity.json with ABC version: ${abc_version}"
  fi
}

# Recover the historical/default yosys-sta flow from git evidence (commit 79952c4).
# Saves the recovered pass sequence to exp_a_upstream_default/yosys_pass_sequence.txt.
recover_exp_a_flow() {
  local exp_dir="${OUTPUT_DIR}/exp_a_upstream_default"
  mkdir -p "${exp_dir}"

  local yosys_sta_git="${YOSYS_STA_HOME}"
  local historical_commit="79952c4"

  info "Recovering historical flow (exp_a_upstream_default) from commit ${historical_commit}..."

  if ! git -C "${yosys_sta_git}" rev-parse --verify "${historical_commit}^{commit}" >/dev/null 2>&1; then
    warn "Historical commit ${historical_commit} not found in yosys-sta git history"
    warn "exp_a_upstream_default recovery requires a full yosys-sta git clone with history"
    return 0
  fi

  local historical_tcl
  historical_tcl=$(git -C "${yosys_sta_git}" show "${historical_commit}:scripts/yosys.tcl" 2>/dev/null || echo "")
  if [ -z "${historical_tcl}" ]; then
    warn "Could not retrieve scripts/yosys.tcl from commit ${historical_commit}"
    return 0
  fi

  # Extract the canonical pass sequence from the historical yosys.tcl.
  # The pass sequence is everything from "yosys -import" to the final "write_verilog".
  # We capture Yosys commands and their arguments, resolving key variables.
  local pass_sequence_txt="${exp_dir}/yosys_pass_sequence.txt"
  local exp_yosys_tcl="${exp_dir}/yosys_historical_79952c4.tcl"

  # Save the full historical Tcl for archival reference
  echo "${historical_tcl}" > "${exp_yosys_tcl}"
  info "Saved historical yosys.tcl (79952c4) → ${exp_yosys_tcl}"

  # Extract the pass sequence: commands after "yosys -import" until EOF,
  # filtering to substantive Yosys operations and resolving variable references.
  python3 -c "
import re, sys

tcl_content = open('${exp_yosys_tcl}', 'r').read()

# Find the main running section (after 'yosys -import')
match = re.search(r'yosys -import.*?\n(.*)', tcl_content, re.DOTALL)
if not match:
    print('ERROR: Could not find yosys -import in historical Tcl')
    sys.exit(0)

main_section = match.group(1)

# Resolve known variable values from the historical Tcl and current env
var_map = {
    'DESIGN': '${DESIGN}',
    'CLK_PERIOD_PS': '10000',  # 100 MHz
    'NETLIST_SYN_V': '${DESIGN}.netlist.v',
    'sdc_file': 'abc.sdc',
    'strategy_script': '\$strategy_script (ABC mapping)',
    'INO_INSERT_BUF': 'BUF_X8 (historical; current PDK uses BUF_CELL=BUF_X8)',
    'BUF_CELL': 'BUF_X8',
}

# Extract command lines: non-comment, non-blank lines that are actual Tcl commands
lines = []
in_comment_block = False
for raw in main_section.split('\n'):
    stripped = raw.strip()
    if not stripped or stripped.startswith('#'):
        continue
    # Skip variable assignments and control-flow statements
    if re.match(r'^\s*(set|foreach|if|proc|log|tee|read_verilog|read_liberty)\s', stripped):
        continue
    # Skip closing braces
    if stripped in ('}', ']'):
        continue
    # Resolve variables
    for var, val in var_map.items():
        stripped = stripped.replace('\$' + var, val)
        stripped = stripped.replace('\${' + var + '}', val)
        # Also handle Tcl variable substitution in braced contexts
        stripped = re.sub(r'\\\$' + re.escape(var), val, stripped)
    # Skip lines that are still pure variable expansions
    if stripped.startswith('{*}'):
        continue
    lines.append(stripped)

# Write the pass sequence
with open('${pass_sequence_txt}', 'w') as f:
    f.write('# =============================================================================\n')
    f.write('# yosys_pass_sequence.txt — exp_a_upstream_default (historical flow)\n')
    f.write('# =============================================================================\n')
    f.write('# Source: yosys-sta commit 79952c4 (refactor: remove unsed read_constr in abc)\n')
    f.write('# Recovered: $(date -u '+%Y-%m-%dT%H:%M:%SZ')\n')
    f.write('#\n')
    f.write('# Driver cell note: historical used \$INO_INSERT_BUF (BUF_X8);\n')
    f.write('# current PDK (nangate45.tcl) uses \$BUF_CELL (BUF_X8). Both resolve to\n')
    f.write('# the same BUF_X8 cell — this is an alias change, not a real cell change.\n')
    f.write('#\n')
    f.write('# Key difference from current flow:\n')
    f.write('#   synth -top \$DESIGN -flatten -run :fine  ← FLATTEN during coarse (pre-ABC)\n')
    f.write('#   vs current: synth -top \$DESIGN -run :fine  ← no flatten (post-ABC flatten)\n')
    f.write('# =============================================================================\n')
    f.write('\n')
    for i, line in enumerate(lines, 1):
        f.write(f'{line}\n')

print(f'Pass sequence written: ${pass_sequence_txt} ({len(lines)} command(s))')
" 2>/dev/null || warn "Failed to extract pass sequence from historical yosys.tcl"

  # Record the recovery metadata
  local recovery_meta="${exp_dir}/recovery_metadata.json"
  python3 -c "
import json, subprocess, os
meta = {
    'source_commit': '${historical_commit}',
    'source_repo': '${yosys_sta_git}',
    'source_file': 'scripts/yosys.tcl',
    'recovered_at': '$(date -u '+%Y-%m-%dT%H:%M:%SZ')',
    'driver_cell_note': 'historical INO_INSERT_BUF (BUF_X8) → current BUF_CELL (BUF_X8)',
    'key_difference': 'synth -flatten -run :fine (pre-ABC flatten) vs synth -run :fine (post-ABC flatten)',
    'design': '${DESIGN}',
    'target_clock_mhz': ${CLK_FREQ_MHZ},
}
with open('${recovery_meta}', 'w') as f:
    json.dump(meta, f, indent=2, sort_keys=True)
print(f'Recovery metadata written: ${recovery_meta}')
" 2>/dev/null || true

  info "Historical flow recovery complete for exp_a_upstream_default"
  info "  pass sequence: ${pass_sequence_txt}"
  info "  historical Tcl: ${exp_yosys_tcl}"
  info "  recovery metadata: ${recovery_meta}"
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

# ── experiment routing ──────────────────────────────────────────
# When SYNTH_EXPERIMENT is set, redirect output to a per-experiment
# directory.  The experiment name determines the pass-order axes;
# SYNTH_QOR_VIEW is ignored (the experiment bakes in its own flatten
# strategy).  Reject unrecognised experiment names so non-experiment
# drift cannot slip through.
if [ -n "${SYNTH_EXPERIMENT}" ]; then
  case "${SYNTH_EXPERIMENT}" in
    exp_a_upstream_default|exp_b_flatten_pre_abc|exp_c_hier_abc|exp_d_postmap_flat)
      OUTPUT_DIR="${OUTPUT_DIR}/${SYNTH_EXPERIMENT}"
      info "Experiment mode: ${SYNTH_EXPERIMENT}"
      info "  Output directory: ${OUTPUT_DIR}"
      info "  (SYNTH_QOR_VIEW is ignored — experiment bakes in flatten strategy)"
      # Reset SYNTH_FLATTEN to a neutral value; yosys.tcl uses SYNTH_EXPERIMENT
      # directly to determine flatten timing.
      ;;
    *)
      cat >&2 <<'EXPERR'
============================================================
 SYNTH_EXPERIMENT REJECTED
============================================================
The requested SYNTH_EXPERIMENT is not one of the four plan-
defined profiles.  Valid values:

  exp_a_upstream_default   – historical flow (pre-ABC flatten)
  exp_b_flatten_pre_abc    – early flatten, modern PDK
  exp_c_hier_abc           – hierarchical ABC, NO flatten
  exp_d_postmap_flat       – hierarchical ABC, flatten after map

Other values are rejected to prevent accidental non-experiment
drift from polluting the controlled comparison matrix.
============================================================
EXPERR
      die "Unknown SYNTH_EXPERIMENT='${SYNTH_EXPERIMENT}' — must be one of: exp_a_upstream_default, exp_b_flatten_pre_abc, exp_c_hier_abc, exp_d_postmap_flat"
      ;;
  esac
fi

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

# ── collect input identity (pre-synthesis) ──
collect_input_identity

# ── invoke yosys-sta ───────────────────────────────────────────────
mkdir -p "${OUTPUT_DIR}"

SYNTH_MAKE_LOG="${OUTPUT_DIR}/synth_make.log"
# For experiments, the experiment name IS the view; bypass SYNTH_QOR_VIEW.
if [ -n "${SYNTH_EXPERIMENT}" ]; then
  RESULT_DIR="${OUTPUT_DIR}/${DESIGN}-${CLK_FREQ_MHZ}MHz"
  VIEW_OUT_DIR="${OUTPUT_DIR}"
else
  VIEW_OUT_DIR="${OUTPUT_DIR}/${SYNTH_QOR_VIEW}"
  RESULT_DIR="${VIEW_OUT_DIR}/${DESIGN}-${CLK_FREQ_MHZ}MHz"
fi

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
  if [ -n "${SYNTH_EXPERIMENT}" ]; then
    info "Launching yosys-sta (PDK: nangate45, experiment: ${SYNTH_EXPERIMENT})..."
  else
    info "Launching yosys-sta (PDK: nangate45, view: ${SYNTH_QOR_VIEW})..."
  fi

  set +e
  SYNTH_EXPERIMENT="${SYNTH_EXPERIMENT:-}" \
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

# ── enrich input identity with post-synthesis fields ──
enrich_input_identity

# ── historical flow recovery (exp_a_upstream_default) ──
# This writes archival yosys_historical_79952c4.tcl and recovery_metadata.json
# to the root-level exp_a directory for documentation purposes.
if [ "${SYNTH_EXPERIMENT}" = "exp_a_upstream_default" ]; then
  recover_exp_a_flow
fi

# ── experiment pass sequence verification ──────────────────
# Every experiment run must produce yosys_pass_sequence.txt in its
# result directory (written by yosys.tcl during synthesis).  Missing
# pass sequences are treated as hard failures — the experiment did
# not record its pass-order axes, making downstream diff impossible.
if [ -n "${SYNTH_EXPERIMENT}" ]; then
  PASS_SEQ="${RESULT_DIR}/yosys_pass_sequence.txt"
  if [ -f "${PASS_SEQ}" ] && [ -s "${PASS_SEQ}" ]; then
    info "Experiment pass sequence recorded: ${PASS_SEQ} ✓"
  else
    die "Experiment ${SYNTH_EXPERIMENT} completed but yosys_pass_sequence.txt is missing or empty at ${PASS_SEQ}. The pass sequence is required for experiment diffs."
  fi
fi

# ── stage stat checkpoint verification ──────────────────────
# Stage checkpoints are written by yosys.tcl during synthesis.
# Missing stage files are non-fatal at this level (some stages may be
# skipped depending on the experiment profile), but missing "final"
# or "post_abc" is suspicious and generates loud warnings.
STAGE_NAMES="post_synth_coarse post_flatten post_share post_clockgate post_dfflibmap pre_abc post_abc final"
MISSING_STAGES=""
CRITICAL_STAGES_MISSING=""
for stage in ${STAGE_NAMES}; do
  if [ -f "${RESULT_DIR}/stage_${stage}.json" ] && [ -s "${RESULT_DIR}/stage_${stage}.json" ]; then
    info "  stage checkpoint: ${stage} ✓"
  else
    MISSING_STAGES="${MISSING_STAGES} ${stage}"
    case "${stage}" in
      final|post_abc)
        CRITICAL_STAGES_MISSING="${CRITICAL_STAGES_MISSING} ${stage}"
        ;;
    esac
  fi
done

if [ -n "${MISSING_STAGES}" ]; then
  warn "Stage checkpoints missing:${MISSING_STAGES}"
  if [ -n "${CRITICAL_STAGES_MISSING}" ]; then
    warn "CRITICAL stages missing:${CRITICAL_STAGES_MISSING} — the CSV comparison will be incomplete"
  fi
  # Check if stage_order.txt was emitted (tells us which stages were expected)
  if [ -f "${RESULT_DIR}/stage_order.txt" ]; then
    info "  stage_order.txt exists — missing stages may be expected for this experiment profile"
  else
    warn "  stage_order.txt is also missing — stage emission may have failed entirely"
  fi
else
  info "All stage checkpoints present ✓"
fi

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
IEDA_BIN="${IEDA_BIN}"
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
# For experiments, write summary inside the experiment directory.
if [ -n "${SYNTH_EXPERIMENT}" ]; then
  SUMMARY_OUT_DIR="${OUTPUT_DIR}"
  QOR_VIEW_ARG="${SYNTH_QOR_VIEW}"
elif [ "${SYNTH_QOR_VIEW}" = "canonical_flat" ]; then
  SUMMARY_OUT_DIR="${OUTPUT_DIR}"
  QOR_VIEW_ARG="${SYNTH_QOR_VIEW}"
else
  SUMMARY_OUT_DIR="${VIEW_OUT_DIR}"
  QOR_VIEW_ARG="${SYNTH_QOR_VIEW}"
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
  --qor-view "${QOR_VIEW_ARG}" \
  --sdc-file "${SDC_FILE}" \
  --ieda-bin "${IEDA_BIN}" \
  --yosys-sta-home "${YOSYS_STA_HOME}" \
  || warn "Failed to write synth summary JSON/text/hotspots"

# ── generate stage comparison CSV (experiment mode) ─────────────
# The stage comparison CSV collects metrics across all experiments that
# have been run so far.  In experiment mode, this rebuilds the full CSV.
if [ -n "${SYNTH_EXPERIMENT}" ]; then
  STAGE_CSV_ROOT="${OUTPUT_DIR}/.."
  info "Generating stage comparison CSV from experiments under ${STAGE_CSV_ROOT}..."
  python3 "${SCRIPT_DIR}/synth_summary.py" \
    --stage-csv \
    --synth-root "${STAGE_CSV_ROOT}" \
    --output-dir "${OUTPUT_DIR}" \
    --design "${DESIGN}" \
    --target-mhz "${CLK_FREQ_MHZ}" \
    || warn "Failed to generate stage comparison CSV"
fi

# ── equivalence check (experiment mode, after all experiments run) ──
# Compares each experiment's pre-ABC netlist against the shared RTL
# gold model using Yosys equiv_make / equiv_simple / equiv_induct.
# The identity gate (generated_verilog_sha256 match) controls comparability.
# Blocked experiments (missing netlists, hash mismatches, unsupported cells)
# are documented as explicit limitations and backstopped by functional tests.
# This is a documentation step only — it does NOT fail the synthesis.
if [ -n "${SYNTH_EXPERIMENT}" ]; then
  SYNTH_ROOT="${OUTPUT_DIR}/.."
  EQUIV_CHECK_PY="${SCRIPT_DIR}/equiv_check.py"

  if [ -f "${EQUIV_CHECK_PY}" ]; then
    YOSYS_BIN=""
    if command -v yosys >/dev/null 2>&1; then
      YOSYS_BIN="yosys"
    else
      YOSYS_BIN_NIX="/nix/store/mq1s3n96svwlp9h8h8d4r9rbn4wd7hkb-yosys-0.62/bin/yosys"
      if [ -x "${YOSYS_BIN_NIX}" ]; then
        YOSYS_BIN="${YOSYS_BIN_NIX}"
      fi
    fi

    if [ -n "${YOSYS_BIN}" ]; then
      info "Running equivalence check (experiment mode)..."
      set +e
      python3 "${EQUIV_CHECK_PY}" \
        --synth-root "${SYNTH_ROOT}" \
        --gold-exp "exp_b_flatten_pre_abc" \
        --top-module "${DESIGN}" \
        --yosys-bin "${YOSYS_BIN}" \
        --output-dir "${OUTPUT_DIR}" \
        --max-seq 10 \
        --timeout 600 \
        2>&1
      EQUIV_EXIT=$?
      set -e
      if [ ${EQUIV_EXIT} -ne 0 ]; then
        warn "Equivalence check completed with non-zero exit (${EQUIV_EXIT}) — see equivalence_report.rpt for details"
      fi
    else
      warn "Yosys not found — equivalence check skipped (install yosys or set YOSYS_BIN)"
    fi
  else
    warn "Equivalence check script not found: ${EQUIV_CHECK_PY}"
  fi
fi

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
# Experiments have their own dedicated comparison via synthesis_flow_diff.rpt.
if [ "${SYNTH_SEARCH}" != "on" ] && [ "${SYNTH_SEARCH}" != "1" ] && [ "${SYNTH_SEARCH}" != "true" ] \
   && [ -z "${SYNTH_EXPERIMENT}" ]; then
  OTHER_VIEW=""
  if [ "${SYNTH_QOR_VIEW}" = "canonical_flat" ]; then
    OTHER_VIEW="hierarchy_attribution"
  else
    OTHER_VIEW="canonical_flat"
  fi
  OTHER_RESULT_DIR="${OUTPUT_DIR}/${OTHER_VIEW}/${DESIGN}-${CLK_FREQ_MHZ}MHz"
  if [ -d "${OTHER_RESULT_DIR}" ]; then
    info "Both QoR views present — generating area flow comparison report..."
    # Pass the input identity for hash-gate validation (fail-closed on mismatch)
    IDENTITY_JSON="${OUTPUT_DIR}/input_identity.json"
    IDENTITY_ARG=""
    if [ -f "${IDENTITY_JSON}" ]; then
      IDENTITY_ARG="--identity ${IDENTITY_JSON}"
    fi
    python3 "${SCRIPT_DIR}/synth_summary.py" \
      --compare \
      --design "${DESIGN}" \
      --target-mhz "${CLK_FREQ_MHZ}" \
      --canonical-dir "${OUTPUT_DIR}/canonical_flat/${DESIGN}-${CLK_FREQ_MHZ}MHz" \
      --attribution-dir "${OUTPUT_DIR}/hierarchy_attribution/${DESIGN}-${CLK_FREQ_MHZ}MHz" \
      --output-dir "${OUTPUT_DIR}" \
      ${IDENTITY_ARG} \
      || warn "Failed to generate area flow comparison report"
  fi
fi

# ── touch sentinel for Makefile incremental check ──────────────────
touch "${OUTPUT_DIR}/.synth_done"

info "Synthesis complete."
exit 0
