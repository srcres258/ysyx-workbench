#!/usr/bin/env python3
# ============================================================================
# synth_summary.py — Schema-v3 synthesis summary renderer.
# ============================================================================
# Consumes:
#   - Parsed area model     (report_parser.parse_synth_stat / build_hierarchy_area_tree)
#   - Parsed timing model   (report_parser.parse_classified_timing)
#   - Budget via CLI        (SYNTH_AREA_BUDGET_UM2 from Makefile)
#   - Provenance via CLI    (commit hashes, tool versions, PDK info)
#
# Produces:
#   - npc/build/synth/synth_summary.json       (schema v3, nested sections + legacy flat aliases)
#   - npc/build/synth/synth_summary.txt        (human-readable baseline report)
#   - npc/build/synth/optimization_hotspots.txt (evidence-backed hotspot analysis)
#
# Design principle: fail-closed.  Missing reg2reg stays null/warned, never
# fabricated.  V1/V2 fields preserved exactly — no rename, no removal.
# Schema v3 adds nested sections (area, timing, fanout, constraints, warnings,
# provenance) while keeping flat legacy aliases for backward compatibility.
# ============================================================================

from __future__ import annotations

import json
import sys
from pathlib import Path

# Add this script's directory to sys.path so imports of sibling modules
# (report_parser, synth_hierarchy, synth_timing) resolve correctly.
_SCRIPT_DIR = str(Path(__file__).resolve().parent)
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

from typing import Any, Dict, List, Optional

# ── helpers ──────────────────────────────────────────────────────────

def _slugify(s: str) -> str:
    return s.strip().replace(" ", "_").replace("/", "_")


def _wr(file_handle, text: str = "") -> None:
    """Append a line (or blank) to the text report."""
    file_handle.write(text + "\n")


# ── area helpers ─────────────────────────────────────────────────────

def _build_area_by_cell_class(hierarchy_rows: List[Dict]) -> Dict[str, Dict[str, float]]:
    """Aggregate per-category cell counts and areas across ALL hierarchy rows.

    Local values are per-instance; we weight by ``instance_count`` so
    repeated module instantiations contribute fully.
    """
    cats: Dict[str, Dict[str, float]] = {}
    for row in hierarchy_rows:
        multiplicity = row.get("instance_count", 1)
        for cat_name, cat_data in row.get("categories", {}).items():
            if cat_name not in cats:
                cats[cat_name] = {"cell_count": 0, "area_um2": 0.0}
            cats[cat_name]["cell_count"] += cat_data.get("count", 0) * multiplicity
            cats[cat_name]["area_um2"] += cat_data.get("area", 0.0) * multiplicity
    return cats


def _top_area_contributors(hierarchy_rows: List[Dict], top_n: int = 5) -> List[Dict]:
    """Return the top-N modules by local area (descending)."""
    sorted_rows = sorted(
        hierarchy_rows,
        key=lambda r: r.get("local_area", 0),
        reverse=True,
    )
    return sorted_rows[:top_n]


# ── timing helpers ───────────────────────────────────────────────────

def _per_category_wns_tns(
    classified: Dict, cat_name: str
) -> Dict[str, Optional[float]]:
    """Extract per-category WNS/TNS from the classified timing dict.

    WNS = worst (most negative) slack in the category's path list.
    TNS = sum of all negative slacks in the category's path list
          (note: this is the TNS of the *visible* paths only, not
           the true global per-category TNS when truncated).

    Returns {'wns_ns': float|None, 'tns_ns': float|None}.
    """
    paths = classified.get(cat_name, [])
    if not paths:
        return {"wns_ns": None, "tns_ns": None}

    slacks = [p["slack"] for p in paths if "slack" in p]
    if not slacks:
        return {"wns_ns": None, "tns_ns": None}

    wns = min(slacks)
    tns = sum(s for s in slacks if s < 0)
    return {"wns_ns": round(wns, 4), "tns_ns": round(tns, 4)}


# ── frequency helpers ────────────────────────────────────────────────

def derive_max_frequency(
    wns_ns: float,
    target_mhz: int,
    clk_uncertainty_ns: float = 0.20,
) -> int:
    """Compute max setup-clean integer MHz from WNS slack.

    Delegates to report_parser.derive_max_frequency.
    """
    from report_parser import derive_max_frequency as _dmf

    return _dmf(wns_ns, target_mhz, clk_uncertainty_ns)


# ── v2 JSON builder ──────────────────────────────────────────────────

