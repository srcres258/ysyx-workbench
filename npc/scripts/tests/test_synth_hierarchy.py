#!/usr/bin/env python3
"""Unit tests for synth_hierarchy.py — register & clock-gating inventory."""
import json
import tempfile
import unittest
from pathlib import Path
from tests import fixture
from synth_hierarchy import (
    classify_cell,
    classify_cell_sub_class,
    is_large_drive_cell,
    CATEGORY_SEQUENTIAL,
    CATEGORY_CLOCK_GATING,
    SUB_CLASS_DFF,
    SUB_CLASS_MUX,
    SUB_CLASS_AOI_OAI,
    SUB_CLASS_NAND_NOR,
    SUB_CLASS_BUF_INV,
    SUB_CLASS_CLOCK_GATING,
    SUB_CLASS_OTHER,
    trace_clock_gating_connectivity,
    extract_register_inventory,
    extract_clock_gating_inventory,
    write_register_inventory_report,
    write_clock_gating_inventory_report,
    RegisterModuleRecord,
    ClockGateModuleRecord,
)


def _write_hierarchy_json(dir_path: Path, modules: dict) -> Path:
    p = dir_path / "synth_hierarchy.json"
    p.write_text(json.dumps({"creator": "test", "modules": modules}))
    return p


class TestCellClassification(unittest.TestCase):

    def test_dff_classified_as_sequential(self):
        self.assertEqual(classify_cell("DFF_X1"), CATEGORY_SEQUENTIAL)

    def test_sdff_classified_as_sequential(self):
        self.assertEqual(classify_cell("SDFF_X2"), CATEGORY_SEQUENTIAL)

    def test_clkgate_classified_as_clock_gating(self):
        self.assertEqual(classify_cell("CLKGATE_X1"), CATEGORY_CLOCK_GATING)

    def test_clkgatetst_classified_as_clock_gating(self):
        self.assertEqual(classify_cell("CLKGATETST_X1"), CATEGORY_CLOCK_GATING)

    def test_icg_classified_as_clock_gating(self):
        self.assertEqual(classify_cell("ICG_X1"), CATEGORY_CLOCK_GATING)


class TestTraceClockGatingConnectivity(unittest.TestCase):

    def test_direct_connection_in_module(self):
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        result = trace_clock_gating_connectivity(netlist)
        self.assertIn("sub_b", result)
        self.assertIn("clock_gates", result["sub_b"])
        self.assertEqual(len(result["sub_b"]["clock_gates"]), 1)
        self.assertEqual(len(result["sub_b"]["registers"]), 1)
        self.assertEqual(result["sub_b"]["registers"][0]["gated_by"], "g1")

    def test_top_module_has_clock_gate_and_direct_register(self):
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        result = trace_clock_gating_connectivity(netlist)
        self.assertIn("top", result)
        regs = result["top"]["registers"]
        gated = [r for r in regs if r.get("gated_by")]
        self.assertEqual(len(gated), 1)
        self.assertEqual(gated[0]["gated_by"], "cg1")

    def test_sub_a_has_no_clock_gate(self):
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        result = trace_clock_gating_connectivity(netlist)
        self.assertIn("sub_a", result)
        self.assertEqual(len(result["sub_a"]["clock_gates"]), 0)
        self.assertEqual(len(result["sub_a"]["registers"]), 2)
        for r in result["sub_a"]["registers"]:
            self.assertIsNone(r.get("gated_by"))

    def test_missing_file_raises(self):
        with self.assertRaises(FileNotFoundError):
            trace_clock_gating_connectivity("/nonexistent/path.v")


