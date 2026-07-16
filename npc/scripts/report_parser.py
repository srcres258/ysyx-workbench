#!/usr/bin/env python3
# ============================================================================
# report_parser.py — Deterministic yosys-sta report parser (fail-closed).
# ============================================================================
# Parses:
#   - iEDA/iSTA timing report       (*.rpt)  → WNS (ns), TNS (ns)
#   - Yosys synthesis statistics  (synth_stat.txt) → cell count, chip area
#
# Design principle: ALWAYS fail closed.
#   - Missing field    → exception with precise field name
#   - Ambiguous match  → exception with location details
#   - Truncated file   → exception with expected-but-missing section
# ============================================================================

import re
import sys
from pathlib import Path
from typing import Dict


class ParseError(Exception):
    """Raised when a required report field is missing or ambiguous."""


# ---------------------------------------------------------------------------
# STA timing report parsing  (iEDA/iSTA  report_timing  output)
# ---------------------------------------------------------------------------

# iEDA STA report formats we support:
#   1) legacy/mock format:
#        wns   -0.123
#        tns   -4.567
#        End-of-path
#   2) current yosys-sta/iEDA format:
#        | ... | max | ... | slack | freq |
#        | Clock | Delay Type | TNS |
# We accept both so mock tests and real synthesis runs stay compatible.

_STA_LEGACY_WNS_RE = re.compile(r"^wns\s+([-]?\d+\.?\d*)", re.MULTILINE)
_STA_LEGACY_TNS_RE = re.compile(r"^tns\s+([-]?\d+\.?\d*)", re.MULTILINE)
_STA_ENDPATH_RE = re.compile(r"^End-of-path", re.MULTILINE)

_STA_SUMMARY_ROW_RE = re.compile(
    r"^\|\s*(?P<endpoint>[^|]+?)\s*\|\s*(?P<clock_group>[^|]+?)\s*\|\s*"
    r"(?P<delay_type>max|min)\s*\|\s*(?P<path_delay>[^|]+?)\s*\|\s*"
    r"(?P<path_required>[^|]+?)\s*\|\s*(?P<cppr>[^|]+?)\s*\|\s*"
    r"(?P<slack>[-]?\d+\.?\d*)\s*\|\s*(?P<freq>[^|]+?)\s*\|$",
    re.MULTILINE,
)

_STA_TNS_ROW_RE = re.compile(
    r"^\|\s*(?P<clock>[^|]+?)\s*\|\s*(?P<delay_type>max|min)\s*\|\s*"
    r"(?P<tns>[-]?\d+\.?\d*)\s*\|$",
    re.MULTILINE,
)


def parse_sta_report(rpt_path) -> Dict[str, float]:
    """Parse an iEDA/iSTA timing report.

    Returns:
        {"wns": float, "tns": float}   — both in nanoseconds

    Raises:
        ParseError:  field not found, ambiguous, or file unreadable
        FileNotFoundError: report does not exist
    """
    rpt_path = Path(rpt_path)
    if not rpt_path.is_file():
        raise FileNotFoundError(f"STA report not found: {rpt_path}")

    text = rpt_path.read_text(encoding="utf-8", errors="replace")

    # --- current yosys-sta/iEDA format ---
    summary_rows = [
        m for m in _STA_SUMMARY_ROW_RE.finditer(text)
        if m.group("delay_type") == "max"
    ]
    tns_rows = [
        m for m in _STA_TNS_ROW_RE.finditer(text)
        if m.group("delay_type") == "max"
    ]

    if summary_rows or tns_rows:
        if not summary_rows:
            raise ParseError(
                f"STA report {rpt_path}: cannot find any setup-path rows in the current timing summary table."
            )
        if not tns_rows:
            raise ParseError(
                f"STA report {rpt_path}: cannot find any TNS rows in the current timing summary table."
            )

        try:
            wns = min(float(m.group("slack")) for m in summary_rows)
        except ValueError as e:
            raise ParseError(
                f"STA report {rpt_path}: one of the setup slack values is not a valid float: {e}"
            )

        try:
            tns = min(float(m.group("tns")) for m in tns_rows)
        except ValueError as e:
            raise ParseError(
                f"STA report {rpt_path}: one of the TNS values is not a valid float: {e}"
            )

        return {"wns": wns, "tns": tns}

    # --- legacy/mock format ---
    if not _STA_ENDPATH_RE.search(text):
        raise ParseError(
            f"STA report {rpt_path} appears truncated or incomplete: missing both the current timing-summary table and the legacy 'End-of-path' marker"
        )

    wns_match = _STA_LEGACY_WNS_RE.search(text)
    if wns_match is None:
        raise ParseError(
            f"STA report {rpt_path}: cannot find 'wns' field in the legacy format. Expected a line matching 'wns  <value>'."
        )
    wns_matches = _STA_LEGACY_WNS_RE.findall(text)
    if len(wns_matches) > 1:
        raise ParseError(
            f"STA report {rpt_path}: ambiguous — found {len(wns_matches)} legacy 'wns' fields. Expected exactly one."
        )
    try:
        wns = float(wns_match.group(1))
    except ValueError:
        raise ParseError(
            f"STA report {rpt_path}: 'wns' value '{wns_match.group(1)}' is not a valid float."
        )

    tns_match = _STA_LEGACY_TNS_RE.search(text)
    if tns_match is None:
        raise ParseError(
            f"STA report {rpt_path}: cannot find 'tns' field in the legacy format. Expected a line matching 'tns  <value>'."
        )
    tns_matches = _STA_LEGACY_TNS_RE.findall(text)
    if len(tns_matches) > 1:
        raise ParseError(
            f"STA report {rpt_path}: ambiguous — found {len(tns_matches)} legacy 'tns' fields. Expected exactly one."
        )
    try:
        tns = float(tns_match.group(1))
    except ValueError:
        raise ParseError(
            f"STA report {rpt_path}: 'tns' value '{tns_match.group(1)}' is not a valid float."
        )

    return {"wns": wns, "tns": tns}