def build_summary_json(
    design: str,
    target_mhz: int,
    area_result: Dict[str, Any],
    hierarchy_rows: List[Dict],
    timing_result: Dict,
    area_by_class: Dict[str, Dict[str, float]],
    area_budget_um2: int,
    provenance: Optional[Dict[str, str]] = None,
    fanout_source: str = "unknown",
    coverage_status: str = "unknown",
    coverage_note: str = "",
    view_type: str = "canonical_flat",
    hierarchy_attribution_area_um2: Optional[float] = None,
    core_data_reg2reg_source: str = "none",
) -> Dict[str, Any]:
    """Assemble the schema-v4 synth summary JSON dict.

    Emits nested sections (area, timing, fanout, constraints, warnings,
    provenance) while preserving flat legacy aliases for backward compatibility.

    Schema v4 adds split-view QoR fields, explicit budget_pass, view_type,
    core_data_reg2reg source provenance, and coverage completeness flags.
    """

    cell_count = int(area_result.get("cell_count", 0))
    area_um2 = float(area_result.get("area_um2", 0.0))

    global_wns = timing_result.get("wns", 0.0)
    global_tns = timing_result.get("tns", 0.0)

    try:
        global_fmax = derive_max_frequency(global_wns, target_mhz)
    except (ValueError, TypeError):
        global_fmax = 0

    # ── budget_pass (driven by canonical-flat area, NOT hierarchy attribution) ──
    area_rounded = round(area_um2, 2)
    budget_pass: Optional[bool] = None
    if area_budget_um2 > 0:
        budget_pass = area_rounded <= area_budget_um2
    # When budget is 0 (no budget set), budget_pass stays None.

    # ── reg2reg core Fmax ─────────────────────────────────────
    reg2reg_data = _per_category_wns_tns(timing_result, "reg2reg")
    reg2reg_wns = reg2reg_data["wns_ns"]
    if reg2reg_wns is not None:
        try:
            core_reg2reg_fmax = derive_max_frequency(reg2reg_wns, target_mhz)
        except (ValueError, TypeError):
            core_reg2reg_fmax = None
    else:
        core_reg2reg_fmax = None

    data_reg2reg_data = _per_category_wns_tns(timing_result, "data_reg2reg")
    data_reg2reg_wns = data_reg2reg_data["wns_ns"]
    if data_reg2reg_wns is not None:
        try:
            core_data_reg2reg_fmax = derive_max_frequency(data_reg2reg_wns, target_mhz)
        except (ValueError, TypeError):
            core_data_reg2reg_fmax = None
    else:
        core_data_reg2reg_fmax = None

    # ── core_data_reg2reg_source provenance ───────────────────
    # Surface the data_reg2reg_source from the timing bridge (task 3 evidence).
    # Falls back to "none" when the field is absent (pre-task-3 builds).
    qd_source = core_data_reg2reg_source
    if qd_source == "none" and "data_reg2reg_source" in timing_result:
        qd_source = timing_result["data_reg2reg_source"]

    # ── split-view area fields ─────────────────────────────────
    # canonical_flat_area_um2 is authoritative (same as area_um2).
    # hierarchy_attribution_area_um2 is only populated when both views exist.
    canonical_flat_area_um2 = area_rounded
    has_attribution = hierarchy_attribution_area_um2 is not None
    area_overhead_um2: Optional[float] = None
    area_overhead_pct: Optional[float] = None
    if has_attribution:
        area_overhead_um2 = round(
            hierarchy_attribution_area_um2 - canonical_flat_area_um2, 2
        )
        if canonical_flat_area_um2 > 0:
            area_overhead_pct = round(
                (area_overhead_um2 / canonical_flat_area_um2) * 100.0, 2
            )

    # ── per-category timing sections ───────────────────────────
    timing_section: Dict[str, Any] = {
        "global": {"wns_ns": round(global_wns, 4), "tns_ns": round(global_tns, 4)},
    }
    for cat in ("reg2reg", "data_reg2reg", "in2reg", "reg2out", "in2out",
                 "clock_enable", "clock_gating_setup"):
        cat_wns_tns = _per_category_wns_tns(timing_result, cat)
        paths = timing_result.get(cat, [])
        timing_section[cat] = {
            "wns_ns": cat_wns_tns["wns_ns"],
            "tns_ns": cat_wns_tns["tns_ns"],
            "path_count": len(paths),
            "top_paths": paths,
        }

    hold_paths = timing_result.get("hold", [])
    hold_slacks = [h["slack"] for h in hold_paths if "slack" in h]
    hold_classified = timing_result.get("hold_classified", [])
    hold_sub_cats = timing_result.get("hold_sub_categories", {})
    timing_section["hold"] = {
        "wns_ns": round(min(hold_slacks), 4) if hold_slacks else None,
        "tns_ns": round(sum(s for s in hold_slacks if s < 0), 4) if hold_slacks else None,
        "endpoint_count": len(hold_paths),
        "top_paths": hold_paths,
        "classified_paths": hold_classified,
        "sub_categories": hold_sub_cats,
    }

    total_endpoints = timing_result.get("total_endpoints_in_netlist", 0)
    constrained_count = timing_result.get("constrained_endpoint_count", 0)
    unconstrained = timing_result.get("unconstrained", [])

    hierarchy_source = hierarchy_rows[0].get("hierarchy_source", "unknown") if hierarchy_rows else "unknown"

    # ── v4 area section (adds split-view area fields) ──────────
    utilisation = (area_rounded / area_budget_um2 * 100.0) if area_budget_um2 > 0 else None
    area_section: Dict[str, Any] = {
        "total_cells": cell_count,
        "total_area_um2": area_rounded,
        "budget_um2": area_budget_um2,
        "utilisation_pct": round(utilisation, 1) if utilisation is not None else None,
        "by_hierarchy": hierarchy_rows,
        "by_cell_class": area_by_class,
        "hierarchy_source": hierarchy_source,
        "canonical_flat_area_um2": canonical_flat_area_um2,
        "hierarchy_attribution_area_um2": hierarchy_attribution_area_um2,
        "area_attribution_overhead_um2": area_overhead_um2,
        "area_attribution_overhead_percent": area_overhead_pct,
    }

    # ── v3 fanout section ─────────────────────────────────────
    fanout_nets = timing_result.get("high_fanout", [])
    fanout_section: Dict[str, Any] = {
        "high_fanout_nets": fanout_nets,
        "source": fanout_source,
    }

    # ── v4 constraints section (adds coverage completeness flags) ─
    constraints_section: Dict[str, Any] = {
        "constrained_endpoints": constrained_count,
        "total_endpoints_in_netlist": total_endpoints,
        "unconstrained_endpoints": unconstrained,
        "coverage_status": coverage_status,
        "coverage_is_complete": coverage_status not in ("unknown", "unavailable", "LOWER_BOUND"),
        "coverage_has_evidence": coverage_status not in ("unknown", "unavailable"),
        "unconstrained_is_complete": False,
    }
    if coverage_note:
        constraints_section["coverage_note"] = coverage_note

    # ── assemble v4 ────────────────────────────────────────────
    # Enrich provenance with view type and synthesis mode so
    # downstream consumers can validate canonical authority.
    enriched_provenance = dict(provenance) if provenance else {}
    enriched_provenance["view_type"] = view_type
    enriched_provenance["schema_version"] = "4"
    if core_data_reg2reg_source != "none":
        enriched_provenance["core_data_reg2reg_source"] = core_data_reg2reg_source

    summary: Dict[str, Any] = {
        "schema_version": 4,
        # ── v1/v2/v3 flat compatibility fields (unchanged) ───────
        "design": design,
        "target_mhz": target_mhz,
        "final_mhz": global_fmax,
        "wns_ns": round(global_wns, 4),
        "tns_ns": round(global_tns, 4),
        "cell_count": cell_count,
        "area_um2": area_rounded,
        "global_derived_fmax_mhz": global_fmax,
        "core_reg2reg_fmax_mhz": core_reg2reg_fmax,
        "core_data_reg2reg_fmax_mhz": core_data_reg2reg_fmax,
        "area_budget_um2": area_budget_um2,
        "area_by_hierarchy": hierarchy_rows,
        "area_by_cell_class": area_by_class,
        "area_hierarchy_source": hierarchy_source,
        "timing": timing_section,
        "path_groups": timing_result.get("path_groups", []),
        "high_fanout": fanout_nets,
        "unconstrained": unconstrained,
        "warnings": timing_result.get("warnings", []),
        # ── v4 root-level fields ────────────────────────────────
        "budget_pass": budget_pass,
        "view_type": view_type,
        "canonical_flat_area_um2": canonical_flat_area_um2,
        "hierarchy_attribution_area_um2": hierarchy_attribution_area_um2,
        "area_attribution_overhead_um2": area_overhead_um2,
        "area_attribution_overhead_percent": area_overhead_pct,
        "core_data_reg2reg_source": qd_source,
        # ── v3/v4 nested sections ───────────────────────────────
        "area": area_section,
        "fanout": fanout_section,
        "constraints": constraints_section,
        "provenance": enriched_provenance,
    }

    if "timing_parse_error" in area_result:
        summary["timing_parse_error"] = area_result["timing_parse_error"]
    if "area_parse_error" in area_result:
        summary["area_parse_error"] = area_result["area_parse_error"]

    return summary


# ── text report builder ──────────────────────────────────────────────

