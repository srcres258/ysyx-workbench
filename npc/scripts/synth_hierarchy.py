#!/usr/bin/env python3
# ============================================================================
# synth_hierarchy.py — Cell classifier and hierarchy-area-tree builder.
# ============================================================================
# Consumes:
#   - Yosys stat -json output            (per-module cell counts + area)
#   - Yosys mapped technology netlist    (module declarations + cell instances)
#
# Produces:
#   - Flattened hierarchy table: one row per module, with instance path,
#     module/type, local/recursive cell counts, local/recursive area,
#     % of top area, and cell-category breakdown.
#
# Design principle: conservative, deterministic, fail-closed.
#   - Unknown cell types fall back to "other" with a warning.
#   - Missing/malformed inputs raise errors immediately.
# ============================================================================

from __future__ import annotations

import logging
import re
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

logger = logging.getLogger(__name__)


# ── cell classification ────────────────────────────────────────────

# Categories as described in the task specification.
CATEGORY_SEQUENTIAL = "sequential"
CATEGORY_COMBINATIONAL = "combinational"
CATEGORY_CLOCK_GATING = "clock-gating"
CATEGORY_BUF_INV = "buffer/inverter"
CATEGORY_MUX = "mux"
CATEGORY_ARITHMETIC = "arithmetic"
CATEGORY_OTHER = "other"

ALL_CATEGORIES = [
    CATEGORY_SEQUENTIAL,
    CATEGORY_COMBINATIONAL,
    CATEGORY_CLOCK_GATING,
    CATEGORY_BUF_INV,
    CATEGORY_MUX,
    CATEGORY_ARITHMETIC,
    CATEGORY_OTHER,
]

# Conservative prefix-based rules.
# Ordered deliberately: more-specific patterns before broader ones
# to avoid false matches (e.g., CLKGATE* before CLK*).
#
# Rules are tuples of (regex_pattern, category).
# Pattern matches against the FULL cell-type name string with ^ anchor.
# Matches are tried in order; first match wins.
_CLASSIFICATION_RULES: List[Tuple[str, str]] = [
    # ── clock-gating cells (most specific) ──
    (r"^CLKGATE", CATEGORY_CLOCK_GATING),
    (r"^CLKGATETST", CATEGORY_CLOCK_GATING),
    (r"^CGL", CATEGORY_CLOCK_GATING),                    # common in some PDKs
    (r"^ICG", CATEGORY_CLOCK_GATING),                    # integrated clock gate
    (r"^TLAT", CATEGORY_SEQUENTIAL),                     # transparent latch (sequential, not clock gate)
    # ── sequential cells ──
    (r"^SDFF", CATEGORY_SEQUENTIAL),                     # scan DFF (before DFF to avoid partial match)
    (r"^DFF", CATEGORY_SEQUENTIAL),                      # D flip-flop
    (r"^DLH", CATEGORY_SEQUENTIAL),                      # D latch high
    (r"^DLL", CATEGORY_SEQUENTIAL),                      # D latch low
    (r"^LATCH", CATEGORY_SEQUENTIAL),
    (r"^REG", CATEGORY_SEQUENTIAL),                      # register (not clock REG like CLKGATE)
    (r"^FF_", CATEGORY_SEQUENTIAL),
    # ── arithmetic cells ──
    (r"^FA_", CATEGORY_ARITHMETIC),                      # full adder
    (r"^HA_", CATEGORY_ARITHMETIC),                      # half adder
    (r"^ADDF", CATEGORY_ARITHMETIC),
    (r"^ADDH", CATEGORY_ARITHMETIC),
    (r"^CARRY", CATEGORY_ARITHMETIC),
    # ── mux cells ──
    (r"^MUX", CATEGORY_MUX),
    (r"^MX", CATEGORY_MUX),                              # short form in some PDKs
    (r"^SELECT", CATEGORY_MUX),                          # e.g., SELECT_OP
    # ── buffer / inverter cells ──
    (r"^CLKBUF", CATEGORY_BUF_INV),                      # clock buffer
    (r"^BUF", CATEGORY_BUF_INV),
    (r"^INV", CATEGORY_BUF_INV),
    (r"^TBUF", CATEGORY_BUF_INV),                        # tri-state buffer
    (r"^TINV", CATEGORY_BUF_INV),                        # tri-state inverter
    (r"^DEL", CATEGORY_BUF_INV),                         # delay cell
    # ── combinational logic cells ──
    (r"^NAND", CATEGORY_COMBINATIONAL),
    (r"^NOR", CATEGORY_COMBINATIONAL),
    (r"^AND", CATEGORY_COMBINATIONAL),
    (r"^OR", CATEGORY_COMBINATIONAL),
    (r"^XOR", CATEGORY_COMBINATIONAL),
    (r"^XNOR", CATEGORY_COMBINATIONAL),
    (r"^AOI", CATEGORY_COMBINATIONAL),
    (r"^OAI", CATEGORY_COMBINATIONAL),
    (r"^MAJ", CATEGORY_COMBINATIONAL),                   # majority gate
]

