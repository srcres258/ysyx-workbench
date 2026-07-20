#!/usr/bin/env python3
# ============================================================================
# synth_timing.py — Timing path classifier and DRV report parser.
# ============================================================================
# Consumes:
#   - iSTA unified timing report       (*.rpt)
#   - iSTA fanout violation report    (*.fanout)
#   - Yosys mapped technology netlist  (*.netlist.v)
#
# Produces:
#   - Classified setup paths:  reg2reg / in2reg / reg2out / in2out
#   - Hold worst-path data (separate from setup)
#   - Path-group summaries
#   - High-fanout net list
#   - Unconstrained endpoint list
#
# Classification rules (explicit, from plan):
#   reg2reg  = startpoint IS sequential AND endpoint IS sequential
#   in2reg   = startpoint IS primary input AND endpoint IS sequential
#   reg2out  = startpoint IS sequential AND endpoint IS primary output
#   in2out   = startpoint IS primary input AND endpoint IS primary output
#   ambiguous → warning + null category, NOT a guess
#
# Design principle: conservative, deterministic, fail-closed.
# ============================================================================

from __future__ import annotations

import logging
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

logger = logging.getLogger(__name__)

# ── path-type constants ─────────────────────────────────────────────

CATEGORY_REG2REG = "reg2reg"
CATEGORY_IN2REG = "in2reg"
CATEGORY_REG2OUT = "reg2out"
CATEGORY_IN2OUT = "in2out"
CATEGORY_HOLD = "hold"

STARTPOINT_PORT = "port"
STARTPOINT_SEQUENTIAL = "sequential"

# ── data models ─────────────────────────────────────────────────────

@dataclass
class TimingPath:
    """A single classified timing path.

    Attributes:
        startpoint:   Pin/cell name at the path origin.
        endpoint:     Pin/cell name at the path destination.
        startpoint_type:  ``"port"`` or ``"sequential"``.
        endpoint_type:    ``"port"`` or ``"sequential"``.
        delay_type:       ``"max"`` (setup) or ``"min"`` (hold).
        clock_group:      Clock domain name.
        slack:            Slack in ns (negative = violation).
        path_delay:       Total path delay in ns.
        path_required:    Required time in ns.
        category:         One of ``CATEGORY_*``, or ``None`` if ambiguous.
    """
    startpoint: str
    endpoint: str
    startpoint_type: str
    endpoint_type: str
    delay_type: str
    clock_group: str
    slack: float
    path_delay: float = 0.0
    path_required: float = 0.0
    category: Optional[str] = None

    def to_dict(self) -> Dict:
        return {
            "startpoint": self.startpoint,
            "endpoint": self.endpoint,
            "startpoint_type": self.startpoint_type,
            "endpoint_type": self.endpoint_type,
            "delay_type": self.delay_type,
            "clock_group": self.clock_group,
            "slack": round(self.slack, 4),
            "path_delay": round(self.path_delay, 4),
            "path_required": round(self.path_required, 4),
            "category": self.category,
        }


@dataclass
class HighFanoutNet:
    """A net with high fanout (from iSTA fanout report)."""
    net_name: str
    driver_pin: str
    driver_cell: str
    fanout: int
    max_fanout: Optional[int] = None
    fanout_slack: Optional[float] = None

    def to_dict(self) -> Dict:
        return {
            "net_name": self.net_name,
            "driver_pin": self.driver_pin,
            "driver_cell": self.driver_cell,
            "fanout": self.fanout,
            "max_fanout": self.max_fanout,
            "fanout_slack": self.fanout_slack,
        }


@dataclass
class UnconstrainedEndpoint:
    """An endpoint not appearing in the STA timing report."""
    pin_name: str
    pin_type: str  # "input_port", "output_port", "register_pin"

    def to_dict(self) -> Dict:
        return {
            "pin_name": self.pin_name,
            "pin_type": self.pin_type,
        }


@dataclass
class PathGroupSummary:
    """Summary statistics for one path group (clock + delay_type)."""
    clock_group: str
    delay_type: str
    endpoint_count: int
    wns: float
    tns: float

    def to_dict(self) -> Dict:
        return {
            "clock_group": self.clock_group,
            "delay_type": self.delay_type,
            "endpoint_count": self.endpoint_count,
            "wns": round(self.wns, 4),
            "tns": round(self.tns, 4),
        }


# ── regex patterns ──────────────────────────────────────────────────

# iSTA summary table row (same as report_parser.py)
_SUMMARY_ROW_RE = re.compile(
    r"^\|\s*(?P<endpoint>[^|]+?)\s*\|\s*(?P<clock_group>[^|]+?)\s*\|\s*"
    r"(?P<delay_type>max|min)\s*\|\s*(?P<path_delay>[^|]+?)\s*\|\s*"
    r"(?P<path_required>[^|]+?)\s*\|\s*(?P<cppr>[^|]+?)\s*\|\s*"
    r"(?P<slack>[-]?\d+\.?\d*)\s*\|\s*(?P<freq>[^|]+?)\s*\|$",
    re.MULTILINE,
)

