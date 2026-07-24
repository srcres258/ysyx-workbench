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
from typing import Any, Dict, List, NoReturn, Optional


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


# ── Frozen baseline: the first 26 counter names in their exact order ──────
# These 26 positions are the append-only contract baseline (T1).
# T3 register-context counters (12) begin at index 26 — they extend the
# schema but are NOT part of the frozen baseline enforced by name.
# NEVER rename, reorder, or delete entries from this list.
BASELINE_COUNTER_NAMES: List[str] = [
    "core.cycle",
    "core.instret",
    "core.busy.cycle",
    "core.stall.cycle",
    "inst.class.alu.count",
    "inst.class.load.count",
    "inst.class.store.count",
    "inst.class.branch.count",
    "inst.class.jal.count",
    "inst.class.jalr.count",
    "inst.class.csr.count",
    "inst.class.muldiv.count",
    "state.fetch.cycle",
    "state.decode.cycle",
    "state.execute.cycle",
    "state.memory.cycle",
    "state.writeback.cycle",
    "stall.ifetch.wait_resp.cycle",
    "stall.mem.wait_resp.cycle",
    "stall.mem.req_blocked.cycle",
    "stall.structural.shared_mem.cycle",
    "stall.muldiv.busy.cycle",
    "mem.load.req.count",
    "mem.store.req.count",
    "mem.mmio.req.count",
    "trap.exception.count",
]


def _validate_counter_fields(
    perf_counters: List[Dict[str, Any]],
) -> None:
    """Validate each counter entry has required fields; fail-closed.

    Checks every counter for non-null name, value, and that
    the baseline names/order are preserved for positions 0–25.
    Also rejects duplicate names across the entire counter set.

    Note: 'unit' is NOT mandatory — legacy perf JSON may omit it.
    """
    seen: set = set()

    for idx, ctr in enumerate(perf_counters):
        name = ctr.get("name")
        value = ctr.get("value")

        if name is None:
            fail(f"perf.json: perf_counter[{idx}] missing 'name'")
        if value is None:
            fail(f"perf.json: perf_counter[{idx}] ('{name}') missing 'value'")

        # Baseline name-order lock (positions 0–25)
        if idx < len(BASELINE_COUNTER_NAMES):
            expected = BASELINE_COUNTER_NAMES[idx]
            if name != expected:
                fail(
                    f"perf.json: perf_counter[{idx}] name is '{name}' "
                    f"but expected '{expected}' — baseline counter names "
                    f"are frozen and must not be renamed or reordered"
                )

        # Duplicate-name detection (across ALL counters)
        if name in seen:
            fail(
                f"perf.json: duplicate counter name '{name}' "
                f"at index {idx}"
            )
        seen.add(name)


