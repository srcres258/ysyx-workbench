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
                "--input", str(fixture("mtrace_linear.jsonl")),
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
            self.assertTrue((out / "address_time_data.png").is_file())
            self.assertTrue((out / "address_time_data.svg").is_file())


if __name__ == "__main__":
    unittest.main()
