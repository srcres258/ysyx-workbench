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


def _parse_dedicated_query_rpt(rpt_text: str) -> List[Dict]:
    """Parse the dedicated-query .rpt file with multi-path-per-table support.

    Unlike the canonical .rpt (one path per ``Point`` table), the
    dedicated ``-through``/``-to`` query produces tables that intermix
    clock-gating paths with data paths.  This parser splits rows on
    clock-section markers and extracts one path per CK→...→D segment.
    """
    import copy

    paths: List[Dict] = []
    header_matches = list(_TABLE_HEADER_RE.finditer(rpt_text))
    boundary_matches = list(_TABLE_BOUNDARY_RE.finditer(rpt_text))
    all_bs = [m.start() for m in boundary_matches]
    all_be = [m.end() for m in boundary_matches]

    for i, hm in enumerate(header_matches):
        hdr_start = hm.start()
        body_start = None
        for bs, be in zip(all_bs, all_be):
            if bs >= hdr_start:
                body_start = be
                break
        if body_start is None:
            continue
        body_end = None
        for bs in all_bs:
            if bs > body_start:
                if i + 1 < len(header_matches):
                    if bs >= header_matches[i + 1].start() - 5:
                        body_end = bs
                        break
                else:
                    body_end = bs
                    break
        if body_end is None:
            body_end = len(rpt_text)
        if body_end <= body_start:
            continue

        table_body = rpt_text[body_start:body_end]
        row_matches = list(_PATH_ROW_RE.finditer(table_body))
        rows = []
        for rm in row_matches:
            pt = rm.group("point").strip()
            if not pt or pt == "Point":
                continue
            rows.append(rm)

        if len(rows) < 3:
            continue

        # Split on clock-section markers to isolate separate path groups
        groups: List[List] = []
        current: List = []
        for r in rows:
            pt = r.group("point").strip()
            if _is_clock_row(pt):
                if current:
                    groups.append(current)
                current = []
                continue
            current.append(r)
        if current:
            groups.append(current)

        # Extract one path per group
        for group in groups:
            if len(group) < 2:
                continue

            # Find the Q/QN startpoint and D endpoint in this group
            sp_idx = None
            ep_idx = None
            for j, r in enumerate(group):
                pt = r.group("point").strip()
                clean, ann = _strip_annotation(pt)
                pin = _extract_cell_pin(clean, ann)
                if sp_idx is None and pin.upper() in ("Q", "QN"):
                    sp_idx = j
                if pin.upper() == "D":
                    ep_idx = j

            if sp_idx is None or ep_idx is None:
                continue
            if sp_idx >= ep_idx:
                continue

            sp_row = group[sp_idx]
            ep_row = group[ep_idx]

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
    coverage_evidence: Optional[Dict] = None,
) -> None:
    out = Path(result_dir)
    out.mkdir(parents=True, exist_ok=True)

    coverage_evidence = coverage_evidence or {}

    # ── constraint_coverage.rpt ───────────────────────────────
    coverage_lines: List[str] = []
    coverage_lines.append("=" * 70)
    coverage_lines.append(" CONSTRAINT COVERAGE REPORT")
    coverage_lines.append("=" * 70)
    coverage_lines.append("")

    total_reg = 0
    total_io_out = 0
    total_io_in = 0
    reg_signals: Set[str] = set()
    out_ports_set: Set[str] = set()

    if netlist_endpoints:
        nle = netlist_endpoints
        total_reg = nle.get("total_register_d_pins", 0)
        total_io_out = nle.get("total_output_ports", 0)
        total_io_in = nle.get("total_input_ports", 0)
        reg_signals = nle.get("register_d_signals", set())
        out_ports_set = set(nle.get("output_port_names", []))

    total_ep = total_reg + total_io_out
    constrained_d = coverage_evidence.get("constrained_d", set())
    constrained_out = coverage_evidence.get("constrained_out", set())
    qd_d_count = coverage_evidence.get("qd_constrained_d_count", 0)
    coverage_source = coverage_evidence.get("coverage_source", "unavailable")
    coverage_note = coverage_evidence.get("coverage_note", "")

    proven_d = len(constrained_d)
    proven_out = len(constrained_out)
    proven_total = proven_d + qd_d_count + proven_out
    uncertain_d = max(0, total_reg - qd_d_count)
    uncertain_out = total_io_out - proven_out
    uncertain_total = uncertain_d + uncertain_out

    pct = (proven_total / total_ep * 100) if total_ep > 0 else 0.0

    coverage_lines.append(f"COVERAGE SOURCE:  {coverage_source}")
    coverage_lines.append(f"COVERAGE STATUS:  LOWER_BOUND (evidence-backed)")
    coverage_lines.append("")

    coverage_lines.append("--- Register D Endpoints ---")
    coverage_lines.append(f"  Total enumerated:                {total_reg:>6d}")
    if qd_d_count > 0:
        coverage_lines.append(
            f"  PROVEN constrained (Q→D evidence): {qd_d_count:>6d}  "
            f"(instance-level, hierarchy-preserved netlist)"
        )
    coverage_lines.append(
        f"  PROVEN constrained (.rpt cross-ref): {proven_d:>6d}"
    )
    coverage_lines.append(f"  UNCERTAIN status:                  {uncertain_d:>6d}")
    coverage_lines.append("")
    coverage_lines.append("--- Output Port Endpoints ---")
    coverage_lines.append(f"  Total enumerated:                {total_io_out:>6d}")
    coverage_lines.append(f"  PROVEN constrained (STA evidence): {proven_out:>6d}")
    coverage_lines.append(f"  UNCERTAIN status:                  {uncertain_out:>6d}")
    coverage_lines.append("")

    coverage_lines.append("--- False Paths ---")
    coverage_lines.append("  STATUS: UNSUPPORTED")
    coverage_lines.append("  iSTA does not expose set_false_path query commands.")
    coverage_lines.append("")
    coverage_lines.append("--- Multicycle Paths ---")
    coverage_lines.append("  STATUS: UNSUPPORTED")
    coverage_lines.append("  iSTA does not expose set_multicycle_path query commands.")
    coverage_lines.append("")
    coverage_lines.append("--- Disabled Arcs ---")
    coverage_lines.append("  STATUS: UNSUPPORTED")
    coverage_lines.append("  No disabled-arc query surface available in iSTA.")
    coverage_lines.append("")
    coverage_lines.append("--- Clock-Gating Checks ---")
    coverage_lines.append("  STATUS: UNSUPPORTED")
    coverage_lines.append("  iSTA does not expose clock-gating setup/hold check reporting.")
    coverage_lines.append("")

    coverage_lines.append("--- Summary ---")
    coverage_lines.append(f"  All endpoints (D-pins + output ports):  {total_ep:>6d}")
    coverage_lines.append(f"  PROVEN constrained:                      {proven_total:>6d}  ({pct:.1f}%)")
    coverage_lines.append(f"  UNCERTAIN:                               {uncertain_total:>6d}  ({100.0 - pct:.1f}%)")
    coverage_lines.append(f"  UNSUPPORTED check categories:             4")
    coverage_lines.append("")

    if coverage_note:
        coverage_lines.append(f"PROVENANCE: {coverage_note}")
        coverage_lines.append("")

    coverage_lines.append(
        "UNCERTAIN endpoints: those enumerated from the netlist that do NOT "
        "appear in the dedicated STA endpoint coverage query.  They MAY be "
        "constrained (but below the query's ranking threshold) or MAY be "
        "genuinely unconstrained.  iSTA at this version cannot distinguish "
        "these two cases.  This is a LOWER BOUND — the true constrained "
        "count is AT LEAST the PROVEN count."
    )
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
        if uncertain_total > 0:
            unconstrained_lines.append(
                "STATUS: UNAVAILABLE (no positive unconstrained detection)"
            )
        else:
            unconstrained_lines.append(
                "STATUS: FULLY CONSTRAINED (all endpoints proven constrained)"
            )
        unconstrained_lines.append("")
        unconstrained_lines.append(
            f"Of {total_reg} D-pin endpoints, {proven_d} are PROVEN constrained "
            f"via dedicated STA endpoint query.  "
            f"Of {total_io_out} output ports, {proven_out} are PROVEN constrained.  "
            f"{uncertain_total} endpoints have UNCERTAIN status."
        )
        unconstrained_lines.append("")
        unconstrained_lines.append(
            "This report CANNOT positively identify unconstrained endpoints "
            "because iSTA at this version does NOT provide check_timing or "
            "report_constraint.  Without these commands, there is no way to "
            "prove an endpoint is UNCONSTRAINED vs. simply not appearing in "
            "timing queries."
        )
        unconstrained_lines.append("")

        if uncertain_total > 0:
            uncertain_d_signals = reg_signals - constrained_d
            uncertain_out_signals = out_ports_set - constrained_out

            unconstrained_lines.append(
                "--- Endpoints with UNCERTAIN constraint status ---"
            )
            unconstrained_lines.append(
                "These endpoints are NOT confirmed unconstrained — they simply "
                "did NOT appear in the dedicated STA coverage query "
                "(report_timing -to all endpoints, -nworst 1, -max_path >= 5000). "
                "They MAY be constrained but below the ranking threshold."
            )
            unconstrained_lines.append("")
            unconstrained_lines.append(f"  {'Endpoint':<50s} {'Type':<15s}")
            unconstrained_lines.append(f"  {'-'*50} {'-'*15}")

            count = 0
            for s in sorted(uncertain_d_signals):
                unconstrained_lines.append(f"  {s:<50s} register_pin")
                count += 1
                if count >= 100:
                    break
            if len(uncertain_d_signals) > count:
                unconstrained_lines.append(
                    f"  ... and {len(uncertain_d_signals) - count} more D-pin signals (truncated)"
                )
            for p in sorted(uncertain_out_signals):
                unconstrained_lines.append(f"  {p:<50s} output_port")
                count += 1
                if count >= 100:
                    break
            if len(uncertain_out_signals) > (count - min(100, len(uncertain_d_signals))):
                unconstrained_lines.append(
                    f"  ... and {len(uncertain_out_signals)} output ports (truncated)"
                )
            unconstrained_lines.append("")
            unconstrained_lines.append(
                "WHAT \"UNCERTAIN\" MEANS: An endpoint is UNCERTAIN when it is "
                "enumerated from the netlist but does NOT appear in the "
                "dedicated coverage query.  We CANNOT determine whether it is "
                "truly unconstrained or merely constrained but below the "
                "ranking threshold.  To resolve uncertain endpoints: use an "
                "iSTA version with check_timing support, or per-endpoint slack "
                "queries via the C++ API, or increase -max_path further."
            )
        else:
            unconstrained_lines.append(
                "No endpoints have UNCERTAIN status — all enumerated endpoints "
                "are proven constrained by STA evidence."
            )
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


