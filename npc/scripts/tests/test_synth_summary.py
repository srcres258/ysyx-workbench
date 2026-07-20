#!/usr/bin/env python3
"""Fixture-based tests for synth_summary.py — schema v1/v2 and text rendering."""

import json
import unittest
from pathlib import Path

from tests import fixture
from synth_summary import build_summary_json, build_summary_text


class TestBuildSummaryJson(unittest.TestCase):

    def _mock_area_result(self):
        return {"cell_count": 5432, "area_um2": 9999.50}

    def _mock_hierarchy_rows(self):
        return [
            {
                "instance_path": "top",
                "module_name": "top",
                "parent_path": "",
                "depth": 0,
                "instance_count": 1,
                "local_cells": 5432,
                "local_area": 9999.50,
                "recursive_cells": 5432,
                "recursive_area": 9999.50,
                "pct_of_top_area": 100.0,
                "categories": {
                    "sequential": {"count": 200, "area": 500.0},
                    "combinational": {"count": 4500, "area": 8500.0},
                    "clock-gating": {"count": 12, "area": 30.0},
                    "buffer/inverter": {"count": 600, "area": 700.0},
                    "mux": {"count": 80, "area": 150.0},
                    "arithmetic": {"count": 30, "area": 50.0},
                    "other": {"count": 10, "area": 69.5},
                },
            }
        ]

    def _mock_timing_result(self):
        return {
            "wns": 1.5,
            "tns": 0.0,
            "reg2reg": [
                {
                    "startpoint": "cpu/reg1/CK", "endpoint": "cpu/reg1/D",
                    "startpoint_type": "sequential", "endpoint_type": "sequential",
                    "delay_type": "max", "clock_group": "core_clock",
                    "slack": 1.5, "path_delay": 8.0, "path_required": 9.5,
                    "category": "reg2reg",
                }
            ],
            "in2reg": [],
            "reg2out": [],
            "in2out": [],
            "hold": [],
            "path_groups": [
                {"clock_group": "core_clock", "delay_type": "max",
                 "endpoint_count": 1, "wns": 1.5, "tns": 0.0}
            ],
            "high_fanout": [],
            "unconstrained": [],
            "warnings": [],
        }

    def _mock_area_by_class(self):
        return {
            "sequential": {"cell_count": 200, "area_um2": 500.0},
            "combinational": {"cell_count": 4500, "area_um2": 8500.0},
            "clock-gating": {"cell_count": 12, "area_um2": 30.0},
            "buffer/inverter": {"cell_count": 600, "area_um2": 700.0},
            "mux": {"cell_count": 80, "area_um2": 150.0},
            "arithmetic": {"cell_count": 30, "area_um2": 50.0},
            "other": {"cell_count": 10, "area_um2": 69.5},
        }

    def test_schema_version_is_2(self):
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=self._mock_area_result(),
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=self._mock_timing_result(),
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=23000,
        )
        self.assertEqual(summary["schema_version"], 2)

    def test_v1_fields_preserved(self):
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=self._mock_area_result(),
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=self._mock_timing_result(),
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=23000,
        )
        self.assertEqual(summary["design"], "ysyx_25070190")
        self.assertEqual(summary["target_mhz"], 100)
        self.assertIn("final_mhz", summary)
        self.assertIn("wns_ns", summary)
        self.assertIn("tns_ns", summary)
        self.assertIn("cell_count", summary)
        self.assertIn("area_um2", summary)

    def test_v2_fields_present(self):
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=self._mock_area_result(),
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=self._mock_timing_result(),
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=23000,
        )
        self.assertIn("global_derived_fmax_mhz", summary)
        self.assertIn("core_reg2reg_fmax_mhz", summary)
        self.assertIn("area_budget_um2", summary)
        self.assertIn("area_by_hierarchy", summary)
        self.assertIn("area_by_cell_class", summary)
        self.assertIn("timing", summary)
        self.assertIn("path_groups", summary)
        self.assertIn("high_fanout", summary)
        self.assertIn("unconstrained", summary)
        self.assertIn("warnings", summary)

    def test_core_reg2reg_fmax_when_paths_present(self):
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=self._mock_area_result(),
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=self._mock_timing_result(),
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=23000,
        )
        self.assertIsNotNone(summary["core_reg2reg_fmax_mhz"])
        self.assertIsInstance(summary["core_reg2reg_fmax_mhz"], int)

    def test_core_reg2reg_fmax_null_when_no_paths(self):
        timing = self._mock_timing_result()
        timing["reg2reg"] = []
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=self._mock_area_result(),
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=timing,
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=23000,
        )
        self.assertIsNone(summary["core_reg2reg_fmax_mhz"])

    def test_timing_sections_present(self):
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=self._mock_area_result(),
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=self._mock_timing_result(),
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=23000,
        )
        timing = summary["timing"]
        self.assertIn("global", timing)
        for cat in ("reg2reg", "in2reg", "reg2out", "in2out", "hold"):
            self.assertIn(cat, timing)
            self.assertIn("wns_ns", timing[cat])
            self.assertIn("tns_ns", timing[cat])

    def test_error_fields_propagated(self):
        area_result = self._mock_area_result()
        area_result["area_parse_error"] = "test error"
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=area_result,
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=self._mock_timing_result(),
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=23000,
        )
        self.assertEqual(summary["area_parse_error"], "test error")

    def test_zero_budget_no_status_failure(self):
        summary = build_summary_json(
            design="ysyx_25070190",
            target_mhz=100,
            area_result=self._mock_area_result(),
            hierarchy_rows=self._mock_hierarchy_rows(),
            timing_result=self._mock_timing_result(),
            area_by_class=self._mock_area_by_class(),
            area_budget_um2=0,
        )
        self.assertEqual(summary["area_budget_um2"], 0)


