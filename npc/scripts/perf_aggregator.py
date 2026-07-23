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
import subprocess
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


def format_value(value: Any) -> str:
    """Format numeric values without forcing trailing decimals."""
    if isinstance(value, bool):
        return str(value)
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return str(int(value)) if value.is_integer() else f"{value:g}"
    return str(value)


def get_git_metadata() -> tuple[str, str]:
    """Return the current commit id and title for the report header."""
    try:
        commit_id = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], text=True
        ).strip()
        commit_title = subprocess.check_output(
            ["git", "log", "-1", "--pretty=%s"], text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError) as e:
        fail(f"Unable to query git metadata: {e}")

    if not commit_id:
        fail("git metadata: empty commit id")
    if not commit_title:
        fail("git metadata: empty commit title")
    return commit_id, commit_title


def build_perf_block(perf_data: Dict[str, Any], synth_data: Dict[str, Any]) -> str:
    """Build the required perf report block."""

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
    # final_mhz and area_um2 exist in both v1 and v2 schemas.
    final_mhz = require_field(synth_data, "final_mhz", "synth_summary.json")
    area_um2 = require_field(synth_data, "area_um2", "synth_summary.json")

    # ── v4 view-type guard ───────────────────────────────────────────
    # perf_aggregator MUST always consume canonical-flat authoritative
    # metrics.  Reject hierarchy-attribution summaries to prevent
    # accidental use of non-authoritative QoR data.
    view_type = synth_data.get("view_type")
    if view_type == "hierarchy_attribution":
        fail(
            "synth_summary.json: view_type is 'hierarchy_attribution'. "
            "Perf aggregator requires canonical_flat authoritative metrics. "
            "Point --synth-json at the canonical-flat summary instead."
        )
    # Pre-v4 summaries lack view_type entirely — proceed with a warning
    # to stderr but do not fail-closed (backward compat for v1/v2/v3).
    if view_type is None:
        print(
            "[Perf] WARNING: synth_summary.json has no 'view_type' field "
            "(pre-v4 schema). Proceeding, but recommend re-synthesising with "
            "v4 canonical_flat view for authoritative metrics.",
            file=sys.stderr,
        )

    # core_reg2reg_fmax_mhz is a v2-only field.  When present and usable
    # (non-null integer), prefer it for the per-category core Fmax line.
    # When absent (v1 schema), silently omit — no warning, no fake number.
    # When present but null (v2, no reg2reg paths in STA report), emit an
    # explicit "N/A" marker so downstream consumers are not misled.
    core_reg2reg_fmax = synth_data.get("core_reg2reg_fmax_mhz")
    if core_reg2reg_fmax is not None:
        core_fmax_str = f"核心 reg2reg 频率: {core_reg2reg_fmax}MHz"
    elif "core_reg2reg_fmax_mhz" in synth_data:
        core_fmax_str = "核心 reg2reg 频率: N/A (no reg2reg paths in STA report)"
    else:
        core_fmax_str = None  # v1 schema — omit silently

    commit_id, commit_title = get_git_metadata()

    lines: List[str] = [
        f"commit: {commit_id}",
        f"说明: {commit_title}",
        f"仿真周期数: {format_value(cycles)}",
        f"指令数: {format_value(instret)}",
        f"IPC: {format_value(ipc)}",
        f"综合频率: {format_value(final_mhz)}MHz",
    ]
    if core_fmax_str is not None:
        lines.append(core_fmax_str)
    lines.extend([
        f"综合面积: {format_value(area_um2)}",
        "",
        "--- perf counters ---",
    ])

    for idx, ctr in enumerate(perf_counters):
        name = ctr.get("name")
        value = ctr.get("value")
        if name is None:
            fail(f"perf.json: perf_counter[{idx}] missing 'name'")
        if value is None:
            fail(f"perf.json: perf_counter[{idx}] '{name}' missing 'value'")
        lines.append(f"{name}: {format_value(value)}")

    return "\n".join(lines) + "\n"


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
    ap.add_argument(
        "--output-file",
        type=Path,
        help="Optional path to write the same perf report block",
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

    report = build_perf_block(perf_data, synth_data)
    sys.stdout.write(report)
    if args.output_file is not None:
        try:
            args.output_file.parent.mkdir(parents=True, exist_ok=True)
            args.output_file.write_text(report, encoding="utf-8")
        except OSError as e:
            fail(f"Cannot write {args.output_file}: {e}")


if __name__ == "__main__":
    main()
