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
from pathlib import Path
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


# ============================================================================
# Liberty-backed area reports: cell types, cell classes, modules
# ============================================================================
# These functions consume the hierarchy-preserved synth_hierarchy.json
# (Yosys ``stat -json -liberty -hierarchy`` output after tech mapping)
# to produce three independent area reports with real Liberty-derived
# per-cell areas, non-zero class aggregates, and explicit closure/error
# status on module area accounting.

@dataclass
class CellTypeRecord:
    """One row in the cell-type area report.

    Captures per-cell Liberty area, design-wide instance count, total
    area, percentage of total, and conservative functional classification.
    """
    cell_type: str
    count: int               # design-wide instance count
    per_cell_area: float     # Liberty area per cell (µm²)
    total_area: float        # count × per_cell_area (µm²)
    pct_of_total: float      # (total_area / top_total_area) × 100
    category: str            # classified category (sequential, combinational, etc.)


def extract_cell_types_from_json(json_path) -> Tuple[List[CellTypeRecord], float, List[str]]:
    """Extract per-cell-type Liberty-backed area data from the hierarchy JSON.

    Reads the hierarchy-preserved ``synth_hierarchy.json`` and returns a
    flat table of every distinct PDK cell type used in the design, with
    design-wide instance counts, per-cell Liberty areas, total areas,
    percentage contributions, and conservative functional classification.

    The data source is the **top module's** ``num_cells_by_type`` in the
    hierarchy-preserved JSON, which carries global (design-wide) ``count``
    and ``area`` values — the ``area`` is Liberty-derived (Yosys
    ``stat -json -liberty``), not an estimate.

    Args:
        json_path: Path to ``synth_hierarchy.json`` (hierarchy-preserved,
            captured after tech mapping, BEFORE flatten).

    Returns:
        Tuple of:
        - List of CellTypeRecord (one per cell type, sorted by total_area desc)
        - Float: total area from the top module's global ``area`` field (µm²)
        - List of warning strings (e.g., missing data, zero-count types)
    """
    import json as _json

    json_path = Path(json_path)
    if not json_path.is_file():
        raise FileNotFoundError(f"Hierarchy JSON not found for cell-type extraction: {json_path}")

    data = _json.loads(json_path.read_text(encoding="utf-8", errors="replace"))
    modules = data.get("modules", {})
    warnings: List[str] = []

    if not modules:
        raise ValueError(f"Hierarchy JSON {json_path}: 'modules' dict is empty — cannot extract cell types.")

    # Find the top module: in a hierarchy-preserved JSON, the design-top
    # module has the largest number of submodules and contains global
    # num_cells_by_type with design-wide counts/areas.
    # Heuristic: pick the module with the most submodules_by_type entries.
    top_module_name = None
    max_submodules = -1
    for mod_name, mod_data in modules.items():
        sbt = mod_data.get("num_submodules_by_type", {})
        nsub = len(sbt) if isinstance(sbt, dict) else 0
        if nsub > max_submodules:
            max_submodules = nsub
            top_module_name = mod_name

    if top_module_name is None:
        # Fallback: any module with num_submodules_by_type present
        for mod_name, mod_data in modules.items():
            if isinstance(mod_data.get("num_submodules_by_type"), dict):
                top_module_name = mod_name
                break
        if top_module_name is None:
            top_module_name = list(modules.keys())[0]
            warnings.append(
                "STATUS: No hierarchy detected — only one module present. "
                "Cell-type areas are from the flat JSON (may lack per-type area data)."
            )

    top_mod = modules[top_module_name]
    cells_by_type_raw = top_mod.get("num_cells_by_type", {})

    if not cells_by_type_raw:
        raise ValueError(
            f"Hierarchy JSON {json_path}: top module {top_module_name!r} "
            f"has no 'num_cells_by_type'. Cannot produce cell-type area report."
        )

    # Extract top-level total area for percentage computation.
    # In hierarchical JSON, num_cells is a {count, area, ...} object.
    num_cells = top_mod.get("num_cells", {})
    if isinstance(num_cells, dict):
        total_area = float(num_cells.get("area", 0.0))
    else:
        total_area = float(top_mod.get("area", 0.0))

    if total_area <= 0.0:
        warnings.append(
            "STATUS: UNAVAILABLE — Top-module area is zero. "
            "Liberty may not provide area data or the JSON is from a pre-tech-mapping snapshot."
        )

    records: List[CellTypeRecord] = []
    zero_count_types: List[str] = []

    for cell_type, val in cells_by_type_raw.items():
        if isinstance(val, dict):
            count = int(val.get("count", 0))
            area = float(val.get("area", 0.0))
        elif isinstance(val, (int, float)):
            count = int(val)
            area = 0.0
            if total_area > 0:
                warnings.append(
                    f"Cell type {cell_type!r}: count={count} but no Liberty area — "
                    f"JSON field is plain integer (pre-flatten snapshot without -liberty?)."
                )
        else:
            count = 0
            area = 0.0

        if count == 0:
            zero_count_types.append(cell_type)
            continue

        per_cell_area = area / count if count > 0 else 0.0
        pct = (area / total_area * 100.0) if total_area > 0.0 else 0.0
        category = classify_cell(cell_type)

        records.append(CellTypeRecord(
            cell_type=cell_type,
            count=count,
            per_cell_area=per_cell_area,
            total_area=area,
            pct_of_total=pct,
            category=category,
        ))

    if zero_count_types:
        warnings.append(
            f"Excluded {len(zero_count_types)} cell type(s) with zero instance count "
            f"(present in the library but not instantiated)."
        )

    if not records:
        raise ValueError(
            f"Hierarchy JSON {json_path}: all cell types have zero count. "
            f"The design may be empty or the JSON is corrupted."
        )

    records.sort(key=lambda r: r.total_area, reverse=True)
    return records, total_area, warnings