class TestExtractRegisterInventory(unittest.TestCase):

    def setUp(self):
        self._tmpdir = tempfile.mkdtemp(prefix="test_reg_inv_")
        self._tmp = Path(self._tmpdir)

    def tearDown(self):
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def _hierarchy_json(self) -> Path:
        modules = {
            "top": {
                "num_cells": {"count": 10, "area": 500.0, "local_count": 3, "local_area": 150.0},
                "num_cells_by_type": {
                    "DFF_X1": {"count": 3, "area": 60.0, "local_count": 2, "local_area": 40.0},
                    "NAND2_X1": {"count": 5, "area": 50.0, "local_count": 1, "local_area": 10.0},
                    "SDFF_X1": {"count": 2, "area": 40.0, "local_count": 1, "local_area": 20.0},
                },
                "area": 500.0,
            },
        }
        return _write_hierarchy_json(self._tmp, modules)

    def test_extracts_sequential_cells_only(self):
        json_path = self._hierarchy_json()
        records, total_area, total_cells, warnings = extract_register_inventory(json_path)
        self.assertEqual(total_cells, 3)  # 2 DFF_X1 + 1 SDFF_X1
        self.assertGreater(total_area, 0)

    def test_ignores_combinational_cells(self):
        json_path = self._hierarchy_json()
        records, _, total_cells, _ = extract_register_inventory(json_path)
        cell_types = []
        for rec in records:
            for rt in rec.reg_types:
                cell_types.append(rt["cell_type"])
        self.assertIn("DFF_X1", cell_types)
        self.assertIn("SDFF_X1", cell_types)
        self.assertNotIn("NAND2_X1", cell_types)

    def test_module_record_populated(self):
        json_path = self._hierarchy_json()
        records, _, _, _ = extract_register_inventory(json_path)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].module_name, "top")
        self.assertEqual(records[0].total_reg_cells, 3)

    def test_with_netlist_connectivity(self):
        json_path = self._hierarchy_json()
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        # These don't share module names, so gating won't match.
        # But it should not error.
        records, _, _, warnings = extract_register_inventory(
            json_path, netlist_path=str(netlist)
        )
        self.assertGreaterEqual(len(records), 1)

    def test_empty_modules(self):
        modules = {
            "top": {
                "num_cells": {"count": 0, "area": 0.0, "local_count": 0, "local_area": 0.0},
                "num_cells_by_type": {},
                "area": 0.0,
            }
        }
        json_path = _write_hierarchy_json(self._tmp, modules)
        records, total_area, total_cells, warnings = extract_register_inventory(json_path)
        self.assertEqual(total_cells, 0)
        self.assertEqual(len(records), 0)

    def test_zero_count_sequential_skipped(self):
        modules = {
            "top": {
                "num_cells": {"count": 0, "area": 0.0, "local_count": 0, "local_area": 0.0},
                "num_cells_by_type": {
                    "DFF_X1": {"count": 0, "area": 0.0, "local_count": 0, "local_area": 0.0},
                },
                "area": 0.0,
            }
        }
        json_path = _write_hierarchy_json(self._tmp, modules)
        records, _, _, _ = extract_register_inventory(json_path)
        self.assertEqual(len(records), 0)


class TestExtractClockGatingInventory(unittest.TestCase):

    def setUp(self):
        self._tmpdir = tempfile.mkdtemp(prefix="test_cg_inv_")
        self._tmp = Path(self._tmpdir)

    def tearDown(self):
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def _hierarchy_json(self) -> Path:
        modules = {
            "top": {
                "num_cells": {"count": 3, "area": 15.0, "local_count": 3, "local_area": 15.0},
                "num_cells_by_type": {
                    "CLKGATE_X1": {"count": 2, "area": 10.0, "local_count": 2, "local_area": 10.0},
                    "NAND2_X1": {"count": 1, "area": 5.0, "local_count": 1, "local_area": 5.0},
                },
                "area": 15.0,
            },
        }
        return _write_hierarchy_json(self._tmp, modules)

    def test_extracts_clock_gating_cells_only(self):
        json_path = self._hierarchy_json()
        records, total_area, total_cells, warnings = extract_clock_gating_inventory(json_path)
        self.assertEqual(total_cells, 2)
        self.assertGreater(total_area, 0)

    def test_ignores_combinational(self):
        json_path = self._hierarchy_json()
        records, _, _, _ = extract_clock_gating_inventory(json_path)
        cg_types = []
        for rec in records:
            for ct in rec.cg_types:
                cg_types.append(ct["cell_type"])
        self.assertIn("CLKGATE_X1", cg_types)
        self.assertNotIn("NAND2_X1", cg_types)

    def test_matches_top_netlist_connectivity(self):
        json_path = self._hierarchy_json()
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        records, _, _, _ = extract_clock_gating_inventory(
            json_path, netlist_path=str(netlist)
        )
        # The fixture netlist has CLKGATE in top and sub_b,
        # but our JSON only has "top". Should still extract.
        matching = [r for r in records if r.module_name == "top"]
        if matching:
            self.assertGreaterEqual(matching[0].total_cg_cells, 2)

    def test_empty_no_clock_gates(self):
        modules = {
            "top": {
                "num_cells": {"count": 1, "area": 5.0, "local_count": 1, "local_area": 5.0},
                "num_cells_by_type": {
                    "NAND2_X1": {"count": 1, "area": 5.0, "local_count": 1, "local_area": 5.0},
                },
                "area": 5.0,
            }
        }
        json_path = _write_hierarchy_json(self._tmp, modules)
        records, _, total_cells, warnings = extract_clock_gating_inventory(json_path)
        self.assertEqual(total_cells, 0)
        self.assertEqual(len(records), 0)


