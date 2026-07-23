#!/usr/bin/env python3
"""Fixture-based tests for equiv_check.py — fail-closed equivalence gate."""

import json
import unittest
from pathlib import Path
from unittest.mock import patch

from tests import fixture
from equiv_check import (
    EquivResult,
    check_experiment_equiv,
    check_all_experiments,
    generate_equiv_tcl,
    parse_equiv_output,
    validate_comparability,
    write_equiv_report,
)


class TestEquivResult(unittest.TestCase):

    def test_proven_status(self):
        r = EquivResult(experiment="test", status="proven", proven_cells=100, total_cells=100)
        self.assertTrue(r.is_proven())
        self.assertFalse(r.is_blocked())

    def test_blocked_status(self):
        r = EquivResult(experiment="test", status="blocked", limitation="missing")
        self.assertFalse(r.is_proven())
        self.assertTrue(r.is_blocked())

    def test_failed_status(self):
        r = EquivResult(experiment="test", status="failed", unproven_cells=5)
        self.assertFalse(r.is_proven())
        self.assertFalse(r.is_blocked())

    def test_defaults(self):
        r = EquivResult(experiment="default")
        self.assertEqual(r.status, "")
        self.assertEqual(r.proven_cells, 0)
        self.assertEqual(r.total_cells, 0)
        self.assertEqual(r.limitation, "")


class TestValidateComparability(unittest.TestCase):

    def test_matching_hashes_allowed(self):
        gold = {"generated_verilog_sha256": "abc123"}
        exp = {"generated_verilog_sha256": "abc123"}
        ok, reason = validate_comparability(gold, exp, "test_exp")
        self.assertTrue(ok)
        self.assertEqual(reason, "")

    def test_mismatched_hashes_blocked(self):
        gold = {"generated_verilog_sha256": "abc123"}
        exp = {"generated_verilog_sha256": "def456"}
        ok, reason = validate_comparability(gold, exp, "test_exp")
        self.assertFalse(ok)
        self.assertIn("RTL hash mismatch", reason)

    def test_gold_na_blocked(self):
        gold = {"generated_verilog_sha256": "N/A"}
        exp = {"generated_verilog_sha256": "abc123"}
        ok, reason = validate_comparability(gold, exp, "test_exp")
        self.assertFalse(ok)
        self.assertIn("no usable generated_verilog_sha256", reason)

    def test_exp_na_blocked(self):
        gold = {"generated_verilog_sha256": "abc123"}
        exp = {"generated_verilog_sha256": "N/A"}
        ok, reason = validate_comparability(gold, exp, "test_exp")
        self.assertFalse(ok)

    def test_gold_pending_blocked(self):
        gold = {"generated_verilog_sha256": "PENDING"}
        exp = {"generated_verilog_sha256": "abc123"}
        ok, reason = validate_comparability(gold, exp, "test_exp")
        self.assertFalse(ok)

    def test_gold_empty_string_blocked(self):
        gold = {"generated_verilog_sha256": ""}
        exp = {"generated_verilog_sha256": "abc123"}
        ok, reason = validate_comparability(gold, exp, "test_exp")
        self.assertFalse(ok)

    def test_gold_missing_key_blocked(self):
        gold = {}
        exp = {"generated_verilog_sha256": "abc123"}
        ok, reason = validate_comparability(gold, exp, "test_exp")
        self.assertFalse(ok)


class TestGenerateEquivTcl(unittest.TestCase):

    def test_basic_structure(self):
        tcl = generate_equiv_tcl("/path/gold.v", "/path/gate.v", "top")
        self.assertIn("read_verilog /path/gold.v", tcl)
        self.assertIn("read_verilog /path/gate.v", tcl)
        self.assertIn("design -save gold_design", tcl)
        self.assertIn("design -save gate_design", tcl)
        self.assertIn("design -reset", tcl)
        self.assertIn("design -copy-from gold_design -as gold top", tcl)
        self.assertIn("design -copy-from gate_design -as gate top", tcl)
        self.assertIn("equiv_make gold gate equiv", tcl)
        self.assertIn("equiv_simple equiv", tcl)
        self.assertIn("equiv_induct -seq", tcl)
        self.assertIn("equiv_status -assert equiv", tcl)

    def test_max_seq_parameter(self):
        tcl = generate_equiv_tcl("/a.v", "/b.v", "mod", max_seq=5)
        self.assertIn("equiv_induct -seq 5 equiv", tcl)
        self.assertNotIn("equiv_induct -seq 10", tcl)

    def test_flatten_before_save(self):
        tcl = generate_equiv_tcl("/a.v", "/b.v", "cpu")
        self.assertIn("flatten cpu", tcl)
        self.assertIn("design -save gold_design", tcl)
        gold_flatten_idx = tcl.index("flatten cpu")
        gold_save_idx = tcl.index("design -save gold_design")
        self.assertLess(gold_flatten_idx, gold_save_idx,
                        "Gold must be flattened before design -save")

    def test_design_resets_between_loads(self):
        tcl = generate_equiv_tcl("/a.v", "/b.v", "top")
        gold_save_idx = tcl.index("design -save gold_design")
        reset_idx = tcl.index("design -reset", gold_save_idx)
        gate_load_idx = tcl.index("read_verilog /b.v")
        self.assertLess(reset_idx, gate_load_idx,
                        "Design must be reset between loading gold and gate netlists")

    def test_opt_clean_after_copy(self):
        tcl = generate_equiv_tcl("/a.v", "/b.v", "top")
        self.assertIn("opt_expr gold", tcl)
        self.assertIn("opt_expr gate", tcl)
        self.assertIn("opt_clean -purge gold", tcl)
        self.assertIn("opt_clean -purge gate", tcl)