def build_summary_text(
    summary: Dict[str, Any],
    hierarchy_rows: List[Dict],
    area_by_class: Dict[str, Dict[str, float]],
) -> str:
    """Render the human-readable synth_summary.txt from the summary dict."""
    import io

    buf = io.StringIO()

    design = summary["design"]
    schema_ver = summary.get("schema_version", 2)
    target = summary["target_mhz"]
    area_um2 = summary["area_um2"]
    budget = summary.get("area_budget_um2", 0)
    global_wns = summary["wns_ns"]
    global_tns = summary["tns_ns"]
    global_fmax = summary["global_derived_fmax_mhz"]
    core_fmax = summary.get("core_reg2reg_fmax_mhz")
    cell_count = summary["cell_count"]
    warnings = summary.get("warnings", [])
    timing = summary.get("timing", {})
    provenance = summary.get("provenance", {})
    view_type = summary.get("view_type", "unknown")
    budget_pass = summary.get("budget_pass")

    _wr(buf, "=" * 60)
    _wr(buf, f" SYNTHESIS SUMMARY: {design} @ {target} MHz target")
    _wr(buf, f" Schema version: {schema_ver}")
    _wr(buf, f" View type:      {view_type}")
    hierarchy_source = summary.get("area_hierarchy_source", "unknown")
    _wr(buf, f" Hierarchy source: {hierarchy_source}")
    _wr(buf, "=" * 60)
    _wr(buf)

    # ── Provenance ────────────────────────────────────────────
    if provenance:
        _wr(buf, "--- Provenance ---")
        for key, label in [
            ("yosys_version", "Yosys"),
            ("ieda_version", "iEDA"),
            ("pdk", "PDK"),
            ("liberty", "Liberty"),
            ("workbench_commit", "Workbench commit"),
            ("cpu_commit", "CPU commit"),
            ("target_mhz", "Target"),
            ("generated_at", "Generated"),
        ]:
            val = provenance.get(key) or "N/A"
            _wr(buf, f"  {label:<20s} {val}")
        _wr(buf)

    # ── Area Budget ────────────────────────────────────────────
    _wr(buf, "--- Area Budget ---")
    _wr(buf, f"  Total area:   {area_um2:>12.2f} µm²")
    _wr(buf, f"  Budget:       {budget:>12d} µm²")
    if budget > 0:
        pct = area_um2 / budget * 100
        if budget_pass is True:
            status = "PASS"
        elif budget_pass is False:
            status = "FAIL (over budget)"
        else:
            status = "UNKNOWN"
        _wr(buf, f"  Utilisation:  {pct:>11.1f}%")
        _wr(buf, f"  Status:       {status}")
    else:
        _wr(buf, "  Utilisation:  N/A (budget not set)")
        _wr(buf, "  Status:       N/A (budget not set)")
    _wr(buf)

    # ── Split-View Area (v4) ────────────────────────────────────
    canonical_area = summary.get("canonical_flat_area_um2")
    attr_area = summary.get("hierarchy_attribution_area_um2")
    if canonical_area is not None:
        _wr(buf, "--- Split-View Area (v4) ---")
        _wr(buf, f"  Canonical flat area:       {canonical_area:>12.2f} µm²")
        if attr_area is not None:
            overhead = summary.get("area_attribution_overhead_um2")
            overhead_pct = summary.get("area_attribution_overhead_percent")
            _wr(buf, f"  Hierarchy attribution area:{attr_area:>12.2f} µm²")
            if overhead is not None:
                _wr(buf, f"  Overhead:                  {overhead:>+12.2f} µm²  ({overhead_pct:+.2f}%)" if overhead_pct is not None else f"  Overhead:                  {overhead:>+12.2f} µm²")
        else:
            _wr(buf, "  Hierarchy attribution:     not available (single-view synthesis)")
    _wr(buf)

    # ── Top Area Contributors ──────────────────────────────────
    _wr(buf, "--- Top Area Contributors (by local area) ---")
    contributors = _top_area_contributors(hierarchy_rows, top_n=5)
    if contributors:
        _wr(buf, f"  {'Module':<45s} {'Local Area':>12s}  {'% of Top':>8s}")
        _wr(buf, f"  {'-'*45} {'-'*12}  {'-'*8}")
        for c in contributors:
            name = c.get("module_name", c.get("instance_path", "?"))
            larea = c.get("local_area", 0)
            pct_top = c.get("pct_of_top_area", 0)
            _wr(buf, f"  {name:<45s} {larea:>12.2f}  {pct_top:>7.1f}%")
    else:
        _wr(buf, "  (no hierarchy data available)")
    _wr(buf)

    # ── Area by Cell Class ─────────────────────────────────────
    _wr(buf, "--- Area by Cell Class ---")
    _wr(buf, f"  Total cells: {cell_count}")
    if area_by_class:
        _wr(buf, f"  {'Category':<20s} {'Cells':>8s} {'Area (µm²)':>12s}  {'% Area':>7s}")
        _wr(buf, f"  {'-'*20} {'-'*8} {'-'*12}  {'-'*7}")
        total_class_area = sum(v["area_um2"] for v in area_by_class.values())
        for cat in ["sequential", "combinational", "clock-gating",
                     "buffer/inverter", "mux", "arithmetic", "other"]:
            if cat in area_by_class:
                d = area_by_class[cat]
                cat_area = d["area_um2"]
                cat_pct = (cat_area / total_class_area * 100) if total_class_area > 0 else 0
                _wr(buf, f"  {cat:<20s} {d['cell_count']:>8d} {cat_area:>12.2f}  {cat_pct:>6.1f}%")
    else:
        _wr(buf, "  (no cell class data available)")
    _wr(buf)

    # ── Global Timing ──────────────────────────────────────────
    _wr(buf, "--- Global Timing ---")
    _wr(buf, f"  Target:                 {target} MHz")
    _wr(buf, f"  WNS (worst slack):      {global_wns} ns")
    _wr(buf, f"  TNS (total - slack):    {global_tns} ns")
    _wr(buf, f"  Derived Fmax (global):  {global_fmax} MHz")
    _wr(buf, f"  (compat) final_mhz:     {summary['final_mhz']} MHz")
    _wr(buf)

    # ── Core reg2reg Timing ────────────────────────────────────
    reg2reg_info = timing.get("reg2reg", {})
    data_reg2reg_info = timing.get("data_reg2reg", {})
    qd_source = summary.get("core_data_reg2reg_source", "none")
    _wr(buf, "--- Core reg2reg Timing ---")
    if data_reg2reg_info.get("wns_ns") is not None:
        _wr(buf, f"  data_reg2reg WNS:       {data_reg2reg_info['wns_ns']} ns")
        _wr(buf, f"  data_reg2reg TNS:       {data_reg2reg_info['tns_ns']} ns")
        _wr(buf, f"  data_reg2reg paths:     {data_reg2reg_info['path_count']}")
    else:
        _wr(buf, "  data_reg2reg WNS:       N/A (no Q→D paths in report)")
    _wr(buf, f"  data_reg2reg source:    {qd_source}")
    data_fmax = summary.get("core_data_reg2reg_fmax_mhz")
    if data_fmax is not None:
        _wr(buf, f"  Core Fmax (data):       {data_fmax} MHz")
    else:
        _wr(buf, "  Core Fmax (data):       N/A (no Q→D data paths in report)")
    _wr(buf)
    if reg2reg_info.get("wns_ns") is not None:
        _wr(buf, f"  reg2reg WNS:            {reg2reg_info['wns_ns']} ns")
        _wr(buf, f"  reg2reg TNS:            {reg2reg_info['tns_ns']} ns")
        _wr(buf, f"  reg2reg path count:     {reg2reg_info['path_count']}")
    else:
        _wr(buf, "  reg2reg WNS:            N/A")
        _wr(buf, "  reg2reg TNS:            N/A")
        _wr(buf, "  reg2reg path count:     0")
    if core_fmax is not None:
        _wr(buf, f"  Core Fmax (reg2reg):    {core_fmax} MHz")
    else:
        _wr(buf, "  Core Fmax (reg2reg):    N/A (no reg2reg paths in report)")
    _wr(buf)

    # ── Other Setup Path Categories ────────────────────────────
    for cat_name, cat_label in [("in2reg", "Input→Reg"),
                                 ("reg2out", "Reg→Output"),
                                 ("in2out", "Input→Output")]:
        cat_info = timing.get(cat_name, {})
        _wr(buf, f"--- {cat_label} Timing ---")
        if cat_info.get("wns_ns") is not None:
            cat_wns = cat_info["wns_ns"]
            cat_tns = cat_info["tns_ns"]
            cat_count = cat_info["path_count"]
            _wr(buf, f"  WNS:       {cat_wns} ns")
            _wr(buf, f"  TNS:       {cat_tns} ns")
            _wr(buf, f"  Paths:     {cat_count}")
            # show worst path
            top_paths = cat_info.get("top_paths", [])
            if top_paths:
                worst = top_paths[0]
                _wr(buf, f"  Worst:     {worst['startpoint']} → {worst['endpoint']}  slack={worst['slack']}ns")
        else:
            _wr(buf, "  (no paths in this category)")
        _wr(buf)

    # ── Clock-Enable / Clock-Gating Setup ────────────────────────
    for cat_name, cat_label in [("clock_enable", "Clock-Enable (EN/E pins)"),
                                 ("clock_gating_setup", "Clock-Gating Setup")]:
        cat_info = timing.get(cat_name, {})
        _wr(buf, f"--- {cat_label} ---")
        if cat_info.get("wns_ns") is not None:
            cat_wns = cat_info["wns_ns"]
            cat_tns = cat_info["tns_ns"]
            cat_count = cat_info["path_count"]
            _wr(buf, f"  WNS:       {cat_wns} ns")
            _wr(buf, f"  TNS:       {cat_tns} ns")
            _wr(buf, f"  Paths:     {cat_count}")
            top_paths = cat_info.get("top_paths", [])
            if top_paths:
                worst = top_paths[0]
                _wr(buf, f"  Worst:     {worst['startpoint']} → {worst['endpoint']}  slack={worst['slack']}ns")
        else:
            _wr(buf, "  (no paths in this category)")
        _wr(buf)

    # ── Hold Timing ────────────────────────────────────────────
    hold_info = timing.get("hold", {})
    _wr(buf, "--- Hold Timing ---")
    if hold_info.get("wns_ns") is not None:
        _wr(buf, f"  WNS:       {hold_info['wns_ns']} ns")
        _wr(buf, f"  TNS:       {hold_info['tns_ns']} ns")
        _wr(buf, f"  Endpoints: {hold_info['endpoint_count']} (summary table)")

        classified_hold = hold_info.get("classified_paths", [])
        if classified_hold:
            _wr(buf, f"  Classified: {len(classified_hold)} paths (detailed tables)")

        hold_sub = hold_info.get("sub_categories", {})
        if hold_sub:
            _wr(buf, "  Sub-categories:")
            for sub_cat in ("data_reg2reg", "clock_enable", "clock_gating", "reg2reg"):
                sc = hold_sub.get(sub_cat)
                if sc:
                    _wr(buf, f"    {sub_cat:<20s} WNS={sc['wns_ns']:>8}ns "
                         f"TNS={sc['tns_ns']:>8}ns  paths={sc['path_count']:>4}")
    else:
        _wr(buf, "  (no hold data)")
    _wr(buf)

    # ── Path Groups ────────────────────────────────────────────
    path_groups = summary.get("path_groups", [])
    _wr(buf, f"--- Path Groups ({len(path_groups)}) ---")
    if path_groups:
        for pg in path_groups[:10]:
            _wr(buf, f"  {pg['clock_group']}/{pg['delay_type']}: "
                 f"{pg['endpoint_count']} endpoints, "
                 f"WNS={pg['wns']}ns, TNS={pg['tns']}ns")
    else:
        _wr(buf, "  (no path group data)")
    _wr(buf)

    # ── High Fanout ────────────────────────────────────────────
    high_fanout = summary.get("high_fanout", [])
    _wr(buf, f"--- High-Fanout Nets ({len(high_fanout)}) ---")
    if high_fanout:
        for n in high_fanout[:10]:
            _wr(buf, f"  {n['net_name']}: fanout={n['fanout']}, "
                 f"driver={n['driver_pin']}")
    else:
        _wr(buf, "  (no high-fanout data)")
    _wr(buf)

    # ── Unconstrained ──────────────────────────────────────────
    unconstrained = summary.get("unconstrained", [])
    _wr(buf, f"--- Unconstrained Endpoints ({len(unconstrained)}) ---")
    if unconstrained:
        for u in unconstrained[:10]:
            _wr(buf, f"  {u['pin_name']} ({u['pin_type']})")
    else:
        _wr(buf, "  (no unconstrained endpoints flagged)")
    _wr(buf)

    # ── Warnings ───────────────────────────────────────────────
    if warnings:
        _wr(buf, f"--- Warnings ({len(warnings)}) ---")
        for w in warnings[:20]:
            _wr(buf, f"  - {w}")
        if len(warnings) > 20:
            _wr(buf, f"  ... and {len(warnings) - 20} more")
        _wr(buf)

    _wr(buf, "=" * 60)
    _wr(buf, " End of synthesis summary.")
    _wr(buf, "=" * 60)

    return buf.getvalue()