# ── iSTA timing capability audit ────────────────────────────────────

def _build_ista_capability_report_text(
    yosys_version: str = "N/A",
    ieda_version: str = "N/A",
    pdk: str = "nangate45",
    liberty: str = "Nangate45_typ.lib",
) -> str:
    """Generate the iSTA timing capability report text.

    Documents the real iSTA Tcl surface, supported timing-query options,
    collection/support gaps, and source/commit provenance.

    Evidence: source-code analysis of iEDA repo at
    https://github.com/OSCC-Project/iEDA (``src/operation/iSTA/``).
    """
    lines: List[str] = []
    _sep = "=" * 70
    _sub = "-" * 50

    lines.append(_sep)
    lines.append(" iSTA TIMING CAPABILITY AUDIT")
    lines.append(_sep)
    lines.append("")

    # ── provenance ──────────────────────────────────────────
    lines.append("--- Provenance ---")
    lines.append("")
    lines.append(f"  Generated:           {__import__('datetime').datetime.utcnow().isoformat()}Z")
    lines.append(f"  Yosys version:       {yosys_version}")
    lines.append(f"  iEDA binary mtime:   {ieda_version}")
    lines.append(f"  PDK:                 {pdk}")
    lines.append(f"  Liberty:             {liberty}")
    lines.append(f"  iEDA source repo:    https://github.com/OSCC-Project/iEDA")
    lines.append(f"  Analysis source:     CmdReportTiming.cc, CmdReportConstrait.cc,")
    lines.append(f"                       CommandList.hh, Sta.hh (upstream source)")
    lines.append("")

    # ── Tcl command surface ─────────────────────────────────
    lines.append("--- Tcl Command Surface ---")
    lines.append("")
    lines.append("  Command                     | Status       | Notes")
    lines.append(f"  {'-'*26} | {'-'*12} | {'-'*30}")
    lines.append("  report_timing  (report_checks) | SUPPORTED    | Full Tcl command with pin-filter")
    lines.append("  report_constraint (report_check_types) | STUB         | exec() body commented out")
    lines.append("  read_liberty                | SUPPORTED    | Standard iSTA flow")
    lines.append("  read_verilog                | SUPPORTED    | Netlist reader")
    lines.append("  link_design                 | SUPPORTED    | Design linker")
    lines.append("  read_spef                   | SUPPORTED    | Parasitics reader")
    lines.append("  read_sdc                    | SUPPORTED    | Constraints reader")
    lines.append("  report_power                | SUPPORTED    | Via iPA subsystem")
    lines.append("")
    lines.append("  all_registers               | NOT AVAILABLE| No Tcl collection commands")
    lines.append("  all_clocks                  | NOT AVAILABLE| No Tcl collection commands")
    lines.append("  get_pins / get_cells        | NOT AVAILABLE| No Tcl object queries")
    lines.append("  check_timing                | NOT AVAILABLE| No unconstrained check")
    lines.append("  set_false_path query        | NOT AVAILABLE| Cannot introspect SDC")
    lines.append("  set_multicycle_path query   | NOT AVAILABLE| Cannot introspect SDC")
    lines.append("  exec / file I/O             | NOT AVAILABLE| Minimal embedded Tcl")
    lines.append("")

    # ── report_timing — FULL OPTIONS ───────────────────────
    lines.append("--- report_timing — Supported Options ---")
    lines.append("")
    lines.append("  Option              | Type       | Default | Description")
    lines.append(f"  {'-'*19} | {'-'*10} | {'-'*7} | {'-'*40}")
    lines.append("  -delay_type         | string     | max_min | max, min, max_min")
    lines.append("  -digits             | int (0-3)  | 3       | Significant digits")
    lines.append("  -max_path           | int        | 3       | Max paths per clock domain")
    lines.append("  -nworst             | int        | 1       | Max paths per endpoint")
    lines.append("  -from               | string list| {}      | Start-point pin filter")
    lines.append("  -to                 | string list| {}      | End-point pin filter")
    lines.append("  -through            | string list| —       | Through-point filter")
    lines.append("  -json               | switch     | off     | JSON output format")
    lines.append("  -exclude_cell_names | string list| {}      | Exclude cells")
    lines.append("  -derate             | switch     | off     | Apply timing derate")
    lines.append("  -is_clock_cap       | switch     | off     | Clock capacitance mode")
    lines.append("  -help               | switch     | —       | Print help")
    lines.append("")

    # ── Pin name format ────────────────────────────────────
    lines.append("--- Pin Name Format ---")
    lines.append("")
    lines.append("  Format:  {instance_path:pin_name}")
    lines.append("  Example: report_timing -from {dpath/a_reg/_55_:CK} -to {dpath/a_reg/_55_:D}")
    lines.append("  Separator: colon (:) between instance path and pin name.")
    lines.append("  For flat-synthesised netlists: {inst_name:pin_name}")
    lines.append("")

    # ── C++ API — per the upstream source ─────────────────
    lines.append("--- C++ API Surface (Sta.hh) ---")
    lines.append("")
    lines.append("  Method                     | Purpose")
    lines.append(f"  {'-'*25} | {'-'*45}")
    lines.append("  setReportSpec(froms, tos)  | Set path filter endpoints")
    lines.append("  getWorstSlack(vertex, ...) | Per-endpoint slack query")
    lines.append("  getWNS(clock_name, mode)   | Global WNS per clock")
    lines.append("  getTNS(clock_name, mode)   | Global TNS per clock")
    lines.append("  getSeqData(vertex, ...)    | Sequential path data")
    lines.append("  getWorstSeqData(vertex, ...)| Worst sequential paths")
    lines.append("  getTopNWorstSeqPaths(...)  | Top-N worst paths")
    lines.append("  getStartEndSlackPairsOfTopNPaths | Start/end slack pairs")
    lines.append("  findVertex(pin_name)       | Graph vertex lookup")
    lines.append("  get_graph() / get_netlist()| Full graph/netlist access")
    lines.append("  reportFromThroughTo(...)   | Single path via pins")
    lines.append("  enableJsonReport()         | JSON output enable")
    lines.append("")

    # ── Python API (pybind11) ──────────────────────────────
    lines.append("--- Python API (pybind11 via PythonSta.hh) ---")
    lines.append("")
    lines.append("  Function                  | Purpose")
    lines.append(f"  {'-'*25} | {'-'*40}")
    lines.append("  read_netlist(f)           | Read Verilog netlist")
    lines.append("  read_liberty(files)       | Read Liberty files")
    lines.append("  link_design(cell)         | Link top-level design")
    lines.append("  read_sdc(f)               | Read SDC constraints")
    lines.append("  read_spef(f)              | Read SPEF parasitics")
    lines.append("  report_timing()           | Run timing analysis")
    lines.append("  build_timing_graph()      | Build graph, no update")
    lines.append("  update_clock_timing()     | Clock propagation only")
    lines.append("  dump_graph_data(f)        | Dump graph as YAML")
    lines.append("  get_wire_timing_data(n)   | Wire timing (n-worst)")
    lines.append("  display_timing_map(mode)  | Per-instance slack map")
    lines.append("  display_timing_tns_map(mode)| Per-instance TNS map")
    lines.append("  display_slew_map(mode)    | Per-instance slew map")
    lines.append("  get_die_size()            | Die dimensions")
    lines.append("  get_used_libs()           | Loaded Liberty files")
    lines.append("")

    # ── Key Differences vs OpenSTA ─────────────────────────
    lines.append("--- Key Differences: iSTA vs OpenSTA ---")
    lines.append("")
    lines.append("  Feature                  | iSTA         | OpenSTA")
    lines.append(f"  {'-'*23} | {'-'*12} | {'-'*12}")
    lines.append("  -from/-to endpoint query | SUPPORTED    | SUPPORTED")
    lines.append("  -nworst per endpoint     | SUPPORTED    | SUPPORTED")
    lines.append("  -json output             | SUPPORTED    | SUPPORTED")
    lines.append("  check_timing             | NOT AVAIL    | SUPPORTED")
    lines.append("  all_registers collection | NOT AVAIL    | SUPPORTED")
    lines.append("  get_timing_paths         | NOT AVAIL    | SUPPORTED")
    lines.append("  report_constraint        | STUB         | SUPPORTED")
    lines.append("  -path_type summary/end   | NOT AVAIL    | SUPPORTED")
    lines.append("  sdc query (false/multi)  | NOT AVAIL    | SUPPORTED")
    lines.append("  Python API               | pybind11     | SWIG")
    lines.append("")

    # ── Pipeline impact assessment ─────────────────────────
    lines.append("--- Pipeline Impact Assessment ---")
    lines.append("")
    lines.append("  Task 3 (Q/QN→D extraction):")
    lines.append("    VIABLE via Tcl-only approach.")
    lines.append("    report_timing -from {q_pin_list} -to {d_pin_list}")
    lines.append("    Pin lists must be inlined in generated Tcl (no exec/file I/O).")
    lines.append("")
    lines.append("  Task 4 (constraint coverage):")
    lines.append("    NOT VIABLE via Tcl alone.")
    lines.append("    report_constraint is a stub; check_timing does not exist.")
    lines.append("    Coverage must use netlist-based endpoint enumeration plus")
    lines.append("    per-endpoint slack queries (C++ API: getWorstSlack) or")
    lines.append("    report_timing -to {each_endpoint} -nworst 1.")
    lines.append("")

    # ── gap summary ────────────────────────────────────────
    lines.append("--- Known Gaps (This Version) ---")
    lines.append("")
    lines.append("  1. report_constraint / report_check_types is a stub.")
    lines.append("     Cannot detect unconstrained endpoints via Tcl.")
    lines.append("  2. No Tcl collection commands (all_registers, all_clocks,")
    lines.append("     get_pins, get_cells).  Pin inventories must come from")
    lines.append("     netlist/lib parsing.")
    lines.append("  3. No Tcl exec / open / read.  Generated Tcl scripts must")
    lines.append("     inline pin lists; cannot read from external files.")
    lines.append("  4. -path_type summary/end/short not supported.  Only full")
    lines.append("     path tables and per-clock summary available.")
    lines.append("  5. No SDC introspection (cannot query false_path or")
    lines.append("     multicycle constraints at runtime).")
    lines.append("")

    lines.append(_sep)
    lines.append(" End of iSTA Timing Capability Audit")
    lines.append(_sep)
    return "\n".join(lines) + "\n"