class TestWriteRegisterInventoryReport(unittest.TestCase):

    def setUp(self):
        self._tmpdir = tempfile.mkdtemp(prefix="test_reg_rpt_")
        self._tmp = Path(self._tmpdir)

    def tearDown(self):
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def test_writes_nonempty_report(self):
        records = [
            RegisterModuleRecord(
                module_name="top",
                instance_path="top",
                depth=0,
                instance_count=1,
                total_reg_cells=200,
                total_reg_area=400.0,
                reg_types=[
                    {"cell_type": "DFF_X1", "local_count": 150, "weighted_count": 150,
                     "local_area": 300.0, "weighted_area": 300.0, "bits_per_cell": 1},
                    {"cell_type": "SDFF_X1", "local_count": 50, "weighted_count": 50,
                     "local_area": 100.0, "weighted_area": 100.0, "bits_per_cell": 1},
                ],
                gated=True,
            ),
        ]
        out = self._tmp / "register_inventory.rpt"
        write_register_inventory_report(records, 400.0, 200, out)
        self.assertTrue(out.exists())
        text = out.read_text()
        self.assertIn("REGISTER INVENTORY", text)
        self.assertIn("DFF_X1", text)
        self.assertIn("GATED", text)

    def test_empty_report_still_writes(self):
        out = self._tmp / "register_inventory.rpt"
        write_register_inventory_report([], 0.0, 0, out)
        self.assertTrue(out.exists())
        text = out.read_text()
        self.assertIn("REGISTER INVENTORY", text)
        self.assertIn("UNAVAILABLE", text)


class TestWriteClockGatingInventoryReport(unittest.TestCase):

    def setUp(self):
        self._tmpdir = tempfile.mkdtemp(prefix="test_cg_rpt_")
        self._tmp = Path(self._tmpdir)

    def tearDown(self):
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def test_writes_nonempty_report(self):
        records = [
            ClockGateModuleRecord(
                module_name="top",
                instance_path="top",
                depth=0,
                instance_count=1,
                total_cg_cells=4,
                total_cg_area=8.0,
                cg_types=[
                    {"cell_type": "CLKGATE_X1", "local_count": 4, "weighted_count": 4,
                     "local_area": 8.0, "weighted_area": 8.0},
                ],
                est_downstream_regs=150,
            ),
        ]
        out = self._tmp / "clock_gating_inventory.rpt"
        write_clock_gating_inventory_report(records, 8.0, 4, out)
        self.assertTrue(out.exists())
        text = out.read_text()
        self.assertIn("CLOCK-GATING INVENTORY", text)
        self.assertIn("CLKGATE_X1", text)

    def test_empty_report_shows_none_status(self):
        out = self._tmp / "clock_gating_inventory.rpt"
        write_clock_gating_inventory_report([], 0.0, 0, out)
        self.assertTrue(out.exists())
        text = out.read_text()
        self.assertIn("CLOCK-GATING INVENTORY", text)
        self.assertIn("NONE", text)


class TestConnectivityWithFixtureNetlist(unittest.TestCase):

    def test_top_module_gating_structure(self):
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        result = trace_clock_gating_connectivity(netlist)
        top = result["top"]
        self.assertEqual(len(top["clock_gates"]), 1)
        self.assertEqual(len(top["registers"]), 1)

    def test_sub_b_gating_structure(self):
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        result = trace_clock_gating_connectivity(netlist)
        sub_b = result["sub_b"]
        self.assertEqual(len(sub_b["clock_gates"]), 1)
        self.assertEqual(len(sub_b["registers"]), 1)
        self.assertEqual(sub_b["registers"][0]["gated_by"], "g1")

    def test_sub_a_no_gating(self):
        netlist = Path(fixture("synth_hierarchy_netlist.v"))
        result = trace_clock_gating_connectivity(netlist)
        sub_a = result["sub_a"]
        self.assertEqual(len(sub_a["clock_gates"]), 0)
        self.assertEqual(len(sub_a["registers"]), 2)


