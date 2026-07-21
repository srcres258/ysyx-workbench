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
CATEGORY_DATA_REG2REG = "data_reg2reg"
CATEGORY_IN2REG = "in2reg"
CATEGORY_REG2OUT = "reg2out"
CATEGORY_IN2OUT = "in2out"
CATEGORY_HOLD = "hold"
CATEGORY_CLOCK_ENABLE = "clock_enable"
CATEGORY_CLOCK_GATING_SETUP = "clock_gating_setup"

# Data pin suffixes for true-data-path classification.
# Q/QN  = true data output (startpoint must be one of these)
# D     = true data input   (endpoint must be this)
_DATA_STARTPOINT_PINS = frozenset({"Q", "QN"})
_DATA_ENDPOINT_PINS = frozenset({"D"})
_CLOCK_PINS = frozenset({"CK", "GCK", "GCLK", "CLK", "CP"})
_ENABLE_PINS = frozenset({"EN", "E", "SE", "TE"})
_ASYNC_CTRL_PINS = frozenset({"SN", "RN", "CDN", "SDN"})

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
        startpoint_pin:   Cell pin at startpoint (``"Q"``, ``"CK"``, ``""``, etc.).
        endpoint_pin:     Cell pin at endpoint (``"D"``, ``"EN"``, ``""``, etc.).
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
    startpoint_pin: str = ""
    endpoint_pin: str = ""
    delay_type: str = "max"
    clock_group: str = ""
    slack: float = 0.0
    path_delay: float = 0.0
    path_required: float = 0.0
    category: Optional[str] = None

    def to_dict(self) -> Dict:
        return {
            "startpoint": self.startpoint,
            "endpoint": self.endpoint,
            "startpoint_type": self.startpoint_type,
            "endpoint_type": self.endpoint_type,
            "startpoint_pin": self.startpoint_pin,
            "endpoint_pin": self.endpoint_pin,
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


def _extract_cell_pin(clean_name: str, annotation: Optional[str]) -> str:
    """Extract the cell pin suffix from a point's cleaned name.

    For instances like ``top.cpu.reg0:Q``, returns ``"Q"``.
    For ports (annotation=``"port"``) or annotationless nets, returns ``""``.
    """
    if annotation is None or annotation == "port":
        return ""
    if ":" in clean_name:
        return clean_name.rsplit(":", 1)[-1].strip()
    return ""


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


def _is_clock_gating_cell(cell_type: Optional[str]) -> bool:
    """Return True if cell_type is an integrated clock-gating cell."""
    if cell_type is None:
        return False
    prefixes = ("ICG", "CLKGATE", "CLKGATETST", "CGL")
    upper = cell_type.upper()
    return any(upper.startswith(p) for p in prefixes)


def _classify_path_type(
    startpoint_type: str,
    endpoint_type: str,
    startpoint: str,
    endpoint: str,
    startpoint_pin: str = "",
    endpoint_pin: str = "",
    startpoint_cell: Optional[str] = None,
    endpoint_cell: Optional[str] = None,
) -> Optional[str]:
    """Classify a timing path with pin-aware sub-classification for sequential paths.

    Returns ``None`` when the classification is ambiguous.
    """
    is_start_port = (startpoint_type == STARTPOINT_PORT)
    is_end_port = (endpoint_type == STARTPOINT_PORT)
    is_start_seq = (startpoint_type == STARTPOINT_SEQUENTIAL)
    is_end_seq = (endpoint_type == STARTPOINT_SEQUENTIAL)

    if is_start_seq and is_end_seq:
        sp_pin = startpoint_pin.upper()
        ep_pin = endpoint_pin.upper()

        if _is_clock_gating_cell(startpoint_cell) or _is_clock_gating_cell(endpoint_cell):
            return CATEGORY_CLOCK_GATING_SETUP

        if sp_pin in _DATA_STARTPOINT_PINS and ep_pin in _DATA_ENDPOINT_PINS:
            return CATEGORY_DATA_REG2REG

        if ep_pin in _ENABLE_PINS:
            return CATEGORY_CLOCK_ENABLE

        return CATEGORY_REG2REG

    if is_start_port and is_end_seq:
        return CATEGORY_IN2REG

    if is_start_seq and is_end_port:
        return CATEGORY_REG2OUT

    if is_start_port and is_end_port:
        return CATEGORY_IN2OUT

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
        sp_pin = _extract_cell_pin(sp_clean, sp_ann)

        ep_text = ep_row.group("point").strip()
        ep_clean, ep_ann = _strip_annotation(ep_text)
        ep_type = _determine_point_type(ep_ann)
        if ep_type == "unknown":
            ep_type = _infer_type_from_context(ep_text, "endpoint")
        ep_pin = _extract_cell_pin(ep_clean, ep_ann)

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

        category = _classify_path_type(
            sp_type, ep_type, sp_clean, ep_clean,
            startpoint_pin=sp_pin,
            endpoint_pin=ep_pin,
            startpoint_cell=sp_ann if sp_ann and sp_ann != "port" else None,
            endpoint_cell=ep_ann if ep_ann and ep_ann != "port" else None,
        )

        paths.append({
            "startpoint": sp_clean,
            "endpoint": ep_clean,
            "startpoint_pin": sp_pin,
            "endpoint_pin": ep_pin,
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
    max_path_count: int = 50,
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


# ── full netlist connectivity fanout analysis ──────────────────────

# Pins that are cell OUTPUTS (drivers), not loads.
# Excluding these from fanout counting gives a more accurate load count.
_DRIVER_PIN_NAMES = frozenset({
    "Q", "QN", "Z", "ZN", "CO", "S", "Y",
    "C",  # carry-out on half/full adders (but also CK on DFF — handled by context)
})

# Net name patterns for clock-like signals.
_CLOCK_NET_KW = frozenset({
    "clock", "clk", "_ck_", "_ck", "ck_",
})
_RESET_NET_KW = frozenset({
    "reset", "rst", "rst_n", "_rst_", "_rst",
})
_CONTROL_NET_KW = frozenset({
    "_en_", "_en", "enable", "ce_", "te_", "se_",
})

# Constant patterns (tied-off nets).
_CONSTANT_RE = re.compile(r"^\d+'[bhd]\w*$", re.IGNORECASE)

# Cell instance pattern: captures the pin name and signal
# Matches: .PIN(signal_name) or .PIN( signal_name )
_CELL_PIN_RE = re.compile(
    r"[.](\w+)\s*[(]\s*(?P<sig>[^)]+?)\s*[)]",
    re.MULTILINE,
)

# Wire declaration pattern: wire [optional range] name ;
_WIRE_DECL_RE = re.compile(
    r"^\s*wire\s+(?:\[\d+:\d+\]\s+)?(?P<name>[^;\s]+)\s*;",
    re.MULTILINE,
)


def _compute_netlist_fanout(netlist_text: str) -> Tuple[List[Dict], List[str]]:
    """Compute fanout for every net from full netlist connectivity.

    Parses every ``.PIN(signal)`` reference in cell instantiations and
    counts how many times each signal name appears as a pin connection.
    Nets are classified as clock, reset, control, or logic based on
    keyword matching.

    Returns ``(fanout_list, caveats)`` where *fanout_list* is sorted
    by fanout descending.
    """
    caveats: List[str] = []

    # ── extract all pin→signal connections ──────────────────────
    fanout_map: Dict[str, int] = defaultdict(int)
    fanout_pin_map: Dict[str, List[str]] = defaultdict(list)

    for m in _CELL_PIN_RE.finditer(netlist_text):
        pin_name = m.group(1).strip()
        sig = m.group("sig").strip().rstrip(",")

        # Skip constant nets
        if _CONSTANT_RE.match(sig):
            continue
        # Skip empty / whitespace-only signals
        if not sig:
            continue

        fanout_map[sig] += 1
        fanout_pin_map[sig].append(pin_name)

    if not fanout_map:
        caveats.append(
            "STATUS: UNAVAILABLE — No pin→signal connections found in netlist. "
            "The netlist may be empty, malformed, or in an unsupported format."
        )
        return [], caveats

    # ── classify nets ───────────────────────────────────────────
    clock_nets: List[Dict] = []
    reset_nets: List[Dict] = []
    control_nets: List[Dict] = []
    logic_nets: List[Dict] = []

    for net_name, fanout in fanout_map.items():
        net_lower = net_name.lower().rstrip("\\")
        pins = fanout_pin_map.get(net_name, [])

        # Determine the most common pin type for this net
        pin_types = set(p.strip().upper() for p in pins)

        entry = {
            "net_name": net_name,
            "fanout": fanout,
            "pin_types": sorted(pin_types),
        }

        # Clock classification: CK/CLK pins dominate OR net name matches
        if "CK" in pin_types or "CLK" in pin_types or "GCLK" in pin_types:
            entry["net_type"] = "clock"
            clock_nets.append(entry)
        elif any(kw in net_lower for kw in _CLOCK_NET_KW):
            entry["net_type"] = "clock"
            clock_nets.append(entry)
        elif any(kw in net_lower for kw in _RESET_NET_KW):
            entry["net_type"] = "reset"
            reset_nets.append(entry)
        elif any(kw in net_lower for kw in _CONTROL_NET_KW):
            entry["net_type"] = "control"
            control_nets.append(entry)
        else:
            entry["net_type"] = "logic"
            logic_nets.append(entry)

    # Sort within each category by fanout descending
    for lst in [clock_nets, reset_nets, control_nets, logic_nets]:
        lst.sort(key=lambda x: x["fanout"], reverse=True)

    all_nets = clock_nets + reset_nets + control_nets + logic_nets

    caveats.append(
        f"Fanout counts all .PIN(signal) references (including both inputs "
        f"and outputs).  Output pins ({', '.join(sorted(_DRIVER_PIN_NAMES))}) "
        f"contribute 1 to the count.  Subtract 1 for true gate-load fanout "
        f"on driver-annotated nets."
    )

    return all_nets, caveats


def _enumerate_netlist_endpoints(
    netlist_text: str,
) -> Dict:
    """Enumerate all STA-relevant endpoints from the full mapped netlist.

    Returns a dict with:
        - ``total_register_d_pins``: count of .D(sig) connections on
          sequential cells (DFF, SDFF, etc.)
        - ``total_input_ports``, ``total_output_ports``: I/O port counts
        - ``register_d_signals``: set of signal names seen on D pins
        - ``input_port_names``, ``output_port_names``: port name lists
        - ``total_endpoints``: sum of register D pins + output ports
    """
    # ── register D pins ──────────────────────────────────────────
    d_pin_re = re.compile(
        r"[.]D\s*[(]\s*(?P<sig>[^)]+?)\s*[)]",
        re.MULTILINE,
    )
    d_signals: Set[str] = set()
    for m in d_pin_re.finditer(netlist_text):
        sig = m.group("sig").strip().rstrip(",")
        if sig and not _CONSTANT_RE.match(sig):
            d_signals.add(sig)

    # ── I/O ports ────────────────────────────────────────────────
    port_re = re.compile(
        r"^\s*(?P<dir>input|output)\s+(?:\[\d+:\d+\]\s+)?(?P<name>[^;\s]+)\s*;",
        re.MULTILINE,
    )
    input_ports: List[str] = []
    output_ports: List[str] = []
    for m in port_re.finditer(netlist_text):
        name = m.group("name").strip().rstrip(",")
        if m.group("dir") == "input":
            input_ports.append(name)
        else:
            output_ports.append(name)

    total_reg = len(d_signals)
    total_in = len(input_ports)
    total_out = len(output_ports)
    total_endpoints = total_reg + total_out

    return {
        "total_register_d_pins": total_reg,
        "total_input_ports": total_in,
        "total_output_ports": total_out,
        "register_d_signals": d_signals,
        "input_port_names": input_ports,
        "output_port_names": output_ports,
        "total_endpoints": total_endpoints,
    }


# ── auxiliary timing report file writers ─────────────────────────────

def _write_timing_report_files(
    result_dir: str,
    summary_rows: List[Dict],
    tns_rows: List[Dict],
    unconstrained: List[UnconstrainedEndpoint],
    warnings: List[str],
    max_path: int = 50,
    netlist_endpoints: Optional[Dict] = None,
) -> None:
    out = Path(result_dir)
    out.mkdir(parents=True, exist_ok=True)

    # ── build constrained endpoint set from .rpt summary ──────
    constrained_setup: Set[str] = set()
    constrained_hold: Set[str] = set()
    for sr in summary_rows:
        ep = sr["endpoint"].strip()
        if not ep:
            continue
        if sr.get("delay_type") == "min":
            constrained_hold.add(ep)
        else:
            constrained_setup.add(ep)
    all_constrained = constrained_setup | constrained_hold

    # ── constraint_coverage.rpt ───────────────────────────────
    coverage_lines: List[str] = []
    coverage_lines.append("=" * 70)
    coverage_lines.append(" CONSTRAINT COVERAGE REPORT")
    coverage_lines.append("=" * 70)
    coverage_lines.append("")

    total_ep = 0
    total_reg = 0
    total_io_out = 0
    total_io_in = 0
    total_constrained_netlist = 0
    pct = 0.0
    reg_signals: Set[str] = set()
    out_ports: Set[str] = set()

    if netlist_endpoints:
        nle = netlist_endpoints
        total_ep = nle.get("total_endpoints", 0)
        total_reg = nle.get("total_register_d_pins", 0)
        total_io_out = nle.get("total_output_ports", 0)
        total_io_in = nle.get("total_input_ports", 0)

        reg_signals = nle.get("register_d_signals", set())
        out_ports = set(nle.get("output_port_names", []))

        reg_constrained = sum(1 for s in reg_signals if s in all_constrained)
        out_constrained = sum(1 for p in out_ports if p in all_constrained)
        total_constrained_netlist = reg_constrained + out_constrained

        pct = (total_constrained_netlist / total_ep * 100) if total_ep > 0 else 0.0

        coverage_lines.append("COVERAGE STATUS: LOWER_BOUND")
        coverage_lines.append("")
        coverage_lines.append("Endpoints enumerated from full mapped netlist:")
        coverage_lines.append(f"  Register D-pin endpoints:  {total_reg:>6d}")
        coverage_lines.append(f"  Output port endpoints:     {total_io_out:>6d}")
        coverage_lines.append(f"  Input ports (startpoints): {total_io_in:>6d}")
        coverage_lines.append(f"  Total endpoints:           {total_ep:>6d}")
        coverage_lines.append("")
        coverage_lines.append("Endpoints covered by STA report_timing summary:")
        coverage_lines.append(f"  Constrained in .rpt:       {total_constrained_netlist:>6d}")
        coverage_lines.append(f"  Coverage ratio:            {pct:>6.1f}%")
        coverage_lines.append("")
        coverage_lines.append(
            f"NOTE: report_timing -max_path {max_path} limits the .rpt to the "
            f"worst-N endpoints per clock group.  The true constrained set "
            f"is LARGER than what appears here.  This coverage ratio is a "
            f"LOWER BOUND — it CANNOT detect endpoints that ARE constrained "
            f"but do not rank among the worst-{max_path}."
        )
    else:
        coverage_lines.append("STATUS: UNAVAILABLE — no netlist endpoint data available")
        coverage_lines.append("")
        coverage_lines.append(
            "Without netlist endpoint enumeration, coverage cannot be "
            "computed.  Provide a mapped netlist (.netlist.v) to enable "
            "full coverage analysis."
        )

    coverage_lines.append("")
    coverage_lines.append("--- Per clock-group (from .rpt summary, truncated) ---")
    coverage_lines.append(f"  {'Clock Group':<30s} {'Max (setup)':>12s} {'Min (hold)':>12s}")
    coverage_lines.append(f"  {'-'*30} {'-'*12} {'-'*12}")
    clock_groups: Dict[str, Dict[str, int]] = defaultdict(lambda: defaultdict(int))
    for sr in summary_rows:
        cg = sr.get("clock_group", "unknown")
        dt = sr.get("delay_type", "unknown")
        clock_groups[cg][dt] += 1
    for cg in sorted(clock_groups.keys()):
        max_count = clock_groups[cg].get("max", 0)
        min_count = clock_groups[cg].get("min", 0)
        coverage_lines.append(f"  {cg:<30s} {max_count:>12d} {min_count:>12d}")
    coverage_lines.append("")
    coverage_lines.append("=" * 70)

    (out / "constraint_coverage.rpt").write_text("\n".join(coverage_lines) + "\n")

    # ── unconstrained_endpoints.rpt ────────────────────────────
    unconstrained_lines: List[str] = []
    unconstrained_lines.append("=" * 70)
    unconstrained_lines.append(" UNCONSTRAINED ENDPOINTS REPORT")
    unconstrained_lines.append("=" * 70)
    unconstrained_lines.append("")

    if netlist_endpoints and total_ep > 0:
        missing = total_ep - total_constrained_netlist
        unconstrained_lines.append(f"STATUS: UNAVAILABLE (lower-bound estimate)")
        unconstrained_lines.append("")
        unconstrained_lines.append(
            f"report_timing -max_path {max_path} captures only {total_constrained_netlist} "
            f"of {total_ep} total netlist endpoints ({pct:.1f}%)."
        )
        unconstrained_lines.append(
            f"The remaining {missing} endpoints do NOT appear in the .rpt summary "
            f"but may still be constrained — they simply do not rank among the "
            f"worst-{max_path} paths per clock group."
        )
        unconstrained_lines.append("")
        unconstrained_lines.append(
            "iSTA at this version does not provide check_timing or "
            "report_constraint.  Unconstrained endpoint DETECTION is "
            "impossible without one of these commands — this report CANNOT "
            "distinguish truly unconstrained endpoints from endpoints that "
            "are constrained but not in the top-N paths."
        )
        if unconstrained:
            unconstrained_lines.append("")
            unconstrained_lines.append(
                f"--- Candidate unconstrained endpoints (from .rpt-vs-netlist diff, {len(unconstrained)} signals) ---"
            )
            unconstrained_lines.append(
                "These signals are register D-pins or I/O ports NOT appearing in the "
                ".rpt summary.  They are CANDIDATES — not confirmed unconstrained."
            )
            unconstrained_lines.append("")
            unconstrained_lines.append(f"  {'Pin Name':<50s} {'Type':<15s}")
            unconstrained_lines.append(f"  {'-'*50} {'-'*15}")
            for u in unconstrained[:200]:
                unconstrained_lines.append(f"  {u.pin_name:<50s} {u.pin_type:<15s}")
            if len(unconstrained) > 200:
                unconstrained_lines.append(f"  ... and {len(unconstrained) - 200} more (truncated)")
    else:
        unconstrained_lines.append("STATUS: UNAVAILABLE")
        unconstrained_lines.append("")
        unconstrained_lines.append(
            "Without netlist endpoint enumeration, unconstrained detection cannot "
            "proceed.  Provide a mapped netlist (.netlist.v) to enable analysis."
        )

    unconstrained_lines.append("")
    unconstrained_lines.append("=" * 70)

    (out / "unconstrained_endpoints.rpt").write_text("\n".join(unconstrained_lines) + "\n")

    # ── analysis_warnings.rpt ──────────────────────────────────
    warn_lines: List[str] = []
    warn_lines.append("=" * 70)
    warn_lines.append(" ANALYSIS WARNINGS")
    warn_lines.append("=" * 70)
    warn_lines.append("")
    if warnings:
        for i, w in enumerate(warnings, 1):
            warn_lines.append(f"  [{i}] {w}")
    else:
        warn_lines.append("  (no warnings)")
    warn_lines.append("")
    warn_lines.append("=" * 70)

    (out / "analysis_warnings.rpt").write_text("\n".join(warn_lines) + "\n")


def _write_high_fanout_nets_report(
    result_dir: str,
    fanout_nets: List[Dict],
    total_nets: int,
    warnings: List[str],
) -> None:
    out = Path(result_dir)
    out.mkdir(parents=True, exist_ok=True)

    lines: List[str] = []
    lines.append("=" * 70)
    lines.append(" HIGH-FANOUT NETS REPORT (full netlist connectivity)")
    lines.append("=" * 70)
    lines.append("")
    lines.append(f"Total nets analysed: {total_nets}")
    lines.append(f"Net categories: clock / reset / control / logic")
    lines.append("")
    lines.append(
        "COMPUTATION METHOD: Full netlist connectivity.  Every .PIN(signal) "
        "reference in cell instantiations counts as a connection on that net.  "
        "Output pins (Q, QN, Z, ZN, CO, S, Y) are INCLUDED — the driver "
        "contributes 1 to the count, so true gate-load fanout = (reported - 1) "
        "for nets driven by a single output pin."
    )
    lines.append("")

    # ── summary by category ────────────────────────────────────
    cats: Dict[str, List[Dict]] = defaultdict(list)
    for n in fanout_nets:
        cats[n.get("net_type", "logic")].append(n)

    lines.append("--- Category Summary ---")
    lines.append(f"  {'Category':<12s} {'Count':>8s} {'Max Fanout':>12s} {'Top Net':<40s}")
    lines.append(f"  {'-'*12} {'-'*8} {'-'*12} {'-'*40}")
    for cat_name in ("clock", "reset", "control", "logic"):
        cat_nets = cats.get(cat_name, [])
        max_fo = cat_nets[0]["fanout"] if cat_nets else 0
        top_name = cat_nets[0]["net_name"] if cat_nets else "(none)"
        lines.append(f"  {cat_name:<12s} {len(cat_nets):>8d} {max_fo:>12d} {top_name:<40s}")
    lines.append("")

    # ── clock nets ─────────────────────────────────────────────
    lines.append("--- Clock Nets (top 20 by fanout) ---")
    clock_nets = cats.get("clock", [])
    if clock_nets:
        lines.append(f"  {'#':>3s} {'Net Name':<50s} {'Fanout':>8s} {'Pins':<30s}")
        lines.append(f"  {'-'*3} {'-'*50} {'-'*8} {'-'*30}")
        for i, n in enumerate(clock_nets[:20], 1):
            pins_str = ",".join(n.get("pin_types", []))
            lines.append(f"  {i:>3d} {n['net_name']:<50s} {n['fanout']:>8d} {pins_str:<30s}")
    else:
        lines.append("  (no clock nets identified)")
    lines.append("")

    # ── reset nets ─────────────────────────────────────────────
    lines.append("--- Reset Nets ---")
    reset_nets = cats.get("reset", [])
    if reset_nets:
        lines.append(f"  {'#':>3s} {'Net Name':<50s} {'Fanout':>8s} {'Pins':<30s}")
        lines.append(f"  {'-'*3} {'-'*50} {'-'*8} {'-'*30}")
        for i, n in enumerate(reset_nets[:20], 1):
            pins_str = ",".join(n.get("pin_types", []))
            lines.append(f"  {i:>3d} {n['net_name']:<50s} {n['fanout']:>8d} {pins_str:<30s}")
    else:
        lines.append("  (no reset nets identified)")
    lines.append("")

    # ── control nets (top 20) ──────────────────────────────────
    lines.append("--- Control Nets (enable/select, top 20) ---")
    control_nets = cats.get("control", [])
    if control_nets:
        lines.append(f"  {'#':>3s} {'Net Name':<50s} {'Fanout':>8s} {'Pins':<30s}")
        lines.append(f"  {'-'*3} {'-'*50} {'-'*8} {'-'*30}")
        for i, n in enumerate(control_nets[:20], 1):
            pins_str = ",".join(n.get("pin_types", []))
            lines.append(f"  {i:>3d} {n['net_name']:<50s} {n['fanout']:>8d} {pins_str:<30s}")
    else:
        lines.append("  (no control nets identified)")
    lines.append("")

    # ── logic nets (top 50) ────────────────────────────────────
    lines.append("--- Logic Nets (top 50 by fanout) ---")
    logic_nets = cats.get("logic", [])
    if logic_nets:
        lines.append(f"  {'#':>3s} {'Net Name':<50s} {'Fanout':>8s} {'Pins':<30s}")
        lines.append(f"  {'-'*3} {'-'*50} {'-'*8} {'-'*30}")
        for i, n in enumerate(logic_nets[:50], 1):
            pins_str = ",".join(n.get("pin_types", []))
            lines.append(f"  {i:>3d} {n['net_name']:<50s} {n['fanout']:>8d} {pins_str:<30s}")
    else:
        lines.append("  (no logic nets identified)")
    lines.append("")

    # ── high-fanout outliers (> 50) ────────────────────────────
    lines.append("--- High-Fanout Outliers (fanout > 50, any category) ---")
    outliers = [n for n in fanout_nets if n["fanout"] > 50]
    if outliers:
        lines.append(f"  {'#':>3s} {'Net Name':<50s} {'Fanout':>8s} {'Category':<12s} {'Pins':<30s}")
        lines.append(f"  {'-'*3} {'-'*50} {'-'*8} {'-'*12} {'-'*30}")
        for i, n in enumerate(outliers, 1):
            pins_str = ",".join(n.get("pin_types", []))
            lines.append(
                f"  {i:>3d} {n['net_name']:<50s} {n['fanout']:>8d} "
                f"{n.get('net_type', 'logic'):<12s} {pins_str:<30s}"
            )
    else:
        lines.append("  (no nets with fanout > 50)")
    lines.append("")

    if warnings:
        lines.append("--- Caveats ---")
        for w in warnings:
            lines.append(f"  - {w}")
        lines.append("")

    lines.append("=" * 70)
    lines.append(" End of high_fanout_nets.rpt.")
    lines.append("=" * 70)

    (out / "high_fanout_nets.rpt").write_text("\n".join(lines) + "\n")


def _write_timing_category_report(
    out: Path,
    filename: str,
    title: str,
    paths: List[Dict],
    extra_sections: Optional[Dict[str, str]] = None,
    classification_notes: Optional[List[str]] = None,
) -> None:
    """Write a per-category timing report file.

    Args:
        out: Output directory Path.
        filename: Report filename (e.g. ``"timing_reg2reg_data.rpt"``).
        title: Human-readable report title.
        paths: List of path dicts with ``startpoint``, ``endpoint``, ``slack``,
               ``startpoint_pin``, ``endpoint_pin``, ``category``, ``path_delay``.
        extra_sections: Optional dict of section_title → text lines appended
                        after the path list.
        classification_notes: Optional list of classification evidence notes.
    """
    lines: List[str] = []
    lines.append("=" * 70)
    lines.append(f" {title}")
    lines.append("=" * 70)
    lines.append("")

    if not paths:
        lines.append("STATUS: NO PATHS — this category has no timing paths")
        lines.append("in the current STA report.  This may be because:")
        lines.append("  - The category is truly empty (no such paths in design)")
        lines.append("  - Paths exist but are not among the worst-N captured by")
        lines.append("    report_timing -max_path (try increasing -max_path)")
        lines.append("")
    else:
        slacks = [p.get("slack", 0.0) for p in paths]
        wns = round(min(slacks), 4)
        tns = round(sum(s for s in slacks if s < 0), 4)
        lines.append(f"Path count:         {len(paths)}")
        lines.append(f"Worst slack (WNS):  {wns} ns")
        lines.append(f"Total - slack (TNS): {tns} ns")
        lines.append("")

        lines.append(f"{'#':>3s}  {'Startpoint':<45s} {'pin':>6s}  →  {'Endpoint':<45s} {'pin':>6s}  {'slack':>10s}  {'delay':>10s}")
        lines.append(f"{'-'*3}  {'-'*45} {'-'*6}  →  {'-'*45} {'-'*6}  {'-'*10}  {'-'*10}")
        for i, p in enumerate(paths, 1):
            sp = p.get("startpoint", "?")
            ep = p.get("endpoint", "?")
            sp_pin = p.get("startpoint_pin", "")
            ep_pin = p.get("endpoint_pin", "")
            slack = p.get("slack", 0.0)
            delay = p.get("path_delay", 0.0)
            lines.append(
                f"{i:>3d}  {sp:<45s} {sp_pin:>6s}  →  {ep:<45s} {ep_pin:>6s}  "
                f"{slack:>10.4f}  {delay:>10.4f}"
            )
        lines.append("")

    if classification_notes:
        lines.append("Classification Evidence:")
        for note in classification_notes:
            lines.append(f"  - {note}")
        lines.append("")

    if extra_sections:
        for section_title, section_text in extra_sections.items():
            lines.append(f"--- {section_title} ---")
            lines.append(section_text)
            lines.append("")

    lines.append("=" * 70)
    lines.append(f" End of {filename}.")
    lines.append("=" * 70)

    (out / filename).write_text("\n".join(lines) + "\n")


def _write_per_category_timing_reports(
    result_dir: str,
    reg2reg: List,
    data_reg2reg: List,
    in2reg: List,
    reg2out: List,
    in2out: List,
    clock_enable: List,
    clock_gating_setup: List,
    hold_data_reg2reg: List,
    hold_reg2reg: List,
    hold_clock_enable: List,
    hold_clock_gating: List,
    hold_in2reg: List,
    hold_reg2out: List,
    hold_in2out: List,
    path_groups: List[Dict],
    hold_paths: List[Dict],
    hold_sub_categories: Dict[str, Dict],
    warnings: List[str],
) -> None:
    """Write all plan-required per-category timing report files.

    Produces 9 report files:
      - timing_reg2reg_data.rpt     (Q/QN→D setup paths)
      - timing_reg2reg_summary.rpt  (reg2reg catch-all setup paths)
      - timing_clock_enable.rpt     (clock-enable + gating setup paths)
      - timing_in2reg.rpt           (input→register setup paths)
      - timing_reg2out.rpt          (register→output setup paths)
      - timing_in2out.rpt           (input→output setup paths)
      - timing_hold_data.rpt        (Q/QN→D hold paths)
      - timing_hold_clock_gating.rpt (clock-gating hold paths)
      - timing_path_groups.rpt      (per-clock-group WNS/TNS)

    Also writes ``hold_timing.rpt`` (convenience combined hold report)
    and ``constraint_coverage.rpt``, ``unconstrained_endpoints.rpt``,
    ``analysis_warnings.rpt``.
    """
    out = Path(result_dir)
    out.mkdir(parents=True, exist_ok=True)

    # ── setup: timing_reg2reg_data.rpt ─────────────────────────
    data_reg2reg_dicts = [p.to_dict() if hasattr(p, 'to_dict') else p for p in data_reg2reg]
    _write_timing_category_report(
        out, "timing_reg2reg_data.rpt",
        "DATA REG2REG TIMING — Q/QN → D paths only",
        data_reg2reg_dicts,
        classification_notes=[
            "Startpoint pin ∈ {Q, QN} — verified from iSTA point annotation",
            "Endpoint pin = D — verified from iSTA point annotation",
            "CK, GCK, EN, SE, TE, RN, SN pins are EXCLUDED",
            "Paths through ICG/CLKGATE cells are in timing_clock_enable.rpt",
        ],
    )

    # ── setup: timing_reg2reg_summary.rpt ──────────────────────
    reg2reg_dicts = [p.to_dict() if hasattr(p, 'to_dict') else p for p in reg2reg]
    _write_timing_category_report(
        out, "timing_reg2reg_summary.rpt",
        "REG2REG SUMMARY TIMING — other sequential→sequential paths",
        reg2reg_dicts,
        classification_notes=[
            "Startpoint IS sequential AND endpoint IS sequential",
            "Does NOT include Q/QN→D paths (those are in timing_reg2reg_data.rpt)",
            "Does NOT include EN/E/SE/TE endpoint paths (those are in timing_clock_enable.rpt)",
            "Does NOT include ICG/CLKGATE paths (those are in timing_clock_enable.rpt)",
            "May include CK→CK, CK→D, Q→CK, and other non-data sequential paths",
        ],
    )

    # ── setup: timing_clock_enable.rpt ─────────────────────────
    combined_clock = []
    for p in clock_enable:
        d = p.to_dict() if hasattr(p, 'to_dict') else p
        d["_sub"] = "clock_enable"
        combined_clock.append(d)
    for p in clock_gating_setup:
        d = p.to_dict() if hasattr(p, 'to_dict') else p
        d["_sub"] = "clock_gating_setup"
        combined_clock.append(d)
    combined_clock.sort(key=lambda p: p["slack"])

    _write_timing_category_report(
        out, "timing_clock_enable.rpt",
        "CLOCK-ENABLE & CLOCK-GATING TIMING",
        combined_clock,
        extra_sections={
            "Sub-categories": (
                f"  clock_enable:       {len(clock_enable)} paths "
                f"(endpoint EN/E/SE/TE pin)\n"
                f"  clock_gating_setup: {len(clock_gating_setup)} paths "
                f"(through ICG/CLKGATE cells)"
            ),
        },
        classification_notes=[
            "clock_enable:       endpoint pin ∈ {EN, E, SE, TE}",
            "clock_gating_setup: startpoint or endpoint is ICG/CLKGATE/CLKGATETST",
            "These are NOT data paths — core_data_reg2reg_fmax_mhz excludes them",
        ],
    )

    # ── setup: timing_in2reg.rpt ───────────────────────────────
    in2reg_dicts = [p.to_dict() if hasattr(p, 'to_dict') else p for p in in2reg]
    _write_timing_category_report(
        out, "timing_in2reg.rpt",
        "INPUT→REGISTER TIMING",
        in2reg_dicts,
        classification_notes=[
            "Startpoint is primary input port (annotated '(port)' in iSTA)",
            "Endpoint is sequential element (DFF/SDFF/DLH/ICG/etc.)",
        ],
    )

    # ── setup: timing_reg2out.rpt ──────────────────────────────
    reg2out_dicts = [p.to_dict() if hasattr(p, 'to_dict') else p for p in reg2out]
    _write_timing_category_report(
        out, "timing_reg2out.rpt",
        "REGISTER→OUTPUT TIMING",
        reg2out_dicts,
        classification_notes=[
            "Startpoint is sequential element",
            "Endpoint is primary output port (annotated '(port)' in iSTA)",
        ],
    )

    # ── setup: timing_in2out.rpt ───────────────────────────────
    in2out_dicts = [p.to_dict() if hasattr(p, 'to_dict') else p for p in in2out]
    _write_timing_category_report(
        out, "timing_in2out.rpt",
        "INPUT→OUTPUT TIMING",
        in2out_dicts,
        classification_notes=[
            "Startpoint is primary input port",
            "Endpoint is primary output port",
        ],
    )

    # ── hold: timing_hold_data.rpt ─────────────────────────────
    hold_data_dicts = [p.to_dict() if hasattr(p, 'to_dict') else p for p in hold_data_reg2reg]
    _write_timing_category_report(
        out, "timing_hold_data.rpt",
        "HOLD DATA TIMING — Q/QN → D hold paths",
        hold_data_dicts,
        classification_notes=[
            "Hold (min-delay) paths where startpoint = Q/QN, endpoint = D",
            "Separate from setup timing — setup is in timing_reg2reg_data.rpt",
            "CK, EN, GCK pins are EXCLUDED from this category",
        ],
    )

    # ── hold: timing_hold_clock_gating.rpt ─────────────────────
    hold_cg_dicts = [p.to_dict() if hasattr(p, 'to_dict') else p for p in hold_clock_gating]
    _write_timing_category_report(
        out, "timing_hold_clock_gating.rpt",
        "HOLD CLOCK-GATING TIMING",
        hold_cg_dicts,
        classification_notes=[
            "Hold (min-delay) paths through ICG/CLKGATE cells",
            "Separate from setup clock-gating — setup is in timing_clock_enable.rpt",
        ],
    )

    # ── timing_path_groups.rpt ─────────────────────────────────
    pg_lines: List[str] = []
    pg_lines.append("=" * 70)
    pg_lines.append(" PATH GROUP SUMMARIES")
    pg_lines.append("=" * 70)
    pg_lines.append("")
    if path_groups:
        pg_lines.append(f"Total path groups: {len(path_groups)}")
        pg_lines.append("")
        pg_lines.append(f"  {'Clock Group':<30s} {'Delay':>6s} {'Endpoints':>10s} {'WNS (ns)':>10s} {'TNS (ns)':>10s}")
        pg_lines.append(f"  {'-'*30} {'-'*6} {'-'*10} {'-'*10} {'-'*10}")
        for pg in sorted(path_groups, key=lambda g: (g.get("clock_group", ""), g.get("delay_type", ""))):
            pg_lines.append(
                f"  {pg.get('clock_group', '?'):<30s} "
                f"{pg.get('delay_type', '?'):>6s} "
                f"{pg.get('endpoint_count', 0):>10d} "
                f"{pg.get('wns', 0.0):>10.4f} "
                f"{pg.get('tns', 0.0):>10.4f}"
            )
    else:
        pg_lines.append("STATUS: UNAVAILABLE — no path group data")
    pg_lines.append("")
    pg_lines.append("=" * 70)
    pg_lines.append(" End of timing_path_groups.rpt.")
    pg_lines.append("=" * 70)
    (out / "timing_path_groups.rpt").write_text("\n".join(pg_lines) + "\n")

    # ── combined hold_timing.rpt (convenience) ─────────────────
    classified_all_hold = [
        (p.to_dict() if hasattr(p, 'to_dict') else p)
        for hp_list in [hold_data_reg2reg, hold_clock_enable, hold_clock_gating,
                          hold_reg2reg, hold_in2reg, hold_reg2out, hold_in2out]
        for p in hp_list
    ]
    _write_timing_category_report(
        out, "hold_timing.rpt",
        "COMBINED HOLD TIMING REPORT",
        classified_all_hold,
        extra_sections={
            "Hold Sub-Categories": (
                f"  data_reg2reg:   {len(hold_data_reg2reg)} paths\n"
                f"  clock_enable:   {len(hold_clock_enable)} paths\n"
                f"  clock_gating:   {len(hold_clock_gating)} paths\n"
                f"  reg2reg:        {len(hold_reg2reg)} paths"
            ) if any([hold_data_reg2reg, hold_clock_enable, hold_clock_gating, hold_reg2reg])
            else "  (no classified hold paths)"
        },
        classification_notes=[
            "This is a convenience combined report.",
            "Per-category hold reports are in timing_hold_*.rpt files.",
        ],
    )


# ── public API ──────────────────────────────────────────────────────

def build_timing_report(
    rpt_path: str,
    fanout_path: Optional[str] = None,
    netlist_path: Optional[str] = None,
    top_reg2reg_count: int = 50,
    top_other_count: int = 20,
    result_dir: Optional[str] = None,
    max_path: int = 50,
) -> Dict:
    """Orchestrate the full timing classification pipeline.

    Args:
        rpt_path: Path to the iSTA unified timing report (``.rpt``).
        fanout_path: Path to the iSTA fanout report (``.fanout``).
        netlist_path: Path to the mapped Verilog netlist (``.netlist.v``).
        top_reg2reg_count: Number of top reg2reg/data_reg2reg paths to keep.
        top_other_count: Number of top paths for other categories.
        result_dir: If provided, write ``constraint_coverage.rpt``,
            ``unconstrained_endpoints.rpt``, ``high_fanout_nets.rpt``,
            and ``analysis_warnings.rpt`` to this directory.
        max_path: The ``-max_path`` value used by ``report_timing``
            (default 50, matching the current sta.tcl).

    Returns:
        A dict with keys:
        - ``reg2reg``, ``data_reg2reg``, ``in2reg``, ``reg2out``, ``in2out``:
          lists of ``TimingPath`` dicts (setup)
        - ``clock_enable``, ``clock_gating_setup``: lists of ``TimingPath`` dicts
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

    # ── 0. Extract actual -max_path from .rpt if present ──────────
    rpt_max_match = re.search(
        r"report_timing\s+-max_path\s+(?P<n>\d+)", rpt_text, re.IGNORECASE
    )
    if rpt_max_match:
        try:
            max_path = int(rpt_max_match.group("n"))
        except (ValueError, TypeError):
            pass

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
            startpoint_pin=dp.get("startpoint_pin", ""),
            endpoint_pin=dp.get("endpoint_pin", ""),
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
    data_reg2reg = [p for p in classified_paths if p.category == CATEGORY_DATA_REG2REG and p.delay_type == "max"]
    in2reg = [p for p in classified_paths if p.category == CATEGORY_IN2REG and p.delay_type == "max"]
    reg2out = [p for p in classified_paths if p.category == CATEGORY_REG2OUT and p.delay_type == "max"]
    in2out = [p for p in classified_paths if p.category == CATEGORY_IN2OUT and p.delay_type == "max"]
    clock_enable = [p for p in classified_paths if p.category == CATEGORY_CLOCK_ENABLE and p.delay_type == "max"]
    clock_gating_setup = [p for p in classified_paths if p.category == CATEGORY_CLOCK_GATING_SETUP and p.delay_type == "max"]

    # Sort by slack (worst first = most negative first)
    for lst in [reg2reg, data_reg2reg, in2reg, reg2out, in2out, clock_enable, clock_gating_setup]:
        lst.sort(key=lambda p: p.slack)

    # Retain top N
    reg2reg = reg2reg[:top_reg2reg_count]
    data_reg2reg = data_reg2reg[:top_reg2reg_count]
    in2reg = in2reg[:top_other_count]
    reg2out = reg2out[:top_other_count]
    in2out = in2out[:top_other_count]
    clock_enable = clock_enable[:top_other_count]
    clock_gating_setup = clock_gating_setup[:top_other_count]

    # ── 3. Hold classification (from detailed path tables) ────────────
    # Hold paths were previously discarded after classification.
    # Task 4: classify hold paths into sub-categories for richer hold
    # reporting.  Hold paths share the same startpoint/endpoint
    # categories as setup (data_reg2reg, clock_enable, clock_gating_setup,
    # reg2reg, in2reg, reg2out, in2out) but are dispatched separately.
    hold_classified = [p for p in classified_paths if p.delay_type == "min"]

    hold_data_reg2reg = [p for p in hold_classified if p.category == CATEGORY_DATA_REG2REG]
    hold_reg2reg = [p for p in hold_classified if p.category == CATEGORY_REG2REG]
    hold_clock_enable = [p for p in hold_classified if p.category == CATEGORY_CLOCK_ENABLE]
    hold_clock_gating = [p for p in hold_classified if p.category == CATEGORY_CLOCK_GATING_SETUP]
    hold_in2reg = [p for p in hold_classified if p.category == CATEGORY_IN2REG]
    hold_reg2out = [p for p in hold_classified if p.category == CATEGORY_REG2OUT]
    hold_in2out = [p for p in hold_classified if p.category == CATEGORY_IN2OUT]

    for lst in [hold_data_reg2reg, hold_reg2reg, hold_clock_enable,
                hold_clock_gating, hold_in2reg, hold_reg2out, hold_in2out]:
        lst.sort(key=lambda p: p.slack)

    # ── 4. Hold worst-paths from summary table (backward compat) ───────
    hold_paths: List[Dict] = []
    for hr in sorted(hold_rows, key=lambda r: r["slack"]):
        hold_paths.append({
            "endpoint": hr["endpoint"],
            "clock_group": hr["clock_group"],
            "slack": hr["slack"],
            "path_delay": hr["path_delay"],
            "path_required": hr["path_required"],
        })

    # Enrich hold_paths with classified hold paths from detailed tables.
    # Insert detailed-path records before summary-only records for the
    # same endpoints; summary records serve as fallback.
    classified_hold_paths: List[Dict] = []
    for hp_list in [hold_data_reg2reg, hold_clock_enable, hold_clock_gating,
                     hold_reg2reg, hold_in2reg, hold_reg2out, hold_in2out]:
        for p in hp_list[:top_other_count]:
            classified_hold_paths.append(p.to_dict())

    # ── 4. Hold sub-category summary (for reporting) ──────────────────
    hold_sub_categories: Dict[str, Dict] = {}
    for sub_cat, hp_list in [
        ("data_reg2reg", hold_data_reg2reg),
        ("clock_enable", hold_clock_enable),
        ("clock_gating", hold_clock_gating),
        ("reg2reg", hold_reg2reg),
    ]:
        if hp_list:
            slacks = [p.slack for p in hp_list]
            hold_sub_categories[sub_cat] = {
                "wns_ns": round(min(slacks), 4),
                "tns_ns": round(sum(s for s in slacks if s < 0), 4),
                "path_count": len(hp_list),
            }

    # ── 5. Path group summaries ─────────────────────────────────────
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

    # ── 6. Global WNS/TNS ──────────────────────────────────────────
    global_wns = min((r["slack"] for r in setup_rows), default=0.0)
    global_tns = min((r["tns"] for r in tns_rows if r["delay_type"] == "max"), default=0.0)

    # ── 7. High-fanout ──────────────────────────────────────────────
    high_fanout: List[HighFanoutNet] = []
    fanout_nets_netlist: List[Dict] = []
    fanout_total_netlist: int = 0
    fanout_caveats: List[str] = []

    if fanout_path and Path(fanout_path).is_file():
        fanout_text = Path(fanout_path).read_text(encoding="utf-8", errors="replace")
        all_fanout_nets = _parse_fanout_report(fanout_text)
        all_fanout_nets.sort(key=lambda n: n.fanout, reverse=True)
        high_fanout = all_fanout_nets[:top_other_count]
    else:
        if fanout_path:
            warnings.append(f"Fanout report not found: {fanout_path} — high-fanout data unavailable.")
        else:
            warnings.append("No fanout report path provided — high-fanout data unavailable.")

    # ── 7a. Full-netlist fanout computation ─────────────────────
    if netlist_path and Path(netlist_path).is_file():
        netlist_text_fanout = Path(netlist_path).read_text(encoding="utf-8", errors="replace")
        fanout_nets_netlist, fanout_caveats = _compute_netlist_fanout(netlist_text_fanout)
        fanout_total_netlist = len(fanout_nets_netlist)
        warnings.extend(fanout_caveats)
    else:
        fanout_caveats.append(
            "STATUS: UNAVAILABLE — Netlist not found; full-connectivity fanout "
            "computation skipped."
        )

    # ── 8. Unconstrained endpoints ──────────────────────────────────
    unconstrained: List[UnconstrainedEndpoint] = []
    netlist_endpoints: Optional[Dict] = None
    if netlist_path and Path(netlist_path).is_file():
        netlist_text = Path(netlist_path).read_text(encoding="utf-8", errors="replace")
        unconstrained, uc_caveats = _detect_unconstrained_from_netlist_text(
            summary_rows, netlist_text, max_path_count=max_path,
        )
        warnings.extend(uc_caveats)

        # ── 8a. Full netlist endpoint enumeration ───────────────
        netlist_endpoints = _enumerate_netlist_endpoints(netlist_text)
        total_ep = netlist_endpoints.get("total_endpoints", 0)
        if total_ep > 0:
            warnings.append(
                f"Netlist endpoint enumeration: {total_ep} total endpoints "
                f"({netlist_endpoints['total_register_d_pins']} register D-pins, "
                f"{netlist_endpoints['total_output_ports']} output ports, "
                f"{netlist_endpoints['total_input_ports']} input ports).  "
                f"Only {len(summary_rows)} appear in the truncated .rpt summary "
                f"(-max_path {max_path}).  Coverage estimate is a LOWER BOUND."
            )

        # Suppress misleading unconstrained listing when most endpoints
        # are absent only due to -max_path truncation.
        if len(summary_rows) < 100:
            warnings.append(
                f"Unconstrained count suppressed: only {len(summary_rows)} endpoints "
                f"in .rpt summary (truncated by -max_path).  Increase -max_path for "
                f"meaningful unconstrained detection (STA tool limitation)."
            )
            unconstrained = []
    else:
        if netlist_path:
            warnings.append(f"Netlist not found: {netlist_path} — unconstrained endpoint detection skipped.")
        else:
            warnings.append("No netlist path provided — unconstrained endpoint detection skipped.")

    # ── 9. Assemble output ──────────────────────────────────────────
    result = {
        "reg2reg": [p.to_dict() for p in reg2reg],
        "data_reg2reg": [p.to_dict() for p in data_reg2reg],
        "in2reg": [p.to_dict() for p in in2reg],
        "reg2out": [p.to_dict() for p in reg2out],
        "in2out": [p.to_dict() for p in in2out],
        "clock_enable": [p.to_dict() for p in clock_enable],
        "clock_gating_setup": [p.to_dict() for p in clock_gating_setup],
        "hold": hold_paths,
        "hold_classified": classified_hold_paths,
        "hold_sub_categories": hold_sub_categories,
        "path_groups": [pg.to_dict() for pg in path_groups.values()],
        "high_fanout": [n.to_dict() for n in high_fanout],
        "unconstrained": [u.to_dict() for u in unconstrained],
        "wns": round(global_wns, 4),
        "tns": round(global_tns, 4),
        "warnings": warnings,
    }

    # ── 10. Write auxiliary timing report files ────────────────────
    if result_dir:
        _write_timing_report_files(
            result_dir, summary_rows, tns_rows, unconstrained, warnings,
            max_path=max_path,
            netlist_endpoints=netlist_endpoints,
        )
        if fanout_nets_netlist:
            _write_high_fanout_nets_report(
                result_dir, fanout_nets_netlist, fanout_total_netlist,
                fanout_caveats,
            )
        _write_per_category_timing_reports(
            result_dir=result_dir,
            reg2reg=reg2reg,
            data_reg2reg=data_reg2reg,
            in2reg=in2reg,
            reg2out=reg2out,
            in2out=in2out,
            clock_enable=clock_enable,
            clock_gating_setup=clock_gating_setup,
            hold_data_reg2reg=hold_data_reg2reg,
            hold_reg2reg=hold_reg2reg,
            hold_clock_enable=hold_clock_enable,
            hold_clock_gating=hold_clock_gating,
            hold_in2reg=hold_in2reg,
            hold_reg2out=hold_reg2out,
            hold_in2out=hold_in2out,
            path_groups=[pg.to_dict() for pg in path_groups.values()],
            hold_paths=hold_paths,
            hold_sub_categories=hold_sub_categories,
            warnings=warnings,
        )

    return result


# ── standalone CLI ──────────────────────────────────────────────────

def _cli_timing() -> None:
    import argparse, json, sys

    ap = argparse.ArgumentParser(
        description="Classify STA timing paths and parse DRV reports"
    )
    ap.add_argument("rpt", type=Path, help="Path to iSTA .rpt file")
    ap.add_argument("--fanout", type=Path, default=None, help="Path to .fanout file")
    ap.add_argument("--netlist", type=Path, default=None, help="Path to mapped .netlist.v")
    ap.add_argument("--result-dir", type=Path, default=None,
                    help="Write constraint_coverage.rpt, unconstrained_endpoints.rpt, "
                         "and analysis_warnings.rpt to this directory")
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
            result_dir=str(args.result_dir) if args.result_dir else None,
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

        for cat_name in [CATEGORY_REG2REG, CATEGORY_DATA_REG2REG, CATEGORY_IN2REG,
                          CATEGORY_REG2OUT, CATEGORY_IN2OUT, CATEGORY_CLOCK_ENABLE,
                          CATEGORY_CLOCK_GATING_SETUP]:
            paths = result[cat_name]
            print(f"--- {cat_name} ({len(paths)} paths) ---")
            for p in paths[:5]:
                print(f"  {p['startpoint']} → {p['endpoint']}  slack={p['slack']}ns")
            if len(paths) > 5:
                print(f"  ... and {len(paths) - 5} more")
            print()

        print(f"--- hold ({len(result['hold'])} endpoints from summary table) ---")
        for h in result["hold"][:5]:
            print(f"  {h['endpoint']}  slack={h['slack']}ns")
        if len(result["hold"]) > 5:
            print(f"  ... and {len(result['hold']) - 5} more")
        print()

        # Hold sub-category breakdown (Task 4)
        hold_sub = result.get("hold_sub_categories", {})
        if hold_sub:
            print(f"--- hold sub-categories ---")
            for sub_cat in ["data_reg2reg", "clock_enable", "clock_gating", "reg2reg"]:
                sc = hold_sub.get(sub_cat)
                if sc:
                    print(f"  {sub_cat}: WNS={sc['wns_ns']}ns, TNS={sc['tns_ns']}ns, paths={sc['path_count']}")
            print()

        # Classified hold paths (Task 4)
        hold_classified = result.get("hold_classified", [])
        if hold_classified:
            print(f"--- hold classified ({len(hold_classified)} paths from detailed tables) ---")
            for hp in hold_classified[:5]:
                cat = hp.get("category", "?")
                print(f"  [{cat}] {hp['startpoint']} → {hp['endpoint']}  slack={hp['slack']}ns")
            if len(hold_classified) > 5:
                print(f"  ... and {len(hold_classified) - 5} more")
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
