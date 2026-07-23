#!/usr/bin/env bash
# ============================================================================
# iSTA coverage crash reproducer — entrypoint
# ============================================================================
# Runs ista_coverage_batch_runner.py against the build/synth artifacts,
# generating batched Tcl and (if --run) invoking iEDA.
#
# Invoked from this source-tree location, it auto-detects the work directory
# as ${NPC_HOME}/build/synth (NPC_HOME defaults to parent of this script's
# directory).
#
# Usage:
#   bash run_ista_coverage_repro.sh              # Dry-run: generate Tcl + JSON only
#   bash run_ista_coverage_repro.sh --run        # Full run: invoke iEDA for each batch
#   bash run_ista_coverage_repro.sh --run --batch-sizes 32,64,128
#
#   WORK_DIR=/path/to/build/synth bash run_ista_coverage_repro.sh --run
#   NPC_HOME=/path/to/npc bash run_ista_coverage_repro.sh --run
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NPC_HOME="${NPC_HOME:-${SCRIPT_DIR}/..}"
WORK_DIR="${WORK_DIR:-${NPC_HOME}/build/synth}"

echo "=== iSTA coverage crash reproducer ==="
echo "NPC_HOME: ${NPC_HOME}"
echo "Work dir: ${WORK_DIR}"

if [ ! -d "${WORK_DIR}" ]; then
    echo "ERROR: Work directory does not exist: ${WORK_DIR}"
    echo "  Run 'make -C npc synth' first to generate build artifacts."
    exit 1
fi

# Run the batch runner
python3 "${SCRIPT_DIR}/ista_coverage_batch_runner.py" \
    --work-dir "${WORK_DIR}" \
    "$@"