# Cell types that are explicitly non-logic and should be "other".
_EXPLICIT_OTHER_PATTERNS = [
    r"^FILL",
    r"^FILLCELL",
    r"^LOGIC0",
    r"^LOGIC1",
    r"^TIE",
    r"^ANTENNA",
    r"^DIODE",
    r"^DECAP",
    r"^ENDCAP",
    r"^WELLTAP",
    r"^TAPCELL",
    r"^HEADER",
    r"^FOOTER",
    r"^SLEEP",
    r"^ISOL",
    r"^LEVEL",
    r"^CLAMP",
    r"^PWR",
    r"^GND",
    r"^VSS",
    r"^VDD",
]

# ── public API ──────────────────────────────────────────────────────

def classify_cell(cell_type: str) -> str:
    """Assign a conservative functional category to a cell type name.

    Args:
        cell_type: The cell type string from Yosys stat (e.g., ``"NAND2_X1"``,
                   ``"DFF_X1"``).

    Returns:
        One of the ``CATEGORY_*`` constants.

    Classification is **prefix-based and conservative**:
    - Matches are tried in the fixed rule order above.
    - The first matching rule wins.
    - If no rule matches, the cell falls back to ``"other"`` and a warning
      is logged.
    - Cell types matching well-known physical-only patterns (fill, tie,
      antenna, decap, etc.) are explicitly placed in ``"other"``.
    """
    # Check explicit-other patterns first (physical-only cells)
    for pat in _EXPLICIT_OTHER_PATTERNS:
        if re.match(pat, cell_type):
            return CATEGORY_OTHER

    # Try classification rules in order
    for pat, cat in _CLASSIFICATION_RULES:
        if re.match(pat, cell_type):
            return cat

    # Fallback: unknown → other + warn
    logger.warning(
        "Unrecognised cell type %r — classifying as %r. "
        "Consider updating _CLASSIFICATION_RULES.",
        cell_type,
        CATEGORY_OTHER,
    )
    return CATEGORY_OTHER


def classify_cell_counts(
    cells_by_type: Dict[str, Dict[str, float]],
) -> Dict[str, Dict[str, float]]:
    """Aggregate per-type cell counts into category totals.

    Args:
        cells_by_type: Dict mapping cell type name → ``{"count": N, "area": A}``.

    Returns:
        Dict mapping category → ``{"count": total_count, "area": total_area}``.
        Categories with zero cells are included with zero values.
    """
    result: Dict[str, Dict[str, float]] = {
        cat: {"count": 0.0, "area": 0.0} for cat in ALL_CATEGORIES
    }
    for cell_type, stats in cells_by_type.items():
        cat = classify_cell(cell_type)
        result[cat]["count"] += stats.get("count", 0)
        result[cat]["area"] += stats.get("area", 0.0)
    return result


# ── hierarchy data model ────────────────────────────────────────────