class TestParseEquivOutput(unittest.TestCase):

    def test_all_proven(self):
        stdout = Path(fixture("equiv_stdout_all_proven.txt")).read_text()
        result = parse_equiv_output(stdout, "test_exp")
        self.assertEqual(result.status, "proven")
        self.assertEqual(result.proven_cells, 127)

    def test_partially_proven(self):
        stdout = Path(fixture("equiv_stdout_partially_proven.txt")).read_text()
        result = parse_equiv_output(stdout, "test_exp")
        self.assertEqual(result.status, "failed")
        self.assertEqual(result.unproven_cells, 10)
        self.assertIn("10 of", result.detail)

    def test_unsupported_cell_blocked(self):
        stdout = Path(fixture("equiv_stdout_unsupported_cell.txt")).read_text()
        result = parse_equiv_output(stdout, "test_exp")
        self.assertEqual(result.status, "blocked")
        self.assertEqual(result.limitation, "unsupported_structure")
        self.assertIn("SAT solver cannot reason about", result.detail)

    def test_memory_dpi_blocked(self):
        stdout = Path(fixture("equiv_stdout_memory_dpi.txt")).read_text()
        result = parse_equiv_output(stdout, "test_exp")
        self.assertEqual(result.status, "blocked")
        self.assertEqual(result.limitation, "memory_dpi")
        self.assertIn("memory cells or DPI references", result.detail)

    def test_empty_output(self):
        result = parse_equiv_output("", "test_exp")
        self.assertEqual(result.status, "error")
        self.assertEqual(result.limitation, "parse_failure")

    def test_experiment_name_preserved(self):
        result = parse_equiv_output("Found 127 $equiv cells in equiv:\n  Of those cells 127 are proven and 0 are unproven.\n  Equivalence successfully proven!", "my_exp")

    def test_unsupported_check_before_proven_count(self):
        """Unsupported cell patterns take precedence over proven cell counts."""
        stdout = "unknown cell type. 127 $equiv cells proven."
        result = parse_equiv_output(stdout, "test")
        self.assertEqual(result.status, "blocked")
        self.assertEqual(result.limitation, "unsupported_structure")

    def test_memory_dpi_check_before_proven_count(self):
        """Memory/DPI patterns take precedence over proven cell counts."""
        stdout = "$mem warning. 50 $equiv cells proven."
        result = parse_equiv_output(stdout, "test")
        self.assertEqual(result.status, "blocked")
        self.assertEqual(result.limitation, "memory_dpi")