def write_area_cell_types_report(
    records: List[CellTypeRecord],
    total_area: float,
    output_path,
    extra_warnings: Optional[List[str]] = None,
) -> None:
    """Write ``area_cell_types.rpt`` — per-cell-type Liberty-backed area.

    Each row lists: cell type name, instance count, per-cell area (µm²),
    total area (µm²), percentage of total area, and functional class.

    The report header includes explicit status when Liberty area data is
    missing or partial.
    """
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    all_warnings = list(extra_warnings or [])

    # Detect data quality
    non_zero_area = sum(1 for r in records if r.per_cell_area > 0)
    total_cells = sum(r.count for r in records)
    total_from_cells = sum(r.total_area for r in records)

    area_gap = total_area - total_from_cells
    area_gap_pct = (area_gap / total_area * 100.0) if total_area > 0 else 0.0

    if non_zero_area == 0:
        all_warnings.insert(0,
            "STATUS: UNAVAILABLE — Zero Liberty areas for all cell types. "
            "The JSON was likely captured before tech mapping (pre-flatten). "
            "Run synth with a hierarchy-preserved post-mapping snapshot."
        )

    lines = []
    lines.append("=" * 90)
    lines.append(" AREA BY CELL TYPE — Liberty-backed per-cell areas")
    lines.append("=" * 90)
    lines.append(f" Source:       hierarchy-preserved synth JSON (post tech-mapping)")
    lines.append(f" Cell types:   {len(records)} distinct")
    lines.append(f" Total cells:  {total_cells}")
    lines.append(f" Total area:   {total_area:.4f} µm² (design-top global)")
    lines.append(f" Area from cell types: {total_from_cells:.4f} µm²")
    if abs(area_gap) > 0.01:
        lines.append(f" Area gap:     {area_gap:+.4f} µm² ({area_gap_pct:+.2f}%)")
    lines.append(f" With Liberty area: {non_zero_area}/{len(records)} cell types")
    lines.append("")

    if all_warnings:
        lines.append("--- STATUS / WARNINGS ---")
        for w in all_warnings:
            lines.append(f"  {w}")
        lines.append("")

    # Header
    lines.append(
        f" {'Cell Type':<28s} {'Count':>7s} {'Per-Cell (µm²)':>15s} "
        f"{'Total Area (µm²)':>16s} {'%Total':>8s} {'Class':<20s}"
    )
    lines.append(f" {'-'*28} {'-'*7} {'-'*15} {'-'*16} {'-'*8} {'-'*20}")

    for r in records:
        lines.append(
            f" {r.cell_type:<28s} {r.count:>7d} {r.per_cell_area:>15.4f} "
            f"{r.total_area:>16.4f} {r.pct_of_total:>7.2f}% {r.category:<20s}"
        )

    lines.append("")
    lines.append("=" * 90)
    lines.append(" End of area_cell_types.rpt")
    lines.append("=" * 90)
    lines.append("")

    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_area_cell_classes_report(
    records: List[CellTypeRecord],
    total_area: float,
    output_path,
    extra_warnings: Optional[List[str]] = None,
) -> None:
    """Write ``area_cell_classes.rpt`` — aggregate by functional class.

    Aggregates per-cell-type Liberty-backed areas into the seven
    conservative categories.  Includes closure metrics and explicit
    status when class areas cannot be determined.
    """
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    all_warnings = list(extra_warnings or [])

    # Aggregate by category
    class_data: Dict[str, Dict[str, float]] = {}
    for r in records:
        cat = r.category
        if cat not in class_data:
            class_data[cat] = {"cell_count": 0.0, "area_um2": 0.0}
        class_data[cat]["cell_count"] += r.count
        class_data[cat]["area_um2"] += r.total_area

    total_from_classes = sum(d["area_um2"] for d in class_data.values())
    total_cells_from_classes = sum(d["cell_count"] for d in class_data.values())

    # Compute closure
    if total_area > 0:
        closure_gap = total_area - total_from_classes
        closure_gap_pct = (closure_gap / total_area) * 100.0
    else:
        closure_gap = 0.0
        closure_gap_pct = 0.0

    # Status classification
    has_nonzero_area = any(d["area_um2"] > 0 for d in class_data.values())
    if not has_nonzero_area:
        all_warnings.insert(0,
            "STATUS: UNAVAILABLE — All class areas are zero. "
            "Liberty area data is absent or the JSON was captured pre-tech-mapping."
        )
    elif abs(closure_gap_pct) > 5.0:
        all_warnings.append(
            f"STATUS: CLOSURE GAP — Class area sum ({total_from_classes:.2f} µm²) "
            f"differs from top area ({total_area:.2f} µm²) by {closure_gap_pct:+.1f}%. "
            f"This may indicate multi-module area accounting drift or missing cell types."
        )

    lines = []
    lines.append("=" * 80)
    lines.append(" AREA BY CELL CLASS — Liberty-backed class aggregates")
    lines.append("=" * 80)
    lines.append(f" Total area (top):  {total_area:.4f} µm²")
    lines.append(f" Class area sum:     {total_from_classes:.4f} µm²")
    if total_area > 0:
        lines.append(f" Closure gap:        {closure_gap:+.4f} µm² ({closure_gap_pct:+.2f}%)")
    lines.append(f" Total cells:        {int(total_cells_from_classes)}")
    lines.append("")

    if all_warnings:
        lines.append("--- STATUS / WARNINGS ---")
        for w in all_warnings:
            lines.append(f"  {w}")
        lines.append("")

    # Header
    lines.append(
        f" {'Class':<20s} {'Cell Count':>12s} {'Area (µm²)':>14s} "
        f"{'% Area':>8s}  Status"
    )
    lines.append(f" {'-'*20} {'-'*12} {'-'*14} {'-'*8}  {'-'*6}")

    category_order = [
        CATEGORY_SEQUENTIAL, CATEGORY_COMBINATIONAL, CATEGORY_CLOCK_GATING,
        CATEGORY_BUF_INV, CATEGORY_MUX, CATEGORY_ARITHMETIC, CATEGORY_OTHER,
    ]
    for cat in category_order:
        d = class_data.get(cat, {"cell_count": 0.0, "area_um2": 0.0})
        cell_count = int(d["cell_count"])
        area = d["area_um2"]
        pct = (area / total_area * 100.0) if total_area > 0 else 0.0

        if area > 0:
            status = "OK"
        elif cell_count > 0:
            status = "NO_AREA"
        else:
            status = "EMPTY"

        lines.append(
            f" {cat:<20s} {cell_count:>12d} {area:>14.4f} "
            f"{pct:>7.2f}%  {status}"
        )

    lines.append("")
    lines.append("=" * 80)
    lines.append(" End of area_cell_classes.rpt")
    lines.append("=" * 80)
    lines.append("")

    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_area_modules_report(
    hierarchy_rows: List[Dict],
    top_area: float,
    output_path,
    extra_warnings: Optional[List[str]] = None,
) -> None:
    """Write ``area_modules.rpt`` — per-module area with closure/error status.

    Grouped by module name (not instance path).  Each entry reports
    local and recursive cell counts / area, % of top area, and instance
    count.  An explicit top-level closure check compares the sum of
    per-module local areas (weighted by instance_count) against the
    top area and emits a status line when the gap exceeds tolerance.
    """
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    all_warnings = list(extra_warnings or [])

    if not hierarchy_rows:
        all_warnings.insert(0,
            "STATUS: UNAVAILABLE — No hierarchy data available. "
            "Run synth with a hierarchy-preserved area source enabled."
        )

    # ── closure check: sum of weighted local areas vs top area ──
    aggregate_local_area = 0.0
    for row in hierarchy_rows:
        inst_count = row.get("instance_count", 1)
        aggregate_local_area += row.get("local_area", 0.0) * inst_count

    if top_area > 0:
        closure_error = top_area - aggregate_local_area
        closure_error_pct = (closure_error / top_area) * 100.0
    else:
        closure_error = 0.0
        closure_error_pct = 0.0

    tolerance_pct = 1.0  # 1% tolerance
    if abs(closure_error_pct) > tolerance_pct and top_area > 0:
        all_warnings.append(
            f"CLOSURE WARNING: Sum of per-module local areas ({aggregate_local_area:.2f} µm²) "
            f"differs from top area ({top_area:.2f} µm²) by {closure_error:+.2f} µm² "
            f"({closure_error_pct:+.2f}%). "
            f"This may indicate instance-count accounting drift or missing module data."
        )

    # ── determine hierarchy source ──
    hierarchy_source = "unknown"
    for row in hierarchy_rows:
        src = row.get("hierarchy_source", "")
        if src:
            hierarchy_source = src
            break

    lines = []
    lines.append("=" * 105)
    lines.append(" AREA BY MODULE — with closure / error status")
    lines.append("=" * 105)
    lines.append(f" Top module:     {hierarchy_rows[0].get('module_name', '?') if hierarchy_rows else '?'}")
    lines.append(f" Total modules:  {len(hierarchy_rows)}")
    lines.append(f" Top area:       {top_area:.4f} µm² (recursive)")
    lines.append(f" Aggregated:     {aggregate_local_area:.4f} µm² (sum of weighted local areas)")
    if top_area > 0:
        lines.append(f" Closure error:  {closure_error:+.4f} µm² ({closure_error_pct:+.2f}%)")
    else:
        lines.append(f" Closure error:  N/A (top area is zero)")
    lines.append(f" Source:         {hierarchy_source}")
    lines.append("")

    if all_warnings:
        lines.append("--- STATUS / WARNINGS ---")
        for w in all_warnings:
            lines.append(f"  {w}")
        lines.append("")

    # Header
    lines.append(
        f" {'Module':<30s} {'Inst':>5s} {'Local Cells':>12s} {'Local Area':>14s} "
        f"{'Rec Cells':>12s} {'Rec Area':>14s} {'%Top':>8s}  Closure"
    )
    lines.append(f" {'-'*30} {'-'*5} {'-'*12} {'-'*14} {'-'*12} {'-'*14} {'-'*8}  {'-'*7}")

    for row in hierarchy_rows:
        mod_name = row.get("module_name", row.get("instance_path", "?"))
        inst_count = row.get("instance_count", 1)
        local_cells = row.get("local_cells", 0)
        local_area = row.get("local_area", 0.0)
        rec_cells = row.get("recursive_cells", 0)
        rec_area = row.get("recursive_area", 0.0)
        pct_top = row.get("pct_of_top_area", 0.0)

        # Per-module closure: local area * instance_count = contribution
        contrib = local_area * inst_count
        if rec_area > 0:
            # Recursive area should account for children.
            # Closure is tight when recursive_area ≈ local_area + Σ(children)
            # For leaf modules: rec_area == local_area.
            closure_status = "leaf" if rec_cells == local_cells else "parent"
        else:
            closure_status = "no_area"

        lines.append(
            f" {mod_name:<30s} {inst_count:>5d} {local_cells:>12d} {local_area:>14.4f} "
            f"{rec_cells:>12d} {rec_area:>14.4f} {pct_top:>7.2f}%  {closure_status}"
        )

    lines.append("")
    lines.append("=" * 105)
    lines.append(" End of area_modules.rpt")
    lines.append("=" * 105)
    lines.append("")

    output_path.write_text("\n".join(lines), encoding="utf-8")


