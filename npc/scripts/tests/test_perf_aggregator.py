#!/usr/bin/env python3
"""Fixture-based tests for perf_aggregator.py — v1/v2 compatibility and fail-closed."""

import json
import sys
import unittest
from pathlib import Path

# Make npc/scripts/ importable when invoked from repo root.
_sys_path_scripts = str(Path(__file__).resolve().parent.parent)
if _sys_path_scripts not in sys.path:
    sys.path.insert(0, _sys_path_scripts)

from tests import fixture
from perf_aggregator import (
    load_json, require_field, build_perf_block,
    BASELINE_COUNTER_NAMES, _run_strict_checks,
)


def _read_json(name):
    return json.loads(Path(fixture(name)).read_text(encoding="utf-8"))


class TestLoadJson(unittest.TestCase):

    def test_valid_json(self):
        data = load_json(fixture("perf_valid.json"))
        self.assertEqual(data["schema_version"], 1)
        self.assertEqual(len(data["perf_counters"]), 109)

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
            "perf_counters": [{"name": f"c{i}", "value": i} for i in range(62)],
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

    # ── T1: Append-only contract and fail-closed guards ───────────

    def test_duplicate_counter_name_raises(self):
        """Duplicate counter names must be rejected fail-closed."""
        with self.assertRaises(SystemExit):
            build_perf_block(
                _read_json("perf_duplicate_name.json"),
                _read_json("synth_summary_v1.json"),
            )

    def test_name_order_mismatch_raises(self):
        """Baseline counter names must appear in exact frozen order."""
        with self.assertRaises(SystemExit):
            build_perf_block(
                _read_json("perf_name_order_mismatch.json"),
                _read_json("synth_summary_v1.json"),
            )

    def test_expanded_counter_set_accepted(self):
        """Expanded counter sets (>=26 with correct baseline) must be accepted."""
        block = build_perf_block(
            _read_json("perf_expanded_valid.json"),
            _read_json("synth_summary_v1.json"),
        )
        self.assertIn("--- perf counters ---", block)
        self.assertIn("core.cycle:", block)
        self.assertIn("trap.exception.count:", block)
        # Expanded counters must appear after baseline
        self.assertIn("stage.if.input_fire.count:", block)
        self.assertIn("stage.if.output_fire.count:", block)

    def test_baseline_counter_names_preserve_order(self):
        """BASELINE_COUNTER_NAMES must match perf_valid.json positions 0–25."""
        perf_data = _read_json("perf_valid.json")
        counters = perf_data["perf_counters"]
        for idx, expected_name in enumerate(BASELINE_COUNTER_NAMES):
            self.assertEqual(
                counters[idx]["name"], expected_name,
                f"BASELINE_COUNTER_NAMES[{idx}] mismatch: "
                f"expected '{expected_name}', got '{counters[idx]['name']}'"
            )
        self.assertEqual(len(counters), 109)

    def test_baseline_has_26_entries(self):
        """BASELINE_COUNTER_NAMES must define exactly 26 frozen entries."""
        self.assertEqual(len(BASELINE_COUNTER_NAMES), 26)

    def test_baseline_names_are_unique(self):
        """Every name in BASELINE_COUNTER_NAMES must be unique."""
        self.assertEqual(len(BASELINE_COUNTER_NAMES), len(set(BASELINE_COUNTER_NAMES)))

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


class TestStrictChecks(unittest.TestCase):

    def test_strict_closure_broken_raises(self):
        """Strict mode must fail-closed on broken closure."""
        with self.assertRaises(SystemExit):
            build_perf_block(
                _read_json("perf_closure_broken.json"),
                _read_json("synth_summary_v1.json"),
                strict=True,
            )

    def test_non_strict_closure_broken_accepted(self):
        """Without strict mode, broken closure must still produce a block."""
        block = build_perf_block(
            _read_json("perf_closure_broken.json"),
            _read_json("synth_summary_v1.json"),
            strict=False,
        )
        self.assertIn("--- perf counters ---", block)

    def test_strict_valid_passes(self):
        """Strict mode on valid data must pass."""
        block = build_perf_block(
            _read_json("perf_valid.json"),
            _read_json("synth_summary_v1.json"),
            strict=True,
        )
        self.assertIn("--- perf counters ---", block)

    def test_strict_inst_class_closure_fails(self):
        """Direct check: _run_strict_checks must reject broken inst-class sum."""
        perf_data = _read_json("perf_closure_broken.json")
        with self.assertRaises(SystemExit):
            _run_strict_checks(perf_data["perf_counters"])


if __name__ == "__main__":
    unittest.main()
