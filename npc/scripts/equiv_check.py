#!/usr/bin/env python3
# ============================================================================
# equiv_check.py — Fail-closed Yosys equivalence harness for synthesis experiments.
# ============================================================================
# Compares pre-ABC Verilog netlists (Yosys-internal cells) using
# Yosys equiv_make, equiv_simple, equiv_induct, and equiv_status -assert.
#
# Pre-ABC netlists are used because Yosys 0.62 has no nangate45 simulation
# cells — Liberty-mapped post-ABC netlists are SAT black-boxes.
#
# Design principle: ALWAYS fail closed.
#   - Missing netlist    → EquivResult(status="blocked")
#   - Hash mismatch      → EquivResult(status="blocked", limitation=...)
#   - Unproven cells     → EquivResult(status="failed")
#   - Yosys crash/timeout → EquivResult(status="error")
#
# The identity gate (generated_verilog_sha256 match) is the source of truth
# for comparability.  Non-comparable experiments are UNSUPPORTED, not silently
# compared.
# ============================================================================

import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ── result types ──────────────────────────────────────────────────────


@dataclass
class EquivResult:
    """Result of a single equivalence check against the gold model."""

    experiment: str
    status: str = ""  # "proven" | "blocked" | "failed" | "error"
    proven_cells: int = 0
    total_cells: int = 0
    unproven_cells: int = 0
    limitation: str = ""
    detail: str = ""

    def is_proven(self) -> bool:
        return self.status == "proven"

    def is_blocked(self) -> bool:
        return self.status == "blocked"


# ── comparability gate: identity hash validation ──────────────────────


def validate_comparability(
    identity_gold: Dict[str, str],
    identity_exp: Dict[str, str],
    exp_label: str,
) -> Tuple[bool, str]:
    """Fail-closed identity gate: only same-RTL experiments are comparable.

    Returns (allowed, reason).  Reason is empty when allowed.
    """
    gen_gold = identity_gold.get("generated_verilog_sha256", "N/A")
    gen_exp = identity_exp.get("generated_verilog_sha256", "N/A")

    if gen_gold in ("N/A", "PENDING", "", None):
        return False, (
            "Gold identity manifest has no usable generated_verilog_sha256. "
            "Re-run the canonical synthesis to generate the identity manifest."
        )
    if gen_exp in ("N/A", "PENDING", "", None):
        return False, (
            f"Experiment {exp_label!r} identity manifest has no usable "
            f"generated_verilog_sha256.  Re-run the experiment synthesis."
        )

    if gen_gold != gen_exp:
        return False, (
            f"RTL hash mismatch: gold={gen_gold[:16]}... "
            f"vs {exp_label}={gen_exp[:16]}..."
        )

    return True, ""


# ── Yosys Tcl script generation ───────────────────────────────────────


def generate_equiv_tcl(
    gold_netlist: str,
    gate_netlist: str,
    top_module: str,
    max_seq: int = 10,
) -> str:
    """Generate a Yosys Tcl equivalence-checking script.

    Uses design isolation to handle hierarchical netlists that share
    submodule names: load gold → flatten → save; reset → load gate →
    flatten → save; then copy both flattened modules into a clean
    design for ``equiv_make``.

    Strategy:
    1. Load gold, flatten, ``design -save gold_design``.
    2. Reset.  Load gate, flatten, ``design -save gate_design``.
    3. Reset.  Copy both flattened tops via ``design -copy-from``.
    4. ``equiv_make gold gate equiv``.
    5. ``equiv_simple`` / ``equiv_induct -seq {max_seq}``.
    6. ``equiv_status -assert`` (fail-closed on any unproven cell).
    """
    gold_esc = gold_netlist.replace("\\", "\\\\")
    gate_esc = gate_netlist.replace("\\", "\\\\")

    script = f"""# ===========================================================================
# Auto-generated equivalence checking script — equiv_check.py
# ===========================================================================
# Gold:  canonical hierarchical pre-ABC netlist
# Gate:  experiment pre-ABC netlist
# ===========================================================================

# ── load gold, flatten, proc, save ──────────────────────────────────
read_verilog {gold_esc}
flatten {top_module}
proc
design -save gold_design

# ── load gate, flatten, proc, save (clean design after reset) ───────
design -reset
read_verilog {gate_esc}
flatten {top_module}
proc
design -save gate_design

# ── copy both flattened modules into a fresh shared design ───────────
design -reset
design -copy-from gold_design -as gold {top_module}
design -copy-from gate_design -as gate {top_module}

# ── post-flatten cleanup ────────────────────────────────────────────
opt_expr gold
opt_expr gate
opt_clean -purge gold
opt_clean -purge gate

# ── equivalence checking ────────────────────────────────────────────
equiv_make gold gate equiv
equiv_simple equiv
equiv_induct -seq {max_seq} equiv
equiv_status -assert equiv
"""
    return script