# ============================================================================
# Register inventory and clock-gating inventory reports
# ============================================================================
# These functions consume the hierarchy-preserved synth_hierarchy.json
# (Yosys ``stat -json -hierarchy -liberty`` after tech mapping) plus the
# hierarchical mapped netlist (synth_hierarchy.v) to produce:
#   - register_inventory.rpt: per-module sequential cell breakdown with
#     register-bit counts, gating context, and Liberty-backed area.
#   - clock_gating_inventory.rpt: per-module clock-gating cell breakdown
#     with downstream register-bit fanout estimates and area.

from collections import defaultdict as _defaultdict

# ── Netlist connectivity tracing ────────────────────────────────────

# Verilog keywords that should NOT be treated as cell types when scanning
# the netlist body for instances.
_VERILOG_KEYWORDS = frozenset({
    "module", "endmodule", "input", "output", "inout", "wire", "reg",
    "assign", "always", "initial", "function", "endfunction",
    "task", "endtask", "begin", "end", "if", "else", "case",
    "endcase", "for", "while", "parameter", "localparam",
    "specify", "endspecify", "generate", "endgenerate",
    "supply0", "supply1", "tri", "wand", "wor",
})

# Matches a cell instance with its port connections.
_NETLIST_CELL_INST_RE = re.compile(
    r"^\s*(?P<cell_type>\S+)\s+(?P<inst_name>\S+)\s*[(]\s*"
    r"(?P<ports>[^;]*?)"
    r"\s*[)]\s*;",
    re.MULTILINE | re.DOTALL,
)