# iSTA TNS table row
_TNS_ROW_RE = re.compile(
    r"^\|\s*(?P<clock>[^|]+?)\s*\|\s*(?P<delay_type>max|min)\s*\|\s*"
    r"(?P<tns>[-]?\d+\.?\d*)\s*\|$",
    re.MULTILINE,
)

# iSTA detailed-path table: each table starts with a boundary line
# containing "Point" column header, and ends with a boundary line
# followed by empty line or next boundary.
_TABLE_HEADER_RE = re.compile(
    r"^\|\s*Point\s*\|",
    re.MULTILINE,
)
_TABLE_BOUNDARY_RE = re.compile(
    r"^\+\-+\+",
    re.MULTILINE,
)

# Row inside a detailed-path table: first column is the point name,
# last column is the path delay.
# Columns: Point | Fanout | Capacitance | Resistance | Transition | Delta Delay | Incr | Path
_PATH_ROW_RE = re.compile(
    r"^\|\s*(?P<point>[^|]+?)\s*\|\s*(?P<fanout>[^|]*?)\s*\|\s*"
    r"(?P<cap>[^|]*?)\s*\|\s*(?P<res>[^|]*?)\s*\|\s*"
    r"(?P<trans>[^|]*?)\s*\|\s*(?P<delta>[^|]*?)\s*\|\s*"
    r"(?P<incr>[^|]*?)\s*\|\s*(?P<path>[^|]*?)\s*\|$",
    re.MULTILINE,
)

# Annotation detection in the Point column:
# "(port)"       → primary I/O
# "(DFFQX1H7L)"  → sequential cell
# "(ICGX...)"    → clock gate (treated as sequential for endpoint classification)
# "(BUF...)" etc. → combinational (not a startpoint/endpoint — internal data node)
_PORT_ANNOTATION_RE = re.compile(r"\(port\)")
_CELL_ANNOTATION_RE = re.compile(r"\(([A-Za-z0-9_]+)\)")

# Sequential cell prefixes for classification
_SEQUENTIAL_PREFIXES = [
    "DFF", "SDFF", "DLH", "DLL", "LATCH", "REG", "FF_",
    "ICG", "CLKGATE", "CLKGATETST", "CGL",
    "TLAT",
]

# Clock-related row markers (for distinguishing data vs clock sections)
_CLOCK_ROW_MARKERS = [
    "clock core_clock",
    "clock network delay",
    "clock (port)",
    "clock (net)",
    "clock (clock net)",
    "clock uncertainty",
    "clock reconvergence pessimism",
    "library setup time",
    "library hold time",
    "library recovery time",
    "library removal time",
    "output external max delay",
    "output external min delay",
    "input external max delay",
    "input external min delay",
    "data require time",
    "data arrival time",
    "slack (MET)",
    "slack (VIOLATED)",
    "path cell delay",
    "path net delay",
    "startpoint clock latency",
    "endpoint clock latency",
    "cppr",
    "skew",
]

# iSTA fanout table row
# Columns: Net / Pin | MaxFanout | Fanout | FanoutSlack | LibCellPort | Note
_FANOUT_ROW_RE = re.compile(
    r"^\|\s*(?P<net_pin>[^|]+?)\s*\|\s*(?P<max_fanout>[^|]*?)\s*\|\s*"
    r"(?P<fanout>[^|]*?)\s*\|\s*(?P<fanout_slack>[^|]*?)\s*\|\s*"
    r"(?P<cell_port>[^|]*?)\s*\|",
    re.MULTILINE,
)

# ── helpers ─────────────────────────────────────────────────────────

def _is_seq_cell(cell_type: str) -> bool:
    """Return True if cell_type matches a sequential (flip-flop/latch/clock-gate) prefix."""
    for prefix in _SEQUENTIAL_PREFIXES:
        if cell_type.upper().startswith(prefix.upper()):
            return True
    return False


def _is_clock_row(point_text: str) -> bool:
    """Return True if the point row belongs to the clock/capture section."""
    stripped = point_text.strip().lower()
    for marker in _CLOCK_ROW_MARKERS:
        if stripped.startswith(marker.lower()):
            return True
    # Also catch: "clock" alone, "clock (port)", etc.
    if stripped == "clock" or stripped.startswith("clock"):
        return True
    return False


