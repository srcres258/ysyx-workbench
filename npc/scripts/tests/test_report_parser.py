#!/usr/bin/env python3
"""Fixture-based tests for report_parser.py — happy-path and fail-closed."""

import unittest

from tests import fixture
from report_parser import (
    ParseError,
    parse_sta_report,
    parse_synth_stat,
    parse_synth_json,
    parse_netlist_hierarchy,
    derive_max_frequency,
    build_hierarchy_area_tree,
)


class TestParseStaReport(unittest.TestCase):

    def test_legacy_typical(self):
        result = parse_sta_report(fixture("sta_legacy_typical.rpt"))
        self.assertEqual(round(result["wns"], 4), 0.25)
        self.assertEqual(round(result["tns"], 4), 0.0)

    def test_legacy_failing(self):
        result = parse_sta_report(fixture("sta_legacy_failing.rpt"))
        self.assertEqual(round(result["wns"], 4), -0.456)
        self.assertEqual(round(result["tns"], 4), -12.34)

    def test_current_format(self):
        result = parse_sta_report(fixture("sta_current_typical.rpt"))
        self.assertEqual(round(result["wns"], 4), 0.15)
        self.assertEqual(round(result["tns"], 4), 0.0)

    def test_malformed_raises(self):
        with self.assertRaises(ParseError):
            parse_sta_report(fixture("sta_malformed.rpt"))

    def test_file_not_found(self):
        with self.assertRaises(FileNotFoundError):
            parse_sta_report("/nonexistent/path.rpt")


class TestParseSynthStat(unittest.TestCase):

    def test_typical(self):
        result = parse_synth_stat(fixture("synth_stat_typical.txt"))
        self.assertEqual(result["cell_count"], 9876)
        self.assertEqual(round(result["area_um2"], 3), 12345.678)  # type: ignore[arg-type]

    def test_legacy_format(self):
        result = parse_synth_stat(fixture("synth_stat_legacy.txt"))
        self.assertEqual(result["cell_count"], 9876)
        self.assertEqual(round(result["area_um2"], 3), 12345.678)  # type: ignore[arg-type]

    def test_legacy_with_comma(self):
        result = parse_synth_stat(fixture("synth_stat_legacy_format.txt"))
        self.assertEqual(result["cell_count"], 5432)
        self.assertEqual(round(result["area_um2"], 2), 9999.50)  # type: ignore[arg-type]

    def test_current_format_cells(self):
        result = parse_synth_stat(fixture("synth_stat_current_format.txt"))
        self.assertEqual(result["cell_count"], 5432)
        self.assertEqual(round(result["area_um2"], 2), 9999.50)  # type: ignore[arg-type]

    def test_no_area_raises(self):
        with self.assertRaises(ParseError):
            parse_synth_stat(fixture("synth_stat_no_area.txt"))

    def test_missing_cells_raises(self):
        with self.assertRaises(ParseError):
            parse_synth_stat(fixture("synth_stat_missing_cells.txt"))

    def test_wrong_design_raises(self):
        with self.assertRaises(ParseError):
            parse_synth_stat(fixture("synth_stat_wrong_design.txt"))

    def test_file_not_found(self):
        with self.assertRaises(FileNotFoundError):
            parse_synth_stat("/nonexistent/path.txt")

    def test_custom_design_name_mismatch(self):
        with self.assertRaises(ParseError):
            parse_synth_stat(fixture("synth_stat_typical.txt"), design_name="wrong_top")


class TestParseSynthJson(unittest.TestCase):

    def test_typical_with_liberty(self):
        data = parse_synth_json(fixture("synth_stat_typical.json"))
        self.assertIn("creator", data)
        self.assertIn("modules", data)
        self.assertIn("ysyx_25070190", data["modules"])
        mod = data["modules"]["ysyx_25070190"]
        self.assertIn("num_cells", mod)
        self.assertIsInstance(mod["num_cells"], dict)
        self.assertEqual(mod["num_cells"]["count"], 9876)

    def test_minimal_no_liberty(self):
        data = parse_synth_json(fixture("synth_stat_minimal.json"))
        self.assertIn("ysyx_25070190", data["modules"])
        mod = data["modules"]["ysyx_25070190"]
        self.assertIn("num_cells", mod)
        self.assertIsInstance(mod["num_cells"], int)
        self.assertEqual(mod["num_cells"], 3000)

    def test_escaped_module_names_normalized(self):
        data = parse_synth_json(fixture("synth_stat_escaped.json"))
        self.assertNotIn("\\\\ysyx_25070190", data["modules"])
        self.assertIn("ysyx_25070190", data["modules"])

    def test_empty_object_raises(self):
        with self.assertRaises(ParseError):
            parse_synth_json(fixture("synth_stat_empty.json"))

    def test_missing_modules_raises(self):
        with self.assertRaises(ParseError):
            parse_synth_json(fixture("synth_stat_missing_modules.json"))

    def test_missing_num_cells_raises(self):
        with self.assertRaises(ParseError):
            parse_synth_json(fixture("synth_stat_missing_cells.json"))

    def test_file_not_found(self):
        with self.assertRaises(FileNotFoundError):
            parse_synth_json("/nonexistent/path.json")