# Within a port list, match a single named port connection: .PORTNAME(net)
_PORT_CONN_RE = re.compile(
    r"[.]\s*(?P<port>\w+)\s*[(]\s*(?P<net>\S+?)\s*[)]"
)


def _parse_cell_ports(port_text: str) -> Dict[str, str]:
    """Extract port->net mapping from a Verilog instance port list.

    Example: ``.CK(clk), .D(d_in), .Q(q_out)``
    -> ``{"CK": "clk", "D": "d_in", "Q": "q_out"}``
    """
    result: Dict[str, str] = {}
    for m in _PORT_CONN_RE.finditer(port_text):
        result[m.group("port")] = m.group("net")
    return result


def trace_clock_gating_connectivity(netlist_path) -> Dict[str, Dict]:
    """Trace clock-gate outputs to register clock inputs in a hierarchical netlist.

    Parses the hierarchical mapped Verilog netlist and, **within each
    module**, matches clock-gate ``.GCK`` (or ``.GCLK``) outputs to
    sequential-cell ``.CK`` (or ``.CLK``) inputs on the same net.

    Cross-module connections (clock gate in module A, register in module B
    connected via ports) are **not** traced - this is a conservative
    intra-module-only analysis.

    Args:
        netlist_path: Path to the hierarchical mapped netlist
                      (captured post-tech-mapping, pre-flatten).

    Returns:
        ``{module_name: {
            "clock_gates": {inst_name: {"type": str, "out_net": str}},
            "registers": [
                {"inst_name": str, "type": str, "clk_net": str,
                 "gated_by": str|None}
            ]
        }}``

    Raises:
        FileNotFoundError: netlist does not exist
    """
    netlist_path = Path(netlist_path)
    if not netlist_path.is_file():
        raise FileNotFoundError(f"Hierarchical netlist not found: {netlist_path}")

    text = netlist_path.read_text(encoding="utf-8", errors="replace")

    # Find module boundaries
    _MOD_START_RE = re.compile(r"^\s*module\s+(?P<name>\S+)\s*[(<]", re.MULTILINE)
    _MOD_END_RE = re.compile(r"^\s*endmodule\b", re.MULTILINE)

    mod_starts = [(m.group("name"), m.start()) for m in _MOD_START_RE.finditer(text)]
    mod_ends = [m.end() for m in _MOD_END_RE.finditer(text)]

    # Collect module names to skip them when scanning for cell instances
    _module_names_set = frozenset(name for name, _ in mod_starts)
    _skip_types = _VERILOG_KEYWORDS | _module_names_set

    if len(mod_starts) != len(mod_ends):
        return {}  # malformed netlist - fail gracefully

    result: Dict[str, Dict] = {}

    for i, (mod_name, mod_start) in enumerate(mod_starts):
        mod_end = mod_ends[i]
        mod_body = text[mod_start:mod_end]

        cg_outputs: Dict[str, str] = {}  # inst_name -> out_net
        registers: List[Dict] = []

        for cell_m in _NETLIST_CELL_INST_RE.finditer(mod_body):
            cell_type = cell_m.group("cell_type")
            if cell_type in _skip_types:
                continue
            inst_name = cell_m.group("inst_name")
            if inst_name in _skip_types:
                continue
            ports = _parse_cell_ports(cell_m.group("ports"))

            # Check if this is a clock-gating cell
            if classify_cell(cell_type) == CATEGORY_CLOCK_GATING:
                # Find the output pin - typically .GCK or .GCLK
                out_net = ports.get("GCK") or ports.get("GCLK") or ports.get("Q") or ports.get("Z")
                if out_net:
                    cg_outputs[inst_name] = out_net

            # Check if this is a sequential cell
            elif classify_cell(cell_type) == CATEGORY_SEQUENTIAL:
                clk_net = ports.get("CK") or ports.get("CLK") or ports.get("G")
                if clk_net:
                    registers.append({
                        "inst_name": inst_name,
                        "type": cell_type,
                        "clk_net": clk_net,
                    })

        # Match clock-gate outputs to register clock inputs
        net_to_cg: Dict[str, str] = {}
        for cg_inst, cg_out_net in cg_outputs.items():
            net_to_cg[cg_out_net] = cg_inst

        for reg in registers:
            reg["gated_by"] = net_to_cg.get(reg["clk_net"])

        if cg_outputs or registers:
            # Enrich clock-gate records with cell type
            cg_enriched = {}
            for cg_inst, cg_out_net in cg_outputs.items():
                cg_type = _cell_type_for_instance(mod_body, cg_inst)
                cg_enriched[cg_inst] = {"type": cg_type, "out_net": cg_out_net}

            result[mod_name] = {
                "clock_gates": cg_enriched,
                "registers": registers,
            }

    return result