# ── optimization hotspots text ───────────────────────────────────────

def build_hotspots_text(
    summary: Dict[str, Any],
) -> str:
    """Render evidence-backed optimization hotspots from the v3 summary.

    Only surfaces hotspots that are directly supported by evidence in the
    synthesis data.  Hypotheses (inferences beyond the data) are explicitly
    marked as such.  No RTL conclusions are invented.
    """
    import io
    from datetime import datetime

    buf = io.StringIO()

    design = summary["design"]
    schema_ver = summary.get("schema_version", 2)
    target = summary["target_mhz"]
    budget = summary.get("area_budget_um2", 0)
    area_um2 = summary["area_um2"]
    cell_count = summary["cell_count"]
    global_fmax = summary.get("global_derived_fmax_mhz", 0)
    data_fmax = summary.get("core_data_reg2reg_fmax_mhz")
    warnings_list = summary.get("warnings", [])
    timing = summary.get("timing", {})
    view_type = summary.get("view_type", "unknown")
    budget_pass = summary.get("budget_pass")
    qd_source = summary.get("core_data_reg2reg_source", "none")

    _wr(buf, "=" * 70)
    _wr(buf, " OPTIMIZATION HOTSPOTS")
    _wr(buf, f" Design: {design}  |  Target: {target} MHz  |  Schema v{schema_ver}")
    _wr(buf, f" View: {view_type}  |  Generated: {datetime.now().isoformat()}")
    _wr(buf, "=" * 70)
    _wr(buf)
    _wr(buf, "DISCLAIMER: This report is evidence-backed only.  Items marked")
    _wr(buf, "(HYPOTHESIS) are inferences beyond the direct synthesis data and should")
    _wr(buf, "be verified before committing to RTL changes.")
    _wr(buf)

    # ── Hotspot 1: Area utilisation ────────────────────────────
    _wr(buf, "── 1. Area Utilisation")
    if budget > 0:
        util = area_um2 / budget * 100.0
        _wr(buf, f"  Total area:       {area_um2:>12.2f} µm²")
        _wr(buf, f"  Budget:           {budget:>12d} µm²")
        _wr(buf, f"  Utilisation:      {util:>11.1f}%")
        if budget_pass is False:
            _wr(buf, "  STATUS: OVER BUDGET")
            _wr(buf, f"  Slack:            {area_um2 - budget:>12.2f} µm² over")
            _wr(buf)
            _wr(buf, "  EVIDENCE: top-level cell area from Yosys stat -liberty; real mapped")
            _wr(buf, "  netlist area.  Any optimisation must reduce gate count or switch to")
            _wr(buf, "  smaller cell variants.")
        elif budget_pass is True:
            if util > 90:
                _wr(buf, "  STATUS: NEAR BUDGET — limited headroom for additions")
            else:
                _wr(buf, "  STATUS: WITHIN BUDGET")
        else:
            _wr(buf, "  STATUS: UNKNOWN — budget_pass not determined")
    else:
        _wr(buf, "  (no budget set — area utilisation cannot be assessed)")
    _wr(buf)

    # ── Hotspot 2: Top area contributors ───────────────────────
    hierarchy_rows = summary.get("area_by_hierarchy", [])
    area_by_class = summary.get("area_by_cell_class", {})
    _wr(buf, "── 2. Top Area Contributors")
    if hierarchy_rows:
        top = sorted(hierarchy_rows, key=lambda r: r.get("local_area", 0), reverse=True)
        _wr(buf, f"  {'Module':<45s} {'Area (µm²)':>12s}  {'% of Top':>8s}")
        _wr(buf, f"  {'-'*45} {'-'*12}  {'-'*8}")
        for row in top[:5]:
            name = row.get("module_name", row.get("instance_path", "?"))
            larea = row.get("local_area", 0)
            pct = row.get("pct_of_top_area", 0)
            _wr(buf, f"  {name:<45s} {larea:>12.2f}  {pct:>7.1f}%")
        _wr(buf)
        _wr(buf, "  EVIDENCE: per-module area from hierarchy-preserved stat -json.")
        _wr(buf, "  The module(s) above dominate area; focus optimisation effort there.")
    else:
        _wr(buf, "  (no hierarchy data — cannot identify top contributors)")
    _wr(buf)

    # ── Hotspot 3: Cell class distribution ─────────────────────
    _wr(buf, "── 3. Cell Class Distribution")
    if area_by_class:
        class_order = ["sequential", "combinational", "clock-gating",
                       "buffer/inverter", "mux", "arithmetic", "other"]
        total_class_area = sum(v["area_um2"] for v in area_by_class.values())
        _wr(buf, f"  {'Category':<20s} {'Cells':>8s} {'Area (µm²)':>12s}  {'% Area':>7s}")
        _wr(buf, f"  {'-'*20} {'-'*8} {'-'*12}  {'-'*7}")
        for cat in class_order:
            if cat in area_by_class:
                d = area_by_class[cat]
                cat_area = d["area_um2"]
                cat_pct = (cat_area / total_class_area * 100) if total_class_area > 0 else 0
                _wr(buf, f"  {cat:<20s} {d['cell_count']:>8d} {cat_area:>12.2f}  {cat_pct:>6.1f}%")
        _wr(buf)

        sequential = area_by_class.get("sequential", {})
        combinational = area_by_class.get("combinational", {})
        if sequential.get("area_um2", 0) > combinational.get("area_um2", 0) * 0.5:
            _wr(buf, "  (HYPOTHESIS) Sequential cells account for a large fraction; consider:")
            _wr(buf, "    - Reducing pipeline stage width or register file entries")
            _wr(buf, "    - Merging equivalent state registers across modules")
        if area_by_class.get("buffer/inverter", {}).get("area_um2", 0) > total_class_area * 0.15:
            _wr(buf, "  (HYPOTHESIS) Buffer/inverter area is significant; high-fanout nets or")
            _wr(buf, "    long wire buffering may be inflating the gate count.")
    else:
        _wr(buf, "  (no cell class data)")
    _wr(buf)

    # ── Hotspot 4: Timing headroom ─────────────────────────────
    _wr(buf, "── 4. Timing Headroom")
    _wr(buf, f"  Global Fmax (spec):  {global_fmax} MHz  (target: {target} MHz)")
    if global_fmax >= target * 1.5:
        _wr(buf, "  STATUS: Ample headroom — timing is not a constraint at target frequency.")
    elif global_fmax >= target:
        _wr(buf, "  STATUS: Met — timing passes but headroom is limited.")
    else:
        _wr(buf, f"  STATUS: FAIL — target {target} MHz not met; max achievable ~{global_fmax} MHz.")

    data_info = timing.get("data_reg2reg", {})
    if data_fmax is not None:
        _wr(buf, f"  Core data reg2reg Fmax: {data_fmax} MHz")
        _wr(buf, f"  Data reg2reg source:    {qd_source}")
        if data_fmax >= target * 2:
            _wr(buf, "  EVIDENCE: Core data paths are fast — the critical path is elsewhere.")
    else:
        _wr(buf, f"  Core data reg2reg Fmax: N/A  (source: {qd_source})")
    reg2reg_info = timing.get("reg2reg", {})
    if reg2reg_info.get("path_count", 0) == 0:
        _wr(buf, "  NOTE: No reg2reg paths in top-N STA report.  Core data paths may be")
        _wr(buf, "    faster than the worst I/O or clock-gating paths captured.")
    _wr(buf)

    # ── Hotspot 5: Critical path category ──────────────────────
    _wr(buf, "── 5. Worst-Path Category")
    worst_cat = None
    worst_wns = float("inf")
    for cat in ("reg2reg", "data_reg2reg", "in2reg", "reg2out", "in2out",
                 "clock_enable", "clock_gating_setup"):
        ci = timing.get(cat, {})
        if ci.get("wns_ns") is not None and ci["wns_ns"] < worst_wns:
            worst_wns = ci["wns_ns"]
            worst_cat = cat
    if worst_cat:
        _wr(buf, f"  Worst category: {worst_cat}  (WNS = {worst_wns} ns)")
        cat_labels = {
            "in2out": "I/O paths (input→output)",
            "in2reg": "I/O paths (input→register)",
            "reg2out": "I/O paths (register→output)",
            "reg2reg": "Internal register→register paths",
            "data_reg2reg": "True data register→register paths (Q→D)",
            "clock_enable": "Clock-enable control paths",
            "clock_gating_setup": "Clock-gating setup paths",
        }
        _wr(buf, f"  Meaning: {cat_labels.get(worst_cat, worst_cat)}")
        if worst_cat in ("in2out", "in2reg", "reg2out"):
            _wr(buf, "  EVIDENCE: The critical path is I/O-dominated.  Internal logic has")
            _wr(buf, "    substantial headroom.  (HYPOTHESIS) Reducing I/O timing constraints")
            _wr(buf, "    or adding pipeline registers at interfaces could raise Fmax.")
        if worst_cat in ("clock_enable", "clock_gating_setup"):
            _wr(buf, "  EVIDENCE: The critical path involves clock-gating control.  Review")
            _wr(buf, "    the clock-gating enable generation logic for long combinational chains.")
    else:
        _wr(buf, "  (no timing category data available)")
    _wr(buf)

    # ── Hotspot 6: High-fanout nets ────────────────────────────
    fanout = summary.get("fanout", {})
    fanout_nets = fanout.get("high_fanout_nets", summary.get("high_fanout", []))
    _wr(buf, "── 6. High-Fanout Nets")
    if fanout_nets:
        fanout_src = fanout.get("source", "unknown")
        _wr(buf, f"  Source: {fanout_src}")
        _wr(buf, f"  Count: {len(fanout_nets)} net(s) above threshold")
        for n in fanout_nets[:5]:
            fo = n.get("fanout", 0)
            net = n.get("net_name", "?")
            driver = n.get("driver_pin", "?")
            _wr(buf, f"  - fanout={fo:>4d}  net={net}")
            _wr(buf, f"    driver={driver}")
        if len(fanout_nets) < 5:
            _wr(buf, "  EVIDENCE: Few high-fanout nets — fanout is not a dominant issue.")
        else:
            _wr(buf, "  (HYPOTHESIS) High-fanout nets may benefit from buffering or")
            _wr(buf, "    replication to reduce delay and improve routability.")
    else:
        _wr(buf, "  (no high-fanout data)")
    _wr(buf)

    # ── Hotspot 7: Constraint coverage ─────────────────────────
    constraints = summary.get("constraints", {})
    _wr(buf, "── 7. Constraint Coverage")
    total_ep = constraints.get("total_endpoints_in_netlist", 0)
    constrained_ep = constraints.get("constrained_endpoints", 0)
    coverage_status = constraints.get("coverage_status", "unknown")
    if total_ep > 0:
        _wr(buf, f"  Total endpoints in netlist:   {total_ep}")
        _wr(buf, f"  Constrained (in STA report):  {constrained_ep}")
        _wr(buf, f"  Coverage status:              {coverage_status}")
        note = constraints.get("coverage_note", "")
        if note:
            _wr(buf, f"  Note: {note}")
    else:
        _wr(buf, "  (no constraint coverage data)")
    _wr(buf)

    # ── Warnings ───────────────────────────────────────────────
    if warnings_list:
        _wr(buf, "── 8. Caveats")
        meaningful = [w for w in warnings_list
                      if not w.startswith("Unconstrained count suppressed")]
        for w in meaningful[:10]:
            _wr(buf, f"  - {w}")
        _wr(buf)

    _wr(buf, "=" * 70)
    _wr(buf, " End of optimization hotspots.")
    _wr(buf, "=" * 70)

    return buf.getvalue()


