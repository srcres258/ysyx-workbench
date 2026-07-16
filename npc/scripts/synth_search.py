#!/usr/bin/env python3
"""Deterministic STA frequency search driver for yosys-sta.

Binary search over integer MHz.  Calls `make -C yosys-sta syn sta` for each
candidate frequency, parses the resulting timing report, and returns the
highest setup-clean integer MHz.

Pass criterion:  WNS >= 0  (zero or positive slack).
Search is fully deterministic — identical bounds always produce the same result.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path
from typing import Optional

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from report_parser import parse_sta_report, derive_max_frequency  # noqa: E402


def _yosys_result_dir(probe_dir: Path, design: str, freq_mhz: int) -> Path:
    """Resolve the actual yosys-sta output subdirectory for a probe.

    yosys-sta's Makefile sets RESULT_DIR = $(O)/$(DESIGN)-$(CLK_FREQ_MHZ)MHz,
    so reports land one level deeper than the probe directory.
    """
    return probe_dir / f"{design}-{freq_mhz}MHz"


DEFAULT_LOW_MHZ   = 1
DEFAULT_HIGH_MHZ  = 500
WNS_PASS_THRESHOLD = 0.0


def run_synthesis(
    yosys_sta_home: Path,
    output_dir: Path,
    design: str,
    sdc_file: Path,
    clk_port_name: str,
    clk_freq_mhz: int,
    rtl_files: str,
    timeout_s: int = 7200,
) -> int:
    """Run `make syn sta` in yosys-sta for a single frequency probe."""
    env = os.environ.copy()
    env["CLK_FREQ_MHZ"]  = str(clk_freq_mhz)
    env["CLK_PORT_NAME"]  = clk_port_name

    result = subprocess.run(
        [
            "make", "-C", str(yosys_sta_home),
            "syn", "sta",
            f"DESIGN={design}",
            f"SDC_FILE={sdc_file}",
            f"CLK_FREQ_MHZ={clk_freq_mhz}",
            f"CLK_PORT_NAME={clk_port_name}",
            f"O={output_dir}",
            f"RTL_FILES={rtl_files}",
        ],
        capture_output=True,
        text=True,
        timeout=timeout_s,
        env=env,
    )

    if result.returncode != 0:
        print(
            f"[Search] WARNING: synthesis failed at {clk_freq_mhz} MHz "
            f"(exit {result.returncode})",
            file=sys.stderr,
        )
    return result.returncode


def check_timing(probe_dir: Path, design: str, freq_mhz: int) -> float:
    """Parse the STA report inside the yosys-sta result subdirectory."""
    result_dir = _yosys_result_dir(probe_dir, design, freq_mhz)
    rpt_path = result_dir / f"{design}.rpt"
    result = parse_sta_report(rpt_path)
    return result["wns"]


# ── binary search ──────────────────────────────────────────────────

def binary_search_freq(
    yosys_sta_home: Path,
    output_dir_base: Path,
    design: str,
    sdc_file: Path,
    clk_port_name: str,
    rtl_files: str,
    low_mhz: int = DEFAULT_LOW_MHZ,
    high_mhz: int = DEFAULT_HIGH_MHZ,
    resume_mhz: Optional[int] = None,
    keep_all_probes: bool = False,
    timeout_s: int = 7200,
) -> tuple[int, int]:
    """Deterministically binary-search for highest setup-clean integer MHz.

    Returns:
        (max_mhz, probe_mhz) — derived max frequency and the actual probe
        frequency that passed timing.

    Args:
        yosys_sta_home:   Path to yosys-sta/ root
        output_dir_base:  Base output directory (probe results go under
                          `<output_dir_base>/probe_<freq>MHz/`)
        design:           Top module name
        sdc_file:         SDC constraint file
        clk_port_name:    Clock port name in RTL
        rtl_files:        Space-separated RTL source paths
        low_mhz:          Search lower bound (inclusive)
        high_mhz:         Search upper bound (inclusive)
        resume_mhz:       If set, start search from this candidate (bias hint)
        keep_all_probes:  Retain all probe results (default: clean up intermediates)
        timeout_s:        Per-probe timeout

    Returns:
        Maximum integer MHz with WNS >= 0 for the given RTL/PDK.

    Raises:
        RuntimeError:   no frequency in [low_mhz, high_mhz] passes timing
    """
    lo = low_mhz
    hi = high_mhz

    last_pass: Optional[int] = None

    if resume_mhz is not None:
        resume_mhz = max(lo, min(hi, resume_mhz))
        probe_dir = output_dir_base / f"probe_{resume_mhz}MHz"
        probe_dir.mkdir(parents=True, exist_ok=True)
        rc = run_synthesis(
            yosys_sta_home, probe_dir, design, sdc_file,
            clk_port_name, resume_mhz, rtl_files, timeout_s,
        )
        if rc == 0:
            try:
                wns = check_timing(probe_dir, design, resume_mhz)
                if wns >= WNS_PASS_THRESHOLD:
                    lo = resume_mhz
                    last_pass = resume_mhz
                else:
                    hi = resume_mhz - 1
            except Exception as e:
                print(f"[Search] Error parsing probe at {resume_mhz} MHz: {e}", file=sys.stderr)
                hi = resume_mhz - 1
        else:
            hi = resume_mhz - 1
            last_fail = resume_mhz

    while lo <= hi:
        mid = (lo + hi) // 2
        probe_dir = output_dir_base / f"probe_{mid}MHz"
        probe_dir.mkdir(parents=True, exist_ok=True)

        print(
            f"[Search] Probing {mid} MHz  (range [{lo}, {hi}]) ...",
            file=sys.stderr,
        )

        rc = run_synthesis(
            yosys_sta_home, probe_dir, design, sdc_file,
            clk_port_name, mid, rtl_files, timeout_s,
        )

        if rc != 0:
            hi = mid - 1
            if not keep_all_probes:
                shutil.rmtree(probe_dir, ignore_errors=True)
            continue

        try:
            wns = check_timing(probe_dir, design, mid)
        except Exception as e:
            print(f"[Search] Error parsing probe at {mid} MHz: {e}", file=sys.stderr)
            hi = mid - 1
            if not keep_all_probes:
                shutil.rmtree(probe_dir, ignore_errors=True)
            continue

        if wns >= WNS_PASS_THRESHOLD:
            lo = mid + 1
            last_pass = mid
        else:
            hi = mid - 1

        if not keep_all_probes and mid != last_pass:
            shutil.rmtree(probe_dir, ignore_errors=True)

    if last_pass is None:
        raise RuntimeError(
            f"No frequency in [{low_mhz}, {high_mhz}] MHz passes "
            f"setup timing (WNS >= 0). The design may be unrouteable "
            f"at any practical frequency with this PDK."
        )

    best_probe_dir = output_dir_base / f"probe_{last_pass}MHz"
    try:
        wns = check_timing(best_probe_dir, design, last_pass)
        max_mhz = derive_max_frequency(wns, last_pass)
    except Exception:
        max_mhz = last_pass

    if not keep_all_probes:
        for child in output_dir_base.iterdir():
            if (
                child.is_dir()
                and child.name.startswith("probe_")
                and child != best_probe_dir
            ):
                shutil.rmtree(child, ignore_errors=True)

    return max_mhz, last_pass


# ── CLI ────────────────────────────────────────────────────────────

def _parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Deterministic STA frequency search for yosys-sta"
    )
    ap.add_argument(
        "--yosys-sta-home", required=True, type=Path,
        help="Path to yosys-sta/ root",
    )
    ap.add_argument(
        "--output-dir", required=True, type=Path,
        help="Base output directory for probe results",
    )
    ap.add_argument(
        "--design", default="ysyx_25070190",
        help="Top module name",
    )
    ap.add_argument(
        "--sdc-file", required=True, type=Path,
        help="SDC constraint file path",
    )
    ap.add_argument(
        "--clk-port", default="clock",
        help="Clock port name in RTL",
    )
    ap.add_argument(
        "--rtl-files", default="",
        help="Space-separated RTL source file paths",
    )
    ap.add_argument(
        "--low", type=int, default=DEFAULT_LOW_MHZ,
        help=f"Lower bound MHz (default: {DEFAULT_LOW_MHZ})",
    )
    ap.add_argument(
        "--high", type=int, default=DEFAULT_HIGH_MHZ,
        help=f"Upper bound MHz (default: {DEFAULT_HIGH_MHZ})",
    )
    ap.add_argument(
        "--resume", type=int, default=None,
        help="Bias hint: start search from this MHz",
    )
    ap.add_argument(
        "--keep-probes", action="store_true",
        help="Retain all intermediate probe results",
    )
    ap.add_argument(
        "--timeout", type=int, default=7200,
        help="Per-probe timeout in seconds",
    )
    # Mock mode: for testing without real yosys/iEDA
    ap.add_argument(
        "--mock", action="store_true",
        help=argparse.SUPPRESS,
    )
    ap.add_argument(
        "--mock-max-mhz", type=int, default=100,
        help=argparse.SUPPRESS,
    )
    return ap.parse_args()


def main() -> None:
    args = _parse_args()

    if args.mock:
        _mock_search(args)
        return

    max_mhz, probe_mhz = binary_search_freq(
        yosys_sta_home=args.yosys_sta_home,
        output_dir_base=args.output_dir,
        design=args.design,
        sdc_file=args.sdc_file,
        clk_port_name=args.clk_port,
        rtl_files=args.rtl_files,
        low_mhz=args.low,
        high_mhz=args.high,
        resume_mhz=args.resume,
        keep_all_probes=args.keep_probes,
        timeout_s=args.timeout,
    )

    print(f"MAX_FREQ_MHZ={max_mhz}")
    print(f"PROBE_MHZ={probe_mhz}")
    result = {
        "max_freq_mhz": max_mhz,
        "search_low": args.low,
        "search_high": args.high,
    }
    print(f"RESULT_JSON={json.dumps(result)}")


def _mock_search(args: argparse.Namespace) -> None:
    """Simulate frequency search with synthetic yosys-sta reports for testing."""
    mock_max = args.mock_max_mhz
    result_dir = args.output_dir
    result_dir.mkdir(parents=True, exist_ok=True)

    lo, hi = args.low, args.high
    last_pass = None

    while lo <= hi:
        mid = (lo + hi) // 2
        probe_dir = result_dir / f"probe_{mid}MHz"
        result_subdir = _yosys_result_dir(probe_dir, args.design, mid)
        result_subdir.mkdir(parents=True, exist_ok=True)

        target_period_ns = 1000.0 / mid
        if mid <= mock_max:
            wns = target_period_ns * 0.05
            tns = 0.0
        else:
            period_excess = (1000.0 / mock_max) - target_period_ns
            wns = period_excess
            tns = wns * 3.0

        rpt_content = textwrap.dedent(f"""\
            =============================================================================
              Static Timing Analysis Report
            =============================================================================
            Design: {args.design}
            Clock:  core_clock  ({mid} MHz)
            PDK:    nangate45
            =============================================================================
            ...
            Start-of-path
            ...
            wns                     {wns:.4f}
            tns                     {tns:.4f}
            ...
            End-of-path
            =============================================================================
        """)
        (result_subdir / f"{args.design}.rpt").write_text(rpt_content)

        stat_content = textwrap.dedent(f"""\
            === {args.design} ===
               Number of wires:               12345
               Number of wire bits:           54321
               Number of public wires:        1234
               Number of public wire bits:    4321
               Number of cells:               9876
               Chip area for top module '\\{args.design}':  12345.678
        """)
        (result_subdir / "synth_stat.txt").write_text(stat_content)

        if mid <= mock_max:
            lo = mid + 1
            last_pass = mid
        else:
            hi = mid - 1

    if last_pass is None:
        raise RuntimeError(
            f"[Mock] No frequency in [{args.low}, {args.high}] MHz passes."
        )

    best_dir = result_dir / f"probe_{last_pass}MHz"
    best_wnss = check_timing(best_dir, args.design, last_pass)
    max_mhz = derive_max_frequency(best_wnss, last_pass)

    if not args.keep_probes:
        for child in result_dir.iterdir():
            if (
                child.is_dir()
                and child.name.startswith("probe_")
                and child != best_dir
            ):
                shutil.rmtree(child, ignore_errors=True)

    print(f"MAX_FREQ_MHZ={max_mhz}")
    print(f"PROBE_MHZ={last_pass}")
    result = {
        "max_freq_mhz": max_mhz,
        "search_low": args.low,
        "search_high": args.high,
    }
    print(f"RESULT_JSON={json.dumps(result)}")


if __name__ == "__main__":
    main()