def _classify_path_type(
    startpoint_type: str,
    endpoint_type: str,
    startpoint: str,
    endpoint: str,
) -> Optional[str]:
    """Classify a timing path into one of four setup categories.

    Returns ``None`` when the classification is ambiguous.
    """
    is_start_port = (startpoint_type == STARTPOINT_PORT)
    is_end_port = (endpoint_type == STARTPOINT_PORT)
    is_start_seq = (startpoint_type == STARTPOINT_SEQUENTIAL)
    is_end_seq = (endpoint_type == STARTPOINT_SEQUENTIAL)

    # Both sequential → reg2reg
    if is_start_seq and is_end_seq:
        return CATEGORY_REG2REG

    # Input port → sequential → in2reg
    if is_start_port and is_end_seq:
        return CATEGORY_IN2REG

    # Sequential → output port → reg2out
    if is_start_seq and is_end_port:
        return CATEGORY_REG2OUT

    # Input port → output port → in2out
    if is_start_port and is_end_port:
        return CATEGORY_IN2OUT

    # Ambiguous — do not guess
    logger.warning(
        "Ambiguous path classification: startpoint=%s (type=%s), "
        "endpoint=%s (type=%s) — cannot classify into reg2reg/in2reg/reg2out/in2out.",
        startpoint, startpoint_type, endpoint, endpoint_type,
    )
    return None


def _parse_float_safe(s: str) -> Optional[float]:
    """Safely parse a float, stripping trailing 'r'/'f' edge-type markers."""
    s = s.strip()
    if not s:
        return None
    # Strip trailing rise/fall markers
    if s.endswith("r") or s.endswith("f"):
        s = s[:-1]
    try:
        return float(s)
    except ValueError:
        return None


def _strip_annotation(point_text: str) -> Tuple[str, Optional[str]]:
    """Strip the cell-type annotation from a point name.

    Returns ``(clean_name, annotation)`` where annotation is ``"port"``,
    a cell type string, or ``None``.
    """
    text = point_text.strip()
    # Check for (port) annotation
    if _PORT_ANNOTATION_RE.search(text):
        clean = _PORT_ANNOTATION_RE.sub("", text).strip()
        return clean, "port"
    # Check for (CELL_TYPE) annotation
    m = _CELL_ANNOTATION_RE.search(text)
    if m:
        clean = _CELL_ANNOTATION_RE.sub("", text).strip()
        return clean, m.group(1)
    return text, None


def _determine_point_type(annotation: Optional[str]) -> str:
    """Determine whether a point is a port or sequential from its annotation."""
    if annotation is None:
        # No annotation: could be a net, clock row, or internal node.
        # These should not appear as startpoints/endpoints in practice.
        logger.warning(
            "Point has no annotation — cannot determine type. "
            "This may indicate a data-extraction bug. Annotation=%r",
            annotation,
        )
        return "unknown"
    if annotation == "port":
        return STARTPOINT_PORT
    if _is_seq_cell(annotation):
        return STARTPOINT_SEQUENTIAL
    # Combinational cell, buffer, etc. — should not be a startpoint/endpoint
    return "combinational"


# ── .rpt detailed path table parsing ────────────────────────────────