class TestParseNetlistHierarchy(unittest.TestCase):

    def test_minimal(self):
        hier = parse_netlist_hierarchy(fixture("netlist_minimal.v"))
        self.assertIn("ysyx_25070190", hier)
        cells = hier["ysyx_25070190"]["cells"]
        self.assertIn("DFF_X1", cells)
        self.assertEqual(cells["DFF_X1"], 2)
        self.assertIn("NAND2_X1", cells)
        self.assertIn("INV_X1", cells)

    def test_hierarchy(self):
        hier = parse_netlist_hierarchy(fixture("netlist_hierarchy.v"))
        self.assertIn("top", hier)
        self.assertIn("sub_a", hier)
        self.assertIn("sub_b", hier)
        self.assertIn("sub_a", hier["top"]["submodules"])
        self.assertIn("sub_b", hier["top"]["submodules"])

    def test_empty_file_raises(self):
        import tempfile
        with tempfile.NamedTemporaryFile(suffix=".v", mode="w", delete=False) as f:
            f.write("// empty\n")
            f.flush()
            path = f.name
        try:
            with self.assertRaises(ParseError):
                parse_netlist_hierarchy(path)
        finally:
            import os
            os.unlink(path)

    def test_file_not_found(self):
        with self.assertRaises(FileNotFoundError):
            parse_netlist_hierarchy("/nonexistent/path.v")


class TestDeriveMaxFrequency(unittest.TestCase):

    def test_positive_slack(self):
        fmax = derive_max_frequency(0.5, 100)
        self.assertGreater(fmax, 100)

    def test_negative_slack(self):
        fmax = derive_max_frequency(-1.0, 100)
        self.assertLess(fmax, 100)

    def test_deeply_negative_reduces_frequency(self):
        fmax = derive_max_frequency(-100.0, 100)
        self.assertLess(fmax, 10)
        self.assertGreater(fmax, 0)

    def test_zero_slack(self):
        fmax = derive_max_frequency(0.0, 100)
        self.assertLessEqual(fmax, 100)

    def test_invalid_target_raises(self):
        with self.assertRaises(ValueError):
            derive_max_frequency(0.0, 0)

    def test_invalid_types_raises(self):
        with self.assertRaises(ValueError):
            derive_max_frequency("not_a_number", 100)  # type: ignore[arg-type]


class TestBuildHierarchyAreaTree(unittest.TestCase):

    def test_flat_no_netlist(self):
        rows = build_hierarchy_area_tree(
            fixture("synth_stat_typical.json"),
            netlist_path=None,
            top_module="ysyx_25070190",
        )
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["module_name"], "ysyx_25070190")
        self.assertIn("sequential", rows[0]["categories"])
        self.assertIn("combinational", rows[0]["categories"])

    def test_with_netlist_hierarchy(self):
        rows = build_hierarchy_area_tree(
            fixture("synth_stat_hierarchy.json"),
            netlist_path=fixture("netlist_hierarchy.v"),
            top_module="top",
        )
        module_names = {r["module_name"] for r in rows}
        self.assertIn("top", module_names)
        self.assertIn("sub_a", module_names)
        self.assertIn("sub_b", module_names)
        for r in rows:
            self.assertGreaterEqual(r["depth"], 0)
            self.assertIn(r["module_name"], module_names)

    def test_missing_module_raises(self):
        with self.assertRaises(KeyError):
            build_hierarchy_area_tree(
                fixture("synth_stat_minimal.json"),
                netlist_path=fixture("netlist_hierarchy.v"),
                top_module="top",
            )


if __name__ == "__main__":
    unittest.main()
