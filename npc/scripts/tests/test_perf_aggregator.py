#!/usr/bin/env python3
"""Fixture-based tests for perf_aggregator.py — v1/v2 compatibility and fail-closed."""

import json
import unittest
from pathlib import Path

from tests import fixture
from perf_aggregator import load_json, require_field, build_perf_block


def _read_json(name):
    return json.loads(Path(fixture(name)).read_text(encoding="utf-8"))


class TestLoadJson(unittest.TestCase):

    def test_valid_json(self):
        data = load_json(fixture("perf_valid.json"))
        self.assertEqual(data["schema_version"], 1)
        self.assertEqual(len(data["perf_counters"]), 26)

    def test_invalid_json_raises(self):
        with self.assertRaises(SystemExit):
            load_json(fixture("perf_invalid_json.json"))

    def test_nonexistent_file_raises(self):
        with self.assertRaises(SystemExit):
            load_json(Path("/nonexistent/perf.json"))


class TestRequireField(unittest.TestCase):

    def test_present_field(self):
        data = {"key": "value"}
        result = require_field(data, "key", "test.json")
        self.assertEqual(result, "value")

    def test_missing_field_raises(self):
        data = {"key": "value"}
        with self.assertRaises(SystemExit):
            require_field(data, "missing", "test.json")

    def test_null_field_raises(self):
        data = {"key": None}
        with self.assertRaises(SystemExit):
            require_field(data, "key", "test.json")

    def test_zero_is_not_null(self):
        data = {"key": 0}
        result = require_field(data, "key", "test.json")
        self.assertEqual(result, 0)


class TestBuildPerfBlock(unittest.TestCase):

    def test_v1_synth_summary_no_core_reg2reg_line(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v1.json"),
        )
        self.assertIn("综合频率:", block)
        self.assertNotIn("核心 reg2reg 频率:", block)

    def test_v2_synth_summary_with_core_reg2reg_line(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v2.json"),
        )
        self.assertIn("核心 reg2reg 频率:", block)
        self.assertIn("125MHz", block)

    def test_v2_synth_summary_null_reg2reg_shows_na(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v2_null_reg2reg.json"),
        )
        self.assertIn("核心 reg2reg 频率: N/A", block)

    def test_v3_synth_summary_preserves_final_mhz(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v3.json"),
        )
        self.assertIn("综合频率:", block)
        self.assertIn("综合面积:", block)
        self.assertIn("核心 reg2reg 频率:", block)

    def test_v3_synth_summary_null_reg2reg_shows_na(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v3_null.json"),
        )
        self.assertIn("综合频率:", block)
        self.assertIn("核心 reg2reg 频率: N/A", block)

    def test_missing_required_field_raises(self):
        perf_data = {
            "schema_version": 1,
            "cycles": None,
            "instret": 50,
            "ipc": 0.5,
            "perf_counters": [{"name": f"c{i}", "value": i} for i in range(26)],
        }
        with self.assertRaises(SystemExit):
            build_perf_block(perf_data, _read_json("synth_summary_v1.json"))

    def test_structure_contains_git_metadata(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v1.json"),
        )
        self.assertIn("commit:", block)
        self.assertIn("说明:", block)

    def test_structure_contains_perf_counters(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v1.json"),
        )
        self.assertIn("--- perf counters ---", block)
        self.assertIn("core.cycle:", block)
        self.assertIn("trap.exception.count:", block)

    def test_wrong_counter_count_raises(self):
        with self.assertRaises(SystemExit):
            build_perf_block(
                _read_json("perf_wrong_counter_count.json"),
                _read_json("synth_summary_v1.json"),
            )

    def test_missing_instret_raises(self):
        with self.assertRaises(SystemExit):
            build_perf_block(
                _read_json("perf_missing_instret.json"),
                _read_json("synth_summary_v1.json"),
            )

    # ── v4 view-type guard tests ────────────────────────────

    def test_v4_synth_summary_canonical_flat_accepted(self):
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v4.json"),
        )
        self.assertIn("综合频率:", block)
        self.assertIn("综合面积:", block)

    def test_view_type_hierarchy_attribution_fails_closed(self):
        v4_data = _read_json("synth_summary_v4.json")
        v4_data["view_type"] = "hierarchy_attribution"
        with self.assertRaises(SystemExit):
            build_perf_block(_read_json("perf_valid.json"), v4_data)

    def test_missing_view_type_pre_v4_warns_but_proceeds(self):
        import io
        import sys
        v1_data = _read_json("synth_summary_v1.json")
        self.assertNotIn("view_type", v1_data)
        saved_stderr = sys.stderr
        try:
            sys.stderr = io.StringIO()
            block = build_perf_block(
                _read_json("perf_valid.json"),
                v1_data,
            )
            self.assertIn("综合频率:", block)
            stderr_output = sys.stderr.getvalue()
            self.assertIn("no 'view_type' field", stderr_output)
        finally:
            sys.stderr = saved_stderr


if __name__ == "__main__":
    unittest.main()