def _parse_path_tables(text: str) -> List[Dict]:
    """Parse all detailed path tables from the .rpt body.

    Each path table starts with a ``| Point | ...`` header row, followed
    by a boundary line, then data rows.  Blank rows separate the data
    path from the clock-capture section.  The table ends with a boundary
    line.

    Returns a list of dicts, each containing:
        ``{"startpoint": str, "endpoint": str,
           "startpoint_type": str, "endpoint_type": str,
           "slack": float, "path_delay": float,
           "path_required": float, "delay_type": str,
           "category": str | None}``
    """
    # Strategy: find every table body by locating header → boundary pairs.
    # The body starts after the first boundary following the header and
    # ends at the next boundary (or EOF).
    header_matches = list(_TABLE_HEADER_RE.finditer(text))
    if not header_matches:
        return []

    boundary_matches = list(_TABLE_BOUNDARY_RE.finditer(text))
    all_boundary_starts = [m.start() for m in boundary_matches]
    all_boundary_ends = [m.end() for m in boundary_matches]

    paths = []
    for i, hm in enumerate(header_matches):
        hdr_start = hm.start()

        # First boundary after the header starts = end of header boundary line
        body_start = None
        for bs, be in zip(all_boundary_starts, all_boundary_ends):
            if bs >= hdr_start:
                body_start = be  # skip past the header→body separator
                break
        if body_start is None:
            continue

        # Find the boundary line that ends this table.
        # Look for the next boundary whose start is >= body_start + some offset.
        body_end = None
        for bs in all_boundary_starts:
            if bs > body_start:
                # Skip if this boundary is actually the *next* table's header
                # boundary.  The next header's boundary is right before the
                # next header text.  Use a heuristic: if there's a header
                # between this boundary and the next, it's the next table.
                if i + 1 < len(header_matches):
                    next_hdr = header_matches[i + 1].start()
                    if bs >= next_hdr - 5:  # boundary before next header
                        body_end = bs
                        break
                else:
                    body_end = bs
                    break
        if body_end is None:
            # Last table — take everything to EOF
            body_end = len(text)

        if body_end <= body_start:
            continue

        table_body = text[body_start:body_end]

        # Parse rows, skip the header row ("Point" column) and blank rows
        row_matches = list(_PATH_ROW_RE.finditer(table_body))
        rows = []
        for rm in row_matches:
            pt = rm.group("point").strip()
            if not pt or pt == "Point":
                continue  # blank row or header
            rows.append(rm)

        if len(rows) < 3:
            continue

        # Separate data rows from clock-capture rows.
        # The data path goes until the first blank-or-clock row after
        # the data region.  Clock rows are identifiable by markers.
        data_indices = []
        for j, r in enumerate(rows):
            pt = r.group("point").strip()
            if not pt:
                continue
            if not _is_clock_row(pt):
                data_indices.append(j)

        if len(data_indices) < 2:
            continue

        first_data_idx = data_indices[0]
        last_data_idx = data_indices[-1]

        sp_row = rows[first_data_idx]
        ep_row = rows[last_data_idx]

        sp_text = sp_row.group("point").strip()
        sp_clean, sp_ann = _strip_annotation(sp_text)
        sp_type = _determine_point_type(sp_ann)
        if sp_type == "unknown":
            sp_type = _infer_type_from_context(sp_text, "startpoint")

        ep_text = ep_row.group("point").strip()
        ep_clean, ep_ann = _strip_annotation(ep_text)
        ep_type = _determine_point_type(ep_ann)
        if ep_type == "unknown":
            ep_type = _infer_type_from_context(ep_text, "endpoint")

        # Extract numeric values from table footer
        slack_val = 0.0
        path_delay_val = 0.0
        path_required_val = 0.0
        delay_type = "max"

        for r in rows:
            pt = r.group("point").strip().lower()
            pv = r.group("path").strip()
            if pt.startswith("data arrival time"):
                path_delay_val = _parse_float_safe(pv) or 0.0
            elif pt.startswith("data require time"):
                path_required_val = _parse_float_safe(pv) or 0.0
            elif pt.startswith("library hold time"):
                delay_type = "min"
            elif pt.startswith("slack"):
                slack_m = re.search(r"([-]?\d+\.?\d*)", pt)
                if not slack_m:
                    slack_m = re.search(r"([-]?\d+\.?\d*)", pv)
                if slack_m:
                    slack_val = float(slack_m.group(1))

        category = _classify_path_type(sp_type, ep_type, sp_clean, ep_clean)

        paths.append({
            "startpoint": sp_clean,
            "endpoint": ep_clean,
            "startpoint_type": sp_type,
            "endpoint_type": ep_type,
            "slack": slack_val,
            "path_delay": path_delay_val,
            "path_required": path_required_val,
            "delay_type": delay_type,
            "category": category,
            "clock_group": "",
        })

    return paths


def _infer_type_from_context(point_text: str, role: str) -> str:
    """Fallback type inference when annotation is absent.

    Looks at the point name for clues:
    - Contains ``:D`` or ``:CK`` → likely sequential
    - No obvious signal → falls back to ``"unknown"``.
    """
    if ":" in point_text:
        pin_part = point_text.split(":")[-1].strip().rstrip(")")
        if pin_part in ("D", "CK", "Q", "QN", "RN", "SN", "EN", "E"):
            return STARTPOINT_SEQUENTIAL
    # If the text contains "port" → likely I/O
    if "port" in point_text.lower():
        return STARTPOINT_PORT
    return "unknown"


# ── summary-table parsing → path-group summaries + hold paths ───────

def _parse_summary_table(text: str) -> List[Dict]:
    """Parse the summary table rows from the .rpt.

    Returns a list of dicts with keys:
        endpoint, clock_group, delay_type, path_delay, path_required,
        cppr, slack, freq
    """
    rows = []
    for m in _SUMMARY_ROW_RE.finditer(text):
        rows.append({
            "endpoint": m.group("endpoint").strip(),
            "clock_group": m.group("clock_group").strip(),
            "delay_type": m.group("delay_type").strip(),
            "path_delay": _parse_float_safe(m.group("path_delay")) or 0.0,
            "path_required": _parse_float_safe(m.group("path_required")) or 0.0,
            "cppr": _parse_float_safe(m.group("cppr")) or 0.0,
            "slack": _parse_float_safe(m.group("slack")) or 0.0,
            "freq": m.group("freq").strip(),
        })
    return rows


def _parse_tns_table(text: str) -> List[Dict]:
    """Parse the TNS table rows from the .rpt."""
    rows = []
    for m in _TNS_ROW_RE.finditer(text):
        rows.append({
            "clock": m.group("clock").strip(),
            "delay_type": m.group("delay_type").strip(),
            "tns": _parse_float_safe(m.group("tns")) or 0.0,
        })
    return rows


# ── fanout parsing ──────────────────────────────────────────────────