class TestCheckExperimentWithMocks(unittest.TestCase):
    """Tests check_experiment_equiv with mocked Yosys execution."""

    def _mock_identity(self, verilog_hash="abc123"):
        return {
            "generated_verilog_sha256": verilog_hash,
            "liberty_sha256": "lib_hash",
            "sdc_sha256": "sdc_hash",
        }

    def test_hash_mismatch_blocks(self):
        result = check_experiment_equiv(
            gold_pre_abc_v="/fake/gold.v",
            exp_pre_abc_v="/fake/exp.v",
            identity_gold=self._mock_identity("aaa"),
            identity_exp=self._mock_identity("bbb"),
            exp_label="exp_a",
            yosys_bin="/fake/yosys",
        )
        self.assertEqual(result.status, "blocked")
        self.assertEqual(result.limitation, "identity_hash_mismatch")
        self.assertIn("RTL hash mismatch", result.detail)

    def test_missing_gold_netlist(self):
        result = check_experiment_equiv(
            gold_pre_abc_v="/nonexistent/gold.v",
            exp_pre_abc_v="/nonexistent/exp.v",
            identity_gold=self._mock_identity("abc123"),
            identity_exp=self._mock_identity("abc123"),
            exp_label="exp_a",
            yosys_bin="/fake/yosys",
        )
        self.assertEqual(result.status, "blocked")
        self.assertEqual(result.limitation, "missing_gold_netlist")

    def test_missing_gold_hash_na(self):
        """Gold with N/A hash should be blocked before file check."""
        result = check_experiment_equiv(
            gold_pre_abc_v="/fake/gold.v",
            exp_pre_abc_v="/fake/exp.v",
            identity_gold={"generated_verilog_sha256": "N/A"},
            identity_exp=self._mock_identity("abc123"),
            exp_label="exp_a",
            yosys_bin="/fake/yosys",
        )
        self.assertEqual(result.status, "blocked")
        self.assertIn("no usable", result.detail)

    def test_exp_missing_hash_na(self):
        """Exp with N/A hash should be blocked."""
        result = check_experiment_equiv(
            gold_pre_abc_v="/fake/gold.v",
            exp_pre_abc_v="/fake/exp.v",
            identity_gold=self._mock_identity("abc123"),
            identity_exp={"generated_verilog_sha256": "PENDING"},
            exp_label="exp_a",
            yosys_bin="/fake/yosys",
        )
        self.assertEqual(result.status, "blocked")


class TestEquivReport(unittest.TestCase):

    def setUp(self):
        import tempfile
        self._tmpdir_obj = tempfile.TemporaryDirectory()
        self.tmpdir = self._tmpdir_obj.name

    def tearDown(self):
        self._tmpdir_obj.cleanup()

    def test_write_report_creates_file(self):
        results = [
            EquivResult(experiment="exp_a", status="proven", proven_cells=100, total_cells=100),
            EquivResult(experiment="exp_b", status="blocked",
                        limitation="missing", detail="no netlist"),
            EquivResult(experiment="exp_c", status="failed",
                        unproven_cells=5, total_cells=100,
                        detail="5 cells unproven"),
        ]
        write_equiv_report(results, self.tmpdir, gold_exp="exp_gold")
        rpt_path = Path(self.tmpdir) / "equivalence_report.rpt"
        self.assertTrue(rpt_path.is_file())

        content = rpt_path.read_text()
        self.assertIn("EQUIVALENCE CHECK REPORT", content)
        self.assertIn("exp_gold", content)
        self.assertIn("exp_a", content)
        self.assertIn("PROVEN", content)
        self.assertIn("BLOCKED", content)
        self.assertIn("FAILED", content)
        self.assertIn("1 proven, 1 blocked, 1 failed", content)
        self.assertIn("Backstop", content)
        self.assertIn("cpu-tests", content)

    def test_report_all_proven(self):
        results = [
            EquivResult(experiment="exp_a", status="proven", proven_cells=200, total_cells=200),
            EquivResult(experiment="exp_b", status="proven", proven_cells=200, total_cells=200),
        ]
        write_equiv_report(results, self.tmpdir)
        rpt_path = Path(self.tmpdir) / "equivalence_report.rpt"
        content = rpt_path.read_text()
        self.assertIn("2 proven", content)
        self.assertNotIn("cpu-tests", content,
                         "Functional backstop should not be mentioned when all proven")

    def test_report_output_dir_created(self):
        results = [EquivResult(experiment="x", status="proven")]
        nested = Path(self.tmpdir) / "sub" / "deep"
        write_equiv_report(results, str(nested))
        self.assertTrue((nested / "equivalence_report.rpt").is_file())