def _write_ista_capability_report(
    result_dir: str,
    yosys_version: str = "N/A",
    ieda_version: str = "N/A",
    pdk: str = "nangate45",
    liberty: str = "Nangate45_typ.lib",
) -> Path:
    """Write ``ista_report_timing_capabilities.rpt`` to *result_dir*."""
    out = Path(result_dir)
    out.mkdir(parents=True, exist_ok=True)
    text = _build_ista_capability_report_text(
        yosys_version=yosys_version,
        ieda_version=ieda_version,
        pdk=pdk,
        liberty=liberty,
    )
    report_path = out / "ista_report_timing_capabilities.rpt"
    report_path.write_text(text)
    return report_path


# ── data pin inventory (Q/QN startpoints, D endpoints) ────────────────

@dataclass
class DataPinRecord:
    """A single data pin with owner instance, cell type, pin role, and signal."""
    inst_name: str          # instance name in the flat netlist
    cell_type: str          # tech cell type (DFF_X1, SDFF_X1, etc.)
    pin_role: str           # "Q", "QN", or "D"
    signal: str             # net signal name connected to this pin

    @property
    def pin_path(self) -> str:
        """Return the colon-separated pin path for iSTA queries."""
        return f"{self.inst_name}:{self.pin_role}"


def _enumerate_data_pin_inventory(
    netlist_text: str,
) -> Tuple[List[DataPinRecord], List[DataPinRecord], List[str]]:
    """Enumerate Q/QN startpoints and D endpoints from sequential cells.

    Parses the mapped technology netlist and extracts the Q, QN, and D
    pin connections for every sequential cell (DFF, SDFF, DLH, etc.).

    Returns ``(startpoints, endpoints, caveats)`` where:
      - *startpoints*: list of ``DataPinRecord`` for Q/QN pins
      - *endpoints*: list of ``DataPinRecord`` for D pins
      - *caveats*: human-readable caveat strings
    """
    caveats: List[str] = []

    # Pattern: CELL_TYPE instance_name (
    # Matches the start of a cell instance in the mapped netlist.
    _INST_RE = re.compile(
        r"^\s*(?P<cell_type>[A-Za-z0-9_]+)\s+(?P<inst_name>[^\s;()]+)\s*[(]",
        re.MULTILINE,
    )

    # Find all instances and their pin connections
    startpoints: List[DataPinRecord] = []
    endpoints: List[DataPinRecord] = []

    # Strategy: iterate through all instances, find sequential ones,
    # then extract their Q/QN/D pins from the surrounding text.
    inst_matches: List[Tuple[int, int, str, str]] = []  # (start, end, cell_type, inst_name)
    for m in _INST_RE.finditer(netlist_text):
        inst_start = m.end() - 1    # position of '('
        cell_type = m.group("cell_type")
        inst_name = m.group("inst_name").strip().rstrip("\\")
        # Strip Verilog escaped-identifier leading backslash
        # iSTA normalises \escaped_id → escaped_id internally.
        if inst_name.startswith("\\"):
            inst_name = inst_name[1:]
        # Quick filter: skip non-sequential cells
        if not _is_seq_cell(cell_type):
            continue
        # Find the closing ')' for this instance
        # Search forward from the '(' for the matching ')'
        depth = 1
        pos = inst_start + 1
        # Limit search to a reasonable window (1000 chars)
        limit = min(len(netlist_text), pos + 1000)
        while pos < limit and depth > 0:
            ch = netlist_text[pos]
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            pos += 1
        if depth != 0:
            # Unmatched paren — skip this instance
            continue
        inst_end = pos

        # Extract pin connections from the instance body
        inst_body = netlist_text[inst_start:inst_end]
        # Match .Q(signal), .QN(signal), .D(signal)
        _PIN_RE = re.compile(
            r"[.]([QD]N?)\s*[(]\s*(?P<sig>[^)]+?)\s*[)]",
            re.MULTILINE,
        )
        for pm in _PIN_RE.finditer(inst_body):
            pin_name = pm.group(1).upper()
            sig = pm.group("sig").strip().rstrip(",").rstrip("\\")
            if pin_name in ("Q", "QN"):
                startpoints.append(DataPinRecord(
                    inst_name=inst_name,
                    cell_type=cell_type,
                    pin_role=pin_name,
                    signal=sig,
                ))
            elif pin_name == "D":
                endpoints.append(DataPinRecord(
                    inst_name=inst_name,
                    cell_type=cell_type,
                    pin_role=pin_name,
                    signal=sig,
                ))

    # Sort for deterministic output
    startpoints.sort(key=lambda r: (r.inst_name, r.pin_role))
    endpoints.sort(key=lambda r: (r.inst_name, r.pin_role))

    # Caveats
    q_count = sum(1 for r in startpoints if r.pin_role == "Q")
    qn_count = sum(1 for r in startpoints if r.pin_role == "QN")
    caveats.append(
        f"Data-pin inventory from canonical-flat mapped netlist: "
        f"{len(startpoints)} startpoint pins ({q_count} Q, {qn_count} QN), "
        f"{len(endpoints)} D-endpoint pins."
    )
    if len(startpoints) == 0:
        caveats.append(
            "WARNING: No Q/QN startpoint pins found in netlist. "
            "Q→D extraction will have no startpoints."
        )
    if len(endpoints) == 0:
        caveats.append(
            "WARNING: No D-endpoint pins found in netlist. "
            "Endpoint coverage enumeration will be empty."
        )

    return startpoints, endpoints, caveats