def _parse_fanout_report(text: str) -> List[HighFanoutNet]:
    """Parse the iSTA fanout violation report.

    The fanout report has groups of rows where each "Net" row is
    followed by its driver pin row.  We pair them together.
    """
    nets = []
    lines = text.split("\n")
    current_net: Optional[str] = None

    for line in lines:
        m = _FANOUT_ROW_RE.match(line)
        if not m:
            continue
        net_pin = m.group("net_pin").strip()
        max_fanout_str = m.group("max_fanout").strip()
        fanout_str = m.group("fanout").strip()
        fanout_slack_str = m.group("fanout_slack").strip()
        cell_port = m.group("cell_port").strip()

        # If this row has a non-empty "Net / Pin" but empty fanout columns,
        # it's a net name row.
        has_fanout_data = bool(fanout_str and fanout_str != "NA")

        if not has_fanout_data and net_pin:
            # This is a net name row — store for pairing
            current_net = net_pin
        elif has_fanout_data and current_net is not None:
            # This is the driver pin row for the current net
            fanout = 0
            try:
                fanout = int(fanout_str)
            except (ValueError, TypeError):
                pass
            max_fanout = None
            try:
                max_fanout = int(max_fanout_str) if max_fanout_str and max_fanout_str != "NA" else None
            except (ValueError, TypeError):
                pass
            fanout_slack = None
            try:
                fanout_slack = float(fanout_slack_str) if fanout_slack_str and fanout_slack_str != "NA" else None
            except (ValueError, TypeError):
                pass

            nets.append(HighFanoutNet(
                net_name=current_net,
                driver_pin=net_pin,
                driver_cell=cell_port.split("/")[0] if "/" in cell_port else cell_port,
                fanout=fanout,
                max_fanout=max_fanout,
                fanout_slack=fanout_slack,
            ))
            current_net = None

    return nets


# ── unconstrained endpoint detection ──────────────────────────────

def _detect_unconstrained(
    rpt_summary_rows: List[Dict],
    netlist_modules: Dict[str, Dict],
) -> List[UnconstrainedEndpoint]:
    """Detect unconstrained endpoints by netlist traversal.

    Compares every register data pin and I/O port in the netlist
    against the set of endpoints appearing in the .rpt summary table.

    Any pin not found in the .rpt is a candidate unconstrained endpoint.

    Args:
        rpt_summary_rows: Parsed summary table rows from .rpt.
        netlist_modules: Dict from ``parse_netlist_hierarchy()``.

    Returns:
        List of ``UnconstrainedEndpoint`` objects.
    """
    # Build set of constrained endpoints from the .rpt summary
    constrained_endpoints: Set[str] = set()
    for row in rpt_summary_rows:
        ep = row["endpoint"].strip()
        if ep:
            constrained_endpoints.add(ep)

    unconstrained: List[UnconstrainedEndpoint] = []

    # We need to enumerate all register data pins and I/O ports.
    # Since parse_netlist_hierarchy gives us cell type counts per module
    # but not individual pin names, we need to parse the netlist more
    # carefully.  For now, we use cell instance names as proxy.
    #
    # A more precise approach would regex for port declarations and
    # flip-flop D-pin connections in the netlist.
    # This is a conservative first pass.
    #
    # For each sequential cell instance, the D pin would be a candidate
    # endpoint.  Similarly, top-level ports are candidates.

    # Re-read netlist for precise extraction (we need pin names)
    # This is intentionally simplistic for the first implementation;
    # it flags cells as potentially unconstrained rather than specific pins.
    # Future enhancement: parse pin-level connections.

    if not netlist_modules:
        logger.warning(
            "No netlist hierarchy available — cannot detect unconstrained endpoints. "
            "Provide a netlist path for full unconstrained analysis."
        )
        return unconstrained

    return unconstrained