def _run_strict_checks(
    perf_counters: List[Dict[str, Any]],
) -> None:
    """Run strict closure checks on counter values. Fail-closed on any violation.

    Checks:
        - sum(inst.class.*) == core.instret (within slack of 1)
        - sum(state.*.cycle) == core.busy.cycle (within slack of 5)
        - sum(ifetch.phase.*) == state.fetch.cycle (within slack of 2)
        - sum(lsu.load.byte+half+word) == sum(lsu.load.aligned+unaligned)
        - sum(lsu.store.byte+half+word) == sum(lsu.store.aligned+unaligned)
        - gpr.write.suppressed_x0 <= gpr.write.total
        - csr.read.concurrent_3port <= csr.read.concurrent_2port
    """
    by_name: Dict[str, int] = {}
    for ctr in perf_counters:
        by_name[ctr["name"]] = ctr.get("value", 0)

    def counter(name: str) -> int:
        if name not in by_name:
            fail(f"strict check: counter '{name}' not found in perf_counters")
        return by_name[name]

    def check_close(label: str, actual: int, expected: int, slack: int = 1) -> None:
        delta = actual - expected
        if delta < 0 or delta > slack:
            fail(
                f"strict check: {label} -- sum={actual} expected={expected} "
                f"delta={delta} (max slack={slack})"
            )

    def check_le(label: str, a: int, b: int) -> None:
        if a > b:
            fail(f"strict check: {label} -- {a} > {b}")

    # 1. Instruction class closure
    inst_classes = [
        "inst.class.alu.count",
        "inst.class.load.count",
        "inst.class.store.count",
        "inst.class.branch.count",
        "inst.class.jal.count",
        "inst.class.jalr.count",
        "inst.class.csr.count",
        "inst.class.muldiv.count",
    ]
    inst_sum = sum(counter(n) for n in inst_classes)
    check_close("inst.class sum == instret", inst_sum, counter("core.instret"), slack=1)

    # 2. Stage cycle closure
    stages = [
        "state.fetch.cycle",
        "state.decode.cycle",
        "state.execute.cycle",
        "state.memory.cycle",
        "state.writeback.cycle",
    ]
    state_sum = sum(counter(n) for n in stages)
    check_close("state sum == core.busy.cycle", state_sum, counter("core.busy.cycle"), slack=5)

    # 3. IFetch phase closure
    # Exclude accept_pc (idle): state.fetch.cycle counts only working cycles (state≠s_idle).
    ifetch_phases = [
        "ifetch.phase.prepare_request.cycle",
        "ifetch.phase.request_blocked.cycle",
        "ifetch.phase.wait_response.cycle",
        "ifetch.phase.response_buffered.cycle",
        "ifetch.phase.output_blocked.cycle",
    ]
    ifetch_phase_sum = sum(counter(n) for n in ifetch_phases)
    check_close("ifetch phase sum == state.fetch.cycle", ifetch_phase_sum, counter("state.fetch.cycle"), slack=2)

    # 4. LSU load closure: size sum == aligned + unaligned
    lsu_load_sizes = [
        "lsu.load.byte.count",
        "lsu.load.half.count",
        "lsu.load.word.count",
    ]
    lsu_load_align = ["lsu.load.aligned.count", "lsu.load.unaligned.count"]
    check_close("lsu load size-sum == aligned+unaligned",
                sum(counter(n) for n in lsu_load_sizes),
                sum(counter(n) for n in lsu_load_align),
                slack=1)

    # 5. LSU store closure
    lsu_store_sizes = [
        "lsu.store.byte.count",
        "lsu.store.half.count",
        "lsu.store.word.count",
    ]
    lsu_store_align = ["lsu.store.aligned.count", "lsu.store.unaligned.count"]
    check_close("lsu store size-sum == aligned+unaligned",
                sum(counter(n) for n in lsu_store_sizes),
                sum(counter(n) for n in lsu_store_align),
                slack=1)

    # 6. GPR write suppression invariant
    check_le("gpr.write.suppressed_x0 <= gpr.write.total",
             counter("gpr.write.suppressed_x0.count"),
             counter("reg.gpr.write.count") + counter("gpr.write.suppressed_x0.count"))

    # 7. CSR concurrent read invariant
    check_le("csr concurrent_3port <= concurrent_2port",
             counter("csr.read.concurrent_3port.count"),
             counter("csr.read.concurrent_2port.count"))

    # 8. Execution concurrency: all retired instructions must be classified
    conc_vals = [
        counter("ex.concurrency.alu_only.count"),
        counter("ex.concurrency.pc_only.count"),
        counter("ex.concurrency.both.count"),
    ]
    check_close("concurrency sum == instret", sum(conc_vals), counter("core.instret"), slack=1)

    print("[Perf] Strict closure checks PASSED", file=sys.stderr)


def build_perf_block(
    perf_data: Dict[str, Any],
    synth_data: Dict[str, Any],
    strict: bool = False,
) -> str:
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
    if len(perf_counters) < len(BASELINE_COUNTER_NAMES):
        fail(
            f"perf.json: expected at least {len(BASELINE_COUNTER_NAMES)} "
            f"perf counters, got {len(perf_counters)}"
        )

    _validate_counter_fields(perf_counters)

    if strict:
        _run_strict_checks(perf_counters)

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
        name = ctr["name"]
        value = ctr["value"]
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
    ap.add_argument(
        "--strict",
        action="store_true",
        help="Enable strict closure checks on counter relationships",
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

    report = build_perf_block(perf_data, synth_data, strict=args.strict)
    sys.stdout.write(report)
    if args.output_file is not None:
        try:
            args.output_file.parent.mkdir(parents=True, exist_ok=True)
            args.output_file.write_text(report, encoding="utf-8")
        except OSError as e:
            fail(f"Cannot write {args.output_file}: {e}")


if __name__ == "__main__":
    main()