@dataclass
class HierarchyRow:
    """One row in the flattened hierarchy area table.

    Attributes:
        instance_path: Dotted path from the design top (e.g. ``"top.cpu.alu"``).
            The top-level row has ``instance_path`` equal to ``module_name``.
        module_name: The Verilog module name (e.g. ``"alu"``, ``"ysyx_25070190"``).
        parent_path: Instance path of the parent row, or ``""`` for the top.
        depth: Zero-based nesting depth.
        local_cells: Cell count contributed solely by this module (excl. children).
        local_area: Area contributed solely by this module (excl. children).
        recursive_cells: Cell count including all descendant submodules.
        recursive_area: Area including all descendant submodules.
        pct_of_top_area: ``(recursive_area / top_recursive_area) * 100``.
        categories: Per-category cell-count breakdown for local cells.
    """
    instance_path: str
    module_name: str
    parent_path: str
    depth: int
    instance_count: int = 1
    local_cells: int = 0
    local_area: float = 0.0
    recursive_cells: int = 0
    recursive_area: float = 0.0
    pct_of_top_area: float = 0.0
    categories: Dict[str, Dict[str, float]] = field(default_factory=dict)

    def to_dict(self) -> Dict:
        """Serialise to a plain dict for JSON / downstream consumers."""
        return {
            "instance_path": self.instance_path,
            "module_name": self.module_name,
            "parent_path": self.parent_path,
            "depth": self.depth,
            "instance_count": self.instance_count,
            "local_cells": self.local_cells,
            "local_area": self.local_area,
            "recursive_cells": self.recursive_cells,
            "recursive_area": self.recursive_area,
            "pct_of_top_area": round(self.pct_of_top_area, 4),
            "categories": {
                cat: {
                    "count": int(self.categories.get(cat, {}).get("count", 0)),
                    "area": self.categories.get(cat, {}).get("area", 0.0),
                }
                for cat in ALL_CATEGORIES
            },
        }


# ── tree builder ────────────────────────────────────────────────────

def _build_tree(
    module_name: str,
    parent_path: str,
    depth: int,
    json_modules: Dict[str, Dict],
) -> HierarchyRow:
    """Recursively construct a HierarchyRow node and its children.

    Returns the *root* node for the subtree; children are returned
    alongside it in a flat list via the caller.
    """
    mod_data = json_modules.get(module_name)
    if mod_data is None:
        raise KeyError(
            f"Module {module_name!r} referenced in netlist hierarchy "
            f"but not found in JSON stats. Check that the netlist and "
            f"JSON correspond to the same synthesis run."
        )

    instance_path = (
        f"{parent_path}.{module_name}" if parent_path else module_name
    )

    # ── extract numeric fields from JSON module data ──
    # Fields may be plain integers (no -liberty) or {count, area, local_count, local_area}
    def _resolve_field(field_name: str, key: str, default: float = 0.0) -> float:
        val = mod_data.get(field_name)
        if val is None:
            return default
        if isinstance(val, (int, float)):
            if "area" in key:
                return 0.0
            return float(val)
        if isinstance(val, dict):
            return float(val.get(key, default))
        return default

    local_cells = int(_resolve_field("num_cells", "local_count", 0.0))
    recursive_cells = int(_resolve_field("num_cells", "count", float(local_cells)))
    local_area = _resolve_field("num_cells", "local_area", 0.0)
    recursive_area = _resolve_field("num_cells", "area", local_area)

    # When num_cells is a plain integer (no -hierarchy), Yosys still
    # emits a module-level 'area' key when -liberty is active.
    # Fall back to it so the hierarchy area display is non-zero.
    if local_area == 0.0:
        local_area = mod_data.get("area", 0.0)
    if recursive_area == 0.0:
        recursive_area = mod_data.get("area", local_area)

    # ── cell classification from num_cells_by_type ──
    cells_by_type_raw = mod_data.get("num_cells_by_type", {})
    cells_by_type: Dict[str, Dict[str, float]] = {}
    for ct, val in cells_by_type_raw.items():
        if isinstance(val, (int, float)):
            cells_by_type[ct] = {"count": float(val), "area": 0.0}
        elif isinstance(val, dict):
            cells_by_type[ct] = {
                "count": float(val.get("local_count", val.get("count", 0))),
                "area": float(val.get("local_area", val.get("area", 0.0))),
            }
    categories = classify_cell_counts(cells_by_type)

    return HierarchyRow(
        instance_path=instance_path,
        module_name=module_name,
        parent_path=parent_path,
        depth=depth,
        local_cells=local_cells,
        local_area=local_area,
        recursive_cells=recursive_cells,
        recursive_area=recursive_area,
        pct_of_top_area=0.0,  # filled in later
        categories=categories,
    )