def _cell_type_for_instance(mod_body: str, inst_name: str) -> str:
    """Given a module body and instance name, find the cell type.

    This is a helper for ``trace_clock_gating_connectivity``.
    """
    pattern = re.compile(
        r"^\s*(?P<ctype>\S+)\s+" + re.escape(inst_name) + r"\s*[(]",
        re.MULTILINE,
    )
    m = pattern.search(mod_body)
    return m.group("ctype") if m else "unknown"


# ── Register inventory data model ───────────────────────────────────

@dataclass
class RegisterModuleRecord:
    """Register summary for one module (collapsed by type, not instance).

    Captures the local register (sequential) cell breakdown for a single
    module name, with multiplicity-aware instance counts and areas from
    the hierarchy-preserved JSON.
    """
    module_name: str
    instance_path: str
    depth: int
    instance_count: int           # how many times this module type appears
    total_reg_cells: int = 0      # total sequential cell instances (weighted by instance_count)
    total_reg_area: float = 0.0   # total sequential cell area (weighted by instance_count)
    reg_types: List[Dict] = field(default_factory=list)  # [{cell_type, count, area, pct_area}]
    gated: bool = False           # True if any register in this module is clock-gated
    unverified: List[str] = field(default_factory=list)  # warnings (e.g., "no_area", "cross_module")


@dataclass
class ClockGateModuleRecord:
    """Clock-gating summary for one module (collapsed by type)."""
    module_name: str
    instance_path: str
    depth: int
    instance_count: int
    total_cg_cells: int = 0
    total_cg_area: float = 0.0
    cg_types: List[Dict] = field(default_factory=list)  # [{cell_type, count, area}]
    est_downstream_regs: int = 0     # estimated register bits downstream of clock gates
    unverified: List[str] = field(default_factory=list)


# ── Register inventory extraction ───────────────────────────────────

