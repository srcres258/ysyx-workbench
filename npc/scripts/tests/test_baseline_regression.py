#!/usr/bin/env python3
"""Regression fixtures and end-to-end validation for the canonical baseline workflow.

Tests the baseline contract enforced by:
- ``synth_summary.build_canonical_baseline()`` — canonical_baseline section builder
- ``synth_summary.validate_input_identity()`` — fail-closed identity gate
- ``synth_summary.identity_gate_check()`` — hash-based comparison gate
- ``perf_aggregator.build_perf_block()`` — view-type enforcement

Coverage:
- Happy path: canonical-flat baseline is authoritative, machine-readable
- Fail-closed mismatch: hierarchy_attribution rejected, mismatched hashes block comparison
- Non-comparable path: historical flow marked non-comparable, identity gate fail-closed
- Q/QN→D critical path: canonical summary exposes data_reg2reg with Q→D paths
"""

import json
import unittest
from pathlib import Path

from tests import fixture


# ── helpers ───────────────────────────────────────────────────────────

def _load_json(name: str):
    return json.loads(Path(fixture(name)).read_text(encoding="utf-8"))


# ── canonical baseline contract ────────────────────────────────────────

class TestCanonicalBaselineContract(unittest.TestCase):
    """Verify the canonical_baseline section contract from build_canonical_baseline()."""

    def test_flow_id_canonical_flat_is_correct(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "canonical_flat", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="ab" * 32,
            hierarchy_attribution_area_um2=None,
        )
        self.assertEqual(baseline["flow_id"], "canonical_flat_v4_late_flatten")

    def test_flow_id_hierarchy_attribution_is_correct(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "hierarchy_attribution", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="cd" * 32,
            hierarchy_attribution_area_um2=200.0,
        )
        self.assertEqual(baseline["flow_id"], "hierarchy_attribution_v4_no_flatten")

    def test_all_required_keys_present(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "canonical_flat", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="ab" * 32,
            hierarchy_attribution_area_um2=None,
        )
        required = [
            "flow_id", "flow_description", "decision_reason",
            "canonical_sha256", "canonical_cell_count", "canonical_area_um2",
            "hierarchy_attribution_area_um2", "area_difference_reason",
            "baseline_comparable", "historical_reference_only",
            "historical_flow_commit", "historical_cell_count_approx",
            "historical_area_um2_approx", "flow_comparison",
            "decision_timestamp",
        ]
        for key in required:
            self.assertIn(key, baseline, f"canonical_baseline missing key: {key}")

    def test_flow_comparison_has_canonical_entry(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "canonical_flat", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="ab" * 32,
            hierarchy_attribution_area_um2=None,
        )
        canonical_entries = [e for e in baseline["flow_comparison"] if e["status"] == "canonical"]
        self.assertEqual(len(canonical_entries), 1)
        self.assertTrue(canonical_entries[0]["comparable_to_baseline"])

    def test_flow_comparison_has_analysis_only_entry(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "canonical_flat", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="ab" * 32,
            hierarchy_attribution_area_um2=300.0,
        )
        analysis_entries = [e for e in baseline["flow_comparison"] if e["status"] == "analysis-only"]
        self.assertEqual(len(analysis_entries), 1)
        self.assertEqual(analysis_entries[0]["area_um2"], 300.0)

    def test_historical_flow_marked_non_comparable(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "canonical_flat", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="ab" * 32,
            hierarchy_attribution_area_um2=None,
            historical_cell_count=8881,
            historical_area_um2=19650.75,
        )
        historical_entries = [
            e for e in baseline["flow_comparison"]
            if e["comparable_to_baseline"] is False
        ]
        self.assertEqual(len(historical_entries), 1)
        self.assertIn("non-comparable", historical_entries[0]["status"])
        self.assertIn("note", historical_entries[0])

    def test_baseline_comparable_is_false_for_current_rtl(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "canonical_flat", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="ab" * 32,
            hierarchy_attribution_area_um2=None,
        )
        # baseline_comparable is always False because RTL has changed since historical
        self.assertFalse(baseline["baseline_comparable"])
        self.assertTrue(baseline["historical_reference_only"])

    def test_canonical_sha256_is_not_na(self):
        from synth_summary import build_canonical_baseline
        summary = {"view_type": "canonical_flat", "cell_count": 100, "area_um2": 200.0}
        baseline = build_canonical_baseline(
            summary=summary,
            canonical_netlist_sha256="ab" * 32,
            hierarchy_attribution_area_um2=None,
        )
        self.assertNotEqual(baseline["canonical_sha256"], "N/A")
        self.assertEqual(len(baseline["canonical_sha256"]), 64)


