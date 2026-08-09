#!/usr/bin/env python3
"""ICache DSE: offline direct-mapped I-cache sweep and candidate table generator.

Convenience wrapper around mtrace_analyzer.py that accepts blockBytes / numEntries
geometry specs, derives line_size / capacity_bytes / associativity=1, and runs
the locality analyzer with calibrated miss penalties from T4 measured RTL data.

Usage:
    # Fixed-64B data-capacity sweep
    python3 icache_dse.py --input trace.jsonl --output dse.csv \\
        --block-bytes 4,8,16,32 --capacity-bytes 64

    # 32B-capacity sweep
    python3 icache_dse.py --input trace.jsonl --output dse_32B.csv \\
        --block-bytes 4,8,16 --capacity-bytes 32

    # Full grid sweep
    python3 icache_dse.py --input trace.jsonl --output full.csv \\
        --block-bytes 4,8,16,32 --num-entries 2,4,8,16
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Set, Tuple

from mtrace_analyzer import (
    CALIBRATED_MISS_PENALTY_PER_WORD,
    LATENCY_SOURCE_CALIBRATED,
    CacheConfig,
    CacheSimulator,
    TraceAnalyzer,
    TraceRecord,
    load_trace,
    parse_csv_ints,
    fail,
    flatten_csv_filters,
    DEFAULT_ASSOCIATIVITIES,
    DEFAULT_LINE_SIZES,
    DEFAULT_CACHE_SIZES,
    DEFAULT_WINDOW_ACCESSES,
    DEFAULT_SAMPLE_STEP,
    DEFAULT_ASSUMED_MISS_PENALTY_CYCLES,
    DEFAULT_ASSUMED_HIT_LATENCY_CYCLES,
)


def _make_dse_args(
    input_paths: List[Path],
    line_sizes: List[int],
    cache_sizes: List[int],
    miss_penalty_per_word: float,
    latency_source: str,
    stream_kind: str,
    window_accesses: int = DEFAULT_WINDOW_ACCESSES,
    sample_step: int = DEFAULT_SAMPLE_STEP,
    assumed_miss_penalty: int = DEFAULT_ASSUMED_MISS_PENALTY_CYCLES,
    assumed_hit_latency: int = DEFAULT_ASSUMED_HIT_LATENCY_CYCLES,
    kind_filter: Optional[List[str]] = None,
) -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", nargs="+", type=Path, default=input_paths)
    ap.add_argument("--output-dir", type=Path, default=Path("/dev/null"))
    ap.add_argument("--line-sizes", default=",".join(str(s) for s in line_sizes))
    ap.add_argument("--window-accesses", type=int, default=window_accesses)
    ap.add_argument("--cache-sizes", default=",".join(str(c) for c in cache_sizes))
    ap.add_argument("--associativities", default="1")
    ap.add_argument("--x-axis", default="cycle")
    ap.add_argument("--sample-step", type=int, default=sample_step)
    ap.add_argument("--trace-types", default="mtrace")
    ap.add_argument("--assumed-miss-penalty-cycles", type=int, default=assumed_miss_penalty)
    ap.add_argument("--assumed-hit-latency-cycles", type=int, default=assumed_hit_latency)
    ap.add_argument("--miss-penalty-per-word", type=float, default=miss_penalty_per_word)
    ap.add_argument("--latency-source", type=str, default=latency_source)
    ap.add_argument("--region", dest="region_filter", action="append", default=[])
    ap.add_argument("--kind", "--mtrace-kind", dest="kind_filter", action="append",
                    default=(kind_filter if kind_filter else []))
    ap.add_argument("--max-records", dest="max_records", type=int, default=None)
    ap.add_argument("--from-cycle", dest="from_cycle", type=int, default=None)
    ap.add_argument("--to-cycle", dest="to_cycle", type=int, default=None)
    ap.add_argument("--from-instret", dest="from_instret", type=int, default=None)
    ap.add_argument("--to-instret", dest="to_instret", type=int, default=None)
    return ap.parse_args([])


def _stream_filter(stream_kind: str) -> List[str]:
    if stream_kind in ("ifetch", "fetch", "instruction_fetch"):
        return ["ifetch"]
    if stream_kind in ("eligible", "all"):
        return ["ifetch", "load", "store"]
    return [stream_kind]


def run_dse(
    input_paths: List[Path],
    output_path: Path,
    block_bytes: Optional[List[int]] = None,
    num_entries: Optional[List[int]] = None,
    capacity_bytes: Optional[List[int]] = None,
    miss_penalty_per_word: float = CALIBRATED_MISS_PENALTY_PER_WORD,
    latency_source: str = LATENCY_SOURCE_CALIBRATED,
    stream_kind: str = "eligible",
    window_accesses: int = DEFAULT_WINDOW_ACCESSES,
    sample_step: int = DEFAULT_SAMPLE_STEP,
    assumed_miss_penalty: int = DEFAULT_ASSUMED_MISS_PENALTY_CYCLES,
    assumed_hit_latency: int = DEFAULT_ASSUMED_HIT_LATENCY_CYCLES,
) -> List[Dict[str, Any]]:
    if block_bytes is None:
        block_bytes = DEFAULT_LINE_SIZES
    if num_entries is None and capacity_bytes is None:
        num_entries = [1, 2, 4, 8, 16]

    candidates: Set[Tuple[int, int]] = set()
    if capacity_bytes is not None:
        for cap in capacity_bytes:
            for bb in block_bytes:
                if cap % bb == 0:
                    candidates.add((bb, cap // bb))
    if num_entries is not None:
        for n in num_entries:
            for bb in block_bytes:
                candidates.add((bb, n))

    line_sizes = sorted({bb for bb, _ in candidates})
    cache_sizes = sorted({bb * n for bb, n in candidates})

    kind_filter = _stream_filter(stream_kind)
    args = _make_dse_args(
        input_paths=input_paths,
        line_sizes=line_sizes,
        cache_sizes=cache_sizes,
        miss_penalty_per_word=miss_penalty_per_word,
        latency_source=latency_source,
        stream_kind=stream_kind,
        window_accesses=window_accesses,
        sample_step=sample_step,
        assumed_miss_penalty=assumed_miss_penalty,
        assumed_hit_latency=assumed_hit_latency,
        kind_filter=kind_filter,
    )
    args.line_sizes = parse_csv_ints(args.line_sizes)
    args.cache_sizes = parse_csv_ints(args.cache_sizes)
    args.associativities = parse_csv_ints(args.associativities)
    args.region_filter = flatten_csv_filters(args.region_filter)
    args.kind_filter = flatten_csv_filters(args.kind_filter)
    args.trace_types = [item.strip().lower() for item in args.trace_types.split(",") if item.strip()]

    analyzer = TraceAnalyzer(args)
    for record in load_trace(input_paths):
        analyzer.observe(record)
    analyzer.finalize()

    all_rows = analyzer.cache_rows()
    candidate_keys = {(bb, bb * n) for bb, n in candidates}
    dse_rows = [
        row for row in all_rows
        if row["associativity"] == 1
        and row["stream_kind"] in kind_filter
        and (int(row["block_bytes"]), int(row["capacity_bytes"])) in candidate_keys
    ]

    if output_path != Path("/dev/null"):
        output_path.parent.mkdir(parents=True, exist_ok=True)
        if dse_rows:
            with output_path.open("w", newline="", encoding="utf-8") as fh:
                writer = csv.DictWriter(fh, fieldnames=list(dse_rows[0].keys()))
                writer.writeheader()
                writer.writerows(dse_rows)

    return dse_rows


def _parse_cli(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Offline direct-mapped I-cache DSE sweep"
    )
    ap.add_argument("--input", required=True, nargs="+", type=Path, help="JSONL trace input path(s)")
    ap.add_argument("--output", required=True, type=Path, help="Output CSV path")
    ap.add_argument("--block-bytes", default=None, help="Comma-separated block sizes (default: 4,8,16,32)")
    ap.add_argument("--num-entries", default=None, help="Comma-separated entry counts")
    ap.add_argument("--capacity-bytes", default=None, help="Comma-separated capacity constraints (filters num-entries)")
    ap.add_argument("--miss-penalty-per-word", type=float, default=CALIBRATED_MISS_PENALTY_PER_WORD,
                    help=f"Per-4B-word miss penalty (default: {CALIBRATED_MISS_PENALTY_PER_WORD})")
    ap.add_argument("--latency-source", type=str, default=LATENCY_SOURCE_CALIBRATED,
                    help=f"Provenance label (default: {LATENCY_SOURCE_CALIBRATED})")
    ap.add_argument("--stream-kind", type=str, default="eligible",
                    help="Stream kind filter: ifetch, eligible, load, store, all")
    return ap.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> None:
    args = _parse_cli(argv)
    block_bytes = parse_csv_ints(args.block_bytes) if args.block_bytes else None
    num_entries = parse_csv_ints(args.num_entries) if args.num_entries else None
    capacity_bytes = parse_csv_ints(args.capacity_bytes) if args.capacity_bytes else None

    rows = run_dse(
        input_paths=args.input,
        output_path=args.output,
        block_bytes=block_bytes,
        num_entries=num_entries,
        capacity_bytes=capacity_bytes,
        miss_penalty_per_word=args.miss_penalty_per_word,
        latency_source=args.latency_source,
        stream_kind=args.stream_kind,
    )
    print(f"[icache_dse] Wrote {len(rows)} candidate rows to {args.output}")


if __name__ == "__main__":
    main()