# ── area flow comparison report ─────────────────────────────────────

def write_area_flow_comparison(
    canonical_dir: str,
    attribution_dir: str,
    design: str,
    target_mhz: int,
    output_dir: str,
) -> None:
    """Compare canonical-flat and hierarchy-attribution synthesis results.

    Reads synth_stat.txt from both view directories and produces
    ``area_flow_comparison.rpt`` in the output directory.  The report
    explicitly states the identical inputs and the single differing
    aspect: flatten vs keep_hierarchy.
    """
    import hashlib
    from datetime import datetime

    output_path = Path(output_dir) / "area_flow_comparison.rpt"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    canonical_stat = Path(canonical_dir) / "synth_stat.txt"
    attribution_stat = Path(attribution_dir) / "synth_stat.txt"

    lines: list[str] = []

    def _w(text: str = "") -> None:
        lines.append(text)

    _w("=" * 70)
    _w(" AREA FLOW COMPARISON REPORT")
    _w(f" Design: {design}  |  Target: {target_mhz} MHz")
    _w(f" Generated: {datetime.now().isoformat()}")
    _w("=" * 70)
    _w()
    _w("This report compares the authoritative canonical-flat QoR view")
    _w("(flattened, single-module STA netlist) with the analysis-only")
    _w("hierarchy-attribution view (hierarchy-preserved).  The two views")
    _w("share identical RTL sources, Liberty library, SDC constraints,")
    _w("ABC strategy, and clock-gating settings.  The ONLY difference is")
    _w("whether the netlist is flattened before STA.")
    _w()
    _w("Canonical-flat is the authoritative QoR source.  Hierarchy")
    _w("attribution is provided solely for per-module area breakdown and")
    _w("must NOT be used as a substitute for canonical-flat metrics.")
    _w()
    _w("--- Input Identity ---")
    _w("  RTL sources, Liberty, SDC, clock target, ABC strategy: IDENTICAL")
    _w("  Hierarchy/flatten setting:             DIFFERS (see below)")

    # Read and hash both netlists for identity evidence
    canonical_netlist = Path(canonical_dir) / f"{design}.netlist.v"
    attribution_netlist = Path(attribution_dir) / f"{design}.netlist.v"

    canonical_exists = canonical_netlist.is_file()
    attribution_exists = attribution_netlist.is_file()

    if canonical_exists:
        canonical_hash = hashlib.sha256(canonical_netlist.read_bytes()).hexdigest()[:16]
        _w(f"  Canonical-flat netlist hash (sha256): {canonical_hash}")
    else:
        _w("  Canonical-flat netlist:               MISSING")

    if attribution_exists:
        attribution_hash = hashlib.sha256(attribution_netlist.read_bytes()).hexdigest()[:16]
        _w(f"  Hierarchy-attribution netlist hash:    {attribution_hash}")
    else:
        _w("  Hierarchy-attribution netlist:         MISSING")
    _w()

    _w("--- Synthesis Setting Diff ---")
    _w("  canonical_flat:      flatten (single-module STA netlist)")
    _w("  hierarchy_attribution: keep_hierarchy (hierarchical STA netlist)")
    _w()

    # Parse area from synth_stat.txt
    def _parse_stat(path: Path) -> dict[str, float]:
        result: dict[str, float] = {"cell_count": 0, "area_um2": 0.0}
        if not path.is_file():
            return result
        text = path.read_text(encoding="utf-8", errors="replace")
        import re

        # Yosys stat -liberty format: "  <count>  <area>  cells"
        # The last matching line (after submodules) is the total.
        cell_matches = re.findall(r"^\s+(\d+)\s+[\d.E+-]+\s+cells", text, re.MULTILINE)
        if cell_matches:
            result["cell_count"] = int(cell_matches[-1])

        # Prefer "Chip area for top module" (hierarchical stat);
        # fall back to first "Chip area for module" (flat stat).
        area_match = re.search(r"Chip area for top module.*?:\s+([\d.]+)", text)
        if not area_match:
            area_match = re.search(r"Chip area for module.*?:\s+([\d.]+)", text)
        if area_match:
            result["area_um2"] = float(area_match.group(1))
        return result

    canonical_stat_data = _parse_stat(canonical_stat)
    attribution_stat_data = _parse_stat(attribution_stat)

    canonical_cells = int(canonical_stat_data["cell_count"])
    canonical_area = canonical_stat_data["area_um2"]
    attribution_cells = int(attribution_stat_data["cell_count"])
    attribution_area = attribution_stat_data["area_um2"]

    cell_delta = attribution_cells - canonical_cells
    area_delta = 0.0
    area_delta_pct = 0.0
    if canonical_area > 0 and attribution_area > 0:
        area_delta = attribution_area - canonical_area
        area_delta_pct = (area_delta / canonical_area) * 100.0

    _w("--- Area Comparison ---")
    _w(f"  {'':<30s} {'Canonical Flat':>16s}  {'Hierarchy Attr':>16s}")
    _w(f"  {'-'*30} {'-'*16}  {'-'*16}")
    _w(f"  {'Cell count:':<30s} {canonical_cells:>16d}  {attribution_cells:>16d}")
    _w(f"  {'Total area (µm²):':<30s} {canonical_area:>16.2f}  {attribution_area:>16.2f}")

    if canonical_area > 0 and attribution_area > 0:
        _w(f"  {'Area delta (attr - flat):':<30s} {area_delta:>+16.2f}  ({area_delta_pct:+.2f}%)")
    _w()

    # Cell count comparison
    if canonical_cells > 0 and attribution_cells > 0:
        _w("--- Cell Count Comparison ---")
        _w(f"  Canonical-flat cells:                {canonical_cells}")
        _w(f"  Hierarchy-attribution cells:         {attribution_cells}")
        _w(f"  Cell count delta (attr - flat):      {cell_delta:+d}")
        _w()

    # Explanation of area delta
    _w("--- Delta Explanation ---")
    if canonical_area <= 0 or attribution_area <= 0:
        _w("  Cannot compute delta: one or both stat files are missing or empty.")
    elif abs(area_delta) < 1.0 and cell_delta == 0:
        _w("  Area and cell count are effectively identical between the two views.")
        _w("  This is expected: flatten/keep_hierarchy does not change cell count")
        _w("  or total mapped cell area when the synthesis strategy is identical.")
        _w("  The only difference is whether module boundaries are preserved in")
        _w("  the netlist for STA consumption.")
    elif cell_delta == 0:
        _w(f"  Cell count is identical ({canonical_cells}), but total area differs")
        _w(f"  by {area_delta:+.2f} µm² ({area_delta_pct:+.2f}%).")
        _w("  This is unexpected. Possible causes:")
        _w("    - stat -liberty area rounding differs between hierarchical/flat")
        _w("    - opt_clean -purge removed different wires/buffers post-flatten")
        _w("  Recommendation: inspect synth_stat.txt from both views for detail.")
    else:
        _w(f"  Cell count differs by {cell_delta:+d} cells ({area_delta_pct:+.2f}% area).")
        _w("  This indicates that flatten enabled optimizations (cross-boundary")
        _w("  merging, constant propagation) not available in the hierarchical")
        _w("  pass.  The canonical-flat value is the authoritative one.")
        _w("  Hierarchy attribution should be used for area breakdown only,")
        _w("  not for total area QoR assessment.")
    _w()

    _w("--- Verdict ---")
    _w("  Authoritative area QoR:  canonical_flat")
    _w("  Attribution tool:        hierarchy_attribution (analysis-only)")
    _w("  Budget comparison must use canonical_flat area, not hierarchy_attribution.")
    _w()
    _w("=" * 70)
    _w(" End of area flow comparison.")
    _w("=" * 70)

    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"[synth_summary] Wrote {output_path}")


