#!/usr/bin/env python3
# ============================================================================
# synth_summary.py — Schema-v2 synthesis summary renderer.
# ============================================================================
# Consumes:
#   - Parsed area model     (report_parser.parse_synth_stat / build_hierarchy_area_tree)
#   - Parsed timing model   (report_parser.parse_classified_timing)
#   - Budget via CLI        (SYNTH_AREA_BUDGET_UM2 from Makefile)
#
# Produces:
#   - npc/build/synth/synth_summary.json   (schema v2, backward-compatible v1 fields)
#   - npc/build/synth/synth_summary.txt    (human-readable baseline report)
#
# Design principle: fail-closed.  Missing reg2reg stays null/warned, never
# fabricated.  V1 fields preserved exactly — no rename, no removal.
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
) -> Dict[str, Any]:
    """Assemble the schema-v2 synth summary JSON dict."""

    cell_count = int(area_result.get("cell_count", 0))
    area_um2 = float(area_result.get("area_um2", 0.0))

    global_wns = timing_result.get("wns", 0.0)  # already from parse_sta_report or classified
    global_tns = timing_result.get("tns", 0.0)

    # Global derived Fmax from global WNS
    try:
        global_fmax = derive_max_frequency(global_wns, target_mhz)
    except (ValueError, TypeError):
        global_fmax = 0

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

    # ── per-category timing sections ───────────────────────────
    timing_section: Dict[str, Any] = {
        "global": {"wns_ns": round(global_wns, 4), "tns_ns": round(global_tns, 4)},
    }
    for cat in ("reg2reg", "in2reg", "reg2out", "in2out"):
        cat_wns_tns = _per_category_wns_tns(timing_result, cat)
        paths = timing_result.get(cat, [])
        timing_section[cat] = {
            "wns_ns": cat_wns_tns["wns_ns"],
            "tns_ns": cat_wns_tns["tns_ns"],
            "path_count": len(paths),
            "top_paths": paths,
        }

    # hold
    hold_paths = timing_result.get("hold", [])
    hold_slacks = [h["slack"] for h in hold_paths if "slack" in h]
    timing_section["hold"] = {
        "wns_ns": round(min(hold_slacks), 4) if hold_slacks else None,
        "tns_ns": round(sum(s for s in hold_slacks if s < 0), 4) if hold_slacks else None,
        "endpoint_count": len(hold_paths),
        "top_paths": hold_paths,
    }

    # ── assemble ───────────────────────────────────────────────
    summary: Dict[str, Any] = {
        "schema_version": 2,
        # v1 compatibility fields (unchanged names, unchanged semantics)
        "design": design,
        "target_mhz": target_mhz,
        "final_mhz": global_fmax,  # compat: global derived fmax
        "wns_ns": round(global_wns, 4),
        "tns_ns": round(global_tns, 4),
        "cell_count": cell_count,
        "area_um2": round(area_um2, 2),
        # v2 additions — timing
        "global_derived_fmax_mhz": global_fmax,
        "core_reg2reg_fmax_mhz": core_reg2reg_fmax,
        # v2 additions — area
        "area_budget_um2": area_budget_um2,
        "area_by_hierarchy": hierarchy_rows,
        "area_by_cell_class": area_by_class,
        # v2 additions — timing categories
        "timing": timing_section,
        "path_groups": timing_result.get("path_groups", []),
        "high_fanout": timing_result.get("high_fanout", []),
        "unconstrained": timing_result.get("unconstrained", []),
        "warnings": timing_result.get("warnings", []),
    }

    # Propagate parse errors if the area/timing dicts had them
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

    _wr(buf, "=" * 60)
    _wr(buf, f" SYNTHESIS SUMMARY: {design} @ {target} MHz target")
    _wr(buf, f" Schema version: {summary['schema_version']}")
    _wr(buf, "=" * 60)
    _wr(buf)

    # ── Area Budget ────────────────────────────────────────────
    _wr(buf, "--- Area Budget ---")
    _wr(buf, f"  Total area:   {area_um2:>12.2f} µm²")
    _wr(buf, f"  Budget:       {budget:>12d} µm²")
    if budget > 0:
        pct = area_um2 / budget * 100
        status = "PASS" if pct <= 100 else "FAIL (over budget)"
        _wr(buf, f"  Utilisation:  {pct:>11.1f}%")
        _wr(buf, f"  Status:       {status}")
    else:
        _wr(buf, "  Utilisation:  N/A (budget not set)")
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
    _wr(buf, "--- Core reg2reg Timing ---")
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

    # ── Hold Timing ────────────────────────────────────────────
    hold_info = timing.get("hold", {})
    _wr(buf, "--- Hold Timing ---")
    if hold_info.get("wns_ns") is not None:
        _wr(buf, f"  WNS:       {hold_info['wns_ns']} ns")
        _wr(buf, f"  TNS:       {hold_info['tns_ns']} ns")
        _wr(buf, f"  Endpoints: {hold_info['endpoint_count']}")
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
    netlist: Optional[str] = None,
    fanout: Optional[str] = None,
    top_reg2reg: int = 50,
    top_others: int = 20,
) -> int:
    """Render schema-v2 JSON and text synth summaries from parsed artifacts.

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
    if synth_stat_json and Path(synth_stat_json).is_file():
        try:
            hierarchy_rows = build_hierarchy_area_tree(
                synth_stat_json,
                netlist if (netlist and Path(netlist).is_file()) else None,
                design,
            )
        except Exception as e:
            warnings.append(f"Hierarchy parse error: {e}")
    else:
        warnings.append(f"synth_stat.json not found or not provided: {synth_stat_json}")

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

    # ── Build v2 JSON ──────────────────────────────────────────
    summary = build_summary_json(
        design=design,
        target_mhz=target_mhz,
        area_result=area_result,
        hierarchy_rows=hierarchy_rows,
        timing_result=timing_result,
        area_by_class=area_by_class,
        area_budget_um2=area_budget_um2,
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

    return 0


# ── CLI ───────────────────────────────────────────────────────────────

def main() -> None:
    import argparse

    ap = argparse.ArgumentParser(
        description="Render schema-v2 synth summary (JSON + text) from parsed artifacts."
    )
    ap.add_argument("--design", required=True, help="Top-level design name (e.g. ysyx_25070190)")
    ap.add_argument("--target-mhz", type=int, required=True, help="Target clock frequency in MHz")
    ap.add_argument("--output-dir", required=True, help="Output directory for summary files (e.g. build/synth)")
    ap.add_argument("--result-dir", required=True, help="Result directory with synthesis artifacts (e.g. build/synth/<design>-<freq>MHz)")
    ap.add_argument("--area-budget", type=int, default=23000, help="Area budget in µm² (default: 23000)")
    ap.add_argument("--sta-rpt", default=None, help="Path to STA timing .rpt file")
    ap.add_argument("--synth-stat", default=None, help="Path to synth_stat.txt")
    ap.add_argument("--synth-stat-json", default=None, help="Path to synth_stat.json")
    ap.add_argument("--netlist", default=None, help="Path to mapped netlist .v file")
    ap.add_argument("--fanout", default=None, help="Path to .fanout report")
    ap.add_argument("--top-reg2reg", type=int, default=50, help="Max reg2reg paths (default: 50)")
    ap.add_argument("--top-others", type=int, default=20, help="Max paths for other categories (default: 20)")
    args = ap.parse_args()

    # Derive individual artifact paths from result_dir if not explicitly given
    result_dir = Path(args.result_dir)
    design = args.design

    sta_rpt = args.sta_rpt or str(result_dir / f"{design}.rpt")
    synth_stat = args.synth_stat or str(result_dir / "synth_stat.txt")
    synth_stat_json = args.synth_stat_json or str(result_dir / "synth_stat.json")
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
        netlist=netlist,
        fanout=fanout,
        top_reg2reg=args.top_reg2reg,
        top_others=args.top_others,
    )
    sys.exit(rc)


if __name__ == "__main__":
    main()