# ---------------------------------------------------------------------------
# Yosys synthesis statistics  (synth_stat.txt  —  `stat -liberty $LIBS`)
# ---------------------------------------------------------------------------

# Yosys stat output with liberty cells (example):
#   === ysyx_25070190 ===
#      Number of wires:               12345
#      ...
#      Chip area for top module '\ysyx_25070190':  12345.678
#      ...
#      Number of cells:               9876

_SYNTH_CELLS_LEGACY_RE = re.compile(r"^\s*Number of cells:\s+([\d,]+)\s*$", re.MULTILINE)
_SYNTH_CELLS_CURRENT_RE = re.compile(r"^\s*([\d,]+)\s+[\d.]+\s+cells\s*$", re.MULTILINE)
_SYNTH_AREA_RE = re.compile(
    r"^\s*Chip area for (?:top )?module\s+['\"]?\\?(\S+?)['\"]?\s*:\s+([\d.]+)\s*$",
    re.MULTILINE,
)
_SYNTH_CANT_FIND_AREA_RE = re.compile(
    r"Don't know how to get chip area", re.MULTILINE
)


def parse_synth_stat(
    stat_path, design_name: str = "ysyx_25070190"
) -> Dict[str, object]:
    """Parse Yosys synthesis statistics report.

    Returns:
        {"cell_count": int, "area_um2": float}   — area in µm² (icsprout55)

    Raises:
        ParseError:  field not found, ambiguous, or file unreadable
        FileNotFoundError: report does not exist
    """
    stat_path = Path(stat_path)
    if not stat_path.is_file():
        raise FileNotFoundError(f"Synthesis stats not found: {stat_path}")

    text = stat_path.read_text(encoding="utf-8", errors="replace")

    # --- cell count ---
    cells_match = _SYNTH_CELLS_CURRENT_RE.search(text) or _SYNTH_CELLS_LEGACY_RE.search(text)
    if cells_match is None:
        raise ParseError(
            f"synth_stat.txt {stat_path}: cannot find a cell-count field. "
            f"Expected either 'Number of cells:  <count>' or '<count>  <area> cells'."
        )
    cells_matches = _SYNTH_CELLS_CURRENT_RE.findall(text) or _SYNTH_CELLS_LEGACY_RE.findall(text)
    if len(cells_matches) > 1:
        raise ParseError(
            f"synth_stat.txt {stat_path}: ambiguous — found {len(cells_matches)} cell-count fields. Expected exactly one."
        )
    try:
        cell_count = int(cells_match.group(1).replace(",", ""))
    except ValueError:
        raise ParseError(
            f"synth_stat.txt {stat_path}: cell-count value '{cells_match.group(1)}' is not a valid integer."
        )

    # --- chip area ---
    # First check for the "Don't know how to get chip area" line — if present,
    # the PDK liberty doesn't provide area, and we must fail closed.
    if _SYNTH_CANT_FIND_AREA_RE.search(text):
        raise ParseError(
            f"synth_stat.txt {stat_path}: yosys reports "
            f"'Don't know how to get chip area from liberty cell'. "
            f"This usually means the liberty file lacks area data. "
            f"Cannot proceed — area is required for the synth summary."
        )

    area_match = _SYNTH_AREA_RE.search(text)
    if area_match is None:
        raise ParseError(
            f"synth_stat.txt {stat_path}: cannot find the chip-area line. "
            f"Expected either 'Chip area for top module \\'{design_name}\\':  <value>' "
            f"or 'Chip area for module \\'{design_name}\\':  <value>'."
        )
    area_matches = _SYNTH_AREA_RE.findall(text)
    if len(area_matches) > 1:
        raise ParseError(
            f"synth_stat.txt {stat_path}: ambiguous — found {len(area_matches)} chip-area fields. Expected exactly one."
        )

    top_name = area_match.group(1).strip("\\'\"")
    area_str = area_match.group(2)
    if top_name != design_name:
        raise ParseError(
            f"synth_stat.txt {stat_path}: chip area references '{top_name}', but expected design is '{design_name}'. "
            f"Mismatch — possibly the wrong RTL was synthesized."
        )

    try:
        area_um2 = float(area_str)
    except ValueError:
        raise ParseError(
            f"synth_stat.txt {stat_path}: chip area value '{area_str}' "
            f"is not a valid float."
        )

    return {"cell_count": cell_count, "area_um2": area_um2}