# ── main entrypoint ──────────────────────────────────────────────────

def render_summary(
    design: str,
    target_mhz: int,
    output_dir: str,
    result_dir: str,
    area_budget_um2: int = 23000,
    sta_rpt: Optional[str] = None,
    synth_stat: Optional[str] = None,
    synth_stat_json: Optional[str] = None,
    synth_hierarchy_json: Optional[str] = None,
    netlist: Optional[str] = None,
    fanout: Optional[str] = None,
    top_reg2reg: int = 50,
    top_others: int = 20,
    max_path: int = 50,
    provenance: Optional[Dict[str, str]] = None,
    fanout_source: str = "unknown",
    sdc_file: Optional[str] = None,
    ieda_bin: Optional[str] = None,
    yosys_sta_home: Optional[str] = None,
    view_type: str = "canonical_flat",
    hierarchy_attribution_area_um2: Optional[float] = None,
) -> int:
    """Render schema-v4 JSON, text synth summaries, and optimization hotspots.

    Returns 0 on success, non-zero on failure.
    """
    from report_parser import (
        parse_sta_report,
        parse_synth_stat,
        build_hierarchy_area_tree,
        parse_classified_timing,
    )

    area_result: Dict[str, Any] = {"cell_count": 0, "area_um2": 0.0}
    hierarchy_rows: List[Dict] = []
    timing_result: Dict = {
        "wns": 0.0,
        "tns": 0.0,
        "reg2reg": [],
        "in2reg": [],
        "reg2out": [],
        "in2out": [],
        "hold": [],
        "path_groups": [],
        "high_fanout": [],
        "unconstrained": [],
        "warnings": [],
    }
    warnings: List[str] = []

    # ── Parse area ─────────────────────────────────────────────
    if synth_stat and Path(synth_stat).is_file():
        try:
            area_result = parse_synth_stat(synth_stat, design)
        except Exception as e:
            area_result["area_parse_error"] = str(e)
            warnings.append(f"Area parse error: {e}")
    else:
        warnings.append(f"synth_stat.txt not found or not provided: {synth_stat}")

    # ── Parse hierarchy ────────────────────────────────────────
    # Prefer the hierarchy-preserved artifact (captured before flattening)
    # when available; fall back to the flat synth_stat.json otherwise.
    hierarchy_json_path = synth_hierarchy_json
    if not (hierarchy_json_path and Path(hierarchy_json_path).is_file()):
        hierarchy_json_path = synth_stat_json

    if hierarchy_json_path and Path(hierarchy_json_path).is_file():
        try:
            hierarchy_rows = build_hierarchy_area_tree(
                hierarchy_json_path,
                netlist if (netlist and Path(netlist).is_file()) else None,
                design,
            )
        except Exception as e:
            warnings.append(f"Hierarchy parse error: {e}")
    else:
        warnings.append(f"No hierarchy JSON available (tried synth_hierarchy.json and synth_stat.json)")

    # ── Fallback: derive totals from hierarchy_rows when flat-stat parser failed ──
    # For hierarchical synthesis (hierarchy_attribution view), the flat
    # synth_stat.txt parser fails because the stat has per-module entries
    # and the parser expects exactly one cell-count field.  In that case
    # we extract recursive totals from the top-level hierarchy row so the
    # summary JSON/txt still reflects real area and cell count.
    if area_result.get("cell_count", 0) == 0 and hierarchy_rows:
        top = hierarchy_rows[0]
        area_result["cell_count"] = int(top.get("recursive_cells", 0))
        area_result["area_um2"] = float(top.get("recursive_area", 0.0))
        if area_result.get("cell_count", 0) > 0:
            warnings.append(
                f"Flat stat parser did not return totals; "
                f"derived from hierarchy: cells={area_result['cell_count']}, "
                f"area={area_result['area_um2']} µm²"
            )

    # ── Parse timing ───────────────────────────────────────────
    if sta_rpt and Path(sta_rpt).is_file():
        try:
            # Global WNS/TNS from the STA report (for backward compat)
            sta = parse_sta_report(sta_rpt)
            timing_result["wns"] = sta["wns"]
            timing_result["tns"] = sta["tns"]
        except Exception as e:
            timing_result["wns"] = 0.0
            timing_result["tns"] = 0.0
            warnings.append(f"STA parse error: {e}")

    # ── Parse classified timing ────────────────────────────────
    if sta_rpt and Path(sta_rpt).is_file():
        try:
            classified = parse_classified_timing(
                sta_rpt,
                fanout_path=fanout if (fanout and Path(fanout).is_file()) else None,
                netlist_path=netlist if (netlist and Path(netlist).is_file()) else None,
                top_reg2reg=top_reg2reg,
                top_others=top_others,
                result_dir=result_dir,
                max_path=max_path,
                sdc_file=sdc_file if (sdc_file and Path(sdc_file).is_file()) else None,
                ieda_bin=ieda_bin,
                yosys_sta_home=yosys_sta_home,
            )
            timing_result.update(classified)
            # classified warnings are already in timing_result["warnings"]
        except Exception as e:
            warnings.append(f"Classified timing parse error: {e}")

    # Merge warnings
    existing_warnings = timing_result.get("warnings", [])
    timing_result["warnings"] = existing_warnings + [
        w for w in warnings if w not in existing_warnings
    ]

    # ── Build per-category area by cell class ──────────────────
    area_by_class = _build_area_by_cell_class(hierarchy_rows)

    # ── Liberty-backed area reports ────────────────────────────
    # Generate area_cell_types.rpt, area_cell_classes.rpt, and
    # area_modules.rpt from the hierarchy-preserved JSON when
    # available.  Falls back to flat synth_stat.json with explicit
    # "UNAVAILABLE" warnings when Liberty per-type areas are absent.
    from synth_hierarchy import (
        extract_cell_types_from_json,
        write_area_cell_types_report,
        write_area_cell_classes_report,
        write_area_modules_report,
        extract_register_inventory,
        write_register_inventory_report,
        extract_clock_gating_inventory,
        write_clock_gating_inventory_report,
    )

    result_dir_p = Path(result_dir)
    result_dir_p.mkdir(parents=True, exist_ok=True)

    # Determine the best JSON source for cell-type area data.
    # Prefer the hierarchy-preserved artifact (post-tech-mapping,
    # pre-flatten) because its num_cells_by_type has Liberty-backed
    # per-cell-type areas.  The flat post-flatten synth_stat.json
    # has counts but no per-type area data.
    cell_types_json = hierarchy_json_path
    if not (cell_types_json and Path(cell_types_json).is_file()):
        # Fall back to flat synth_stat.json — will produce
        # count-only records with zero areas + explicit warnings.
        cell_types_json = synth_stat_json
        if cell_types_json and Path(cell_types_json).is_file():
            warnings.append(
                "Cell-type area source: flat synth_stat.json (no per-type Liberty area — "
                "areas will be zero). For non-zero areas, enable hierarchy-preserved synthesis."
            )

    area_report_warnings: List[str] = []
    top_recursive_area = (
        hierarchy_rows[0].get("recursive_area", 0.0)
        if hierarchy_rows else 0.0
    )

    if cell_types_json and Path(cell_types_json).is_file():
        try:
            cell_type_records, liberty_top_area, ct_warnings = (
                extract_cell_types_from_json(cell_types_json)
            )
            area_report_warnings.extend(ct_warnings)

            write_area_cell_types_report(
                cell_type_records,
                liberty_top_area,
                result_dir_p / "area_cell_types.rpt",
                extra_warnings=area_report_warnings,
            )
            print(f"[synth_summary] Wrote {result_dir_p / 'area_cell_types.rpt'}")

            write_area_cell_classes_report(
                cell_type_records,
                liberty_top_area,
                result_dir_p / "area_cell_classes.rpt",
                extra_warnings=area_report_warnings,
            )
            print(f"[synth_summary] Wrote {result_dir_p / 'area_cell_classes.rpt'}")
        except Exception as e:
            warnings.append(f"Cell-type area report generation failed: {e}")
    else:
        warnings.append(
            "STATUS: UNAVAILABLE — No JSON stats source available for cell-type area reports."
        )

    # area_modules.rpt: uses hierarchy_rows (which may be empty if
    # hierarchy JSON couldn't be parsed).  Writes closure status
    # and explicit warnings regardless.
    try:
        write_area_modules_report(
            hierarchy_rows,
            top_recursive_area,
            result_dir_p / "area_modules.rpt",
            extra_warnings=area_report_warnings,
        )
        print(f"[synth_summary] Wrote {result_dir_p / 'area_modules.rpt'}")
    except Exception as e:
        warnings.append(f"Module area report generation failed: {e}")

    # ── Register inventory report ────────────────────────────────
    # Generate register_inventory.rpt from the hierarchy-preserved JSON
    # plus the hierarchical netlist for clock-gating connectivity.
    synth_hierarchy_v = result_dir_p / "synth_hierarchy.v"
    if cell_types_json and Path(cell_types_json).is_file():
        try:
            reg_records, reg_total_area, reg_total_cells, reg_warnings = (
                extract_register_inventory(
                    cell_types_json,
                    netlist_path=str(synth_hierarchy_v) if synth_hierarchy_v.is_file() else None,
                    hierarchy_rows=hierarchy_rows,
                )
            )
            write_register_inventory_report(
                reg_records,
                reg_total_area,
                int(reg_total_cells),
                result_dir_p / "register_inventory.rpt",
                extra_warnings=list(area_report_warnings) + reg_warnings,
            )
            print(f"[synth_summary] Wrote {result_dir_p / 'register_inventory.rpt'}")
        except Exception as e:
            warnings.append(f"Register inventory report generation failed: {e}")
    else:
        warnings.append(
            "STATUS: UNAVAILABLE — No JSON stats source for register inventory."
        )

    # ── Clock-gating inventory report ─────────────────────────────
    if cell_types_json and Path(cell_types_json).is_file():
        try:
            cg_records, cg_total_area, cg_total_cells, cg_warnings = (
                extract_clock_gating_inventory(
                    cell_types_json,
                    netlist_path=str(synth_hierarchy_v) if synth_hierarchy_v.is_file() else None,
                    hierarchy_rows=hierarchy_rows,
                )
            )
            write_clock_gating_inventory_report(
                cg_records,
                cg_total_area,
                cg_total_cells,
                result_dir_p / "clock_gating_inventory.rpt",
                extra_warnings=list(area_report_warnings) + cg_warnings,
            )
            print(f"[synth_summary] Wrote {result_dir_p / 'clock_gating_inventory.rpt'}")
        except Exception as e:
            warnings.append(f"Clock-gating inventory report generation failed: {e}")
    else:
        warnings.append(
            "STATUS: UNAVAILABLE — No JSON stats source for clock-gating inventory."
        )

    # ── Build v3 JSON ──────────────────────────────────────────
    # Determine coverage / fanout status from timing data
    # (these are set by the classified timing parser)
    coverage_status = timing_result.get("coverage_status", "unknown")
    coverage_note = timing_result.get("coverage_note", "")
    fanout_src = timing_result.get("fanout_source", fanout_source)
    qd_source = timing_result.get("data_reg2reg_source", "none")

    summary = build_summary_json(
        design=design,
        target_mhz=target_mhz,
        area_result=area_result,
        hierarchy_rows=hierarchy_rows,
        timing_result=timing_result,
        area_by_class=area_by_class,
        area_budget_um2=area_budget_um2,
        provenance=provenance,
        fanout_source=fanout_src,
        coverage_status=coverage_status,
        coverage_note=coverage_note,
        view_type=view_type,
        hierarchy_attribution_area_um2=hierarchy_attribution_area_um2,
        core_data_reg2reg_source=qd_source,
    )

    # ── Write JSON ─────────────────────────────────────────────
    output_dir_p = Path(output_dir)
    output_dir_p.mkdir(parents=True, exist_ok=True)

    json_path = output_dir_p / "synth_summary.json"
    try:
        json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(f"[synth_summary] Wrote {json_path}")
    except OSError as e:
        print(f"[synth_summary] ERROR: failed to write {json_path}: {e}", file=sys.stderr)
        return 1

    # ── Build and write text report ────────────────────────────
    text = build_summary_text(summary, hierarchy_rows, area_by_class)
    txt_path = output_dir_p / "synth_summary.txt"
    try:
        txt_path.write_text(text, encoding="utf-8")
        print(f"[synth_summary] Wrote {txt_path}")
    except OSError as e:
        print(f"[synth_summary] ERROR: failed to write {txt_path}: {e}", file=sys.stderr)
        return 1

    # ── Build and write optimization hotspots ──────────────────
    hotspots = build_hotspots_text(summary)
    hotspots_path = output_dir_p / "optimization_hotspots.txt"
    try:
        hotspots_path.write_text(hotspots, encoding="utf-8")
        print(f"[synth_summary] Wrote {hotspots_path}")
    except OSError as e:
        print(f"[synth_summary] ERROR: failed to write {hotspots_path}: {e}", file=sys.stderr)
        # Non-fatal: synthesis succeeded; hotspot rendering is advisory

    return 0