def extract_register_inventory(
    json_path,
    netlist_path=None,
    hierarchy_rows: Optional[List[Dict]] = None,
) -> Tuple[List[RegisterModuleRecord], float, float, List[str]]:
    """Extract per-module register (sequential cell) inventory from hierarchy JSON.

    Consumes the hierarchy-preserved ``synth_hierarchy.json`` and an optional
    hierarchical netlist to determine clock-gating connectivity within each
    module.

    Args:
        json_path: Path to ``synth_hierarchy.json`` (post tech-mapping,
            pre-flatten, with ``-hierarchy`` flag).
        netlist_path: Path to hierarchical ``synth_hierarchy.v`` (optional).
            When provided, clock-gating connectivity is traced per-module.
        hierarchy_rows: Pre-computed hierarchy rows from
            ``build_hierarchy_area_tree`` (optional). If not provided,
            they are built from the JSON.

    Returns:
        Tuple of:
        - List of ``RegisterModuleRecord`` (sorted by reg cell count desc)
        - Float: total sequential area (um2)
        - Float: total sequential cell count
        - List of warning strings
    """
    from report_parser import parse_synth_json, build_hierarchy_area_tree

    json_path = Path(json_path)
    warnings: List[str] = []

    # Parse JSON
    json_data = parse_synth_json(json_path)
    modules = json_data["modules"]

    # Build or reuse hierarchy rows
    if hierarchy_rows is None:
        try:
            hierarchy_rows = build_hierarchy_area_tree(
                json_path, netlist_path,
                top_module=list(modules.keys())[0],
            )
        except Exception:
            hierarchy_rows = []

    # Trace clock-gating connectivity if netlist available
    connectivity: Dict[str, Dict] = {}
    if netlist_path and Path(netlist_path).is_file():
        try:
            connectivity = trace_clock_gating_connectivity(netlist_path)
        except Exception as e:
            warnings.append(f"Clock-gating connectivity tracing failed: {e}")

    # Build module lookup from hierarchy rows
    row_by_module: Dict[str, Dict] = {}
    for row in hierarchy_rows:
        mod_name = row.get("module_name", row.get("instance_path", ""))
        if mod_name not in row_by_module:
            row_by_module[mod_name] = row

    records: List[RegisterModuleRecord] = []
    total_seq_cells = 0
    total_seq_area = 0.0

    seq_cell_types_global: Dict[str, Dict[str, float]] = _defaultdict(
        lambda: {"count": 0.0, "area": 0.0}
    )

    for mod_name in sorted(modules.keys()):
        mod_data = modules[mod_name]
        row = row_by_module.get(mod_name, {})

        cells_by_type = mod_data.get("num_cells_by_type", {})
        instance_count = row.get("instance_count", 1)

        reg_types: List[Dict] = []
        local_reg_cells = 0
        local_reg_area = 0.0

        for cell_type, val in cells_by_type.items():
            if classify_cell(cell_type) != CATEGORY_SEQUENTIAL:
                continue

            # Extract local count/area (this module only)
            if isinstance(val, dict):
                cnt = int(val.get("local_count", val.get("count", 0)))
                area_val = float(val.get("local_area", val.get("area", 0.0)))
            elif isinstance(val, (int, float)):
                cnt = int(val)
                area_val = 0.0
            else:
                cnt = 0
                area_val = 0.0

            if cnt == 0:
                continue

            # Weight by instance_count for the final report
            weighted_cnt = cnt * instance_count
            weighted_area = area_val * instance_count

            reg_types.append({
                "cell_type": cell_type,
                "local_count": cnt,
                "weighted_count": weighted_cnt,
                "local_area": area_val,
                "weighted_area": weighted_area,
                "bits_per_cell": 1,  # conservative: assume 1-bit per DFF
            })

            local_reg_cells += weighted_cnt
            local_reg_area += weighted_area

            seq_cell_types_global[cell_type]["count"] += weighted_cnt
            seq_cell_types_global[cell_type]["area"] += weighted_area

        # Check gating status from connectivity
        mod_conn = connectivity.get(mod_name, {})
        gated = False
        if mod_conn.get("registers"):
            gated = any(r.get("gated_by") for r in mod_conn["registers"])

        if local_reg_cells > 0 or local_reg_area > 0:
            unverified: List[str] = []
            if local_reg_area == 0.0 and local_reg_cells > 0:
                unverified.append("no_area")

            records.append(RegisterModuleRecord(
                module_name=mod_name,
                instance_path=row.get("instance_path", mod_name),
                depth=row.get("depth", 0),
                instance_count=instance_count,
                total_reg_cells=local_reg_cells,
                total_reg_area=local_reg_area,
                reg_types=sorted(reg_types, key=lambda t: t["weighted_count"], reverse=True),
                gated=gated,
                unverified=unverified,
            ))

        total_seq_cells += local_reg_cells
        total_seq_area += local_reg_area

    if total_seq_area == 0.0:
        warnings.append(
            "STATUS: UNAVAILABLE - Zero sequential cell areas. "
            "The hierarchy JSON may lack Liberty area data. "
            "Check that the synth run captured a post-tech-mapping snapshot."
        )

    records.sort(key=lambda r: r.total_reg_cells, reverse=True)
    return records, total_seq_area, total_seq_cells, warnings


# ── Clock-gating inventory extraction ───────────────────────────────

def extract_clock_gating_inventory(
    json_path,
    netlist_path=None,
    hierarchy_rows: Optional[List[Dict]] = None,
) -> Tuple[List[ClockGateModuleRecord], float, int, List[str]]:
    """Extract per-module clock-gating cell inventory from hierarchy JSON.

    Consumes the hierarchy-preserved ``synth_hierarchy.json`` and an optional
    hierarchical netlist to estimate downstream register-bit fanout per
    clock-gate cell.

    Args:
        json_path: Path to ``synth_hierarchy.json``.
        netlist_path: Path to hierarchical ``synth_hierarchy.v`` (optional).
        hierarchy_rows: Pre-computed hierarchy rows (optional).

    Returns:
        Tuple of:
        - List of ``ClockGateModuleRecord`` (sorted by cg cell count desc)
        - Float: total clock-gating cell area (um2)
        - Int: total clock-gating cell count
        - List of warning strings
    """
    from report_parser import parse_synth_json, build_hierarchy_area_tree

    json_path = Path(json_path)
    warnings: List[str] = []

    json_data = parse_synth_json(json_path)
    modules = json_data["modules"]

    if hierarchy_rows is None:
        try:
            hierarchy_rows = build_hierarchy_area_tree(
                json_path, netlist_path,
                top_module=list(modules.keys())[0],
            )
        except Exception:
            hierarchy_rows = []

    # Trace connectivity for fanout estimates
    connectivity: Dict[str, Dict] = {}
    if netlist_path and Path(netlist_path).is_file():
        try:
            connectivity = trace_clock_gating_connectivity(netlist_path)
        except Exception as e:
            warnings.append(f"Clock-gating connectivity tracing failed: {e}")

    row_by_module: Dict[str, Dict] = {}
    for row in hierarchy_rows:
        mod_name = row.get("module_name", row.get("instance_path", ""))
        if mod_name not in row_by_module:
            row_by_module[mod_name] = row

    records: List[ClockGateModuleRecord] = []
    total_cg_cells = 0
    total_cg_area = 0.0

    for mod_name in sorted(modules.keys()):
        mod_data = modules[mod_name]
        row = row_by_module.get(mod_name, {})
        cells_by_type = mod_data.get("num_cells_by_type", {})
        instance_count = row.get("instance_count", 1)

        cg_types: List[Dict] = []
        local_cg_cells = 0
        local_cg_area = 0.0

        for cell_type, val in cells_by_type.items():
            if classify_cell(cell_type) != CATEGORY_CLOCK_GATING:
                continue

            if isinstance(val, dict):
                cnt = int(val.get("local_count", val.get("count", 0)))
                area_val = float(val.get("local_area", val.get("area", 0.0)))
            elif isinstance(val, (int, float)):
                cnt = int(val)
                area_val = 0.0
            else:
                cnt = 0
                area_val = 0.0

            if cnt == 0:
                continue

            weighted_cnt = cnt * instance_count
            weighted_area = area_val * instance_count

            cg_types.append({
                "cell_type": cell_type,
                "local_count": cnt,
                "weighted_count": weighted_cnt,
                "local_area": area_val,
                "weighted_area": weighted_area,
            })

            local_cg_cells += weighted_cnt
            local_cg_area += weighted_area

        # Estimate downstream register bits from connectivity
        mod_conn = connectivity.get(mod_name, {})
        downstream_regs = 0
        if mod_conn.get("registers"):
            downstream_regs = len([
                r for r in mod_conn["registers"] if r.get("gated_by")
            ])

        if local_cg_cells > 0 or local_cg_area > 0:
            unverified: List[str] = []
            if local_cg_area == 0.0 and local_cg_cells > 0:
                unverified.append("no_area")
            if downstream_regs == 0 and local_cg_cells > 0:
                unverified.append("fanout_unverified")

            records.append(ClockGateModuleRecord(
                module_name=mod_name,
                instance_path=row.get("instance_path", mod_name),
                depth=row.get("depth", 0),
                instance_count=instance_count,
                total_cg_cells=local_cg_cells,
                total_cg_area=local_cg_area,
                cg_types=sorted(cg_types, key=lambda t: t["weighted_count"], reverse=True),
                est_downstream_regs=downstream_regs,
                unverified=unverified,
            ))

        total_cg_cells += local_cg_cells
        total_cg_area += local_cg_area

    if total_cg_cells == 0:
        warnings.append(
            "STATUS: NONE - No clock-gating cells found in the design. "
            "This is normal if the RTL does not use explicit clock-gating."
        )

    records.sort(key=lambda r: r.total_cg_cells, reverse=True)
    return records, total_cg_area, total_cg_cells, warnings