class TestClassifySubClass(unittest.TestCase):

    def test_dff_sub_class(self):
        self.assertEqual(classify_cell_sub_class("DFF_X1"), SUB_CLASS_DFF)
        self.assertEqual(classify_cell_sub_class("SDFF_X2"), SUB_CLASS_DFF)
        self.assertEqual(classify_cell_sub_class("DLH_X1"), SUB_CLASS_DFF)
        self.assertEqual(classify_cell_sub_class("DLL_X1"), SUB_CLASS_DFF)
        self.assertEqual(classify_cell_sub_class("LATCH_X1"), SUB_CLASS_DFF)

    def test_mux_sub_class(self):
        self.assertEqual(classify_cell_sub_class("MUX2_X1"), SUB_CLASS_MUX)
        self.assertEqual(classify_cell_sub_class("MUX4_X2"), SUB_CLASS_MUX)

    def test_aoi_oai_sub_class(self):
        self.assertEqual(classify_cell_sub_class("AOI21_X1"), SUB_CLASS_AOI_OAI)
        self.assertEqual(classify_cell_sub_class("OAI21_X1"), SUB_CLASS_AOI_OAI)
        self.assertEqual(classify_cell_sub_class("AOI221_X2"), SUB_CLASS_AOI_OAI)

    def test_nand_nor_sub_class(self):
        self.assertEqual(classify_cell_sub_class("NAND2_X1"), SUB_CLASS_NAND_NOR)
        self.assertEqual(classify_cell_sub_class("NOR2_X1"), SUB_CLASS_NAND_NOR)
        self.assertEqual(classify_cell_sub_class("NOR4_X4"), SUB_CLASS_NAND_NOR)

    def test_buffer_inverter_sub_class(self):
        self.assertEqual(classify_cell_sub_class("BUF_X1"), SUB_CLASS_BUF_INV)
        self.assertEqual(classify_cell_sub_class("INV_X1"), SUB_CLASS_BUF_INV)
        self.assertEqual(classify_cell_sub_class("CLKBUF_X1"), SUB_CLASS_BUF_INV)
        self.assertEqual(classify_cell_sub_class("TBUF_X1"), SUB_CLASS_BUF_INV)

    def test_clock_gating_sub_class(self):
        self.assertEqual(classify_cell_sub_class("CLKGATE_X1"), SUB_CLASS_CLOCK_GATING)
        self.assertEqual(classify_cell_sub_class("CLKGATETST_X1"), SUB_CLASS_CLOCK_GATING)
        self.assertEqual(classify_cell_sub_class("ICG_X1"), SUB_CLASS_CLOCK_GATING)
        self.assertEqual(classify_cell_sub_class("CGL_X1"), SUB_CLASS_CLOCK_GATING)

    def test_other_sub_class(self):
        self.assertEqual(classify_cell_sub_class("XNOR2_X1"), SUB_CLASS_OTHER)
        self.assertEqual(classify_cell_sub_class("XOR2_X1"), SUB_CLASS_OTHER)
        self.assertEqual(classify_cell_sub_class("FA_X1"), SUB_CLASS_OTHER)
        self.assertEqual(classify_cell_sub_class("FILL_X1"), SUB_CLASS_OTHER)

    def test_dff_takes_precedence_over_clock_gating(self):
        # TLAT is sequential (DFF sub-class), not clock-gating
        self.assertEqual(classify_cell_sub_class("TLAT_X1"), SUB_CLASS_DFF)


class TestIsLargeDriveCell(unittest.TestCase):

    def test_large_drive_x4(self):
        self.assertTrue(is_large_drive_cell("BUF_X4"))
        self.assertTrue(is_large_drive_cell("DFF_X8"))
        self.assertTrue(is_large_drive_cell("NAND2_X16"))
        self.assertTrue(is_large_drive_cell("INV_X32"))

    def test_small_drive_not_large(self):
        self.assertFalse(is_large_drive_cell("BUF_X1"))
        self.assertFalse(is_large_drive_cell("DFF_X2"))
        self.assertFalse(is_large_drive_cell("NAND2_X1"))

    def test_no_drive_suffix(self):
        self.assertFalse(is_large_drive_cell("NAND2"))
        self.assertFalse(is_large_drive_cell("DFF_X"))

    def test_large_drive_d_suffix(self):
        # Some PDKs use D suffix for drive strength
        self.assertTrue(is_large_drive_cell("BUF_D4"))
        self.assertTrue(is_large_drive_cell("BUF_D8"))

    def test_large_drive_b_suffix(self):
        self.assertTrue(is_large_drive_cell("BUF_B4"))
        self.assertTrue(is_large_drive_cell("BUF_B8"))

    def test_large_drive_case_insensitive(self):
        self.assertTrue(is_large_drive_cell("buf_x8"))


if __name__ == "__main__":
    unittest.main()