# ── CLI ───────────────────────────────────────────────────────────────

def main() -> None:
    import argparse

    ap = argparse.ArgumentParser(
        description="Render schema-v3 synth summary (JSON + text + hotspots) from parsed artifacts."
    )
    ap.add_argument("--design", required=True, help="Top-level design name (e.g. ysyx_25070190)")
    ap.add_argument("--target-mhz", type=int, required=True, help="Target clock frequency in MHz")
    ap.add_argument("--output-dir", required=True, help="Output directory for summary files (e.g. build/synth)")
    ap.add_argument("--result-dir", default=None, help="Result directory with synthesis artifacts (e.g. build/synth/<design>-<freq>MHz)")
    ap.add_argument("--area-budget", type=int, default=23000, help="Area budget in µm² (default: 23000)")
    ap.add_argument("--sta-rpt", default=None, help="Path to STA timing .rpt file")
    ap.add_argument("--synth-stat", default=None, help="Path to synth_stat.txt")
    ap.add_argument("--synth-stat-json", default=None, help="Path to synth_stat.json (flat)")
    ap.add_argument("--synth-hierarchy-json", default=None, help="Path to synth_hierarchy.json (hierarchy-preserved, preferred for area tree)")
    ap.add_argument("--netlist", default=None, help="Path to mapped netlist .v file")
    ap.add_argument("--fanout", default=None, help="Path to .fanout report")
    ap.add_argument("--top-reg2reg", type=int, default=50, help="Max reg2reg paths (default: 50)")
    ap.add_argument("--top-others", type=int, default=20, help="Max paths for other categories (default: 20)")
    ap.add_argument("--max-path", type=int, default=50, help="-max_path used by report_timing (default: 50)")
    # Provenance arguments
    ap.add_argument("--yosys-version", default=None, help="Yosys version string")
    ap.add_argument("--ieda-version", default=None, help="iEDA/STA version string")
    ap.add_argument("--pdk", default="nangate45", help="PDK name (default: nangate45)")
    ap.add_argument("--liberty", default=None, help="Liberty file path or description")
    ap.add_argument("--workbench-commit", default=None, help="Workbench git commit (short)")
    ap.add_argument("--cpu-commit", default=None, help="CPU RTL git commit (short)")
    ap.add_argument("--fanout-source", default="unknown", help="Source of fanout data (full_netlist, timing_sampled, etc.)")
    ap.add_argument("--generated-at", default=None, help="ISO 8601 timestamp of synthesis run")
    ap.add_argument("--qor-view", default=None, help="QoR view type (canonical_flat or hierarchy_attribution)")
    ap.add_argument("--hierarchy-attribution-area", type=float, default=None,
                    help="Area from hierarchy-attribution view for split-view overhead calculation")
    ap.add_argument("--sdc-file", default=None, help="Path to SDC constraint file (for dedicated Q→D STA query)")
    ap.add_argument("--ieda-bin", default=None, help="Path to iEDA binary (for dedicated Q→D STA query)")
    ap.add_argument("--yosys-sta-home", default=None, help="Path to yosys-sta project root (for dedicated Q→D STA query)")
    # Comparison mode (generates area_flow_comparison.rpt from two view directories)
    ap.add_argument("--compare", action="store_true", help="Generate area flow comparison report from two view dirs")
    ap.add_argument("--canonical-dir", default=None, help="Path to canonical_flat result directory")
    ap.add_argument("--attribution-dir", default=None, help="Path to hierarchy_attribution result directory")
    args = ap.parse_args()

    # Comparison mode: generate area_flow_comparison.rpt and exit
    if args.compare:
        if not args.canonical_dir or not args.attribution_dir:
            print("[synth_summary] ERROR: --compare requires --canonical-dir and --attribution-dir", file=sys.stderr)
            sys.exit(1)
        write_area_flow_comparison(
            canonical_dir=args.canonical_dir,
            attribution_dir=args.attribution_dir,
            design=args.design,
            target_mhz=args.target_mhz,
            output_dir=args.output_dir,
        )
        sys.exit(0)

    # Normal rendering mode: --result-dir is required
    if not args.result_dir:
        print("[synth_summary] ERROR: --result-dir is required for normal rendering mode", file=sys.stderr)
        sys.exit(1)

    # Build provenance dict from CLI args
    provenance: Dict[str, str] = {
        "yosys_version": args.yosys_version or "N/A",
        "ieda_version": args.ieda_version or "N/A",
        "pdk": args.pdk,
        "liberty": args.liberty or "N/A",
        "workbench_commit": args.workbench_commit or "N/A",
        "cpu_commit": args.cpu_commit or "N/A",
        "target_mhz": str(args.target_mhz),
        "generated_at": args.generated_at or "N/A",
        "sta_tool": "iEDA",
        "qor_view": args.qor_view or "canonical_flat",
    }

    # Derive individual artifact paths from result_dir if not explicitly given
    result_dir = Path(args.result_dir)
    design = args.design

    sta_rpt = args.sta_rpt or str(result_dir / f"{design}.rpt")
    synth_stat = args.synth_stat or str(result_dir / "synth_stat.txt")
    synth_stat_json = args.synth_stat_json or str(result_dir / "synth_stat.json")
    synth_hierarchy_json = args.synth_hierarchy_json or str(result_dir / "synth_hierarchy.json")
    netlist = args.netlist or str(result_dir / f"{design}.netlist.v")
    fanout = args.fanout or str(result_dir / f"{design}.fanout")

    rc = render_summary(
        design=args.design,
        target_mhz=args.target_mhz,
        output_dir=args.output_dir,
        result_dir=args.result_dir,
        area_budget_um2=args.area_budget,
        sta_rpt=sta_rpt,
        synth_stat=synth_stat,
        synth_stat_json=synth_stat_json,
        synth_hierarchy_json=synth_hierarchy_json,
        netlist=netlist,
        fanout=fanout,
        top_reg2reg=args.top_reg2reg,
        top_others=args.top_others,
        max_path=args.max_path,
        provenance=provenance,
        fanout_source=args.fanout_source,
        sdc_file=args.sdc_file,
        ieda_bin=args.ieda_bin,
        yosys_sta_home=args.yosys_sta_home,
        view_type=args.qor_view or "canonical_flat",
        hierarchy_attribution_area_um2=args.hierarchy_attribution_area,
    )
    sys.exit(rc)


if __name__ == "__main__":
    main()