# ── Report writers ──────────────────────────────────────────────────

def write_register_inventory_report(
    records: List[RegisterModuleRecord],
    total_area: float,
    total_cells: int,
    output_path,
    extra_warnings: Optional[List[str]] = None,
) -> None:
    """Write ``register_inventory.rpt`` - per-module sequential cell breakdown.

    Each module section lists its register cell types with instance counts,
    Liberty-backed areas, and (where derivable) clock-gating status.
    The global summary aggregates by register cell type across the design.
    """
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    all_warnings = list(extra_warnings or [])
    if total_cells == 0:
        all_warnings.insert(0,
            "STATUS: UNAVAILABLE - No sequential cells found. "
            "The design may lack registers or the JSON source is pre-tech-mapping."
        )
    if total_area == 0.0 and total_cells > 0:
        all_warnings.insert(0,
            "STATUS: UNAVAILABLE - Zero sequential area (Liberty data absent)."
        )

    lines = []
    lines.append("=" * 95)
    lines.append(" REGISTER INVENTORY - Sequential cell breakdown by module")
    lines.append("=" * 95)
    lines.append(f" Source:          hierarchy-preserved synth JSON (post tech-mapping)")
    lines.append(f" Total modules with registers: {len(records)}")
    lines.append(f" Total register cells:        {total_cells}")
    lines.append(f" Estimated total register bits: {total_cells} (conservative: 1 bit/cell)")
    lines.append(f" Total register area:         {total_area:.4f} um2")
    lines.append("")

    if all_warnings:
        lines.append("--- STATUS / WARNINGS ---")
        for w in all_warnings:
            lines.append(f"  {w}")
        lines.append("")

    # Global summary by register cell type
    reg_types_global: Dict[str, Dict[str, float]] = _defaultdict(
        lambda: {"count": 0.0, "area": 0.0}
    )
    for rec in records:
        for rt in rec.reg_types:
            ct = rt["cell_type"]
            reg_types_global[ct]["count"] += rt["weighted_count"]
            reg_types_global[ct]["area"] += rt["weighted_area"]

    if reg_types_global:
        lines.append("--- Global Register Type Summary ---")
        lines.append(
            f" {'Register Type':<22s} {'Cells':>8s} {'Est.Bits':>9s} "
            f"{'Area (um2)':>12s}  {'%Reg Area':>9s}"
        )
        lines.append(f" {'-'*22} {'-'*8} {'-'*9} {'-'*12}  {'-'*9}")
        for ct in sorted(reg_types_global.keys()):
            d = reg_types_global[ct]
            pct = (d["area"] / total_area * 100.0) if total_area > 0 else 0.0
            lines.append(
                f" {ct:<22s} {int(d['count']):>8d} {int(d['count']):>9d} "
                f"{d['area']:>12.4f}  {pct:>8.2f}%"
            )
        lines.append("")

    # Per-module breakdown
    if records:
        lines.append("--- Per-Module Register Breakdown ---")
        for rec in records:
            gated_str = "GATED" if rec.gated else "direct"
            unver_str = f" [{','.join(rec.unverified)}]" if rec.unverified else ""
            lines.append(f"\n Module: {rec.module_name}")
            lines.append(f"   Instance path: {rec.instance_path}")
            lines.append(f"   Instances (multiplicity): {rec.instance_count}")
            lines.append(f"   Register cells: {rec.total_reg_cells}  "
                         f"Area: {rec.total_reg_area:.4f} um2  "
                         f"Clock: {gated_str}{unver_str}")

            if rec.reg_types:
                lines.append(
                    f"   {'Type':<22s} {'Count':>6s} {'Area':>12s}  "
                    f"{'%Mod':>6s}  Bits"
                )
                lines.append(f"   {'-'*22} {'-'*6} {'-'*12}  {'-'*6}  ----")
                mod_total_area = rec.total_reg_area
                for rt in rec.reg_types:
                    pct = (rt["weighted_area"] / mod_total_area * 100.0) if mod_total_area > 0 else 0.0
                    lines.append(
                        f"   {rt['cell_type']:<22s} {rt['weighted_count']:>6d} "
                        f"{rt['weighted_area']:>12.4f}  {pct:>5.1f}%  "
                        f"{rt['weighted_count']}"
                    )

    lines.append("")
    lines.append("=" * 95)
    lines.append(" End of register_inventory.rpt")
    lines.append("=" * 95)
    lines.append("")

    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_clock_gating_inventory_report(
    records: List[ClockGateModuleRecord],
    total_area: float,
    total_cells: int,
    output_path,
    extra_warnings: Optional[List[str]] = None,
) -> None:
    """Write ``clock_gating_inventory.rpt`` - per-module clock-gating cell breakdown.

    Each module section lists its clock-gating cell types with instance
    counts, Liberty-backed areas, and (where derivable from netlist
    connectivity) estimated downstream register-bit fanout.
    """
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    all_warnings = list(extra_warnings or [])
    if total_cells == 0:
        all_warnings.insert(0,
            "STATUS: NONE - No clock-gating cells found in the design. "
            "The RTL may not use explicit clock-gating, or the JSON source "
            "is pre-tech-mapping (generic cells)."
        )

    lines = []
    lines.append("=" * 95)
    lines.append(" CLOCK-GATING INVENTORY - Clock-gating cell breakdown by module")
    lines.append("=" * 95)
    lines.append(f" Source:               hierarchy-preserved synth JSON (post tech-mapping)")
    lines.append(f" Total modules with ICG: {len(records)}")
    lines.append(f" Total ICG cells:       {total_cells}")
    lines.append(f" Total ICG area:        {total_area:.4f} um2")
    lines.append("")

    # Estimate total gated register bits across all modules
    total_gated_bits = sum(r.est_downstream_regs for r in records)
    if total_gated_bits > 0:
        lines.append(f" Estimated gated register bits (intra-module): {total_gated_bits}")
    else:
        lines.append(
            f" Estimated gated register bits: UNAVAILABLE "
            f"(hierarchical netlist not provided or connectivity untraced)"
        )
    lines.append("")

    if all_warnings:
        lines.append("--- STATUS / WARNINGS ---")
        for w in all_warnings:
            lines.append(f"  {w}")
        lines.append("")

    # Global summary by ICG type
    cg_types_global: Dict[str, Dict[str, float]] = _defaultdict(
        lambda: {"count": 0.0, "area": 0.0}
    )
    for rec in records:
        for ct in rec.cg_types:
            cell_t = ct["cell_type"]
            cg_types_global[cell_t]["count"] += ct["weighted_count"]
            cg_types_global[cell_t]["area"] += ct["weighted_area"]

    if cg_types_global:
        lines.append("--- Global ICG Type Summary ---")
        lines.append(
            f" {'ICG Type':<22s} {'Cells':>8s} {'Area (um2)':>12s}  "
            f"{'%ICG Area':>9s}"
        )
        lines.append(f" {'-'*22} {'-'*8} {'-'*12}  {'-'*9}")
        for ct in sorted(cg_types_global.keys()):
            d = cg_types_global[ct]
            pct = (d["area"] / total_area * 100.0) if total_area > 0 else 0.0
            lines.append(
                f" {ct:<22s} {int(d['count']):>8d} {d['area']:>12.4f}  "
                f"{pct:>8.2f}%"
            )
        lines.append("")

    # Per-module breakdown
    if records:
        lines.append("--- Per-Module Clock-Gating Breakdown ---")
        for rec in records:
            unver_str = f" [{','.join(rec.unverified)}]" if rec.unverified else ""
            lines.append(f"\n Module: {rec.module_name}")
            lines.append(f"   Instance path: {rec.instance_path}")
            lines.append(f"   Instances (multiplicity): {rec.instance_count}")
            lines.append(f"   ICG cells: {rec.total_cg_cells}  "
                         f"Area: {rec.total_cg_area:.4f} um2{unver_str}")

            if rec.est_downstream_regs > 0:
                lines.append(f"   Est. gated register bits (intra-module): {rec.est_downstream_regs}")
            elif rec.total_cg_cells > 0:
                lines.append(f"   Est. gated register bits: UNAVAILABLE (no intra-module trace)")

            if rec.cg_types:
                lines.append(
                    f"   {'Type':<22s} {'Count':>6s} {'Area':>12s}"
                )
                lines.append(f"   {'-'*22} {'-'*6} {'-'*12}")
                for ct in rec.cg_types:
                    lines.append(
                        f"   {ct['cell_type']:<22s} {ct['weighted_count']:>6d} "
                        f"{ct['weighted_area']:>12.4f}"
                    )

    lines.append("")
    lines.append("=" * 95)
    lines.append(" End of clock_gating_inventory.rpt")
    lines.append("=" * 95)
    lines.append("")

    output_path.write_text("\n".join(lines), encoding="utf-8")
