#!/usr/bin/env python3
# ============================================================================
# report_parser.py — Deterministic yosys-sta report parser (fail-closed).
# ============================================================================
# Parses:
#   - iEDA/iSTA timing report       (*.rpt)  → WNS (ns), TNS (ns)
#   - Yosys synthesis statistics  (synth_stat.txt) → cell count, chip area
#   - iSTA classified timing       (*.rpt + .fanout + .netlist.v)
#                                   → reg2reg/in2reg/reg2out/in2out/hold/
#                                     high-fanout/unconstrained
#
# Design principle: ALWAYS fail closed.
#   - Missing field    → exception with precise field name
#   - Ambiguous match  → exception with location details
#   - Truncated file   → exception with expected-but-missing section
# ============================================================================

import re
import sys
from pathlib import Path
from typing import Any, Dict, List


class ParseError(Exception):
    """Raised when a required report field is missing or ambiguous."""


# ── module name normalization ────────────────────────────────────────
# Yosys ``stat -json`` emits Verilog escaped identifiers for module
# names (e.g. ``\\ysyx_25070190``).  The rest of the toolchain uses
# plain names.  Normalize once at the parse boundary.

_ESCAPED_MODULE_RE = re.compile(r"^\\(?P<name>\S+)")


def _normalize_module_name(name: str) -> str:
    """Strip a single leading backslash from a Verilog escaped identifier.

    ``\\modname`` or ``\\modname  `` → ``modname``.
    Already-plain names pass through unchanged.
    """
    m = _ESCAPED_MODULE_RE.match(name)
    return m.group("name") if m else name


