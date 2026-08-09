#!/usr/bin/env python3
"""Fixture-based tests for mtrace_analyzer.py."""

import argparse
import tempfile
import unittest
from collections import Counter
from pathlib import Path

from tests import fixture
from mtrace_analyzer import (
    CacheConfig,
    CacheSimulator,
    TraceAnalyzer,
    TraceRecord,
    RegionAccumulator,
    build_line_util_rows,
    build_reuse_rows,
    build_stride_rows,
    load_trace,
    main,
    CALIBRATED_MISS_PENALTY_PER_WORD,
    LATENCY_SOURCE_CALIBRATED,
    LATENCY_SOURCE_ESTIMATED,
)


def make_args(**overrides):
    base = dict(
        input=Path("/dev/null"),
        output_dir=Path("/dev/null"),
        line_sizes=[4, 8, 16, 32, 64],
        window_accesses=4,
        cache_sizes=[8, 16, 32],
        associativities=[1, 2],
        x_axis="cycle",
        sample_step=1,
        from_cycle=None,
        to_cycle=None,
        from_instret=None,
        to_instret=None,
        region_filter=[],
        kind_filter=[],
        trace_types=["mtrace"],
        max_records=None,
        assumed_miss_penalty_cycles=20,
        assumed_hit_latency_cycles=1,
    )
    base.update(overrides)
    return argparse.Namespace(**base)


def analyze_fixture(name: str, **kwargs) -> TraceAnalyzer:
    args = make_args(**kwargs)
    analyzer = TraceAnalyzer(args)
    for record in load_trace(fixture(name)):
        analyzer.observe(record)
    analyzer.finalize()
    return analyzer


def analyze_fixtures(*names: str, **kwargs) -> TraceAnalyzer:
    args = make_args(**kwargs)
    analyzer = TraceAnalyzer(args)
    for record in load_trace([fixture(name) for name in names]):
        analyzer.observe(record)
    analyzer.finalize()
    return analyzer