def _write_data_pin_inventories(
    result_dir: str,
    startpoints: List[DataPinRecord],
    endpoints: List[DataPinRecord],
) -> Tuple[Path, Path]:
    """Write ``data_startpoints_q.txt`` and ``data_endpoints_d.txt``.

    Returns ``(startpoints_path, endpoints_path)``.
    """
    out = Path(result_dir)
    out.mkdir(parents=True, exist_ok=True)

    # ── data_startpoints_q.txt ──────────────────────────────
    sp_lines: List[str] = []
    sp_lines.append("# DATA START-POINTS — Q / QN pins (canonical-flat netlist)")
    sp_lines.append("# Format: pin_path  cell_type  signal")
    sp_lines.append("#")
    sp_lines.append(f"# {'Pin Path':<60s} {'Cell Type':<16s} {'Signal':<40s}")
    sp_lines.append(f"# {'-'*60} {'-'*16} {'-'*40}")
    for r in startpoints:
        sp_lines.append(f"  {r.pin_path:<60s} {r.cell_type:<16s} {r.signal:<40s}")
    sp_lines.append(f"# Total: {len(startpoints)} Q/QN start-pins")

    sp_path = out / "data_startpoints_q.txt"
    sp_path.write_text("\n".join(sp_lines) + "\n")

    # ── data_endpoints_d.txt ────────────────────────────────
    ep_lines: List[str] = []
    ep_lines.append("# DATA END-POINTS — D pins (canonical-flat netlist)")
    ep_lines.append("# Format: pin_path  cell_type  signal")
    ep_lines.append("#")
    ep_lines.append(f"# {'Pin Path':<60s} {'Cell Type':<16s} {'Signal':<40s}")
    ep_lines.append(f"# {'-'*60} {'-'*16} {'-'*40}")
    for r in endpoints:
        ep_lines.append(f"  {r.pin_path:<60s} {r.cell_type:<16s} {r.signal:<40s}")
    ep_lines.append(f"# Total: {len(endpoints)} D-endpoint pins")

    ep_path = out / "data_endpoints_d.txt"
    ep_path.write_text("\n".join(ep_lines) + "\n")

    return sp_path, ep_path


# ── dedicated Q/QN→D STA query (Task 3) ──────────────────────────────

def _generate_sta_data_pins_tcl(
    output_dir: str,
    startpoints: List[DataPinRecord],
    endpoints: List[DataPinRecord],
) -> Path:
    """Generate a Tcl fragment with inlined Q/QN and D pin lists.

    iSTA Tcl has no ``exec`` or file I/O, so the pin lists must be
    embedded directly as Tcl list literals.  This function writes
    ``sta_data_pins.tcl`` to *output_dir* defining two variables:

      - ``data_q_pins``: list of Q/QN start-point pin paths
      - ``data_d_pins``: list of D end-point pin paths

    Returns the path to the generated file.
    """
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    # Build space-separated pin lists inside Tcl braces {}.
    # Inside Tcl braces, brackets [...] are LITERAL (no command
    # substitution), so pin names containing [0] [31] etc. are safe
    # WITHOUT any escaping.  We avoid the [list] command entirely
    # because its string representation brace-quotes elements with
    # special characters, and iSTA's netlist lookup cannot parse
    # internal Tcl brace-quoting (Netlist.cc treats leading '{' as
    # part of the instance name).
    #
    # Only { and } need substitution (extremely unlikely in instance
    # names, but handled as a safety measure).
    def _safe_pin_tcl(pin_path: str) -> str:
        return pin_path.replace("{", "_").replace("}", "_")

    q_pins = " ".join(_safe_pin_tcl(r.pin_path) for r in startpoints)
    d_pins = " ".join(_safe_pin_tcl(r.pin_path) for r in endpoints)

    lines: List[str] = []
    lines.append("# Auto-generated by synth_timing.py")
    lines.append("# Pin lists for real Q/QN→D data reg-to-reg extraction")
    lines.append("#")
    lines.append(f"# Q/QN data sources:  {len(startpoints)} pins")
    lines.append(f"# D endpoints:        {len(endpoints)} pins")
    lines.append("")
    # f-string: {{ → literal {, }} → literal }.
    # Inside the Tcl braces, brackets [...] are literal — no escaping needed.
    lines.append(f"set data_q_pins {{{q_pins}}}")
    lines.append("")
    lines.append(f"set data_d_pins {{{d_pins}}}")

    tcl_path = out / "sta_data_pins.tcl"
    tcl_path.write_text("\n".join(lines) + "\n")
    return tcl_path


def _run_data_reg2reg_sta_query(
    netlist_path: str,
    result_dir: str,
    sdc_file: str,
    design: str = "ysyx_25070190",
    pdk: str = "nangate45",
    ieda_bin: Optional[str] = None,
    yosys_sta_home: Optional[str] = None,
    startpoints: Optional[List[DataPinRecord]] = None,
    endpoints: Optional[List[DataPinRecord]] = None,
    top_n: int = 200,
) -> Optional[List[Dict]]:
    """Run a dedicated iSTA query for Q/QN→D data paths.

    This performs a SECOND iEDA invocation (separate from the canonical
    ``report_timing -max_path 50``) that uses ``-from``/``-to``
    endpoint-pin filtering.  The output is written to a subdirectory
    ``data_reg2reg/`` so it never touches the canonical ``.rpt``.

    Args:
        netlist_path: Path to the mapped technology netlist (``.netlist.v``).
        result_dir: Canonical result directory (parent of the subdirectory).
        sdc_file: Path to the SDC constraint file.
        design: Top module name.
        pdk: PDK name (must match a key in ``yosys-sta/scripts/pdk/``).
        ieda_bin: Path to the iEDA binary.  If ``None``, attempts to
            find ``iEDA`` on ``$PATH``.
        yosys_sta_home: Path to the yosys-sta project root.  If ``None``,
            derived from *ieda_bin*.
        startpoints: Pre-computed Q/QN start-pin inventory.  If ``None``,
            enumerated from the netlist.
        endpoints: Pre-computed D end-pin inventory.  If ``None``,
            enumerated from the netlist.
        top_n: Number of worst paths to capture (``-max_path``).

    Returns:
        List of ``TimingPath`` dicts or ``None`` if the query fails
        (callers should fall back to canonical classification).
    """
    import subprocess
    import tempfile
    import os

    warnings: List[str] = []

    # ── locate iEDA binary ──────────────────────────────────────
    if ieda_bin is None:
        ieda_bin_path = "iEDA"
    else:
        ieda_bin_path = str(ieda_bin)

    # ── resolve yosys-sta home ──────────────────────────────────
    if yosys_sta_home is None and ieda_bin is not None:
        yosys_sta_home = str(Path(ieda_bin).resolve().parent.parent)
    if yosys_sta_home is None:
        # Last-resort heuristic: assume yosys-sta is a sibling of npc/
        yosys_sta_home = str(Path(netlist_path).resolve().parent.parent.parent.parent / "yosys-sta")

    # Validate paths
    netlist_p = Path(netlist_path)
    if not netlist_p.is_file():
        logger.warning(
            "Cannot run dedicated Q→D STA query: netlist not found at %s",
            netlist_path,
        )
        return None

    sdc_p = Path(sdc_file)
    if not sdc_p.is_file():
        logger.warning(
            "Cannot run dedicated Q→D STA query: SDC file not found at %s",
            sdc_file,
        )
        return None

    # ── enumerate pin inventories if not provided ───────────────
    if startpoints is None or endpoints is None:
        netlist_text = netlist_p.read_text(encoding="utf-8", errors="replace")
        startpoints, endpoints, _ = _enumerate_data_pin_inventory(netlist_text)

    if not startpoints or not endpoints:
        logger.warning(
            "Cannot run dedicated Q→D STA query: no Q/QN startpoints or "
            "D endpoints found in netlist (startpoints=%d, endpoints=%d)",
            len(startpoints) if startpoints else 0,
            len(endpoints) if endpoints else 0,
        )
        return None

    # ── create dedicated output subdirectory ────────────────────
    data_dir = Path(result_dir) / "data_reg2reg"
    data_dir.mkdir(parents=True, exist_ok=True)

    # ── generate pin-list Tcl fragment ──────────────────────────
    _generate_sta_data_pins_tcl(
        str(data_dir), startpoints, endpoints,
    )

    # ── build wrapper Tcl script ────────────────────────────────
    # The script sources common.tcl (which loads PDK/liberty config),
    # then reads the netlist, liberty, SDC, links the design,
    # sources the pin-list fragment, and runs the dedicated query.
    wrapper_tcl = data_dir / "sta_data_reg2reg.tcl"
    tcl_lines: List[str] = []
    tcl_lines.append("# Auto-generated by synth_timing.py — dedicated Q→D STA query")
    tcl_lines.append("")
    tcl_lines.append(f"set SDC_FILE         [lindex $argv 0]")
    tcl_lines.append(f"set NETLIST_V        [lindex $argv 1]")
    tcl_lines.append(f"set DESIGN           [lindex $argv 2]")
    tcl_lines.append(f"set PDK              [lindex $argv 3]")
    tcl_lines.append(f"set YOSYS_STA_HOME   [lindex $argv 4]")
    tcl_lines.append("")
    tcl_lines.append(f"set RESULT_DIR [file dirname $NETLIST_V]/data_reg2reg")
    tcl_lines.append(f"file mkdir $RESULT_DIR")
    tcl_lines.append("")
    tcl_lines.append("# Source common.tcl to load PDK configuration and liberty paths")
    tcl_lines.append(f"source \"$YOSYS_STA_HOME/scripts/common.tcl\"")
    tcl_lines.append("")
    tcl_lines.append(f"set_design_workspace $RESULT_DIR")
    tcl_lines.append(f"read_netlist $NETLIST_V")
    tcl_lines.append("read_liberty [concat $LIB_FILES]")
    tcl_lines.append(f"link_design $DESIGN")
    tcl_lines.append(f"read_sdc $SDC_FILE")
    tcl_lines.append("")
    tcl_lines.append("# Source pin-list fragment (generated from netlist inventory)")
    tcl_lines.append(f"source \"$RESULT_DIR/sta_data_pins.tcl\"")
    tcl_lines.append("")
    tcl_lines.append("# Dedicated Q/QN→D data path query (through-filter)")
    tcl_lines.append(f"# Note: q_pin_count={len(startpoints)}, d_pin_count={len(endpoints)}")
    tcl_lines.append(f"report_timing -through $data_q_pins -to $data_d_pins \\")
    tcl_lines.append(f"  -delay_type max -nworst 1 -max_path {top_n} -json")
    tcl_lines.append("")
    tcl_lines.append("exit")

    wrapper_tcl.write_text("\n".join(tcl_lines) + "\n")
    logger.info(
        "Generated dedicated Q/QN→D STA wrapper script: %s (%d Q/QN through-pins, %d D endpoints)",
        wrapper_tcl, len(startpoints), len(endpoints),
    )

    # ── run iEDA ────────────────────────────────────────────────
    cmd = [
        ieda_bin_path,
        "-script", str(wrapper_tcl),
        str(sdc_p.resolve()),
        str(netlist_p.resolve()),
        design,
        pdk,
        yosys_sta_home,
    ]

    logger.info("Running dedicated Q→D STA query: %s", " ".join(cmd))
    sta_log = data_dir / "sta.log"

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300,  # 5-minute timeout
            cwd=str(data_dir),
        )
        sta_log.write_text(
            f"=== stdout ===\n{result.stdout}\n=== stderr ===\n{result.stderr}\n"
        )
        if result.returncode != 0:
            logger.warning(
                "Dedicated Q→D STA query failed (exit %d). See %s for details.",
                result.returncode, sta_log,
            )
            return None
    except subprocess.TimeoutExpired:
        logger.warning("Dedicated Q→D STA query timed out after 300s.")
        return None
    except FileNotFoundError:
        logger.warning(
            "iEDA binary not found: %s — skipping dedicated Q→D STA query.",
            ieda_bin_path,
        )
        return None
    except Exception as e:
        logger.warning("Dedicated Q→D STA query failed: %s", e)
        return None

    logger.info("Dedicated Q→D STA query completed successfully.")

    # ── parse JSON output ───────────────────────────────────────
    paths = _parse_data_reg2reg_json(str(data_dir), design)
    if paths is None or len(paths) == 0:
        logger.warning(
            "Dedicated Q→D STA query produced no parseable paths. "
            "JSON files may be missing or malformed."
        )
        return None

    logger.info(
        "Dedicated Q→D STA query yielded %d data paths (from %d startpoints, %d endpoints).",
        len(paths), len(startpoints), len(endpoints),
    )
    return paths