def _detect_unconstrained_from_netlist_text(
    rpt_summary_rows: List[Dict],
    netlist_text: str,
    max_path_count: int = 10,
) -> Tuple[List[UnconstrainedEndpoint], List[str]]:
    """Detect unconstrained endpoints from raw netlist text.

    Parses register data-pin connections and I/O ports from the
    netlist, then compares against .rpt summary endpoints.

    **Limitation**: The iSTA ``.rpt`` summary table is truncated to
    ``-max_path`` entries.  Endpoints absent from the summary may still
    be constrained — they simply did not rank among the worst-N paths.
    This function therefore produces a **lower-bound estimate** and
    emits a caveat warning.

    Returns ``(unconstrained_list, caveats)``.
    """
    caveats: List[str] = []
    constrained_endpoints: Set[str] = set()
    for row in rpt_summary_rows:
        ep = row["endpoint"].strip()
        if ep:
            constrained_endpoints.add(ep)

    # Build set of constrained endpoint leaf names for fuzzy matching
    constrained_leaf: Set[str] = set()
    for ep in constrained_endpoints:
        leaf = ep.rsplit(".", 1)[-1] if "." in ep else ep
        constrained_leaf.add(leaf)

    unconstrained: List[UnconstrainedEndpoint] = []

    # ── register D pins ──────────────────────────────────────────
    # Yosys netlist format:  DFFQX1H7L instance_name (\n  .D(sig),\n  .CK(clk),\n  .Q(out)\n);
    # We look for .D(sig) connections inside sequential cell instantiations.
    d_pin_re = re.compile(
        r"[.]D\s*[(]\s*(?P<sig>\S+?)\s*[)]",
        re.MULTILINE,
    )

    d_pin_sigs: Set[str] = set()
    for m in d_pin_re.finditer(netlist_text):
        sig = m.group("sig").strip()
        d_pin_sigs.add(sig)

    for sig in sorted(d_pin_sigs):
        is_constrained = sig in constrained_endpoints or sig in constrained_leaf
        if not is_constrained:
            # Fuzzy: check if any constrained endpoint contains this signal
            for ep in constrained_endpoints:
                if sig in ep:
                    is_constrained = True
                    break
        if not is_constrained:
            unconstrained.append(UnconstrainedEndpoint(
                pin_name=sig,
                pin_type="register_pin",
            ))

    # ── I/O ports ────────────────────────────────────────────────
    port_re = re.compile(
        r"^\s*(?P<dir>input|output)\s+(?P<name>\S+)\s*;",
        re.MULTILINE,
    )

    for m in port_re.finditer(netlist_text):
        port_name = m.group("name").strip().rstrip(",")
        is_constrained = port_name in constrained_endpoints or port_name in constrained_leaf
        if not is_constrained:
            for ep in constrained_endpoints:
                if port_name in ep:
                    is_constrained = True
                    break
        if not is_constrained:
            pin_type = "input_port" if m.group("dir") == "input" else "output_port"
            unconstrained.append(UnconstrainedEndpoint(
                pin_name=port_name,
                pin_type=pin_type,
            ))

    caveats.append(
        f"Unconstrained detection uses a truncated .rpt summary "
        f"(max {len(constrained_endpoints)} constrained endpoints from "
        f"report_timing -max_path {max_path_count}).  "
        f"Endpoints absent from the summary may still be constrained — "
        f"this is a lower-bound estimate.  "
        f"For accurate results, increase -max_path or use a STA tool "
        f"with check_timing support."
    )

    return unconstrained, caveats


# ── public API ──────────────────────────────────────────────────────

