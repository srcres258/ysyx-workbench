#!/usr/bin/env python3
# ============================================================================
# perf_aggregator.py — Consume perf.json + synth_summary.json and print a
# stable, fixed-order performance summary block to stdout.
# ============================================================================
# Design principle: ALWAYS fail closed.
#   - Missing file    → non-zero exit with precise path
#   - Missing field   → non-zero exit with field name and source file
#   - Invalid JSON    → non-zero exit with parse error details
# ============================================================================

import json
import sys
from pathlib import Path
from typing import Any, Dict, List, NoReturn


def fail(msg: str) -> NoReturn:
    """Print error to stderr and exit non-zero."""
    print(f"[Perf] ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def load_json(path: Path) -> Dict[str, Any]:
    """Load and parse a JSON file; fail-closed on any I/O or parse error."""
    if not isinstance(path, Path):
        path = Path(path)
    if not path.is_file():
        fail(f"Required file not found: {path}")
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except json.JSONDecodeError as e:
        fail(f"Invalid JSON in {path}: {e}")
    except OSError as e:
        fail(f"Cannot read {path}: {e}")


def require_field(data: Dict[str, Any], field: str, source: str) -> Any:
    """Extract a required field; fail if missing or None."""
    value = data.get(field)
    if value is None:
        fail(f"{source}: missing '{field}' field")
    return value


def print_perf_block(perf_data: Dict[str, Any], synth_data: Dict[str, Any]) -> None:
    """Print the stable perf summary block to stdout.

    Fixed output order (matches plan contract):
      --- perf data ---
      cycles <int>
      instret <int>
      IPC <float>
      frequency <int> MHz
      area <float> um2
      <counter 0 name> <counter 0 value>
      ...
      <counter 25 name> <counter 25 value>
    """

    # ── Core perf metrics from perf.json ──────────────────────────────
    cycles = require_field(perf_data, "cycles", "perf.json")
    instret = require_field(perf_data, "instret", "perf.json")
    ipc = require_field(perf_data, "ipc", "perf.json")
    perf_counters: List[Dict[str, Any]] = require_field(
        perf_data, "perf_counters", "perf.json"
    )

    if not isinstance(perf_counters, list):
        fail("perf.json: 'perf_counters' must be an array")
    if len(perf_counters) != 26:
        fail(
            f"perf.json: expected 26 perf counters, got {len(perf_counters)}"
        )

    # ── Synth metrics from synth_summary.json ─────────────────────────
    final_mhz = require_field(synth_data, "final_mhz", "synth_summary.json")
    area_um2 = require_field(synth_data, "area_um2", "synth_summary.json")

    # ── Print the fixed-order block ───────────────────────────────────
    print("--- perf data ---")
    print(f"cycles  {cycles}")
    print(f"instret  {instret}")
    print(f"IPC  {ipc}")
    print(f"frequency  {final_mhz} MHz")
    print(f"area  {area_um2} um2")

    for idx, ctr in enumerate(perf_counters):
        name = ctr.get("name")
        value = ctr.get("value")
        if name is None:
            fail(f"perf.json: perf_counter[{idx}] missing 'name'")
        if value is None:
            fail(f"perf.json: perf_counter[{idx}] '{name}' missing 'value'")
        print(f"{name}  {value}")


def main() -> None:
    import argparse

    ap = argparse.ArgumentParser(
        description="Aggregate perf + synth data into a stable summary block"
    )
    ap.add_argument(
        "--perf-json",
        type=Path,
        required=True,
        help="Path to npc/build/perf/perf.json (from task 4 perf export)",
    )
    ap.add_argument(
        "--synth-json",
        type=Path,
        required=True,
        help="Path to npc/build/synth/synth_summary.json (from task 3 synth summary)",
    )
    args = ap.parse_args()

    perf_data = load_json(args.perf_json)
    synth_data = load_json(args.synth_json)

    # Schema version check (forward-compat guard)
    schema_ver = perf_data.get("schema_version")
    if schema_ver is None:
        fail("perf.json: missing 'schema_version' field")
    if schema_ver != 1:
        fail(
            f"perf.json: unsupported schema_version {schema_ver} "
            f"(expected 1)"
        )

    print_perf_block(perf_data, synth_data)


if __name__ == "__main__":
    main()