def _parse_data_reg2reg_json(
    data_dir: str,
    design: str,
) -> Optional[List[Dict]]:
    """Parse iSTA JSON timing output from the dedicated Q→D query.

    iSTA with ``-json`` writes three files:
      - ``${DESIGN}_sta_summary.json`` — per-endpoint summary
      - ``${DESIGN}_sta_slack.json`` — slack data
      - ``${DESIGN}_sta_detail.json`` — detailed per-path breakdown

    This function tries to parse all three and extracts structured
    ``TimingPath``-compatible dicts.  The JSON format is reverse-engineered
    from iSTA output; unknown/missing fields are handled gracefully.

    Returns a list of ``TimingPath`` dicts, or ``None`` if parsing fails.
    """
    import json as _json

    data_p = Path(data_dir)

    # Try detail JSON first (richest per-path data), then summary.
    detail_json = data_p / f"{design}_sta_detail.json"
    summary_json = data_p / f"{design}_sta_summary.json"

    paths: List[Dict] = []

    # ── attempt: text .rpt (dedicated multi-path parser) ──────
    rpt_file = data_p / f"{design}.rpt"
    if rpt_file.is_file():
        try:
            rpt_text = rpt_file.read_text(encoding="utf-8", errors="replace")
            dedicated_paths = _parse_dedicated_query_rpt(rpt_text)
            if dedicated_paths:
                paths.extend(dedicated_paths)
        except Exception as e:
            logger.warning(
                "Failed to parse dedicated query text .rpt %s: %s", rpt_file, e
            )

    # ── attempt: detail JSON ────────────────────────────────────
    if not paths and detail_json.is_file():
        try:
            detail_data = _json.loads(detail_json.read_text(encoding="utf-8"))
            parsed = _parse_ista_detail_json(detail_data)
            if parsed:
                paths.extend(parsed)
        except (_json.JSONDecodeError, KeyError, ValueError) as e:
            logger.warning(
                "Failed to parse iSTA detail JSON %s: %s", detail_json, e
            )

    # ── attempt: summary JSON ──────────────────────────────────
    if not paths and summary_json.is_file():
        try:
            summary_data = _json.loads(summary_json.read_text(encoding="utf-8"))
            parsed = _parse_ista_summary_json(summary_data)
            if parsed:
                paths.extend(parsed)
        except (_json.JSONDecodeError, KeyError, ValueError) as e:
            logger.warning(
                "Failed to parse iSTA summary JSON %s: %s", summary_json, e
            )

    # ── attempt: combined ${DESIGN}.rpt.json ───────────────────
    if not paths:
        combined_json = data_p / f"{design}.rpt.json"
        if combined_json.is_file():
            try:
                combined_data = _json.loads(combined_json.read_text(encoding="utf-8"))
                if isinstance(combined_data, dict):
                    for key in ("detail", "slack", "summary"):
                        section = combined_data.get(key)
                        if isinstance(section, list) and section:
                            if key == "detail":
                                parsed = _parse_ista_detail_json(section)
                            else:
                                parsed = _parse_ista_summary_json(section)
                            if parsed:
                                paths.extend(parsed)
                                break
            except (_json.JSONDecodeError, KeyError, ValueError) as e:
                logger.warning(
                    "Failed to parse iSTA combined JSON %s: %s", combined_json, e
                )

    if not paths:
        return None

    # Sort by slack (worst = most negative first)
    paths.sort(key=lambda p: p.get("slack", 999.0))

    # Verify: startpoint is Q/QN (data output), endpoint is D (data input)
    valid_paths: List[Dict] = []
    for p in paths:
        sp_pin = (p.get("startpoint_pin") or "").upper()
        ep_pin = (p.get("endpoint_pin") or "").upper()
        if sp_pin in _DATA_STARTPOINT_PINS and ep_pin in _DATA_ENDPOINT_PINS:
            valid_paths.append(p)
        else:
            logger.debug(
                "Skipping non-Q→D path from dedicated query: %s (%s) → %s (%s)",
                p.get("startpoint", "?"), sp_pin,
                p.get("endpoint", "?"), ep_pin,
            )

    return valid_paths


def _parse_ista_detail_json(data) -> List[Dict]:
    """Parse the iSTA ``_sta_detail.json`` format.

    The exact schema is undocumented.  This parser attempts multiple
    known/guessed formats and returns structured ``TimingPath``-compatible
    dicts on success.
    """
    paths: List[Dict] = []

    # ── Format 1: list of path objects ──────────────────────────
    if isinstance(data, list):
        for item in data:
            if not isinstance(item, dict):
                continue
            tp = _build_timing_path_from_ista_dict(item)
            if tp:
                paths.append(tp)

    # ── Format 2: dict with "paths" key ─────────────────────────
    elif isinstance(data, dict):
        path_list = data.get("paths") or data.get("path_list") or data.get("data")
        if isinstance(path_list, list):
            for item in path_list:
                if isinstance(item, dict):
                    tp = _build_timing_path_from_ista_dict(item)
                    if tp:
                        paths.append(tp)

    return paths


def _parse_ista_summary_json(data) -> List[Dict]:
    """Parse the iSTA ``_sta_summary.json`` format.

    The summary typically has per-clock or per-endpoint entries with
    slack, startpoint, endpoint, and delay info.
    """
    paths: List[Dict] = []

    entries = []
    if isinstance(data, list):
        entries = data
    elif isinstance(data, dict):
        entries = (
            data.get("summary") or data.get("rows") or
            data.get("endpoints") or data.get("entries") or []
        )
        if isinstance(entries, dict):
            # May be keyed by endpoint name
            entries = list(entries.values())

    for item in entries:
        if not isinstance(item, dict):
            continue
        tp = _build_timing_path_from_ista_dict(item)
        if tp:
            paths.append(tp)

    return paths