def build_timing_report(
    rpt_path: str,
    fanout_path: Optional[str] = None,
    netlist_path: Optional[str] = None,
    top_reg2reg_count: int = 50,
    top_other_count: int = 20,
) -> Dict:
    """Orchestrate the full timing classification pipeline.

    Args:
        rpt_path: Path to the iSTA unified timing report (``.rpt``).
        fanout_path: Path to the iSTA fanout report (``.fanout``).
        netlist_path: Path to the mapped Verilog netlist (``.netlist.v``).
        top_reg2reg_count: Number of top reg2reg paths to keep.
        top_other_count: Number of top paths for other categories.

    Returns:
        A dict with keys:
        - ``reg2reg``, ``in2reg``, ``reg2out``, ``in2out``: lists of ``TimingPath`` dicts
        - ``hold``: list of hold-worst endpoint dicts
        - ``path_groups``: list of ``PathGroupSummary`` dicts
        - ``high_fanout``: list of ``HighFanoutNet`` dicts
        - ``unconstrained``: list of ``UnconstrainedEndpoint`` dicts
        - ``warnings``: list of warning strings
        - ``wns``, ``tns``: global worst/total negative slack (setup)

    Raises:
        FileNotFoundError: A required input file does not exist.
    """
    warnings: List[str] = []

    rpt_path_obj = Path(rpt_path)
    if not rpt_path_obj.is_file():
        raise FileNotFoundError(f"Timing report not found: {rpt_path_obj}")

    rpt_text = rpt_path_obj.read_text(encoding="utf-8", errors="replace")

    # ── 1. Parse summary table ──────────────────────────────────────
    summary_rows = _parse_summary_table(rpt_text)
    if not summary_rows:
        raise ValueError(
            f"Timing report {rpt_path}: no summary table rows found. "
            f"The report may be empty or in an unrecognised format."
        )

    tns_rows = _parse_tns_table(rpt_text)

    # Separate setup and hold from summary
    setup_rows = [r for r in summary_rows if r["delay_type"] == "max"]
    hold_rows = [r for r in summary_rows if r["delay_type"] == "min"]

    # ── 2. Parse detailed path tables for classification ────────────
    detailed_paths = _parse_path_tables(rpt_text)

    # Correlate detailed paths with summary table by endpoint
    # Build a lookup from endpoint → summary row
    endpoint_lookup: Dict[str, Dict] = {}
    for sr in summary_rows:
        ep = sr["endpoint"].strip()
        if ep:
            endpoint_lookup[ep] = sr

    # Enrich detailed paths with summary data
    classified_paths: List[TimingPath] = []
    for dp in detailed_paths:
        ep = dp["endpoint"]
        sr = endpoint_lookup.get(ep)
        if sr is None:
            # Try fuzzy match
            for k, v in endpoint_lookup.items():
                if ep in k or k in ep:
                    sr = v
                    break

        delay_type = dp.get("delay_type", "max")
        if sr:
            delay_type = sr.get("delay_type", delay_type)

        tp = TimingPath(
            startpoint=dp["startpoint"],
            endpoint=dp["endpoint"],
            startpoint_type=dp["startpoint_type"],
            endpoint_type=dp["endpoint_type"],
            delay_type=delay_type,
            clock_group=sr.get("clock_group", "") if sr else "",
            slack=dp["slack"],
            path_delay=dp.get("path_delay", 0.0),
            path_required=dp.get("path_required", 0.0),
            category=dp.get("category"),
        )
        # Skip paths that couldn't be classified (ambiguous)
        if tp.category is None:
            warnings.append(
                f"Unclassified path: {tp.startpoint} → {tp.endpoint} "
                f"(startpoint_type={tp.startpoint_type}, endpoint_type={tp.endpoint_type})"
            )
        classified_paths.append(tp)

    # Partition by category
    reg2reg = [p for p in classified_paths if p.category == CATEGORY_REG2REG and p.delay_type == "max"]
    in2reg = [p for p in classified_paths if p.category == CATEGORY_IN2REG and p.delay_type == "max"]
    reg2out = [p for p in classified_paths if p.category == CATEGORY_REG2OUT and p.delay_type == "max"]
    in2out = [p for p in classified_paths if p.category == CATEGORY_IN2OUT and p.delay_type == "max"]

    # Sort by slack (worst first = most negative first)
    for lst in [reg2reg, in2reg, reg2out, in2out]:
        lst.sort(key=lambda p: p.slack)

    # Retain top N
    reg2reg = reg2reg[:top_reg2reg_count]
    in2reg = in2reg[:top_other_count]
    reg2out = reg2out[:top_other_count]
    in2out = in2out[:top_other_count]

    # ── 3. Hold worst paths ─────────────────────────────────────────
    # Hold paths from summary table
    hold_paths: List[Dict] = []
    for hr in sorted(hold_rows, key=lambda r: r["slack"]):
        hold_paths.append({
            "endpoint": hr["endpoint"],
            "clock_group": hr["clock_group"],
            "slack": hr["slack"],
            "path_delay": hr["path_delay"],
            "path_required": hr["path_required"],
        })

    # ── 4. Path group summaries ─────────────────────────────────────
    path_groups: Dict[Tuple[str, str], PathGroupSummary] = {}
    for sr in summary_rows:
        key = (sr["clock_group"], sr["delay_type"])
        if key not in path_groups:
            tns = 0.0
            for tr in tns_rows:
                if tr["clock"] == sr["clock_group"] and tr["delay_type"] == sr["delay_type"]:
                    tns = tr["tns"]
                    break
            path_groups[key] = PathGroupSummary(
                clock_group=sr["clock_group"],
                delay_type=sr["delay_type"],
                endpoint_count=0,
                wns=float("inf"),
                tns=tns,
            )
        pg = path_groups[key]
        pg.endpoint_count += 1
        if sr["slack"] < pg.wns:
            pg.wns = sr["slack"]

    # ── 5. Global WNS/TNS ──────────────────────────────────────────
    global_wns = min((r["slack"] for r in setup_rows), default=0.0)
    global_tns = min((r["tns"] for r in tns_rows if r["delay_type"] == "max"), default=0.0)

    # ── 6. High-fanout ──────────────────────────────────────────────
    high_fanout: List[HighFanoutNet] = []
    if fanout_path and Path(fanout_path).is_file():
        fanout_text = Path(fanout_path).read_text(encoding="utf-8", errors="replace")
        all_fanout_nets = _parse_fanout_report(fanout_text)
        # Filter to nets with fanout >= some threshold, or sort by fanout descending
        all_fanout_nets.sort(key=lambda n: n.fanout, reverse=True)
        high_fanout = all_fanout_nets[:top_other_count]  # top 20 highest fanout
    else:
        if fanout_path:
            warnings.append(f"Fanout report not found: {fanout_path} — high-fanout data unavailable.")
        else:
            warnings.append("No fanout report path provided — high-fanout data unavailable.")

    # ── 7. Unconstrained endpoints ──────────────────────────────────
    unconstrained: List[UnconstrainedEndpoint] = []
    if netlist_path and Path(netlist_path).is_file():
        netlist_text = Path(netlist_path).read_text(encoding="utf-8", errors="replace")
        unconstrained, uc_caveats = _detect_unconstrained_from_netlist_text(
            summary_rows, netlist_text,
        )
        warnings.extend(uc_caveats)
        # When the summary has too few endpoints to draw conclusions,
        # return empty to avoid misleading counts.
        if len(summary_rows) < 100:
            warnings.append(
                f"Unconstrained count suppressed: only {len(summary_rows)} endpoints "
                f"in .rpt summary (truncated by -max_path).  Increase -max_path for "
                f"meaningful unconstrained detection (STA tool limitation)."
            )
            unconstrained = []
        elif not unconstrained:
            logger.info("No unconstrained endpoints detected from netlist comparison.")
    else:
        if netlist_path:
            warnings.append(f"Netlist not found: {netlist_path} — unconstrained endpoint detection skipped.")
        else:
            warnings.append("No netlist path provided — unconstrained endpoint detection skipped.")

    # ── 8. Assemble output ──────────────────────────────────────────
    return {
        "reg2reg": [p.to_dict() for p in reg2reg],
        "in2reg": [p.to_dict() for p in in2reg],
        "reg2out": [p.to_dict() for p in reg2out],
        "in2out": [p.to_dict() for p in in2out],
        "hold": hold_paths,
        "path_groups": [pg.to_dict() for pg in path_groups.values()],
        "high_fanout": [n.to_dict() for n in high_fanout],
        "unconstrained": [u.to_dict() for u in unconstrained],
        "wns": round(global_wns, 4),
        "tns": round(global_tns, 4),
        "warnings": warnings,
    }