def _normalize_module_keys(d: Dict[str, Any]) -> Dict[str, Any]:
    """Return a new dict with every key passed through ``_normalize_module_name``."""
    return {_normalize_module_name(k): v for k, v in d.items()}


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
_SYNTH_CELLS_CURRENT_RE = re.compile(
    r"^\s*([\d,]+)\s+[\d.]+(?:[eE][+-]?\d+)?\s+cells\s*$",
    re.MULTILINE,
)
_SYNTH_AREA_RE = re.compile(
    r"^\s*Chip area for (?:top )?module\s+['\"]?\\?(\S+?)['\"]?\s*:\s+([\d.]+(?:[eE][+-]?\d+)?)\s*$",
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
        {"cell_count": int, "area_um2": float}   — area in µm² for the selected PDK

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
# Yosys JSON statistics  (synth_stat.json  —  `stat -json -liberty $LIBS`)
# ---------------------------------------------------------------------------

# JSON schema produced by `stat -json -liberty <file>`:
#   {
#     "creator": "Yosys <version>",
#     "modules": {
#       "<name>": {
#         "num_cells": <int> | {"count": N, "area": F, "local_count": N, "local_area": F},
#         "num_cells_by_type": { "<cell>": <int> | { ... } },
#         ...
#       }
#     }
#   }
# With -liberty, each field is a 4-key object; without, it is a bare integer.
# This parser handles BOTH formats (conservative, fail-closed).

def parse_synth_json(json_path) -> Dict:
    """Parse the Yosys ``stat -json`` artifact.

    Returns:
        ``{"creator": str, "modules": {module_name: {field: value}}}``

    Raises:
        ParseError: missing required fields, malformed JSON, or type mismatch
        FileNotFoundError: file does not exist
        json.JSONDecodeError: file is not valid JSON
    """
    import json as _json

    json_path = Path(json_path)
    if not json_path.is_file():
        raise FileNotFoundError(f"JSON stats artifact not found: {json_path}")

    try:
        data = _json.loads(json_path.read_text(encoding="utf-8", errors="replace"))
    except _json.JSONDecodeError as e:
        raise ParseError(
            f"JSON stats artifact {json_path} is not valid JSON: {e}"
        )

    if not isinstance(data, dict):
        raise ParseError(
            f"JSON stats artifact {json_path}: expected a JSON object at top level, "
            f"got {type(data).__name__}"
        )

    modules = data.get("modules")
    if modules is None:
        raise ParseError(
            f"JSON stats artifact {json_path}: missing required key 'modules'. "
            f"Available keys: {sorted(data.keys())}"
        )
    if not isinstance(modules, dict):
        raise ParseError(
            f"JSON stats artifact {json_path}: 'modules' must be a JSON object, "
            f"got {type(modules).__name__}"
        )
    if len(modules) == 0:
        raise ParseError(
            f"JSON stats artifact {json_path}: 'modules' object is empty — "
            f"no module statistics found. The synthesis may have failed silently."
        )

    # Validate each module entry has at least num_cells
    for mod_name, mod_data in modules.items():
        if not isinstance(mod_data, dict):
            raise ParseError(
                f"JSON stats artifact {json_path}: module {mod_name!r} data "
                f"is not a JSON object (got {type(mod_data).__name__})"
            )
        if "num_cells" not in mod_data:
            raise ParseError(
                f"JSON stats artifact {json_path}: module {mod_name!r} is "
                f"missing the 'num_cells' field. Module data keys: {sorted(mod_data.keys())}"
            )

    # Normalize escaped Verilog identifiers (``\\\\ysyx_25070190`` → ``ysyx_25070190``)
    # so downstream code uses plain module names consistently.
    data["modules"] = _normalize_module_keys(modules)

    return data


# ---------------------------------------------------------------------------
# Netlist hierarchy parsing  (mapped netlist .v  →  module→children map)
# ---------------------------------------------------------------------------

_NETLIST_MODULE_RE = re.compile(
    r"^\s*module\s+(?P<name>\S+)\s*[(<]",
    re.MULTILINE,
)
_NETLIST_INSTANCE_RE = re.compile(
    r"^\s*(?P<type>\S+)\s+(?P<name>\S+)\s*[(]\s*[.]",
    re.MULTILINE,
)
_NETLIST_ENDMODULE_RE = re.compile(r"^\s*endmodule\b", re.MULTILINE)


def parse_netlist_hierarchy(netlist_path) -> Dict[str, Dict]:
    """Extract module hierarchy from a Yosys-mapped Verilog netlist.

    Parses module declarations and their direct cell/submodule
    instantiations to build a parent→children multiplicity map.

    Returns:
        ``{module_name: {"cells": {cell_type: count},
                         "submodules": {child_type: instance_count}}}``

    Raises:
        ParseError: if the netlist has zero modules or is unparseable
        FileNotFoundError: file does not exist
    """
    netlist_path = Path(netlist_path)
    if not netlist_path.is_file():
        raise FileNotFoundError(f"Netlist not found: {netlist_path}")

    text = netlist_path.read_text(encoding="utf-8", errors="replace")

    modules: Dict[str, Dict] = {}
    module_names = [m.group("name") for m in _NETLIST_MODULE_RE.finditer(text)]

    if not module_names:
        raise ParseError(
            f"Netlist {netlist_path}: no 'module' declarations found. "
            f"File may be empty or not a valid Verilog netlist."
        )

    module_set = frozenset(module_names)

    mod_positions = [
        (m.group("name"), m.start())
        for m in _NETLIST_MODULE_RE.finditer(text)
    ]
    end_positions = [m.end() for m in _NETLIST_ENDMODULE_RE.finditer(text)]

    if len(mod_positions) != len(end_positions):
        raise ParseError(
            f"Netlist {netlist_path}: found {len(mod_positions)} module "
            f"declarations but {len(end_positions)} endmodule markers — "
            f"suspect truncated or malformed file."
        )

    for i, (mod_name, mod_start) in enumerate(mod_positions):
        mod_end = end_positions[i]
        cells: Dict[str, int] = {}
        submodules: Dict[str, int] = {}

        mod_body = text[mod_start:mod_end]
        for m in _NETLIST_INSTANCE_RE.finditer(mod_body):
            cell_type = m.group("type")
            if cell_type in module_set:
                submodules[cell_type] = submodules.get(cell_type, 0) + 1
            else:
                cells[cell_type] = cells.get(cell_type, 0) + 1

        modules[mod_name] = {
            "cells": cells,
            "submodules": submodules,
        }

    return _normalize_module_keys(modules)


# ---------------------------------------------------------------------------
# JSON-inferred hierarchy helpers
# ---------------------------------------------------------------------------

def _resolve_submodule_count(val) -> int:
    """Extract instance count from a ``num_submodules_by_type`` entry.

    Handles both the plain-int format (no ``-liberty``) and the
    ``{count, area}`` object format produced when ``-liberty`` is active.
    """
    if isinstance(val, (int, float)):
        return int(val)
    if isinstance(val, dict):
        return int(val.get("count", val.get("local_count", 0)))
    return 0


def _extract_child_map_from_json(json_modules: Dict[str, Dict]) -> Dict[str, Dict[str, int]]:
    """Derive parent→child multiplicity maps from per-module JSON stats.

    Yosys ``stat -json -liberty`` populates ``num_submodules_by_type``
    for each module that instantiates children.  This function extracts
    those entries and returns a dict suitable as a ``child_map`` for
    ``build_hierarchy_tree``.

    Modules without children (or without the key) default to an empty
    child map.

    Returns:
        ``{parent_module: {child_module_type: instance_count}}``
    """
    child_map: Dict[str, Dict[str, int]] = {}
    for mod_name, mod_data in json_modules.items():
        sub_by_type = mod_data.get("num_submodules_by_type", {})
        if not isinstance(sub_by_type, dict):
            child_map[mod_name] = {}
            continue
        children: Dict[str, int] = {}
        for sub_type, sub_val in sub_by_type.items():
            cnt = _resolve_submodule_count(sub_val)
            if cnt > 0:
                children[sub_type] = cnt
        child_map[mod_name] = children
    return child_map


# ---------------------------------------------------------------------------
# Hierarchy area tree builder  (orchestrates JSON + netlist → area tree)
# ---------------------------------------------------------------------------

def build_hierarchy_area_tree(
    json_path,
    netlist_path=None,
    top_module: str = "ysyx_25070190",
    cell_area_map=None,
) -> List[Dict]:
    """Build a flattened hierarchical area tree from synth artifacts.

    Consumes the Yosys ``stat -json`` artifact and, optionally, the
    mapped Verilog netlist, and returns a list of ``HierarchyRow`` dicts
    representing every module in the design with:
    - instance path, module name, parent path, depth
    - local and recursive cell counts, area, % of top area
    - cell-category breakdown (sequential, combinational, etc.)

    Child-module relationships are resolved in priority order:

    1. **Netlist-derived** — when the mapped netlist has multiple
       modules, their instantiated children provide the ground-truth
       hierarchy.  This is the gold-standard path for hierarchy-preserved
       synth runs.
    2. **JSON-inferred** — when the netlist is flat (single module,
       the common STA-flow case) AND the JSON contains multiple modules
       with ``num_submodules_by_type``, the hierarchy is reconstructed
       from those submodule-count entries.
    3. **Flat fallback** — when neither source yields submodule
       information, every module is treated as a leaf with no children
       (single-row tree).

    Args:
        json_path: Path to a ``stat -json`` artifact (either flat
            ``synth_stat.json`` or hierarchy-preserved
            ``synth_hierarchy.json``).
        netlist_path: Path to the mapped ``.netlist.v`` (optional).
        top_module: Name of the top-level RTL module (for JSON lookup).

    Returns:
        List of dicts, each a serialised ``HierarchyRow``.
    """
    from synth_hierarchy import build_hierarchy_tree as _build

    json_data = parse_synth_json(json_path)
    json_modules = json_data["modules"]

    # ── step 1: try netlist-based hierarchy ──────────────────────
    child_map: Dict[str, Dict[str, int]] = {}
    hierarchy_source = "flat-fallback"

    if netlist_path is not None and Path(netlist_path).is_file():
        netlist_hier = parse_netlist_hierarchy(netlist_path)
        netlist_modules = list(netlist_hier.keys())
        if len(netlist_modules) == 1:
            top_module = netlist_modules[0]

        child_map = {
            mod: data.get("submodules", {})
            for mod, data in netlist_hier.items()
        }
        # If any module has non-empty submodules, the netlist is the
        # authoritative hierarchy source.
        if any(len(v) > 0 for v in child_map.values()):
            hierarchy_source = "netlist-derived"

    # ── step 2: fall back to JSON-inferred hierarchy ─────────────
    if hierarchy_source == "flat-fallback":
        json_child_map = _extract_child_map_from_json(json_modules)
        if any(len(v) > 0 for v in json_child_map.values()):
            child_map = json_child_map
            hierarchy_source = "json-derived"

    # Build child_map for modules that appear in JSON but not in
    # child_map (leaves with no children).
    for mod_name in json_modules:
        if mod_name not in child_map:
            child_map[mod_name] = {}

    rows = _build(json_modules, child_map, top_module, cell_area_map=cell_area_map)

    # ── annotate hierarchy source in each row ────────────────────
    from synth_hierarchy import flatten_tree as _flatten
    flat_rows = _flatten(rows)
    for r in flat_rows:
        r["hierarchy_source"] = hierarchy_source
    return flat_rows


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
# Classified timing report parsing  (orchestrates synth_timing.py)
# ---------------------------------------------------------------------------

def parse_classified_timing(
    rpt_path,
    fanout_path=None,
    netlist_path=None,
    top_reg2reg: int = 50,
    top_others: int = 20,
    result_dir=None,
    max_path: int = 50,
    sdc_file=None,
    ieda_bin=None,
    yosys_sta_home=None,
    design: str = "ysyx_25070190",
    pdk: str = "nangate45",
    dedicated_qd_query: bool = True,
) -> Dict:
    """Parse the iSTA unified timing report plus companion files into
    classified setup/hold/DRV data.

    Args:
        rpt_path: Path to the iSTA ``.rpt`` file.
        fanout_path: Path to the ``.fanout`` file (optional).
        netlist_path: Path to the mapped ``.netlist.v`` (optional).
        top_reg2reg: Max reg2reg/data_reg2reg paths to retain (default 50).
        top_others: Max paths for in2reg/reg2out/in2out/hold/fanout
                    plus clock_enable/clock_gating_setup (default 20).
        result_dir: If provided, writes classification report files.
        max_path: The ``-max_path`` value used by ``report_timing``.
        sdc_file: Path to SDC constraint file (for dedicated Q→D query).
        ieda_bin: Path to iEDA binary (for dedicated Q→D query).
        yosys_sta_home: Path to yosys-sta root (for dedicated Q→D query).
        design: Top module name.
        pdk: PDK name.
        dedicated_qd_query: When True, run dedicated Q/QN→D STA query.

    Returns:
        Dict with keys ``reg2reg``, ``data_reg2reg``, ``in2reg``,
        ``reg2out``, ``in2out``, ``clock_enable``, ``clock_gating_setup``,
        ``hold``, ``hold_classified``, ``hold_sub_categories``,
        ``path_groups``, ``high_fanout``, ``unconstrained``,
        ``wns``, ``tns``, ``warnings``, ``data_reg2reg_source``.
    """
    from synth_timing import build_timing_report

    return build_timing_report(
        rpt_path=str(rpt_path),
        fanout_path=str(fanout_path) if fanout_path else None,
        netlist_path=str(netlist_path) if netlist_path else None,
        top_reg2reg_count=top_reg2reg,
        top_other_count=top_others,
        result_dir=str(result_dir) if result_dir else None,
        max_path=max_path,
        sdc_file=str(sdc_file) if sdc_file else None,
        ieda_bin=str(ieda_bin) if ieda_bin else None,
        yosys_sta_home=str(yosys_sta_home) if yosys_sta_home else None,
        design=design,
        pdk=pdk,
        dedicated_qd_query=dedicated_qd_query,
    )


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


def _cli_parse_hierarchy() -> None:
    import argparse, json
    ap = argparse.ArgumentParser(description="Build hierarchical area tree from synth artifacts")
    ap.add_argument("json_path", type=Path, help="Path to synth_stat.json")
    ap.add_argument("--netlist", type=Path, default=None, help="Path to mapped .netlist.v")
    ap.add_argument("--top", default="ysyx_25070190", help="Top module name")
    ap.add_argument("--json-out", action="store_true", help="Output as JSON")
    args = ap.parse_args()
    try:
        rows = build_hierarchy_area_tree(args.json_path, args.netlist, args.top)
        if args.json_out:
            print(json.dumps(rows, indent=2))
        else:
            for r in rows:
                cats_str = ", ".join(
                    f"{cat}={r['categories'][cat]['count']}"
                    for cat in ("sequential", "combinational", "clock-gating",
                                "buffer/inverter", "mux", "arithmetic", "other")
                )
                print(
                    f"{r['instance_path']:50s} "
                    f"cells(loc/rec)={r['local_cells']:5d}/{r['recursive_cells']:5d}  "
                    f"area(loc/rec)={r['local_area']:10.3f}/{r['recursive_area']:10.3f}  "
                    f"%top={r['pct_of_top_area']:6.2f}  "
                    f"cats: {cats_str}"
                )
    except (ParseError, FileNotFoundError, KeyError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


def _cli_parse_timing() -> None:
    import argparse, json
    ap = argparse.ArgumentParser(description="Classify STA timing paths and extract DRV data")
    ap.add_argument("rpt", type=Path, help="Path to .rpt file")
    ap.add_argument("--fanout", type=Path, default=None, help="Path to .fanout file")
    ap.add_argument("--netlist", type=Path, default=None, help="Path to .netlist.v file")
    ap.add_argument("--result-dir", type=Path, default=None,
                    help="Write constraint_coverage.rpt, unconstrained_endpoints.rpt, "
                         "and analysis_warnings.rpt to this directory")
    ap.add_argument("--json-out", action="store_true", help="Output as JSON")
    ap.add_argument("--top-reg2reg", type=int, default=50, help="Max reg2reg paths")
    ap.add_argument("--top-others", type=int, default=20, help="Max other-category paths")
    args = ap.parse_args()
    try:
        result = parse_classified_timing(
            rpt_path=args.rpt,
            fanout_path=args.fanout,
            netlist_path=args.netlist,
            top_reg2reg=args.top_reg2reg,
            top_others=args.top_others,
            result_dir=args.result_dir,
        )
        if args.json_out:
            print(json.dumps(result, indent=2))
        else:
            print("=== Timing Classification ===")
            print(f"Global WNS (setup): {result['wns']} ns")
            print(f"Global TNS (setup): {result['tns']} ns")
            for cat in ("data_reg2reg", "reg2reg", "clock_enable",
                         "clock_gating_setup", "in2reg", "reg2out", "in2out"):
                paths = result[cat]
                print(f"\n--- {cat} ({len(paths)} paths) ---")
                for p in paths[:5]:
                    print(f"  {p['startpoint']} → {p['endpoint']}  slack={p['slack']}ns")
                if len(paths) > 5:
                    print(f"  ... and {len(paths) - 5} more")
            print(f"\n--- hold ({len(result['hold'])} endpoints) ---")
            for h in result["hold"][:5]:
                print(f"  {h['endpoint']}  slack={h['slack']}ns")
            if len(result["hold"]) > 5:
                print(f"  ... and {len(result['hold']) - 5} more")

            hold_sub = result.get("hold_sub_categories", {})
            if hold_sub:
                print(f"\n--- hold sub-categories ---")
                for sub_cat in ("data_reg2reg", "clock_enable", "clock_gating", "reg2reg"):
                    sc = hold_sub.get(sub_cat)
                    if sc:
                        print(f"  {sub_cat}: WNS={sc['wns_ns']}ns, TNS={sc['tns_ns']}ns, paths={sc['path_count']}")

            hold_classified = result.get("hold_classified", [])
            if hold_classified:
                print(f"\n--- hold classified ({len(hold_classified)} paths from detailed tables) ---")
                for hp in hold_classified[:5]:
                    cat = hp.get("category", "?")
                    print(f"  [{cat}] {hp['startpoint']} → {hp['endpoint']}  slack={hp['slack']}ns")

            print(f"\n--- path groups ({len(result['path_groups'])}) ---")
            for pg in result["path_groups"]:
                print(f"  {pg['clock_group']}/{pg['delay_type']}: {pg['endpoint_count']} endpoints, WNS={pg['wns']}ns, TNS={pg['tns']}ns")
            print(f"\n--- high fanout ({len(result['high_fanout'])} nets) ---")
            for n in result["high_fanout"][:5]:
                print(f"  {n['net_name']}: fanout={n['fanout']}, driver={n['driver_pin']}")
            print(f"\n--- unconstrained ({len(result['unconstrained'])} endpoints) ---")
            for u in result["unconstrained"][:5]:
                print(f"  {u['pin_name']} ({u['pin_type']})")
            if result["warnings"]:
                print(f"\n--- warnings ({len(result['warnings'])}) ---")
                for w in result["warnings"]:
                    print(f"  {w}")
    except (FileNotFoundError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "sta":
        sys.argv.pop(1)
        _cli_parse_sta()
    elif len(sys.argv) > 1 and sys.argv[1] == "stat":
        sys.argv.pop(1)
        _cli_parse_stat()
    elif len(sys.argv) > 1 and sys.argv[1] == "hierarchy":
        sys.argv.pop(1)
        _cli_parse_hierarchy()
    elif len(sys.argv) > 1 and sys.argv[1] == "timing":
        sys.argv.pop(1)
        _cli_parse_timing()
    else:
        print("Usage: report_parser.py {sta|stat|hierarchy|timing} [args...]", file=sys.stderr)
        sys.exit(2)