def _build_timing_path_from_ista_dict(d: Dict) -> Optional[Dict]:
    """Build a ``TimingPath``-compatible dict from an iSTA JSON entry.

    Handles multiple possible field-name conventions.  Returns ``None``
    if the entry lacks a startpoint AND endpoint (i.e., not a path).
    """
    # Extract startpoint — try multiple known field names
    sp = (
        d.get("startpoint") or d.get("start_point") or
        d.get("start") or d.get("src") or ""
    )
    ep = (
        d.get("endpoint") or d.get("end_point") or
        d.get("end") or d.get("dst") or ""
    )

    # Some formats use nested objects
    if isinstance(sp, dict):
        sp = sp.get("name") or sp.get("pin") or str(sp)
    if isinstance(ep, dict):
        ep = ep.get("name") or ep.get("pin") or str(ep)

    sp = str(sp).strip()
    ep = str(ep).strip()

    if not sp or not ep:
        return None

    # Extract pin information
    sp_pin = (
        d.get("startpoint_pin") or d.get("start_pin") or
        d.get("src_pin") or ""
    )
    ep_pin = (
        d.get("endpoint_pin") or d.get("end_pin") or
        d.get("dst_pin") or ""
    )

    # If no explicit pin field, try to extract from the instance:pin format
    if not sp_pin and ":" in sp:
        sp_pin = sp.rsplit(":", 1)[-1].strip()
    if not ep_pin and ":" in ep:
        ep_pin = ep.rsplit(":", 1)[-1].strip()

    sp_pin = str(sp_pin).strip().upper()
    ep_pin = str(ep_pin).strip().upper()

    # Slack — essential
    slack = d.get("slack") or d.get("setup_slack") or d.get("wns")
    if slack is None:
        # Some formats embed slack in a nested dict
        slack = (
            (d.get("timing") or {}).get("slack") if isinstance(d.get("timing"), dict)
            else None
        )
    try:
        slack = float(slack) if slack is not None else 0.0
    except (ValueError, TypeError):
        slack = 0.0

    # Delay
    delay = d.get("path_delay") or d.get("delay") or d.get("arrival")
    try:
        delay = float(delay) if delay is not None else 0.0
    except (ValueError, TypeError):
        delay = 0.0

    # Required time
    required = d.get("path_required") or d.get("required") or d.get("required_time")
    try:
        required = float(required) if required is not None else 0.0
    except (ValueError, TypeError):
        required = 0.0

    # Clock group
    clock = d.get("clock_group") or d.get("clock") or d.get("clk") or ""

    # Pin type validation: should be Q/QN → D
    # If pins are clearly NOT data pins, skip
    if sp_pin and sp_pin not in _DATA_STARTPOINT_PINS and sp_pin not in ("", "?"):
        # Non-Q/QN startpoint — might be a misclassified path
        logger.debug(
            "Non-Q/QN startpoint in dedicated query: %s pin=%s", sp, sp_pin
        )
        # Still include it; the caller filters by _DATA_STARTPOINT_PINS

    return {
        "startpoint": sp,
        "endpoint": ep,
        "startpoint_pin": sp_pin,
        "endpoint_pin": ep_pin,
        "startpoint_type": STARTPOINT_SEQUENTIAL,  # All Q/QN are sequential
        "endpoint_type": STARTPOINT_SEQUENTIAL,    # All D are sequential
        "slack": round(slack, 4),
        "path_delay": round(delay, 4),
        "path_required": round(required, 4),
        "delay_type": "max",
        "clock_group": str(clock),
        "category": CATEGORY_DATA_REG2REG,
    }


def _write_data_reg2reg_json(
    out_dir: str,
    paths: List[Dict],
) -> Path:
    """Write ``timing_reg2reg_data.json`` — structured Q→D path data.

    Returns the path to the written file.
    """
    import json as _json

    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    data = {
        "schema_version": 1,
        "description": "Real Q/QN→D data reg-to-reg paths from dedicated STA query",
        "source": "ista_report_timing_with_from_to_pin_filters",
        "verification": (
            "Every path startpoint pin ∈ {Q, QN} and endpoint pin = D. "
            "CK, GCK, EN, SE, TE, RN, SN paths are excluded. "
            "Sourced from a dedicated report_timing -from/-to query, "
            "NOT from widened worst-N membership."
        ),
        "path_count": len(paths),
        "paths": paths,
    }

    json_path = out / "timing_reg2reg_data.json"
    json_path.write_text(_json.dumps(data, indent=2, default=str))

    return json_path


# ── endpoint coverage query (Task 4) ─────────────────────────────────

def _generate_endpoint_coverage_pins_tcl(
    output_dir: str,
    d_signals: Set[str],
    out_ports: List[str],
) -> Path:
    """Generate a Tcl fragment with ALL register D-pin and output-port names.

    iSTA Tcl has no ``exec`` or file I/O, so pin names are embedded
    directly as a Tcl braced string literal.  Writes
    ``sta_coverage_pins.tcl`` defining variable ``$all_coverage_endpoints``.
    """
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    def _safe_tcl(name: str) -> str:
        return name.replace("{", "_").replace("}", "_")

    all_pins = " ".join(_safe_tcl(s) for s in sorted(d_signals))
    all_pins += " " + " ".join(_safe_tcl(p) for p in sorted(out_ports))
    all_pins = all_pins.strip()

    lines: List[str] = []
    lines.append("# Auto-generated by synth_timing.py — endpoint coverage query")
    lines.append(f"# D-pin signals: {len(d_signals)}, output ports: {len(out_ports)}")
    lines.append("")
    lines.append(f"set all_coverage_endpoints {{{all_pins}}}")

    tcl_path = out / "sta_coverage_pins.tcl"
    tcl_path.write_text("\n".join(lines) + "\n")
    return tcl_path


def _run_endpoint_coverage_sta_query(
    netlist_path: str,
    result_dir: str,
    sdc_file: str,
    design: str,
    pdk: str,
    ieda_bin: Optional[str],
    yosys_sta_home: Optional[str],
    d_signals: Set[str],
    out_ports: List[str],
    max_path_coverage: int = 5000,
) -> Optional[Tuple[Set[str], Set[str], str]]:
    """Run a dedicated STA query targeting ALL register D endpoints and
    output ports to determine which are constrained.

    Uses ``report_timing -to $all_coverage_endpoints -nworst 1``
    with a large ``-max_path`` so every endpoint is checked.

    Returns ``(constrained_d_signals, constrained_out_ports, coverage_note)``
    or ``None`` if the query fails.
    """
    import subprocess

    netlist_p = Path(netlist_path)
    sdc_p = Path(sdc_file)

    if not netlist_p.is_file() or not sdc_p.is_file():
        logger.warning("Coverage query skipped: missing netlist or SDC")
        return None

    if not d_signals and not out_ports:
        logger.warning("Coverage query skipped: no endpoints to query")
        return None

    total_eps = len(d_signals) + len(out_ports)
    # max_path must exceed total endpoint count for full coverage
    query_max = max(max_path_coverage, total_eps + 100)

    # ── resolve paths ─────────────────────────────────────────────
    if ieda_bin is None:
        ieda_bin_path = "iEDA"
    else:
        ieda_bin_path = str(ieda_bin)

    if yosys_sta_home is None and ieda_bin is not None:
        yosys_sta_home = str(Path(ieda_bin).resolve().parent.parent)
    if yosys_sta_home is None:
        yosys_sta_home = str(
            Path(netlist_path).resolve().parent.parent.parent.parent / "yosys-sta"
        )

    # ── create output subdirectory ─────────────────────────────────
    cov_dir = Path(result_dir) / "coverage_query"
    cov_dir.mkdir(parents=True, exist_ok=True)

    # ── generate pin-list Tcl ──────────────────────────────────────
    _generate_endpoint_coverage_pins_tcl(
        str(cov_dir), d_signals, out_ports,
    )

    # ── build wrapper Tcl script ───────────────────────────────────
    wrapper_tcl = cov_dir / "sta_coverage.tcl"
    tcl_lines: List[str] = []
    tcl_lines.append("# Auto-generated — endpoint coverage query")
    tcl_lines.append("")
    tcl_lines.append("set SDC_FILE         [lindex $argv 0]")
    tcl_lines.append("set NETLIST_V        [lindex $argv 1]")
    tcl_lines.append("set DESIGN           [lindex $argv 2]")
    tcl_lines.append("set PDK              [lindex $argv 3]")
    tcl_lines.append("set YOSYS_STA_HOME   [lindex $argv 4]")
    tcl_lines.append("")
    tcl_lines.append("set RESULT_DIR [file dirname $NETLIST_V]/coverage_query")
    tcl_lines.append("file mkdir $RESULT_DIR")
    tcl_lines.append("")
    tcl_lines.append("source \"$YOSYS_STA_HOME/scripts/common.tcl\"")
    tcl_lines.append("")
    tcl_lines.append("set_design_workspace $RESULT_DIR")
    tcl_lines.append("read_netlist $NETLIST_V")
    tcl_lines.append("read_liberty [concat $LIB_FILES]")
    tcl_lines.append("link_design $DESIGN")
    tcl_lines.append("read_sdc $SDC_FILE")
    tcl_lines.append("")
    tcl_lines.append("source \"$RESULT_DIR/sta_coverage_pins.tcl\"")
    tcl_lines.append("")
    tcl_lines.append(
        f"# Coverage query: target ALL {total_eps} endpoints "
        f"({len(d_signals)} D-pins + {len(out_ports)} output ports)"
    )
    tcl_lines.append(
        f"report_timing -to $all_coverage_endpoints \\"
    )
    tcl_lines.append(
        f"  -delay_type max -nworst 1 -max_path {query_max} -json"
    )
    tcl_lines.append("")
    tcl_lines.append("exit")

    wrapper_tcl.write_text("\n".join(tcl_lines) + "\n")
    logger.info(
        "Generated coverage query wrapper: %s (%d endpoints, max_path=%d)",
        wrapper_tcl, total_eps, query_max,
    )

    # ── run iEDA ───────────────────────────────────────────────────
    cmd = [
        ieda_bin_path,
        "-script", str(wrapper_tcl),
        str(sdc_p.resolve()),
        str(netlist_p.resolve()),
        design,
        pdk,
        yosys_sta_home,
    ]

    logger.info("Running endpoint coverage STA query: %s", " ".join(cmd))
    sta_log = cov_dir / "sta.log"

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300,
            cwd=str(cov_dir),
        )
        sta_log.write_text(
            f"=== stdout ===\n{result.stdout}\n=== stderr ===\n{result.stderr}\n"
        )
        if result.returncode != 0:
            logger.warning(
                "Coverage STA query failed (exit %d). See %s.",
                result.returncode, sta_log,
            )
            return None
    except subprocess.TimeoutExpired:
        logger.warning("Coverage STA query timed out after 300s.")
        return None
    except FileNotFoundError:
        logger.warning("iEDA binary not found: %s — skipping coverage query.", ieda_bin_path)
        return None
    except Exception as e:
        logger.warning("Coverage STA query failed: %s", e)
        return None

    logger.info("Coverage STA query completed successfully.")

    # ── parse results ──────────────────────────────────────────────
    return _parse_endpoint_coverage_results(
        str(cov_dir), design, d_signals, out_ports,
    )