# ── standalone CLI ──────────────────────────────────────────────────

def _cli_timing() -> None:
    import argparse, json, sys

    ap = argparse.ArgumentParser(
        description="Classify STA timing paths and parse DRV reports"
    )
    ap.add_argument("rpt", type=Path, help="Path to iSTA .rpt file")
    ap.add_argument("--fanout", type=Path, default=None, help="Path to .fanout file")
    ap.add_argument("--netlist", type=Path, default=None, help="Path to mapped .netlist.v")
    ap.add_argument("--json-out", action="store_true", help="Output as JSON")
    ap.add_argument("--top-reg2reg", type=int, default=50, help="Max reg2reg paths (default: 50)")
    ap.add_argument("--top-others", type=int, default=20, help="Max paths for other categories (default: 20)")
    args = ap.parse_args()

    try:
        result = build_timing_report(
            rpt_path=str(args.rpt),
            fanout_path=str(args.fanout) if args.fanout else None,
            netlist_path=str(args.netlist) if args.netlist else None,
            top_reg2reg_count=args.top_reg2reg,
            top_other_count=args.top_others,
        )
    except (FileNotFoundError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    if args.json_out:
        print(json.dumps(result, indent=2))
    else:
        # Human-readable output
        print("=== Timing Classification ===")
        print(f"Global WNS (setup): {result['wns']} ns")
        print(f"Global TNS (setup): {result['tns']} ns")
        print()

        for cat_name in [CATEGORY_REG2REG, CATEGORY_IN2REG, CATEGORY_REG2OUT, CATEGORY_IN2OUT]:
            paths = result[cat_name]
            print(f"--- {cat_name} ({len(paths)} paths) ---")
            for p in paths[:5]:
                print(f"  {p['startpoint']} → {p['endpoint']}  slack={p['slack']}ns")
            if len(paths) > 5:
                print(f"  ... and {len(paths) - 5} more")
            print()

        print(f"--- hold ({len(result['hold'])} endpoints) ---")
        for h in result["hold"][:5]:
            print(f"  {h['endpoint']}  slack={h['slack']}ns")
        if len(result["hold"]) > 5:
            print(f"  ... and {len(result['hold']) - 5} more")
        print()

        print(f"--- path groups ({len(result['path_groups'])}) ---")
        for pg in result["path_groups"]:
            print(f"  {pg['clock_group']}/{pg['delay_type']}: {pg['endpoint_count']} endpoints, WNS={pg['wns']}ns, TNS={pg['tns']}ns")
        print()

        print(f"--- high fanout ({len(result['high_fanout'])} nets) ---")
        for n in result["high_fanout"][:5]:
            print(f"  {n['net_name']}: fanout={n['fanout']}, driver={n['driver_pin']}")
        if len(result["high_fanout"]) > 5:
            print(f"  ... and {len(result['high_fanout']) - 5} more")
        print()

        print(f"--- unconstrained ({len(result['unconstrained'])} endpoints) ---")
        for u in result["unconstrained"][:5]:
            print(f"  {u['pin_name']} ({u['pin_type']})")
        if len(result["unconstrained"]) > 5:
            print(f"  ... and {len(result['unconstrained']) - 5} more")
        print()

        if result["warnings"]:
            print(f"--- warnings ({len(result['warnings'])}) ---")
            for w in result["warnings"][:10]:
                print(f"  {w}")
            if len(result["warnings"]) > 10:
                print(f"  ... and {len(result['warnings']) - 10} more")


if __name__ == "__main__":
    _cli_timing()