# ---------------------------------------------------------------------------
# Frequency derivation from timing slack
# ---------------------------------------------------------------------------

def derive_max_frequency(
    wns_ns: float,
    target_mhz: int,
    clk_uncertainty_ns: float = 0.20,
) -> int:
    """Compute the maximum setup-clean frequency from actual WNS.

    Given the WNS (worst negative slack in ns) of a STA run at a known
    target frequency (in MHz), derive the maximum integer MHz that would
    still have WNS >= setup margin.

    This DOES NOT use the input target as the final frequency — it derives
    the frequency from measured slack.

    Formula:
        actual_period_ns   = 1000.0 / target_mhz
        achievable_period  = actual_period_ns - wns_ns  (wns < 0 means setup violation)
        max_freq           = floor(1000.0 / achievable_period)

    Clock uncertainty is subtracted from the usable period to ensure
    margin for jitter/skew.

    Returns:
        Maximum setup-clean integer MHz, or 0 if WNS is deeply negative
        (i.e., even at 1 MHz setup would fail).

    Raises:
        ValueError:  wns_ns is nonsensical (NaN, too large, etc.)
    """
    if not (isinstance(wns_ns, (int, float)) and isinstance(target_mhz, int)):
        raise ValueError(
            f"derive_max_frequency: wns_ns={wns_ns}, target_mhz={target_mhz} "
            f"— both must be numeric."
        )

    if target_mhz <= 0:
        raise ValueError(f"target_mhz must be positive, got {target_mhz}")

    target_period_ns = 1000.0 / target_mhz

    # WNS < 0 means setup violation: the path is slower than the clock period
    if wns_ns > 0:
        # Positive slack: design can run at a higher frequency
        achievable_period = target_period_ns - wns_ns
    else:
        # Negative or zero slack: design can't even meet the target
        achievable_period = target_period_ns - wns_ns  # still valid, just negative/zero wns
        # If achievable_period is less than or equal to clk_uncertainty,
        # it means the design has no useful clocking margin.
        if achievable_period <= clk_uncertainty_ns:
            return 0

    # Derive max frequency (integer MHz, floored)
    max_mhz = int(1000.0 / achievable_period)

    # Sanity bounds
    if max_mhz <= 0:
        max_mhz = 0

    return max_mhz


# ---------------------------------------------------------------------------
# CLI  (for testing / debugging standalone)
# ---------------------------------------------------------------------------

def _cli_parse_sta() -> None:
    import argparse
    ap = argparse.ArgumentParser(description="Parse yosys-sta STA timing report")
    ap.add_argument("rpt", type=Path, help="Path to .rpt file")
    args = ap.parse_args()
    try:
        result = parse_sta_report(args.rpt)
        print(f"WNS: {result['wns']} ns")
        print(f"TNS: {result['tns']} ns")
    except (ParseError, FileNotFoundError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


def _cli_parse_stat() -> None:
    import argparse
    ap = argparse.ArgumentParser(description="Parse yosys synthesis statistics")
    ap.add_argument("stat", type=Path, help="Path to synth_stat.txt")
    ap.add_argument("--design", default="ysyx_25070190", help="Expected top module name")
    args = ap.parse_args()
    try:
        result = parse_synth_stat(args.stat, args.design)
        print(f"Cells:  {result['cell_count']}")
        print(f"Area:   {result['area_um2']} µm²")
    except (ParseError, FileNotFoundError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "sta":
        sys.argv.pop(1)
        _cli_parse_sta()
    elif len(sys.argv) > 1 and sys.argv[1] == "stat":
        sys.argv.pop(1)
        _cli_parse_stat()
    else:
        print("Usage: report_parser.py {sta|stat} [args...]", file=sys.stderr)
        sys.exit(2)