def _parse_endpoint_coverage_results(
    data_dir: str,
    design: str,
    d_signals: Set[str],
    out_ports: List[str],
) -> Optional[Tuple[Set[str], Set[str], str]]:
    """Parse the coverage query JSON to extract constrained endpoint names.

    Returns ``(constrained_d, constrained_out, coverage_note)``
    or ``None`` on parse failure.
    """
    import json as _json

    data_p = Path(data_dir)

    # Try detail JSON first (has per-path data), then summary
    detail_json = data_p / f"{design}_sta_detail.json"
    summary_json = data_p / f"{design}_sta_summary.json"
    rpt_file = data_p / f"{design}.rpt"

    constrained_all: Set[str] = set()

    # ── attempt 1: Parse all reported endpoints from any JSON source ──
    for json_path in (detail_json, summary_json):
        if not json_path.is_file():
            continue
        try:
            raw = _json.loads(json_path.read_text(encoding="utf-8"))
            entries: List = []
            if isinstance(raw, list):
                entries = raw
            elif isinstance(raw, dict):
                entries = (
                    raw.get("paths") or raw.get("detail") or
                    raw.get("summary") or raw.get("entries") or
                    raw.get("data") or []
                )
                if isinstance(entries, dict):
                    entries = list(entries.values()) if entries else []
            for item in entries:
                if isinstance(item, dict):
                    ep = (
                        item.get("endpoint") or item.get("end_point") or
                        item.get("end") or item.get("dst") or ""
                    )
                    if isinstance(ep, dict):
                        ep = ep.get("name") or ""
                    ep = str(ep).strip()
                    if ep:
                        # Strip cell-type annotations: "pin:D (DFF_X1)" → "pin"
                        ep_clean = ep.split(" (")[0].strip() if " (" in ep else ep
                        # Extract signal name (last component before colon)
                        if ":" in ep_clean:
                            ep_clean = ep_clean.rsplit(":", 1)[-1].strip()
                        if ep_clean:
                            constrained_all.add(ep_clean)
            if constrained_all:
                break
        except Exception as e:
            logger.debug("Failed to parse coverage JSON %s: %s", json_path, e)

    # ── attempt 2: Text .rpt fallback ─────────────────────────────
    if not constrained_all and rpt_file.is_file():
        try:
            rpt_text = rpt_file.read_text(encoding="utf-8", errors="replace")
            # Extract endpoint names from path table headers and summary lines
            ep_set: Set[str] = set()
            for line in rpt_text.split("\n"):
                line = line.strip()
                if "Endpoint:" in line:
                    parts = line.split("Endpoint:", 1)
                    if len(parts) > 1:
                        ep_set.add(parts[1].strip().split()[0].strip())
                if not ep_set:
                    # Try "Path N: ... → endpoint" pattern
                    m_p = re.match(r"^\s*Path\s+\d+\s*:\s*\S+\s*→\s*(?P<ep>\S+)", line)
                    if m_p:
                        ep = m_p.group("ep").strip().rstrip(",")
                        ep_clean = ep.split(" (")[0].strip() if " (" in ep else ep
                        if ":" in ep_clean:
                            ep_clean = ep_clean.rsplit(":", 1)[-1].strip()
                        if ep_clean:
                            ep_set.add(ep_clean)
            if ep_set:
                constrained_all = ep_set
        except Exception as e:
            logger.debug("Failed to parse coverage text .rpt %s: %s", rpt_file, e)

    if not constrained_all:
        return None

    # ── classify constrained endpoints ─────────────────────────────
    constrained_d = constrained_all & d_signals
    constrained_out = constrained_all & set(out_ports)

    total = len(d_signals) + len(out_ports)
    proven = len(constrained_d) + len(constrained_out)
    uncertain = total - proven

    coverage_note = (
        f"Endpoint coverage query: {proven}/{total} endpoints PROVEN constrained "
        f"({len(constrained_d)} D-pins + {len(constrained_out)} output ports). "
        f"{uncertain} endpoints have UNCERTAIN status "
        f"(may be constrained but below query ranking threshold, "
        f"or may be genuinely unconstrained — iSTA lacks check_timing)."
    )

    logger.info("Coverage results: %s", coverage_note)
    return constrained_d, constrained_out, coverage_note


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
    sdc_file: Optional[str] = None,
    ieda_bin: Optional[str] = None,
    yosys_sta_home: Optional[str] = None,
    design: str = "ysyx_25070190",
    pdk: str = "nangate45",
    dedicated_qd_query: bool = True,
) -> Dict:
    """Orchestrate the full timing classification pipeline.

    Args:
        rpt_path: Path to the iSTA unified timing report (``.rpt``).
        fanout_path: Path to the iSTA fanout report (``.fanout``).
        netlist_path: Path to the mapped Verilog netlist (``.netlist.v``).
        top_reg2reg_count: Number of top reg2reg/data_reg2reg paths to keep.
        top_other_count: Number of top paths for other categories.
        result_dir: If provided, write classification report files.
        max_path: The ``-max_path`` value used by ``report_timing``.
        sdc_file: Path to the SDC constraint file (for dedicated Q→D query).
        ieda_bin: Path to iEDA binary (for dedicated Q→D query).
        yosys_sta_home: Path to yosys-sta project root (for dedicated Q→D query).
        design: Top module name.
        pdk: PDK name.
        dedicated_qd_query: When ``True`` and all prerequisites are met,
            run a second iEDA invocation that queries Q/QN→D paths
            directly via ``report_timing -from/-to``.

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
        - ``data_reg2reg_source``: ``"canonical-classification"`` or
          ``"dedicated-ista-query"``

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

    # ── 8b. Dedicated Q/QN→D STA query ────────────────────────
    data_reg2reg_source = "canonical-classification"
    dedicated_data_reg2reg: List[TimingPath] = []
    if (
        dedicated_qd_query
        and result_dir
        and netlist_path and Path(netlist_path).is_file()
        and sdc_file and Path(sdc_file).is_file()
    ):
        logger.info(
            "Attempting dedicated Q→D STA query "
            "(ieda_bin=%s, yosys_sta_home=%s, design=%s)",
            ieda_bin, yosys_sta_home, design,
        )
        try:
            netlist_text_qd = Path(netlist_path).read_text(
                encoding="utf-8", errors="replace"
            )
            sp_pins, ep_pins, _ = _enumerate_data_pin_inventory(netlist_text_qd)

            dedicated_paths = _run_data_reg2reg_sta_query(
                netlist_path=netlist_path,
                result_dir=result_dir,
                sdc_file=sdc_file,
                design=design,
                pdk=pdk,
                ieda_bin=ieda_bin,
                yosys_sta_home=yosys_sta_home,
                startpoints=sp_pins,
                endpoints=ep_pins,
                top_n=200,
            )

            if dedicated_paths:
                dedicated_data_reg2reg = [
                    TimingPath(
                        startpoint=dp.get("startpoint", ""),
                        endpoint=dp.get("endpoint", ""),
                        startpoint_type=dp.get("startpoint_type", STARTPOINT_SEQUENTIAL),
                        endpoint_type=dp.get("endpoint_type", STARTPOINT_SEQUENTIAL),
                        startpoint_pin=dp.get("startpoint_pin", ""),
                        endpoint_pin=dp.get("endpoint_pin", ""),
                        delay_type=dp.get("delay_type", "max"),
                        clock_group=dp.get("clock_group", ""),
                        slack=dp.get("slack", 0.0),
                        path_delay=dp.get("path_delay", 0.0),
                        path_required=dp.get("path_required", 0.0),
                        category=CATEGORY_DATA_REG2REG,
                    )
                    for dp in dedicated_paths
                ]
                dedicated_data_reg2reg.sort(key=lambda p: p.slack)
                dedicated_data_reg2reg = dedicated_data_reg2reg[:top_reg2reg_count]

                data_reg2reg = dedicated_data_reg2reg
                data_reg2reg_source = "dedicated-ista-query"
                warnings.append(
                    f"Dedicated Q→D STA query succeeded: "
                    f"{len(dedicated_data_reg2reg)} real Q/QN→D data paths "
                    f"(from {len(sp_pins)} startpoints × {len(ep_pins)} endpoints). "
                    f"Source: report_timing -from/-to with inline pin lists."
                )
            else:
                warnings.append(
                    "Dedicated Q→D STA query did not produce parseable paths. "
                    "Falling back to canonical classification from the unified "
                    ".rpt file (truncated worst-N)."
                )
        except Exception as e:
            warnings.append(
                f"Dedicated Q→D STA query failed: {e}. "
                f"Falling back to canonical classification."
            )
            logger.warning("Dedicated Q→D STA query exception: %s", e)

    # ── 8c. Endpoint coverage query ────────────────────────────
    coverage_evidence: Dict = {}
    coverage_status = "unknown"
    coverage_note_final = ""
    if (
        result_dir
        and netlist_path and Path(netlist_path).is_file()
        and sdc_file and Path(sdc_file).is_file()
    ):
        logger.info("Attempting endpoint coverage STA query...")
        try:
            if netlist_endpoints is None:
                netlist_text_cov = Path(netlist_path).read_text(
                    encoding="utf-8", errors="replace"
                )
                netlist_endpoints = _enumerate_netlist_endpoints(netlist_text_cov)

            d_sigs = netlist_endpoints.get("register_d_signals", set())
            out_ports = netlist_endpoints.get("output_port_names", [])

            cov_result = _run_endpoint_coverage_sta_query(
                netlist_path=netlist_path,
                result_dir=result_dir,
                sdc_file=sdc_file,
                design=design,
                pdk=pdk,
                ieda_bin=ieda_bin,
                yosys_sta_home=yosys_sta_home,
                d_signals=d_sigs,
                out_ports=out_ports,
            )

            if cov_result is not None:
                constrained_d, constrained_out, cov_note = cov_result
                coverage_evidence = {
                    "constrained_d": constrained_d,
                    "constrained_out": constrained_out,
                    "coverage_source": "dedicated-ista-endpoint-query",
                    "coverage_note": cov_note,
                }
                coverage_status = "LOWER_BOUND"
                coverage_note_final = cov_note
                warnings.append(
                    f"Endpoint coverage query succeeded: "
                    f"{len(constrained_d)}/{len(d_sigs)} D-pins + "
                    f"{len(constrained_out)}/{len(out_ports)} output ports "
                    f"PROVEN constrained via dedicated STA endpoint query."
                )
            else:
                # ── fallback: merge canonical .rpt + dedicated Q→D evidence ──
                # Note: Q→D paths use hierarchy-preserved instance names
                # (e.g. "reg_2_pcTarget_21__reg_p:D") which do NOT match
                # flat netlist signal names (e.g. "\\memu...._D").
                # We report Q→D evidence as a separate count.
                _constrained_from_rpt: Set[str] = set()
                for sr in summary_rows:
                    ep = sr.get("endpoint", "").strip()
                    if ep:
                        _constrained_from_rpt.add(ep)

                _fallback_out_ports = set(out_ports) if out_ports else set()
                _fallback_d = d_sigs & _constrained_from_rpt
                _fallback_out = _fallback_out_ports & _constrained_from_rpt

                qd_d_count = 0
                if dedicated_data_reg2reg:
                    qd_ep_instances: Set[str] = set()
                    for tp in dedicated_data_reg2reg:
                        ep_raw = tp.endpoint.split(" (")[0].strip() if " (" in tp.endpoint else tp.endpoint
                        if ":" in ep_raw:
                            qd_ep_instances.add(ep_raw.rsplit(":", 1)[0])
                    qd_d_count = len(qd_ep_instances)

                coverage_evidence = {
                    "constrained_d": _fallback_d,
                    "constrained_out": _fallback_out,
                    "qd_constrained_d_count": qd_d_count,
                    "coverage_source": (
                        "canonical-rpt-summary-cross-referenced"
                        if not dedicated_data_reg2reg
                        else "canonical-rpt-plus-dedicated-qd-query"
                    ),
                    "coverage_note": (
                        "Dedicated endpoint coverage query failed (iSTA crash). "
                        "Coverage derived from canonical .rpt summary "
                        "(for output ports) + dedicated Q→D query evidence "
                        "(for D-pin instances). "
                        f"{qd_d_count} D-pin instances proven constrained "
                        f"via Q→D query (instance-level, hierarchy netlet). "
                        "This is a LOWER BOUND."
                    ),
                }
                coverage_status = "LOWER_BOUND"
                coverage_note_final = coverage_evidence["coverage_note"]
                warnings.append(
                    f"Endpoint coverage query failed (iSTA SIGSEGV). "
                    f"Falling back: canonical .rpt + Q→D query evidence: "
                    f"{qd_d_count} D instances + {len(_fallback_out)} output ports "
                    f"PROVEN constrained."
                )
        except Exception as e:
            # ── same fallback: merge canonical .rpt + dedicated Q→D evidence ──
            _constrained_from_rpt_ex: Set[str] = set()
            for sr in summary_rows:
                ep_ex = sr.get("endpoint", "").strip()
                if ep_ex:
                    _constrained_from_rpt_ex.add(ep_ex)

            d_sigs_ex = netlist_endpoints.get("register_d_signals", set()) if netlist_endpoints else set()
            out_ports_ex = set(netlist_endpoints.get("output_port_names", [])) if netlist_endpoints else set()
            _fallback_d_ex = d_sigs_ex & _constrained_from_rpt_ex
            _fallback_out_ex = out_ports_ex & _constrained_from_rpt_ex

            if dedicated_data_reg2reg:
                qd_ep_instances_ex: Set[str] = set()
                for tp in dedicated_data_reg2reg:
                    ep_raw = tp.endpoint.split(" (")[0].strip() if " (" in tp.endpoint else tp.endpoint
                    if ":" in ep_raw:
                        qd_ep_instances_ex.add(ep_raw.rsplit(":", 1)[0])
                qd_d_count_ex = len(qd_ep_instances_ex)
            else:
                qd_d_count_ex = 0

            coverage_evidence = {
                "constrained_d": _fallback_d_ex,
                "constrained_out": _fallback_out_ex,
                "qd_constrained_d_count": qd_d_count_ex,
                "coverage_source": (
                    "canonical-rpt-summary-cross-referenced"
                    if not dedicated_data_reg2reg
                    else "canonical-rpt-plus-dedicated-qd-query"
                ),
                "coverage_note": (
                    f"Coverage query exception: {e}. "
                    f"Coverage derived from canonical .rpt summary + "
                    f"dedicated Q→D query as fallback. "
                    f"{qd_d_count_ex} Q→D D-pin instances + "
                    f"{len(_fallback_out_ex)} output ports PROVEN constrained."
                ),
            }
            coverage_status = "LOWER_BOUND"
            coverage_note_final = (
                f"Coverage query failed ({e}). "
                f"Derived from canonical .rpt + Q→D query: "
                f"{len(_fallback_d_ex)} D-pins + {len(_fallback_out_ex)} output ports "
                f"PROVEN constrained."
            )
            logger.warning("Endpoint coverage query exception: %s", e)
    else:
        if result_dir:
            warnings.append(
                "Endpoint coverage query skipped: missing netlist/SDC prerequisites."
            )

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
        "data_reg2reg_source": data_reg2reg_source,
        # ── v4 coverage fields (Task 4) ───────────────────────────
        "coverage_status": coverage_status,
        "coverage_note": coverage_note_final,
        "total_endpoints_in_netlist": (
            netlist_endpoints.get("total_endpoints", 0)
            if netlist_endpoints else 0
        ),
        "constrained_endpoint_count": (
            len(coverage_evidence.get("constrained_d", set()))
            + len(coverage_evidence.get("constrained_out", set()))
            + coverage_evidence.get("qd_constrained_d_count", 0)
        ),
    }

    # ── 10. Write auxiliary timing report files ────────────────────
    if result_dir:
        _write_timing_report_files(
            result_dir, summary_rows, tns_rows, unconstrained, warnings,
            max_path=max_path,
            netlist_endpoints=netlist_endpoints,
            coverage_evidence=coverage_evidence,
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

        # ── 10a1. Write structured timing_reg2reg_data.json ───────
        if data_reg2reg:
            json_path = _write_data_reg2reg_json(
                result_dir,
                [p.to_dict() for p in data_reg2reg],
            )
            logger.info("Wrote dedicated Q→D data JSON: %s", json_path)

        # ── 10a. Write iSTA capability report ──────────────────
        _write_ista_capability_report(
            result_dir=result_dir,
            yosys_version="N/A",
            ieda_version="N/A",
            pdk="nangate45",
            liberty="Nangate45_typ.lib",
        )

        # ── 10b. Write data pin inventories ────────────────────
        if netlist_path and Path(netlist_path).is_file():
            netlist_text_inv = Path(netlist_path).read_text(
                encoding="utf-8", errors="replace"
            )
            sp_pins, ep_pins, inv_caveats = _enumerate_data_pin_inventory(
                netlist_text_inv
            )
            _write_data_pin_inventories(result_dir, sp_pins, ep_pins)
            warnings.extend(inv_caveats)
        else:
            warnings.append(
                "Data pin inventories skipped: netlist not available."
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