def build_hierarchy_tree(
    json_modules: Dict[str, Dict],
    netlist_hierarchy: Dict[str, Dict[str, int]],
    top_module: str,
) -> List[HierarchyRow]:
    """Build a flattened hierarchy area table.

    Args:
        json_modules: ``modules`` dict from parsed ``synth_stat.json``.
            Keys are module names; values are per-module stat dicts.
        netlist_hierarchy: Dict mapping parent module name →
            ``{child_module_type: instance_count}`` (from parsing the
            netlist's module instantiations).  A fully-flattened design
            has empty dicts for every module.
        top_module: Name of the top-level module.

    Returns:
        List of ``HierarchyRow``, one per module type, in depth-first
        order (top module first).  Each row is fully populated with
        local and recursive counts, category breakdown, ``% of top
        area``, and ``instance_count`` (how many times this module type
        is instantiated within its parent).

    Raises:
        KeyError: A module referenced in ``netlist_hierarchy`` is missing
            from ``json_modules`` (data inconsistency).
    """
    if top_module not in json_modules:
        raise KeyError(
            f"Top module {top_module!r} not found in JSON stats modules. "
            f"Available modules: {sorted(json_modules.keys())}"
        )

    def _dfs(
        mod_name: str, parent_path: str, depth: int, inst_count: int = 1
    ) -> List[HierarchyRow]:
        row = _build_tree(mod_name, parent_path, depth, json_modules)
        row.instance_count = inst_count
        rows = [row]
        for child_name, child_count in netlist_hierarchy.get(mod_name, {}).items():
            rows.extend(_dfs(child_name, row.instance_path, depth + 1, child_count))
        return rows

    all_rows = _dfs(top_module, "", 0, 1)

    # ── compute recursive counts bottom-up ──
    # Build a lookup: instance_path → row
    row_by_path = {r.instance_path: r for r in all_rows}
    # Process deepest-first
    for row in sorted(all_rows, key=lambda r: -r.depth):
        children = netlist_hierarchy.get(row.module_name, {})
        if not children:
            # Leaf: recursive == local (per-instance)
            row.recursive_cells = row.local_cells
            row.recursive_area = row.local_area
        else:
            total_cells = row.local_cells
            total_area = row.local_area
            for child_name, child_count in children.items():
                child_path = f"{row.instance_path}.{child_name}"
                child_row = row_by_path.get(child_path)
                if child_row is not None:
                    total_cells += child_row.recursive_cells * child_count
                    total_area += child_row.recursive_area * child_count
            row.recursive_cells = total_cells
            row.recursive_area = total_area

    # ── compute % of top area ──
    top_row = row_by_path.get(top_module)
    top_area = top_row.recursive_area if top_row else 0.0
    for row in all_rows:
        if top_area > 0:
            row.pct_of_top_area = (row.recursive_area * row.instance_count / top_area) * 100.0
        else:
            row.pct_of_top_area = 0.0

    return all_rows


def flatten_tree(rows: List[HierarchyRow]) -> List[Dict]:
    """Convert a list of HierarchyRow to plain dicts for serialisation."""
    return [r.to_dict() for r in rows]