class TestLocalityPatterns(unittest.TestCase):

    def test_linear_access_has_high_spatial_and_stride(self):
        analyzer = analyze_fixture("mtrace_linear.jsonl", line_sizes=[16], cache_sizes=[8], associativities=[1])
        region_rows = analyzer.region_rows()
        self.assertEqual(region_rows[0]["region_id"], "psram")
        self.assertAlmostEqual(float(region_rows[0]["line_16_mean_utilization"]), 1.0, places=6)
        self.assertEqual(analyzer.stride_hist["bytes::+/-4"], 3)

    def test_reuse_temporal_score_beats_random(self):
        reuse = analyze_fixture("mtrace_reuse.jsonl", line_sizes=[16], cache_sizes=[8], associativities=[1])
        rnd = analyze_fixture("mtrace_random.jsonl", line_sizes=[16], cache_sizes=[8], associativities=[1])
        reuse_score = next(row for row in reuse.region_rows() if row["region_id"] == "psram")["temporal_score"]
        rnd_score = next(row for row in rnd.region_rows() if row["region_id"] == "psram")["temporal_score"]
        self.assertGreater(float(reuse_score), float(rnd_score))

    def test_itrace_records_populate_ifetch_plot_inputs(self):
        analyzer = analyze_fixtures(
            "mtrace_linear.jsonl",
            "itrace_linear.jsonl",
            line_sizes=[16],
            cache_sizes=[8],
            associativities=[1],
        )
        self.assertGreater(len(analyzer.address_time_ifetch), 0)
        self.assertGreater(analyzer.kind_counts["ifetch"], 0)

    def test_temporal_score_handles_long_reuse_buckets(self):
        region = RegionAccumulator(
            region_id="psram",
            region_name="PSRAM",
            region_base=0x80000000,
            region_end=0x80001000,
            memory_class="ram",
            cache_policy="cacheable",
            cache_eligibility=1.0,
            cacheability_reason="",
        )
        region.reuse_access_hist = Counter({">256": 3})
        self.assertEqual(region.temporal_score(), 0.0)

    def test_uncacheable_mmio_can_be_hot_but_not_eligible(self):
        analyzer = analyze_fixture("mtrace_mmio_hot.jsonl", line_sizes=[16], cache_sizes=[8], associativities=[1])
        rtc = next(row for row in analyzer.region_rows() if row["region_id"] == "rtc")
        self.assertEqual(float(rtc["cache_eligibility"]), 0.0)
        self.assertGreater(float(rtc["raw_cache_benefit_score"]), 0.0)
        self.assertEqual(float(rtc["effective_cache_value_score"]), 0.0)

    def test_direct_mapped_and_two_way_cache_simulator(self):
        cfg_dm = CacheConfig("eligible", 4, 8, 1)
        dm = CacheSimulator(cfg_dm, assumed_hit_latency=1, assumed_miss_penalty=20)
        for line_addr in (0x0, 0x8, 0x0):
            dm.access(line_addr)
        dm.finalize()
        self.assertEqual(dm.hits, 0)
        self.assertEqual(dm.misses, 3)

        cfg_tw = CacheConfig("eligible", 4, 8, 2)
        tw = CacheSimulator(cfg_tw, assumed_hit_latency=1, assumed_miss_penalty=20)
        for line_addr in (0x0, 0x8, 0x0, 0x8):
            tw.access(line_addr)
        tw.finalize()
        self.assertEqual(tw.hits, 2)
        self.assertEqual(tw.misses, 2)

    def test_outputs_are_written(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            out = Path(tmpdir)
            main([
                "--input", str(fixture("mtrace_linear.jsonl")), str(fixture("itrace_linear.jsonl")),
                "--output-dir", str(out),
                "--line-sizes", "16",
                "--cache-sizes", "8",
                "--associativities", "1",
                "--window-accesses", "4",
                "--sample-step", "1",
            ])
            self.assertTrue((out / "locality_summary.json").is_file())
            self.assertTrue((out / "locality_summary.txt").is_file())
            self.assertTrue((out / "region_summary.csv").is_file())
            self.assertTrue((out / "cache_sweep.csv").is_file())
            self.assertTrue((out / "line_utilization.csv").is_file())
            self.assertTrue((out / "address_time_ifetch.png").is_file())
            self.assertTrue((out / "address_time_ifetch.svg").is_file())
            self.assertTrue((out / "address_time_data.png").is_file())
            self.assertTrue((out / "address_time_data.svg").is_file())
            self.assertNotIn("No IFetch records were collected", (out / "address_time_ifetch.svg").read_text(encoding="utf-8"))


class TestLineSizeAwareDse(unittest.TestCase):
    """Tests for line-size-aware DSE features added in T6."""

    def test_cache_simulator_refill_words_and_estimated_miss_cycles(self):
        """Line-size-aware miss penalty computes refill_words and estimated_miss_cycles."""
        cfg_4B = CacheConfig("ifetch", 4, 64, 1)
        sim_4B = CacheSimulator(cfg_4B, assumed_hit_latency=1, assumed_miss_penalty=20,
                                miss_penalty_per_word=CALIBRATED_MISS_PENALTY_PER_WORD,
                                latency_source=LATENCY_SOURCE_CALIBRATED)
        for i in range(4):
            sim_4B.access(i * 4)  # 4 sequential words, each in different 4B line
        sim_4B.finalize()
        self.assertEqual(sim_4B.words_per_line, 1)
        self.assertEqual(sim_4B.refill_words, 4)       # 4 misses × 1 word/line
        self.assertAlmostEqual(sim_4B.estimated_miss_cycles, 4 * 16.80, places=6)
        self.assertAlmostEqual(sim_4B.tmt, 4 * 16.80 / 4, places=6)
        self.assertAlmostEqual(sim_4B.estimated_ifetch_cycles, 0 + 4 * 16.80, places=6)
        self.assertEqual(sim_4B.latency_source, LATENCY_SOURCE_CALIBRATED)

        cfg_16B = CacheConfig("ifetch", 16, 64, 1)
        sim_16B = CacheSimulator(cfg_16B, assumed_hit_latency=1, assumed_miss_penalty=20,
                                 miss_penalty_per_word=CALIBRATED_MISS_PENALTY_PER_WORD,
                                 latency_source=LATENCY_SOURCE_CALIBRATED)
        for i in range(4):
            sim_16B.access(i * 4)  # all in same 16B line: 1 miss, 3 hits
        sim_16B.finalize()
        self.assertEqual(sim_16B.words_per_line, 4)
        self.assertEqual(sim_16B.refill_words, 4)      # 1 miss × 4 words/line
        self.assertAlmostEqual(sim_16B.estimated_miss_cycles, 1 * 16.80 * 4, places=6)
        self.assertAlmostEqual(sim_16B.tmt, 1 * 16.80 * 4 / 4, places=6)
        self.assertAlmostEqual(sim_16B.estimated_ifetch_cycles, 3 * 1 + 1 * 16.80 * 4, places=6)

    def test_cache_simulator_legacy_without_miss_penalty_per_word(self):
        """Without miss_penalty_per_word, falls back to assumed_miss_penalty."""
        cfg = CacheConfig("eligible", 16, 64, 1)
        sim = CacheSimulator(cfg, assumed_hit_latency=1, assumed_miss_penalty=20)
        for i in range(4):
            sim.access(i * 16)
        sim.finalize()
        self.assertEqual(sim.words_per_line, 4)
        self.assertEqual(sim.refill_words, 16)          # 4 misses × 4 words/line
        self.assertAlmostEqual(sim.estimated_miss_cycles, 4 * 20, places=6)
        self.assertEqual(sim.latency_source, LATENCY_SOURCE_ESTIMATED)
        self.assertIsNone(sim.miss_penalty_per_word)

    def test_latency_provenance_in_cache_rows(self):
        """Calibrated latency source is propagated to cache_rows output."""
        analyzer = analyze_fixture(
            "itrace_linear.jsonl",
            line_sizes=[4, 16],
            cache_sizes=[64],
            associativities=[1],
            assumed_miss_penalty_cycles=20,
            assumed_hit_latency_cycles=1,
            miss_penalty_per_word=CALIBRATED_MISS_PENALTY_PER_WORD,
            latency_source=LATENCY_SOURCE_CALIBRATED,
        )
        rows = analyzer.cache_rows()
        ifetch_rows = [r for r in rows if r["stream_kind"] == "ifetch"]
        self.assertGreater(len(ifetch_rows), 0)
        for row in ifetch_rows:
            self.assertEqual(row["latency_source"], LATENCY_SOURCE_CALIBRATED)
            self.assertEqual(row["miss_penalty_per_word"], CALIBRATED_MISS_PENALTY_PER_WORD)
            self.assertIn("refill_words", row)
            self.assertIn("estimated_miss_cycles", row)
            self.assertIn("tmt", row)
            self.assertIn("estimated_ifetch_cycles", row)
            self.assertIn("block_bytes", row)
            self.assertIn("num_entries", row)
            self.assertIn("words_per_line", row)

    def test_multi_line_size_sweep_output(self):
        """Sweep over 4B, 8B, 16B, 32B lines at fixed 64B capacity with calibrated penalty."""
        analyzer = analyze_fixture(
            "itrace_linear.jsonl",
            line_sizes=[4, 8, 16, 32],
            cache_sizes=[64],
            associativities=[1],
            assumed_miss_penalty_cycles=20,
            assumed_hit_latency_cycles=1,
            miss_penalty_per_word=CALIBRATED_MISS_PENALTY_PER_WORD,
            latency_source=LATENCY_SOURCE_CALIBRATED,
        )
        rows = analyzer.cache_rows()
        ifetch_rows = [r for r in rows if r["stream_kind"] == "ifetch"]

        geometries = {(int(r["line_size"]), int(r["capacity_bytes"])) for r in ifetch_rows}
        self.assertIn((4, 64), geometries)
        self.assertIn((8, 64), geometries)
        self.assertIn((16, 64), geometries)
        self.assertIn((32, 64), geometries)

        for row in ifetch_rows:
            self.assertEqual(row["associativity"], 1)
            self.assertEqual(int(row["capacity_bytes"]), 64)
            self.assertEqual(int(row["accesses"]), 4)
            line_size = int(row["line_size"])
            wpl = int(row["words_per_line"])
            self.assertEqual(wpl, line_size // 4)
            self.assertGreaterEqual(int(row["refill_words"]), 4)

    def test_icache_dse_sweep_integration(self):
        """icache_dse.run_dse produces expected output for fixed-64B sweep."""
        from icache_dse import run_dse
        with tempfile.TemporaryDirectory() as tmpdir:
            out = Path(tmpdir) / "fixed_64B.csv"
            rows = run_dse(
                input_paths=[fixture("itrace_linear.jsonl")],
                output_path=out,
                block_bytes=[4, 8, 16, 32],
                capacity_bytes=[64],
                miss_penalty_per_word=CALIBRATED_MISS_PENALTY_PER_WORD,
                latency_source=LATENCY_SOURCE_CALIBRATED,
                stream_kind="ifetch",
            )
            self.assertGreater(len(rows), 0)
            self.assertTrue(out.is_file())
            content = out.read_text(encoding="utf-8")
            self.assertIn("refill_words", content)
            self.assertIn("estimated_miss_cycles", content)
            self.assertIn("tmt", content)
            self.assertIn("estimated_ifetch_cycles", content)
            self.assertIn(LATENCY_SOURCE_CALIBRATED, content)

    def test_icache_dse_sweep_32B_capacity(self):
        """icache_dse.run_dse handles 32B capacity sweep correctly."""
        from icache_dse import run_dse
        with tempfile.TemporaryDirectory() as tmpdir:
            out = Path(tmpdir) / "cap_32B.csv"
            rows = run_dse(
                input_paths=[fixture("itrace_linear.jsonl")],
                output_path=out,
                block_bytes=[4, 8, 16],
                capacity_bytes=[32],
                miss_penalty_per_word=CALIBRATED_MISS_PENALTY_PER_WORD,
                latency_source=LATENCY_SOURCE_CALIBRATED,
                stream_kind="ifetch",
            )
            self.assertGreater(len(rows), 0)
            self.assertTrue(out.is_file())
            geometries = {(int(r["block_bytes"]), int(r["num_entries"])) for r in rows}
            self.assertIn((4, 8), geometries)
            self.assertIn((8, 4), geometries)
            self.assertIn((16, 2), geometries)
            for row in rows:
                self.assertEqual(int(row["capacity_bytes"]), 32)
                self.assertEqual(row["associativity"], 1)
                self.assertEqual(row["stream_kind"], "ifetch")

    def test_cache_rows_extended_schema_backward_compat(self):
        """cache_rows extended output still has all legacy fields."""
        analyzer = analyze_fixture(
            "mtrace_linear.jsonl",
            line_sizes=[16],
            cache_sizes=[8],
            associativities=[1],
        )
        rows = analyzer.cache_rows()
        self.assertGreater(len(rows), 0)
        row = rows[0]
        for key in ("stream_kind", "line_size", "capacity_bytes", "associativity",
                     "accesses", "hits", "misses", "hit_rate", "miss_rate",
                     "compulsory_miss_estimate", "capacity_conflict_miss_aggregate",
                     "estimated_stall_cycles", "estimated_saved_cycles", "estimated_amat_cycles"):
            self.assertIn(key, row, f"Legacy field '{key}' missing from cache_rows")


if __name__ == "__main__":
    unittest.main()