class TestBuildSummaryText(unittest.TestCase):

    def _sample_v2_summary(self):
        return {
            "schema_version": 2,
            "design": "ysyx_25070190",
            "target_mhz": 100,
            "final_mhz": 125,
            "wns_ns": 1.5,
            "tns_ns": 0.0,
            "cell_count": 5432,
            "area_um2": 9999.50,
            "global_derived_fmax_mhz": 125,
            "core_reg2reg_fmax_mhz": 130,
            "area_budget_um2": 23000,
            "area_by_hierarchy": [
                {
                    "instance_path": "top",
                    "module_name": "top",
                    "parent_path": "",
                    "depth": 0,
                    "instance_count": 1,
                    "local_cells": 5432,
                    "local_area": 9999.50,
                    "recursive_cells": 5432,
                    "recursive_area": 9999.50,
                    "pct_of_top_area": 100.0,
                    "categories": {
                        "sequential": {"count": 200, "area": 500.0},
                        "combinational": {"count": 4000, "area": 8000.0},
                        "clock-gating": {"count": 12, "area": 30.0},
                        "buffer/inverter": {"count": 1000, "area": 1000.0},
                        "mux": {"count": 100, "area": 200.0},
                        "arithmetic": {"count": 50, "area": 100.0},
                        "other": {"count": 70, "area": 169.5},
                    },
                }
            ],
            "area_by_cell_class": {
                "sequential": {"cell_count": 200, "area_um2": 500.0},
                "combinational": {"cell_count": 4000, "area_um2": 8000.0},
                "clock-gating": {"cell_count": 12, "area_um2": 30.0},
                "buffer/inverter": {"cell_count": 1000, "area_um2": 1000.0},
                "mux": {"cell_count": 100, "area_um2": 200.0},
                "arithmetic": {"cell_count": 50, "area_um2": 100.0},
                "other": {"cell_count": 70, "area_um2": 169.5},
            },
            "timing": {
                "global": {"wns_ns": 1.5, "tns_ns": 0.0},
                "reg2reg": {"wns_ns": 1.5, "tns_ns": 0.0, "path_count": 1, "top_paths": []},
                "in2reg": {"wns_ns": None, "tns_ns": None, "path_count": 0, "top_paths": []},
                "reg2out": {"wns_ns": None, "tns_ns": None, "path_count": 0, "top_paths": []},
                "in2out": {"wns_ns": None, "tns_ns": None, "path_count": 0, "top_paths": []},
                "hold": {"wns_ns": 0.1, "tns_ns": 0.0, "endpoint_count": 2, "top_paths": []},
            },
            "path_groups": [],
            "high_fanout": [],
            "unconstrained": [],
            "warnings": [],
        }

    def test_renders_header(self):
        text = build_summary_text(
            self._sample_v2_summary(),
            self._sample_v2_summary()["area_by_hierarchy"],
            self._sample_v2_summary()["area_by_cell_class"],
        )
        self.assertIn("SYNTHESIS SUMMARY", text)
        self.assertIn("Schema version: 2", text)
        self.assertIn("ysyx_25070190", text)

    def test_renders_area_budget_section(self):
        text = build_summary_text(
            self._sample_v2_summary(),
            self._sample_v2_summary()["area_by_hierarchy"],
            self._sample_v2_summary()["area_by_cell_class"],
        )
        self.assertIn("Area Budget", text)
        self.assertIn("9999.50", text)
        self.assertIn("23000", text)

    def test_renders_core_reg2reg_section(self):
        text = build_summary_text(
            self._sample_v2_summary(),
            self._sample_v2_summary()["area_by_hierarchy"],
            self._sample_v2_summary()["area_by_cell_class"],
        )
        self.assertIn("Core reg2reg Timing", text)
        self.assertIn("130 MHz", text)

    def test_renders_missing_reg2reg_as_na(self):
        summary = self._sample_v2_summary()
        summary["core_reg2reg_fmax_mhz"] = None
        summary["timing"]["reg2reg"] = {"wns_ns": None, "tns_ns": None, "path_count": 0, "top_paths": []}
        text = build_summary_text(
            summary,
            summary["area_by_hierarchy"],
            summary["area_by_cell_class"],
        )
        self.assertIn("N/A", text)

    def test_renders_end_marker(self):
        text = build_summary_text(
            self._sample_v2_summary(),
            self._sample_v2_summary()["area_by_hierarchy"],
            self._sample_v2_summary()["area_by_cell_class"],
        )
        self.assertIn("End of synthesis summary", text)

    def test_renders_category_table(self):
        text = build_summary_text(
            self._sample_v2_summary(),
            self._sample_v2_summary()["area_by_hierarchy"],
            self._sample_v2_summary()["area_by_cell_class"],
        )
        self.assertIn("sequential", text)
        self.assertIn("combinational", text)