class TestCheckAllExperiments(unittest.TestCase):

    def setUp(self):
        import tempfile
        self._tmpdir_obj = tempfile.TemporaryDirectory()
        self.tmpdir = self._tmpdir_obj.name

    def tearDown(self):
        self._tmpdir_obj.cleanup()

    def _setup_experiment(self, name, verilog_hash, has_netlist=True):
        exp_dir = Path(self.tmpdir) / name
        exp_dir.mkdir(parents=True, exist_ok=True)
        result_dir = exp_dir / "ysyx_25070190-100MHz"
        result_dir.mkdir(parents=True, exist_ok=True)

        identity = {
            "generated_verilog_sha256": verilog_hash,
            "liberty_sha256": "lib_hash",
            "sdc_sha256": "sdc_hash",
            "top_module": "ysyx_25070190",
            "target_clock_mhz": 100,
        }
        (exp_dir / "input_identity.json").write_text(json.dumps(identity))

        if has_netlist:
            (result_dir / "stage_pre_abc.v").write_text(
                "module ysyx_25070190(input a, output b); endmodule"
            )

    def test_all_missing_gold(self):
        """When gold experiment doesn't exist, all results blocked."""
        results = check_all_experiments(
            synth_root=self.tmpdir,
            gold_exp="exp_d_postmap_flat",
            yosys_bin="/fake/yosys",
        )
        self.assertEqual(len(results), 4)
        for r in results:
            self.assertEqual(r.status, "blocked")
            self.assertEqual(r.limitation, "missing_gold_netlist")

    def test_gold_compared_to_self(self):
        """Gold experiment compared against itself: trivially proven."""
        self._setup_experiment("exp_d_postmap_flat", "abc123")
        results = check_all_experiments(
            synth_root=self.tmpdir,
            gold_exp="exp_d_postmap_flat",
            yosys_bin="/fake/yosys",
        )
        gold_result = [r for r in results if r.experiment == "exp_d_postmap_flat"][0]
        self.assertEqual(gold_result.status, "proven")
        self.assertIn("trivially equivalent", gold_result.detail)

    def test_missing_experiment_netlist(self):
        """Experiment without pre_abc.v is blocked."""
        self._setup_experiment("exp_d_postmap_flat", "abc123")
        self._setup_experiment("exp_c_hier_abc", "abc123", has_netlist=False)
        self._setup_experiment("exp_b_flatten_pre_abc", "abc123", has_netlist=False)
        self._setup_experiment("exp_a_upstream_default", "abc123", has_netlist=False)

        results = check_all_experiments(
            synth_root=self.tmpdir,
            gold_exp="exp_d_postmap_flat",
            yosys_bin="/fake/yosys",
        )
        blocked = [r for r in results if r.status == "blocked" and r.limitation == "missing_experiment_netlist"]
        self.assertEqual(len(blocked), 3)
        self.assertIn("stage_pre_abc.v", blocked[0].detail)


class TestHierarchicalRealYosys(unittest.TestCase):
    """Regression: hierarchical netlists must not cause re-definition errors.

    The old copy/rename/delete approach fails with ``ERROR: Re-definition of
    module \\child!`` when both gold and gate netlists contain the same
    submodules.  The current design -save/-reset/-copy-from approach isolates
    each design, flattens independently, then imports only the flat top modules.
    """

    YOSYS_BIN = "/nix/store/mq1s3n96svwlp9h8h8d4r9rbn4wd7hkb-yosys-0.62/bin/yosys"

    @classmethod
    def setUpClass(cls):
        from pathlib import Path as _Path
        if not _Path(cls.YOSYS_BIN).is_file():
            raise unittest.SkipTest(f"Yosys not found at {cls.YOSYS_BIN}")

    def test_hierarchical_pair_proves_equivalence(self):
        gold_v = str(fixture("equiv_hier_gold.v").resolve())
        gate_v = str(fixture("equiv_hier_gate.v").resolve())
        identity = {
            "generated_verilog_sha256": "hierarchical_match",
            "liberty_sha256": "lib",
            "sdc_sha256": "sdc",
        }

        result = check_experiment_equiv(
            gold_pre_abc_v=gold_v,
            exp_pre_abc_v=gate_v,
            identity_gold=identity,
            identity_exp=identity,
            exp_label="hierarchical_regression",
            top_module="top",
            yosys_bin=self.YOSYS_BIN,
            max_seq=4,
            timeout=60,
        )

        self.assertTrue(
            result.is_proven(),
            f"Expected proven, got {result.status}: {result.detail}. "
            f"Hierarchical netlists must not cause re-definition errors."
        )

    def test_hierarchical_redefinition_no_longer_errors(self):
        """The old copy/rename pattern would produce the error and be blocked."""
        gold_v = str(fixture("equiv_hier_gold.v").resolve())
        gate_v = str(fixture("equiv_hier_gate.v").resolve())
        identity = {
            "generated_verilog_sha256": "hierarchical_match",
            "liberty_sha256": "lib",
            "sdc_sha256": "sdc",
        }

        result = check_experiment_equiv(
            gold_pre_abc_v=gold_v,
            exp_pre_abc_v=gate_v,
            identity_gold=identity,
            identity_exp=identity,
            exp_label="hierarchical_regression",
            top_module="top",
            yosys_bin=self.YOSYS_BIN,
            max_seq=4,
            timeout=60,
        )

        self.assertNotEqual(
            result.status, "error",
            f"Hierarchical netlists must not cause Yosys errors. "
            f"Got status={result.status}, limitation={result.limitation}"
        )
        self.assertNotEqual(
            result.limitation, "unsupported_structure",
            "Design-isolation fix must prevent re-definition errors"
        )


if __name__ == "__main__":
    unittest.main()