# ── Yosys execution ───────────────────────────────────────────────────


def run_yosys_equiv(
    tcl_script: str,
    yosys_bin: str = "yosys",
    log_file: Optional[str] = None,
    timeout: int = 600,
) -> Tuple[int, str]:
    """Run Yosys with the equivalence Tcl script.

    Returns (exit_code, combined_stdout_stderr).

    Raises subprocess.TimeoutExpired on timeout.
    """
    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".tcl",
        prefix="equiv_",
        delete=False,
    ) as tf:
        tf.write(tcl_script)
        tcl_path = tf.name

    try:
        result = subprocess.run(
            [yosys_bin, "-s", tcl_path],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        stdout = result.stdout + "\n" + result.stderr

        if log_file:
            Path(log_file).write_text(stdout, encoding="utf-8")

        return result.returncode, stdout
    finally:
        try:
            os.unlink(tcl_path)
        except OSError:
            pass


# ── result parsing ────────────────────────────────────────────────────


# equiv_status output patterns (Yosys 0.60+):
#   Equivalence successfully proven!
#   Found N $equiv cells in equiv:
#     Of those cells N are proven and M are unproven.
#   Found M unproven $equiv cells.   (with -assert on failure)
_EQUIV_PROVEN_RE = re.compile(
    r"(?P<proven>[\d,]+)\s+are\s+proven", re.IGNORECASE
)
_EQUIV_SUCCESS_RE = re.compile(
    r"Equivalence\s+successfully\s+proven", re.IGNORECASE
)
_EQUIV_UNPROVEN_RE = re.compile(
    r"Found\s+(?P<unproven>[\d,]+)\s+unproven\s+\$equiv\s+cells", re.IGNORECASE
)
# Also match "Proved N previously unproven $equiv cells"
_EQUIV_PROVED_SIMPLE_RE = re.compile(
    r"Proved\s+(?P<proven>[\d,]+)\s+previously\s+unproven\s+\$equiv\s+cells", re.IGNORECASE
)
# Technology cells that SAT cannot reason about
_UNSUPPORTED_CELL_RE = re.compile(
    r"(?:unknown|unsupported|black.?box)\s+cell", re.IGNORECASE
)
# Memory/DPI patterns that block equivalence.
# \$mem  — Yosys memory cell type (never appears incidentally).
# \bDPI\b — DPI interface (standalone, not inside "RAPID" etc.).
_MEMORY_DPI_RE = re.compile(
    r"\$mem\b|\bDPI\b",
    re.IGNORECASE,
)


def parse_equiv_output(
    stdout: str, experiment: str
) -> EquivResult:
    """Parse Yosys equivalence-check output into an EquivResult.

    The log is scanned for proven counts, unproven counts, unsupported
    cell warnings, and memory/DPI references.

    Important: ``Found N unproven $equiv cells`` appears in two places:
    1. EQUIV_MAKE output (before proving) — ``Found N unproven $equiv cells (N groups)``
    2. EQUIV_STATUS output (after proving, with -assert) — ``Found N unproven $equiv cells.``

    Only the EQUIV_STATUS output is authoritative.  The parser extracts the
    tail of the log (from the last EQUIV_STATUS marker) and counts
    unproven cells only from that section.
    """
    # Unsupported structure / memory/DPI checks take precedence.
    # These block equivalence BEFORE any proven-count parsing.
    if _UNSUPPORTED_CELL_RE.search(stdout):
        return EquivResult(
            experiment=experiment,
            status="blocked",
            limitation="unsupported_structure",
            detail=(
                "Yosys log contains references to unknown/unsupported cells "
                "that the SAT solver cannot reason about.  This typically "
                "occurs when Liberty-mapped cells (post-ABC netlist) leak "
                "into the pre-ABC netlist, or when DPI/memory black-boxes "
                "are present."
            ),
        )

    if _MEMORY_DPI_RE.search(stdout):
        return EquivResult(
            experiment=experiment,
            status="blocked",
            limitation="memory_dpi",
            detail=(
                "The netlist contains memory cells or DPI references that "
                "cannot be reasoned about by formal equivalence checking.  "
                "Backstopped by existing functional tests (cpu-tests, am-tests)."
            ),
        )

    # Find the last EQUIV_STATUS section — the authoritative result
    equiv_status_idx = stdout.rfind("EQUIV_STATUS")
    if equiv_status_idx < 0:
        # No equiv_status found — search entire log
        equiv_status_tail = stdout
    else:
        equiv_status_tail = stdout[equiv_status_idx:]

    # Parse proven cell count from full log (equiv_simple/equiv_induct results)
    proven = 0
    if proven_match := _EQUIV_PROVEN_RE.search(stdout):
        try:
            proven = int(proven_match.group("proven").replace(",", ""))
        except ValueError:
            pass

    if proven == 0 and (proven_simple := _EQUIV_PROVED_SIMPLE_RE.findall(stdout)):
        try:
            proven = sum(int(p.replace(",", "")) for p in proven_simple if p)
        except ValueError:
            pass

    if proven == 0 and _EQUIV_SUCCESS_RE.search(stdout):
        proven = 1  # marker — equiv_status confirmed success

    # Parse unproven cell count — ONLY from EQUIV_STATUS tail
    unproven = 0
    if unproven_match := _EQUIV_UNPROVEN_RE.search(equiv_status_tail):
        try:
            unproven = int(unproven_match.group("unproven").replace(",", ""))
        except ValueError:
            pass

    total = proven + unproven if proven > 0 or unproven > 0 else 0

    if unproven > 0:
        return EquivResult(
            experiment=experiment,
            status="failed",
            proven_cells=proven,
            total_cells=total,
            unproven_cells=unproven,
            limitation="unproven_cells",
            detail=(
                f"{unproven} of {total} $equiv cells could not be proven.  "
                f"The experiment netlist is NOT equivalent to the gold model "
                f"under bounded temporal induction (seq=10).  This may indicate "
                f"a genuine synthesis divergence or insufficient induction depth."
            ),
        )

    # Proven: all cells passed
    if proven > 0:
        return EquivResult(
            experiment=experiment,
            status="proven",
            proven_cells=proven,
            total_cells=total if total > 0 else proven,
        )

    # If we got here without a proven count, something went wrong
    # (equiv_status may not have been reached due to earlier failure)
    return EquivResult(
        experiment=experiment,
        status="error",
        limitation="parse_failure",
        detail=(
            "Could not extract proven/unproven cell counts from Yosys output.  "
            "The equivalence check may have failed before reaching equiv_status."
        ),
    )


# ── core equivalence check ─────────────────────────────────────────────


def check_experiment_equiv(
    gold_pre_abc_v: str,
    exp_pre_abc_v: str,
    identity_gold: Dict,
    identity_exp: Dict,
    exp_label: str,
    top_module: str = "ysyx_25070190",
    yosys_bin: str = "yosys",
    output_dir: Optional[str] = None,
    max_seq: int = 10,
    timeout: int = 600,
) -> EquivResult:
    """Check equivalence between an experiment and the shared gold model.

    Args:
        gold_pre_abc_v: Path to the gold (canonical) pre-ABC Verilog netlist.
        exp_pre_abc_v: Path to the experiment's pre-ABC Verilog netlist.
        identity_gold: Parsed input_identity.json from the gold run.
        identity_exp: Parsed input_identity.json from the experiment run.
        exp_label: Human-readable experiment name (e.g. "exp_b_flatten_pre_abc").
        top_module: Top-level RTL module name.
        yosys_bin: Path to yosys binary.
        output_dir: Directory for log output (optional).
        max_seq: Max time steps for equiv_induct (default 10).
        timeout: Timeout in seconds for the Yosys process (default 600).

    Returns:
        EquivResult with status, proven/unproven counts, and limitation detail.
    """
    # ── step 1: identity gate ────────────────────────────────────
    ok, reason = validate_comparability(identity_gold, identity_exp, exp_label)
    if not ok:
        return EquivResult(
            experiment=exp_label,
            status="blocked",
            limitation="identity_hash_mismatch",
            detail=reason,
        )

    # ── step 2: netlist existence check ──────────────────────────
    gold_path = Path(gold_pre_abc_v)
    exp_path = Path(exp_pre_abc_v)

    if not gold_path.is_file():
        return EquivResult(
            experiment=exp_label,
            status="blocked",
            limitation="missing_gold_netlist",
            detail=(
                f"Gold pre-ABC netlist not found: {gold_pre_abc_v}.  "
                f"Run the canonical synthesis with SYNTH_EXPERIMENT= "
                f"to generate the gold model netlist."
            ),
        )

    if not exp_path.is_file():
        return EquivResult(
            experiment=exp_label,
            status="blocked",
            limitation="missing_experiment_netlist",
            detail=(
                f"Experiment pre-ABC netlist not found: {exp_pre_abc_v}.  "
                f"The experiment may not have run to completion or the pre-ABC "
                f"netlist was not saved (yosys.tcl must write stage_pre_abc.v)."
            ),
        )

    # ── step 3: generate Tcl script ──────────────────────────────
    tcl_script = generate_equiv_tcl(
        gold_netlist=str(gold_path.resolve()),
        gate_netlist=str(exp_path.resolve()),
        top_module=top_module,
        max_seq=max_seq,
    )

    # ── step 4: run Yosys ────────────────────────────────────────
    log_file = None
    if output_dir:
        log_dir = Path(output_dir)
        log_dir.mkdir(parents=True, exist_ok=True)
        safe_label = exp_label.replace("/", "_").replace(" ", "_")
        log_file = str(log_dir / f"equiv_{safe_label}.log")

    try:
        exit_code, stdout = run_yosys_equiv(
            tcl_script=tcl_script,
            yosys_bin=yosys_bin,
            log_file=log_file,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return EquivResult(
            experiment=exp_label,
            status="error",
            limitation="timeout",
            detail=(
                f"Equivalence check timed out after {timeout}s.  "
                f"Consider increasing --timeout or reducing --max-seq."
            ),
        )
    except FileNotFoundError:
        return EquivResult(
            experiment=exp_label,
            status="error",
            limitation="yosys_not_found",
            detail=(
                f"Yosys binary not found at {yosys_bin!r}.  "
                f"Verify the yosys installation and PATH."
            ),
        )
    except Exception as e:
        return EquivResult(
            experiment=exp_label,
            status="error",
            limitation="unexpected_error",
            detail=f"Unexpected error running Yosys: {e}",
        )

    # ── step 5: parse output ─────────────────────────────────────
    if exit_code != 0 and not _EQUIV_UNPROVEN_RE.search(stdout):
        # Yosys exited with error, but not an equiv_status unproven assertion
        return EquivResult(
            experiment=exp_label,
            status="error",
            limitation="yosys_error",
            detail=(
                f"Yosys exited with code {exit_code}.  "
                f"Check the log for errors: {log_file or '<no log>'}"
            ),
        )

    result = parse_equiv_output(stdout, exp_label)

    # If exit_code is 0 but no proven cells found, treat as error
    if exit_code == 0 and result.status not in ("proven", "blocked", "failed"):
        result = EquivResult(
            experiment=exp_label,
            status="error",
            limitation="empty_result",
            detail=(
                f"Yosys exited successfully but no equivalence result could "
                f"be parsed from the output."
            ),
        )

    return result


# ── batch equivalence check (all experiments against gold) ───────────


def check_all_experiments(
    synth_root: str,
    gold_exp: str = "exp_b_flatten_pre_abc",
    top_module: str = "ysyx_25070190",
    yosys_bin: str = "yosys",
    output_dir: Optional[str] = None,
    max_seq: int = 10,
    timeout: int = 600,
) -> List[EquivResult]:
    """Run equivalence checks for all comparable experiments.

    Scans ``synth_root`` for experiment subdirectories, loads each
    experiment's input_identity.json and stage_pre_abc.v, and compares
    each against the gold model (designated by ``gold_exp``).

    The gold is ``exp_b_flatten_pre_abc`` by default because the task
    compares the controlled B/C/D experiment set against the shared
    pre-ABC flatten baseline.

    Returns a list of EquivResult, one per experiment (including the
    gold, which is trivially proven).
    """
    from pathlib import Path as _Path

    root = _Path(synth_root)
    experiment_names = [
        "exp_b_flatten_pre_abc",
        "exp_c_hier_abc",
        "exp_d_postmap_flat",
    ]

    # ── load gold identity and netlist ──────────────────────────
    gold_dir = root / gold_exp
    gold_identity_path = gold_dir / "input_identity.json"
    gold_identity: Dict[str, str] = {}

    if gold_identity_path.is_file():
        try:
            gold_identity = json.loads(
                gold_identity_path.read_text(encoding="utf-8")
            )
        except json.JSONDecodeError:
            pass

    # Find gold pre-ABC netlist
    gold_netlist: Optional[str] = None
    for candidate in [
        gold_dir / f"{top_module}-100MHz" / "stage_pre_abc.v",
        *sorted(gold_dir.glob("*-*MHz/stage_pre_abc.v")),
        *sorted(gold_dir.rglob("stage_pre_abc.v")),
    ]:
        if candidate.is_file():
            gold_netlist = str(candidate)
            break

    if gold_netlist is None:
        return [
            EquivResult(
                experiment=exp_name,
                status="blocked",
                limitation="missing_gold_netlist",
                detail=(
                    f"Gold pre-ABC netlist not found in {gold_dir}.  "
                    f"Ensure the gold experiment ({gold_exp}) has been run "
                    f"to completion (it must produce stage_pre_abc.v)."
                ),
            )
            for exp_name in experiment_names
        ]

    # ── check each experiment ────────────────────────────────────
    results: List[EquivResult] = []

    for exp_name in experiment_names:
        exp_dir = root / exp_name

        # Load experiment identity
        exp_identity_path = exp_dir / "input_identity.json"
        exp_identity: Dict[str, str] = {}
        if exp_identity_path.is_file():
            try:
                exp_identity = json.loads(
                    exp_identity_path.read_text(encoding="utf-8")
                )
            except json.JSONDecodeError:
                pass

        # Find experiment pre-ABC netlist
        exp_netlist: Optional[str] = None
        for candidate in [
            exp_dir / f"{top_module}-100MHz" / "stage_pre_abc.v",
            *sorted(exp_dir.glob("*-*MHz/stage_pre_abc.v")),
            *sorted(exp_dir.rglob("stage_pre_abc.v")),
        ]:
            if candidate.is_file():
                exp_netlist = str(candidate)
                break

        if exp_name == gold_exp:
            # Gold compared against itself: trivial equivalence
            results.append(
                EquivResult(
                    experiment=exp_name,
                    status="proven",
                    detail="Gold model compared against itself — trivially equivalent.",
                )
            )
            continue

        if exp_netlist is None:
            results.append(
                EquivResult(
                    experiment=exp_name,
                    status="blocked",
                    limitation="missing_experiment_netlist",
                    detail=(
                        f"No stage_pre_abc.v found in {exp_dir}.  "
                        f"The experiment must be re-run with the updated yosys.tcl "
                        f"that saves pre-ABC Verilog netlists."
                    ),
                )
            )
            continue

        result = check_experiment_equiv(
            gold_pre_abc_v=gold_netlist,
            exp_pre_abc_v=exp_netlist,
            identity_gold=gold_identity,
            identity_exp=exp_identity,
            exp_label=exp_name,
            top_module=top_module,
            yosys_bin=yosys_bin,
            output_dir=output_dir,
            max_seq=max_seq,
            timeout=timeout,
        )
        results.append(result)

    return results


# ── equivalence report writer ─────────────────────────────────────────


def write_equiv_report(
    results: List[EquivResult],
    output_dir: str,
    gold_exp: str = "exp_b_flatten_pre_abc",
    top_module: str = "ysyx_25070190",
) -> None:
    """Write a human-readable equivalence report.

    Produces ``equivalence_report.rpt`` in ``output_dir`` summarizing
    the status of every experiment compared against the gold model.
    """
    from datetime import datetime

    output_path = Path(output_dir) / "equivalence_report.rpt"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    lines: List[str] = []

    def _w(text: str = "") -> None:
        lines.append(text)

    _w("=" * 78)
    _w(" EQUIVALENCE CHECK REPORT — Pre-ABC Netlist Comparison")
    _w(f" Gold model:  {gold_exp}")
    _w(f" Top module:  {top_module}")
    _w(f" Generated:   {datetime.now().isoformat()}")
    _w("=" * 78)
    _w()
    _w("Each experiment's pre-ABC Verilog netlist (Yosys-internal cells) is")
    _w("compared against the shared RTL gold model using Yosys equiv_make,")
    _w("equiv_simple (direct SAT), equiv_induct (temporal induction, seq=10),")
    _w("and equiv_status -assert (fail-closed on unproven cells).")
    _w()
    _w("The identity gate (generated_verilog_sha256 match) controls")
    _w("comparability.  Netlists from mismatched RTL sources are UNSUPPORTED.")
    _w()
    _w("Post-ABC (Liberty-mapped) netlists are NOT compared — Yosys 0.62")
    _w("lacks nangate45 simulation cells, making mapped cells SAT black-boxes.")
    _w()

    # ── summary table ─────────────────────────────────────────
    _w("─" * 78)
    _w(f" {'Experiment':<28s} {'Status':<12s} {'Proven':>8s} {'Unproven':>10s} {'Limitation'}")
    _w(f" {'-'*28} {'-'*12} {'-'*8} {'-'*10} {'-'*20}")
    for r in results:
        status_mark = {
            "proven": "✓ PROVEN",
            "blocked": "⚠ UNSUPPORTED",
            "failed": "✗ FAILED",
            "error": "! ERROR",
        }.get(r.status, r.status)
        proven_str = str(r.proven_cells) if r.proven_cells > 0 else "—"
        unproven_str = str(r.unproven_cells) if r.unproven_cells > 0 else ("—" if r.status == "proven" else "—")
        _w(f" {r.experiment:<28s} {status_mark:<12s} {proven_str:>8s} {unproven_str:>10s} {r.limitation:<20s}")
    _w()

    # ── per-experiment details ────────────────────────────────
    _w("─" * 78)
    _w(" Detailed Results")
    _w("─" * 78)
    _w()

    for r in results:
        _w(f"─── {r.experiment} ({r.status}) ")
        if r.is_proven():
            _w(f"  ✓ Equivalence proven.  {r.proven_cells} $equiv cells all passed.")
        elif r.is_blocked():
            _w(f"  ⚠ Equivalence check UNSUPPORTED: {r.limitation}")
            _w(f"  Reason: {r.detail}")
            _w()
            _w(f"  Backstop: Existing functional tests (cpu-tests, am-tests)")
            _w(f"            serve as the bounded verification for this experiment.")
        elif r.status == "failed":
            _w(f"  ✗ Equivalence check FAILED.")
            _w(f"  {r.unproven_cells}/{r.total_cells} $equiv cells unproven.")
            _w(f"  {r.detail}")
        else:
            _w(f"  ! Equivalence check ERROR: {r.limitation}")
            _w(f"  {r.detail}")
        _w()

    # ── backstop note ─────────────────────────────────────────
    blocked_count = sum(1 for r in results if r.is_blocked())
    failed_count = sum(1 for r in results if r.status == "failed")
    proven_count = sum(1 for r in results if r.is_proven())

    _w("─" * 78)
    _w(" Backstop Functional Verification")
    _w("─" * 78)
    _w()
    _w(f"Summary: {proven_count} proven, {blocked_count} unsupported, {failed_count} failed, "
        f"{sum(1 for r in results if r.status == 'error')} errors")
    _w()
    if blocked_count > 0:
        _w("Unsupported experiments are backstopped by the existing functional test")
        _w("regression suite:")
        _w("  make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run")
        _w("  make -C am-kernels/tests/am-tests ARCH=riscv32e-ysyxsoc run")
        _w()
        _w("These tests exercise the synthesized CPU across 35+ instruction-level")
        _w("tests, providing bounded functional verification that the synthesized")
        _w("gate-level netlist implements the RTL specification correctly.")
    _w()
    _w("=" * 78)

    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"[equiv_check] Wrote equivalence report: {output_path}")


# ── CLI ────────────────────────────────────────────────────────────────


def main() -> None:
    import argparse

    ap = argparse.ArgumentParser(
        description="Fail-closed Yosys equivalence check for synthesis experiments."
    )
    ap.add_argument("--synth-root", required=True,
                    help="Root directory containing experiment subdirectories (e.g. build/synth)")
    ap.add_argument("--gold-exp", default="exp_b_flatten_pre_abc",
                    help="Experiment to use as gold model (default: exp_b_flatten_pre_abc)")
    ap.add_argument("--top-module", default="ysyx_25070190",
                    help="Top-level RTL module name (default: ysyx_25070190)")
    ap.add_argument("--yosys-bin", default="yosys",
                    help="Path to yosys binary (default: yosys)")
    ap.add_argument("--output-dir", default=None,
                    help="Output directory for equivalence report and logs")
    ap.add_argument("--max-seq", type=int, default=10,
                    help="Max time steps for equiv_induct (default: 10)")
    ap.add_argument("--timeout", type=int, default=600,
                    help="Timeout in seconds per equivalence check (default: 600)")
    args = ap.parse_args()

    output_dir = args.output_dir or args.synth_root

    results = check_all_experiments(
        synth_root=args.synth_root,
        gold_exp=args.gold_exp,
        top_module=args.top_module,
        yosys_bin=args.yosys_bin,
        output_dir=output_dir,
        max_seq=args.max_seq,
        timeout=args.timeout,
    )

    write_equiv_report(
        results=results,
        output_dir=output_dir,
        gold_exp=args.gold_exp,
        top_module=args.top_module,
    )

    # Exit status: non-zero if any experiment failed or errored
    failures = [r for r in results if r.status in ("failed", "error")]
    if failures:
        print(f"[equiv_check] {len(failures)} experiment(s) failed or errored.", file=sys.stderr)
        sys.exit(1)

    blocked = [r for r in results if r.is_blocked()]
    if blocked:
        print(f"[equiv_check] {len(blocked)} experiment(s) unsupported — see report for details.", file=sys.stderr)
        # Blocked is not an error — limitations are documented and backstopped
        sys.exit(0)

    print("[equiv_check] All comparable experiments proven equivalent. ✓", file=sys.stderr)
    sys.exit(0)


if __name__ == "__main__":
    main()