class TestSynthSummaryJsonRoundTrip(unittest.TestCase):

    def test_v1_fixture_has_required_fields(self):
        data = json.loads(Path(fixture("synth_summary_v1.json")).read_text())
        for field in ["design", "target_mhz", "final_mhz", "wns_ns", "tns_ns",
                       "cell_count", "area_um2"]:
            self.assertIn(field, data, f"v1 fixture missing field: {field}")

    def test_v1_fixture_has_no_schema_version(self):
        data = json.loads(Path(fixture("synth_summary_v1.json")).read_text())
        self.assertNotIn("schema_version", data)

    def test_v2_fixture_has_schema_version(self):
        data = json.loads(Path(fixture("synth_summary_v2.json")).read_text())
        self.assertEqual(data["schema_version"], 2)

    def test_v2_fixture_has_v2_fields(self):
        data = json.loads(Path(fixture("synth_summary_v2.json")).read_text())
        for field in ["global_derived_fmax_mhz", "core_reg2reg_fmax_mhz",
                       "area_budget_um2", "area_by_hierarchy", "timing"]:
            self.assertIn(field, data, f"v2 fixture missing field: {field}")

    def test_v2_null_reg2reg_fixture(self):
        data = json.loads(Path(fixture("synth_summary_v2_null_reg2reg.json")).read_text())
        self.assertIsNone(data["core_reg2reg_fmax_mhz"])
        self.assertIsNone(data["timing"]["reg2reg"]["wns_ns"])


if __name__ == "__main__":
    unittest.main()