# ── canonical authority enforcement ────────────────────────────────────

class TestCanonicalAuthorityEnforcement(unittest.TestCase):
    """Verify that only canonical_flat is authoritative; hierarchy_attribution fails closed."""

    def setUp(self):
        # Use a minimal valid perf.json structure so build_perf_block passes schema checks
        self._perf_data = {
            "schema_version": 1,
            "cycles": 1000,
            "instret": 500,
            "ipc": 0.5,
            "perf_counters": [{"name": f"c{i}", "value": i} for i in range(26)],
        }

    def test_perf_aggregator_rejects_hierarchy_attribution(self):
        from perf_aggregator import build_perf_block
        synth_data = _load_json("synth_summary_hierarchy_attribution.json")
        with self.assertRaises(SystemExit):
            build_perf_block(self._perf_data, synth_data)

    def test_perf_aggregator_accepts_canonical_flat(self):
        from perf_aggregator import build_perf_block
        synth_data = _load_json("synth_summary_canonical_baseline.json")
        block = build_perf_block(self._perf_data, synth_data)
        self.assertIn("综合频率:", block)
        self.assertIn("综合面积:", block)

    def test_hierarchy_attribution_is_analysis_only(self):
        """Hierarchy-attribution must never be treated as authoritative QoR."""
        data = _load_json("synth_summary_hierarchy_attribution.json")
        self.assertEqual(data["view_type"], "hierarchy_attribution")

        # The canonical-flat area should not be asserted in hierarchy mode
        self.assertIsNone(data["canonical_flat_area_um2"])

        # Coverage should not be claimed as complete
        self.assertFalse(data["constraints"]["coverage_is_complete"])

    def test_canonical_baseline_section_marks_attribution_analysis_only(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        baseline = data["canonical_baseline"]
        analysis_entries = [e for e in baseline["flow_comparison"] if "analysis-only" in str(e.get("status", ""))]
        self.assertGreaterEqual(len(analysis_entries), 1)
        # The analysis-only entry must NOT have status "canonical"
        for entry in analysis_entries:
            self.assertNotEqual(entry["status"], "canonical")


# ── non-comparable path fail-closed ────────────────────────────────────

class TestNonComparablePathFailClosed(unittest.TestCase):
    """Verify that non-comparable flows are blocked and not silently compared."""

    def test_non_comparable_has_baseline_comparable_false(self):
        data = _load_json("synth_summary_non_comparable.json")
        baseline = data["canonical_baseline"]
        self.assertFalse(baseline["baseline_comparable"])

    def test_non_comparable_flow_entries_block_comparison(self):
        data = _load_json("synth_summary_non_comparable.json")
        baseline = data["canonical_baseline"]
        non_comp = [e for e in baseline["flow_comparison"] if e["comparable_to_baseline"] is False]
        self.assertEqual(len(non_comp), 1)
        self.assertIn("identity gate fail-closed", non_comp[0]["status"])

    def test_non_comparable_note_explains_rtl_change(self):
        data = _load_json("synth_summary_non_comparable.json")
        baseline = data["canonical_baseline"]
        non_comp = [e for e in baseline["flow_comparison"] if e["comparable_to_baseline"] is False]
        note = non_comp[0].get("note", "")
        self.assertIn("RTL", note)

    def test_non_comparable_sha256_diff_documented(self):
        """The non-comparable entry must reference the SHA256 mismatch explicitly."""
        data = _load_json("synth_summary_non_comparable.json")
        baseline = data["canonical_baseline"]
        non_comp = [e for e in baseline["flow_comparison"] if "non-comparable" in str(e.get("status", ""))]
        note = non_comp[0].get("note", "")
        self.assertIn("SHA256", note.upper())

    def test_identity_gate_rejects_mismatched_hash(self):
        from synth_summary import identity_gate_check
        identity = {
            "generated_verilog_sha256": "aa" * 32,
            "liberty_sha256": "bb" * 32,
            "sdc_sha256": "cc" * 32,
        }
        result = identity_gate_check(
            identity=identity,
            comparison_label="canonical_flat",
            generated_verilog_sha256="ff" * 32,  # different
            liberty_sha256="bb" * 32,
            sdc_sha256="cc" * 32,
        )
        self.assertFalse(result, "Identity gate should reject mismatched verilog hash")

    def test_identity_gate_allows_matching_hashes(self):
        from synth_summary import identity_gate_check
        identity = {
            "generated_verilog_sha256": "aa" * 32,
            "liberty_sha256": "bb" * 32,
            "sdc_sha256": "cc" * 32,
        }
        result = identity_gate_check(
            identity=identity,
            comparison_label="canonical_flat",
            generated_verilog_sha256="aa" * 32,
            liberty_sha256="bb" * 32,
            sdc_sha256="cc" * 32,
        )
        self.assertTrue(result, "Identity gate should allow matching hashes")

    def test_identity_gate_skips_na_hashes(self):
        from synth_summary import identity_gate_check
        identity = {
            "generated_verilog_sha256": "N/A",
            "liberty_sha256": "N/A",
            "sdc_sha256": "N/A",
        }
        result = identity_gate_check(
            identity=identity,
            comparison_label="test",
            generated_verilog_sha256="aa" * 32,
            liberty_sha256="bb" * 32,
            sdc_sha256="cc" * 32,
        )
        # N/A hashes are skipped gracefully — comparison proceeds
        self.assertTrue(result, "Identity gate should skip N/A hashes gracefully")

    def test_identity_gate_handles_missing_hash_fields(self):
        from synth_summary import identity_gate_check
        identity = {}  # no hash fields
        result = identity_gate_check(
            identity=identity,
            comparison_label="test",
            generated_verilog_sha256="aa" * 32,
            liberty_sha256="bb" * 32,
            sdc_sha256="cc" * 32,
        )
        self.assertTrue(result, "Identity gate should not crash when hash fields missing")


# ── Q/QN→D critical path field ─────────────────────────────────────────

class TestQQNCriticalPathField(unittest.TestCase):
    """Verify the canonical summary exposes the Q/QN→D critical-path field."""

    def test_data_reg2reg_paths_have_q_d_pins(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        data_reg2reg = data["timing"]["data_reg2reg"]
        self.assertGreater(len(data_reg2reg["top_paths"]), 0)
        for path in data_reg2reg["top_paths"]:
            self.assertEqual(path["startpoint_pin"], "Q",
                             f"Expected Q startpoint_pin, got {path['startpoint_pin']}")
            self.assertEqual(path["endpoint_pin"], "D",
                             f"Expected D endpoint_pin, got {path['endpoint_pin']}")
            self.assertEqual(path["category"], "data_reg2reg")

    def test_core_data_reg2reg_source_in_provenance(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        prov = data["provenance"]
        self.assertIn("core_data_reg2reg_source", prov)
        self.assertEqual(prov["core_data_reg2reg_source"], "dedicated-ista-query")

    def test_core_data_reg2reg_fmax_is_calculated(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        self.assertIsNotNone(data["core_data_reg2reg_fmax_mhz"])
        self.assertIsInstance(data["core_data_reg2reg_fmax_mhz"], (int, float))
        self.assertGreater(data["core_data_reg2reg_fmax_mhz"], 0)

    def test_non_comparable_has_q_d_paths_too(self):
        """Even the non-comparable fixture must preserve Q/D path structure."""
        data = _load_json("synth_summary_non_comparable.json")
        data_reg2reg = data["timing"]["data_reg2reg"]
        self.assertGreater(len(data_reg2reg["top_paths"]), 0)
        path = data_reg2reg["top_paths"][0]
        self.assertEqual(path["startpoint_pin"], "Q")
        self.assertEqual(path["endpoint_pin"], "D")

    def test_hierarchy_attribution_has_no_q_d_paths(self):
        """Hierarchy attribution may lack Q/D paths, but must not fabricate them."""
        data = _load_json("synth_summary_hierarchy_attribution.json")
        data_reg2reg = data["timing"]["data_reg2reg"]
        self.assertEqual(data_reg2reg["path_count"], 0)
        self.assertIsNone(data["core_data_reg2reg_fmax_mhz"])
        self.assertEqual(data["core_data_reg2reg_source"], "none")


# ── end-to-end baseline regression ─────────────────────────────────────

class TestEndToEndBaselineRegression(unittest.TestCase):
    """Full round-trip: load fixtures, assert contract invariants hold."""

    def test_canonical_fixture_round_trip_all_sections(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        self.assertEqual(data["schema_version"], 4)
        self.assertEqual(data["view_type"], "canonical_flat")
        self.assertIn("canonical_baseline", data)
        self.assertIn("provenance", data)
        self.assertIn("area", data)
        self.assertIn("timing", data)
        self.assertIn("constraints", data)
        self.assertIn("fanout", data)

    def test_canonical_fixture_has_correct_flow_id(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        self.assertEqual(
            data["canonical_baseline"]["flow_id"],
            "canonical_flat_v4_late_flatten",
        )

    def test_hierarchy_attribution_fixture_has_correct_flow_id(self):
        data = _load_json("synth_summary_hierarchy_attribution.json")
        self.assertEqual(
            data["canonical_baseline"]["flow_id"],
            "hierarchy_attribution_v4_no_flatten",
        )

    def test_non_comparable_fixture_blocked_from_comparison(self):
        data = _load_json("synth_summary_non_comparable.json")
        baseline = data["canonical_baseline"]
        self.assertFalse(baseline["baseline_comparable"])
        self.assertTrue(baseline["historical_reference_only"])

    def test_canonical_flat_is_authoritative(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        canonical_entry = data["canonical_baseline"]["flow_comparison"][0]
        self.assertEqual(canonical_entry["status"], "canonical")
        self.assertTrue(canonical_entry["comparable_to_baseline"])

    def test_split_view_fields_present(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        self.assertIn("canonical_flat_area_um2", data)
        self.assertIn("hierarchy_attribution_area_um2", data)
        self.assertIn("area_attribution_overhead_um2", data)
        self.assertIn("area_attribution_overhead_percent", data)

    def test_provenance_includes_view_type(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        self.assertEqual(data["provenance"]["view_type"], "canonical_flat")
        self.assertEqual(data["provenance"]["schema_version"], "4")

    def test_constraints_has_coverage_flags(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        constraints = data["constraints"]
        for field in ["coverage_is_complete", "coverage_has_evidence", "unconstrained_is_complete"]:
            self.assertIn(field, constraints)
        self.assertFalse(constraints["coverage_is_complete"])

    def test_area_section_mirrors_root_fields(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        area = data["area"]
        self.assertEqual(area["total_cells"], data["cell_count"])
        self.assertEqual(area["total_area_um2"], data["area_um2"])
        self.assertEqual(area["budget_um2"], data["area_budget_um2"])
        self.assertEqual(area["canonical_flat_area_um2"], data["canonical_flat_area_um2"])
        self.assertEqual(area["hierarchy_attribution_area_um2"], data["hierarchy_attribution_area_um2"])


# ── machine-readable contract ─────────────────────────────────────────

class TestMachineReadableContract(unittest.TestCase):
    """Verify the baseline contract is machine-readable — all fields parseable."""

    def test_flow_id_is_well_formed_string(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        flow_id = data["canonical_baseline"]["flow_id"]
        self.assertIsInstance(flow_id, str)
        self.assertGreater(len(flow_id), 5)
        # Must contain the view type and version
        self.assertIn("v4", flow_id)
        self.assertIn("canonical_flat", flow_id)

    def test_decision_timestamp_is_iso8601(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        ts = data["canonical_baseline"]["decision_timestamp"]
        self.assertIn("T", ts)
        self.assertIn(":", ts)

    def test_flow_comparison_is_list_with_entries(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        fc = data["canonical_baseline"]["flow_comparison"]
        self.assertIsInstance(fc, list)
        self.assertGreaterEqual(len(fc), 1)

    def test_each_flow_entry_has_required_keys(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        for entry in data["canonical_baseline"]["flow_comparison"]:
            for key in ["flow", "cells", "area_um2", "status", "comparable_to_baseline"]:
                self.assertIn(key, entry, f"flow_comparison entry missing key: {key}")

    def test_canonical_entry_has_unique_sha256(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        canonical = data["canonical_baseline"]["flow_comparison"][0]
        self.assertTrue(canonical["comparable_to_baseline"])
        self.assertIn("netlist_sha256", canonical)

    def test_cell_count_and_area_are_numbers(self):
        data = _load_json("synth_summary_canonical_baseline.json")
        baseline = data["canonical_baseline"]
        self.assertIsInstance(baseline["canonical_cell_count"], int)
        self.assertIsInstance(baseline["canonical_area_um2"], (int, float))

    def test_historical_fields_are_nullable(self):
        """Historical reference fields may be null if no historical data exists."""
        data = _load_json("synth_summary_hierarchy_attribution.json")
        baseline = data["canonical_baseline"]
        # historical fields can be null
        self.assertIsNone(baseline["historical_cell_count_approx"])
        self.assertIsNone(baseline["historical_area_um2_approx"])

    def test_non_comparable_has_explicit_block_reason(self):
        data = _load_json("synth_summary_non_comparable.json")
        baseline = data["canonical_baseline"]
        decision = baseline["decision_reason"]
        self.assertIn("identity gate", decision.lower())
        self.assertIn("fail", decision.lower())


# ── identity-gate validation ──────────────────────────────────────────

class TestValidateInputIdentity(unittest.TestCase):
    """Verify validate_input_identity() fail-closed behavior."""

    def test_valid_identity_accepted(self):
        from synth_summary import validate_input_identity
        identity = validate_input_identity(str(fixture("input_identity_valid.json")))
        self.assertEqual(identity["top_module"], "ysyx_25070190")
        self.assertIn("generated_verilog_sha256", identity)

    def test_missing_identity_file_raises(self):
        from synth_summary import validate_input_identity
        with self.assertRaises(SystemExit):
            validate_input_identity("/nonexistent/input_identity.json")

    def test_missing_required_field_in_identity_raises(self):
        from synth_summary import validate_input_identity
        with self.assertRaises(SystemExit):
            validate_input_identity(str(fixture("input_identity_missing_hash.json")))

    def test_malformed_json_raises(self):
        import tempfile
        import os
        fd, tmp = tempfile.mkstemp(suffix=".json", prefix="bad_identity_")
        os.close(fd)
        try:
            Path(tmp).write_text("{not valid json", encoding="utf-8")
            from synth_summary import validate_input_identity
            with self.assertRaises(SystemExit):
                validate_input_identity(tmp)
        finally:
            Path(tmp).unlink(missing_ok=True)


# ── backward compatibility ────────────────────────────────────────────

class TestBackwardCompatibility(unittest.TestCase):
    """Ensure new baseline section does not break existing consumers."""

    def test_perf_aggregator_still_works_with_baseline_enriched_json(self):
        """perf_aggregator must consume canonical_baseline-enriched JSON unchanged."""
        from perf_aggregator import build_perf_block
        perf_data = {
            "schema_version": 1,
            "cycles": 1000,
            "instret": 500,
            "ipc": 0.5,
            "perf_counters": [{"name": f"c{i}", "value": i} for i in range(26)],
        }
        synth_data = _load_json("synth_summary_canonical_baseline.json")
        # Should not crash — canonical_baseline section is additive only
        block = build_perf_block(perf_data, synth_data)
        self.assertIn("综合频率:", block)
        self.assertIn("综合面积:", block)

    def test_build_summary_json_ignores_canonical_baseline(self):
        """build_summary_json must not depend on canonical_baseline section."""
        from synth_summary import build_summary_json
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result={"cell_count": 5432, "area_um2": 9999.50},
            hierarchy_rows=[
                {"instance_path": "top", "module_name": "top", "parent_path": "",
                 "depth": 0, "instance_count": 1, "local_cells": 5432,
                 "local_area": 9999.50, "recursive_cells": 5432,
                 "recursive_area": 9999.50, "pct_of_top_area": 100.0,
                 "categories": {}}
            ],
            timing_result={
                "wns": 1.5, "tns": 0.0,
                "reg2reg": [], "data_reg2reg": [], "in2reg": [],
                "reg2out": [], "in2out": [], "clock_enable": [],
                "clock_gating_setup": [], "hold": [], "path_groups": [],
                "high_fanout": [], "unconstrained": [], "warnings": [],
            },
            area_by_class={},
            area_budget_um2=23000,
        )
        self.assertEqual(summary["schema_version"], 4)
        # canonical_baseline section is NOT part of build_summary_json output
        self.assertNotIn("canonical_baseline", summary)

    def test_v4_fixture_round_trip_unaffected_by_baseline_section(self):
        """Existing v4 fixture tests must still pass with baseline-enriched data."""
        data = _load_json("synth_summary_canonical_baseline.json")
        # All v4 fixture assertions from test_synth_summary.py should hold
        self.assertEqual(data["schema_version"], 4)
        self.assertEqual(data["design"], "ysyx_25070190")
        self.assertIn("budget_pass", data)
        self.assertIn("view_type", data)
        self.assertIn("core_data_reg2reg_source", data)
        self.assertIn("canonical_flat_area_um2", data)
        self.assertIn("hierarchy_attribution_area_um2", data)


if __name__ == "__main__":
    unittest.main()
