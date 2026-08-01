#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
from collections import Counter, defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Deque, Dict, Iterable, Iterator, List, Optional, NoReturn, Sequence, Tuple

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except Exception:  # pragma: no cover - exercised only in missing-dependency envs
    plt = None


SCHEMA_NAME = "npc.mtrace"
SCHEMA_VERSION = 1
DEFAULT_LINE_SIZES = [4, 8, 16, 32, 64]
DEFAULT_CACHE_SIZES = [64, 128, 256, 512, 1024, 2048, 4096]
DEFAULT_ASSOCIATIVITIES = [1, 2, 4]
DEFAULT_WINDOW_ACCESSES = 10000
DEFAULT_SAMPLE_STEP = 10
DEFAULT_ASSUMED_MISS_PENALTY_CYCLES = 20
DEFAULT_ASSUMED_HIT_LATENCY_CYCLES = 1


def fail(msg: str) -> NoReturn:
    print(f"[mtrace] ERROR: {msg}", file=sys.stderr)
    raise SystemExit(1)


def parse_csv_ints(value: str) -> List[int]:
    items = [item.strip() for item in value.split(",") if item.strip()]
    if not items:
        return []
    try:
        return [int(item, 0) for item in items]
    except ValueError as exc:
        fail(f"invalid integer list '{value}': {exc}")


def parse_u64(value: object) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        return int(value, 0)
    fail(f"unsupported integer value: {value!r}")


def parse_hex_or_int(value: object) -> int:
    return parse_u64(value)


def clamp(value: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, value))


def hex32(value: int) -> str:
    return f"0x{value:x}"


def bucket_reuse(distance: int) -> str:
    if distance <= 1:
        return "<=1"
    if distance <= 2:
        return "<=2"
    if distance <= 4:
        return "<=4"
    if distance <= 8:
        return "<=8"
    if distance <= 16:
        return "<=16"
    if distance <= 32:
        return "<=32"
    if distance <= 64:
        return "<=64"
    if distance <= 128:
        return "<=128"
    if distance <= 256:
        return "<=256"
    return ">256"


def stride_bucket(delta: int) -> str:
    if delta == 0:
        return "0"
    abs_delta = abs(delta)
    if abs_delta == 1:
        return "+/-1 byte"
    if abs_delta == 2:
        return "+/-2"
    if abs_delta == 4:
        return "+/-4"
    if abs_delta == 8:
        return "+/-8"
    if abs_delta == 16:
        return "+/-16"
    if abs_delta == 32:
        return "+/-32"
    return "other"


def word_stride(delta: int) -> int:
    if delta == 0:
        return 0
    return int(round(delta / 4.0))


def line_base(address: int, line_size: int) -> int:
    return (address // line_size) * line_size


def byte_range(address: int, size_bytes: int) -> range:
    return range(address, address + max(size_bytes, 1))


def percentile(sorted_values: Sequence[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    if p <= 0:
        return float(sorted_values[0])
    if p >= 100:
        return float(sorted_values[-1])
    idx = (len(sorted_values) - 1) * (p / 100.0)
    lo = math.floor(idx)
    hi = math.ceil(idx)
    if lo == hi:
        return float(sorted_values[int(idx)])
    weight = idx - lo
    return float(sorted_values[lo] * (1.0 - weight) + sorted_values[hi] * weight)


def mode_name(access_kind: str) -> str:
    access_kind = access_kind.lower()
    if access_kind in {"ifetch", "fetch", "instruction_fetch"}:
        return "ifetch"
    if access_kind in {"load", "read"}:
        return "load"
    if access_kind in {"store", "write"}:
        return "store"
    return "other"


def memory_class_latency(memory_class: str) -> float:
    table = {
        "ram": 1.0,
        "rom": 2.0,
        "flash": 10.0,
        "framebuffer": 4.0,
        "mmio_register": 1.0,
        "timer": 1.0,
        "uart": 1.0,
        "keyboard": 1.0,
        "unknown": 1.0,
    }
    return table.get(memory_class, 1.0)


def cache_policy_eligibility(policy: str) -> float:
    policy = policy.lower()
    if policy in {"cacheable", "read_only_cacheable"}:
        return 1.0
    if policy == "write_through_preferred":
        return 0.5
    if policy in {"uncacheable", "unknown"}:
        return 0.0
    return 0.0


@dataclass
class TraceRecord:
    trace_type: str
    event_level: str
    seq: int
    cycle: int
    instret: int
    pc: int
    access_kind: str
    direction: str
    initiator: str
    device: str
    address: int
    size_bytes: int
    data: Optional[int]
    region_id: str
    region_name: str
    region_base: int
    region_end: int
    memory_class: str
    cache_policy: str
    cache_eligibility: float
    volatile: bool
    side_effect: bool
    idempotent_read: bool
    idempotent_write: bool
    cacheability_reason: str

    @classmethod
    def from_json(cls, obj: dict, line_no: int) -> "TraceRecord":
        if obj.get("schema") != SCHEMA_NAME:
            fail(f"line {line_no}: unexpected schema {obj.get('schema')!r}")
        if obj.get("schema_version") != SCHEMA_VERSION:
            fail(f"line {line_no}: unsupported schema_version {obj.get('schema_version')!r}")
        if obj.get("record_type") != "access":
            fail(f"line {line_no}: unsupported record_type {obj.get('record_type')!r}")
        return cls(
            trace_type=str(obj.get("trace_type", "mtrace")),
            event_level=str(obj.get("event_level", "architecture_access")),
            seq=parse_u64(obj.get("seq", 0)),
            cycle=parse_u64(obj.get("cycle", 0)),
            instret=parse_u64(obj.get("instret", 0)),
            pc=parse_hex_or_int(obj.get("pc_u64", obj.get("pc", 0))),
            access_kind=str(obj.get("access_kind", "other")).lower(),
            direction=str(obj.get("direction", "read")).lower(),
            initiator=str(obj.get("initiator", "unknown")).lower(),
            device=str(obj.get("device", "unknown")).lower(),
            address=parse_hex_or_int(obj.get("address_u64", obj.get("address", 0))),
            size_bytes=parse_u64(obj.get("size_bytes", 1)),
            data=None if obj.get("data") is None else parse_hex_or_int(obj.get("data")),
            region_id=str(obj.get("region_id", "unknown")).lower(),
            region_name=str(obj.get("region_name", "UNKNOWN")),
            region_base=parse_hex_or_int(obj.get("region_base", 0)),
            region_end=parse_hex_or_int(obj.get("region_end", 0)),
            memory_class=str(obj.get("memory_class", "unknown")).lower(),
            cache_policy=str(obj.get("cache_policy", "unknown")).lower(),
            cache_eligibility=float(obj.get("cache_eligibility", 0.0)),
            volatile=bool(obj.get("volatile", False)),
            side_effect=bool(obj.get("side_effect", False)),
            idempotent_read=bool(obj.get("idempotent_read", False)),
            idempotent_write=bool(obj.get("idempotent_write", False)),
            cacheability_reason=str(obj.get("cacheability_reason", "")),
        )

    @property
    def kind(self) -> str:
        return mode_name(self.access_kind)

    @property
    def is_eligible(self) -> bool:
        return self.cache_eligibility > 0.0

    def touched_bytes(self) -> List[int]:
        return list(byte_range(self.address, self.size_bytes))


@dataclass
class CacheConfig:
    stream_kind: str
    line_size: int
    capacity_bytes: int
    associativity: int

    def key(self) -> Tuple[str, int, int, int]:
        return self.stream_kind, self.line_size, self.capacity_bytes, self.associativity


@dataclass
class CacheSimulator:
    config: CacheConfig
    assumed_hit_latency: int
    assumed_miss_penalty: int
    hits: int = 0
    misses: int = 0
    compulsory_misses: int = 0
    total_accesses: int = 0
    estimated_stall_cycles: int = 0
    estimated_saved_cycles: int = 0
    _seen_lines: set = field(default_factory=set)

    def __post_init__(self) -> None:
        slots = max(1, self.config.capacity_bytes // max(1, self.config.line_size * self.config.associativity))
        self._sets: List[List[int]] = [[] for _ in range(slots)]

    @property
    def num_sets(self) -> int:
        return len(self._sets)

    def access(self, line_addr: int) -> None:
        self.total_accesses += 1
        line_number = line_addr // self.config.line_size
        set_index = line_number % self.num_sets
        tag = line_number

        if line_number not in self._seen_lines:
            self.compulsory_misses += 1
            self._seen_lines.add(line_number)

        cache_set = self._sets[set_index]
        if tag in cache_set:
            self.hits += 1
            cache_set.remove(tag)
            cache_set.append(tag)
        else:
            self.misses += 1
            cache_set.append(tag)
            if len(cache_set) > self.config.associativity:
                del cache_set[0]

    def finalize(self) -> None:
        self.estimated_stall_cycles = self.misses * self.assumed_miss_penalty
        baseline = self.total_accesses * self.assumed_miss_penalty
        hit_cost = self.hits * self.assumed_hit_latency
        self.estimated_saved_cycles = max(0, baseline - (hit_cost + self.estimated_stall_cycles))

    @property
    def hit_rate(self) -> float:
        return (self.hits / self.total_accesses) if self.total_accesses else 0.0

    @property
    def miss_rate(self) -> float:
        return (self.misses / self.total_accesses) if self.total_accesses else 0.0

    @property
    def amat_cycles(self) -> float:
        return self.assumed_hit_latency + self.miss_rate * self.assumed_miss_penalty


@dataclass
class RegionAccumulator:
    region_id: str
    region_name: str
    region_base: int
    region_end: int
    memory_class: str
    cache_policy: str
    cache_eligibility: float
    cacheability_reason: str
    volatile: bool = False
    side_effect: bool = False
    idempotent_read: bool = False
    idempotent_write: bool = False
    access_count: int = 0
    ifetch_count: int = 0
    load_count: int = 0
    store_count: int = 0
    other_count: int = 0
    read_count: int = 0
    write_count: int = 0
    eligible_count: int = 0
    wait_cycles_estimated: float = 0.0
    reuse_access_hist: Counter[str] = field(default_factory=Counter)
    reuse_cycle_hist: Counter[str] = field(default_factory=Counter)
    last_seen: Dict[int, Tuple[int, int]] = field(default_factory=dict)
    line_bits: Dict[int, Dict[int, int]] = field(default_factory=lambda: defaultdict(dict))

    def observe(self, record: TraceRecord, access_index: int, line_sizes: Sequence[int]) -> None:
        self.access_count += 1
        kind = record.kind
        if kind == "ifetch":
            self.ifetch_count += 1
        elif kind == "load":
            self.load_count += 1
        elif kind == "store":
            self.store_count += 1
        else:
            self.other_count += 1
        if record.direction == "read":
            self.read_count += 1
        else:
            self.write_count += 1
        if record.is_eligible:
            self.eligible_count += 1

        penalty = memory_class_latency(self.memory_class)
        self.wait_cycles_estimated += penalty

        prev = self.last_seen.get(record.address)
        if prev is not None:
            prev_index, prev_cycle = prev
            self.reuse_access_hist[bucket_reuse(access_index - prev_index)] += 1
            self.reuse_cycle_hist[bucket_reuse(max(1, record.cycle - prev_cycle))] += 1
        self.last_seen[record.address] = (access_index, record.cycle)

        for line_size in line_sizes:
            line_map = self.line_bits[line_size]
            for byte_addr in record.touched_bytes():
                lb = line_base(byte_addr, line_size)
                bit = byte_addr - lb
                line_map[lb] = line_map.get(lb, 0) | (1 << bit)

    def line_utilization_stats(self, line_size: int) -> Dict[str, float]:
        line_map = self.line_bits.get(line_size, {})
        if not line_map:
            return {
                "mean": 0.0,
                "median": 0.0,
                "p50": 0.0,
                "p90": 0.0,
                "full_rate": 0.0,
                "one_word_rate": 0.0,
            }
        utils = [mask.bit_count() / float(line_size) for mask in line_map.values()]
        utils_sorted = sorted(utils)
        full_rate = sum(1 for v in utils if math.isclose(v, 1.0)) / len(utils)
        one_word_rate = sum(1 for mask in line_map.values() if mask.bit_count() <= 4) / len(utils)
        return {
            "mean": statistics.fmean(utils),
            "median": statistics.median(utils_sorted),
            "p50": percentile(utils_sorted, 50),
            "p90": percentile(utils_sorted, 90),
            "full_rate": full_rate,
            "one_word_rate": one_word_rate,
        }

    def temporal_score(self) -> float:
        total = sum(self.reuse_access_hist.values())
        if not total:
            return 0.0

        def rate(threshold: str) -> float:
            order = ["<=1", "<=2", "<=4", "<=8", "<=16", "<=32", "<=64", "<=128", "<=256"]
            idx = order.index(threshold)
            accepted = 0
            for bucket, count in self.reuse_access_hist.items():
                if order.index(bucket) <= idx:
                    accepted += count
            return accepted / total

        return clamp(
            0.40 * rate("<=4")
            + 0.30 * rate("<=16")
            + 0.20 * rate("<=64")
            + 0.10 * rate("<=256")
        )

    def spatial_score(self, line_sizes: Sequence[int]) -> float:
        if not line_sizes:
            return 0.0
        means = [self.line_utilization_stats(line_size)["mean"] for line_size in line_sizes]
        return clamp(statistics.fmean(means))

    def coverage_score(self, total_accesses: int) -> float:
        if not total_accesses:
            return 0.0
        return clamp(self.access_count / total_accesses)

    def latency_opportunity_score(self, total_wait_cycles: float) -> float:
        if total_wait_cycles <= 0.0:
            return 0.0
        return clamp(self.wait_cycles_estimated / total_wait_cycles)

    def read_suitability_score(self) -> float:
        total = max(1, self.access_count)
        return clamp((self.ifetch_count * 1.0 + self.load_count * 0.7 + self.store_count * 0.1) / total)

    def raw_cache_benefit_score(self, line_sizes: Sequence[int], total_accesses: int, total_wait_cycles: float) -> float:
        temporal = self.temporal_score()
        spatial = self.spatial_score(line_sizes)
        coverage = self.coverage_score(total_accesses)
        latency = self.latency_opportunity_score(total_wait_cycles)
        read_suitability = self.read_suitability_score()
        return 100.0 * (
            0.30 * temporal
            + 0.25 * spatial
            + 0.20 * coverage
            + 0.20 * latency
            + 0.05 * read_suitability
        )

    def to_row(self, line_sizes: Sequence[int], total_accesses: int, total_wait_cycles: float, total_cycles: int, total_eligible_accesses: int) -> Dict[str, Any]:
        raw = self.raw_cache_benefit_score(line_sizes, total_accesses, total_wait_cycles)
        effective = self.cache_eligibility * raw
        estimated_saved = self.cache_eligibility * self.wait_cycles_estimated * (raw / 100.0)
        reduction = (estimated_saved / total_cycles * 100.0) if total_cycles else 0.0
        line_stats: Dict[int, Dict[str, Any]] = {size: self.line_utilization_stats(size) for size in line_sizes}
        return {
            "region_id": self.region_id,
            "region_name": self.region_name,
            "address_range": f"{hex32(self.region_base)}-{hex32(self.region_end)}",
            "memory_class": self.memory_class,
            "cache_policy": self.cache_policy,
            "cache_eligibility": self.cache_eligibility,
            "cacheability_reason": self.cacheability_reason,
            "access_count": self.access_count,
            "access_share": (self.access_count / total_accesses) if total_accesses else 0.0,
            "wait_cycle_share": (self.wait_cycles_estimated / total_wait_cycles) if total_wait_cycles else 0.0,
            "ifetch_count": self.ifetch_count,
            "load_count": self.load_count,
            "store_count": self.store_count,
            "other_count": self.other_count,
            "read_count": self.read_count,
            "write_count": self.write_count,
            "eligible_count": self.eligible_count,
            "temporal_score": self.temporal_score(),
            "spatial_score": self.spatial_score(line_sizes),
            "coverage_score": self.coverage_score(total_accesses),
            "latency_opportunity_score": self.latency_opportunity_score(total_wait_cycles),
            "read_suitability_score": self.read_suitability_score(),
            "raw_cache_benefit_score": raw,
            "effective_cache_value_score": effective,
            "estimated_wait_cycles": self.wait_cycles_estimated,
            "estimated_saved_cycles": estimated_saved,
            "estimated_cycle_reduction_percent": reduction,
            **{f"line_{size}_mean_utilization": line_stats[size]["mean"] for size in line_sizes},
            **{f"line_{size}_full_line_rate": line_stats[size]["full_rate"] for size in line_sizes},
            **{f"line_{size}_one_word_rate": line_stats[size]["one_word_rate"] for size in line_sizes},
        }


class TraceAnalyzer:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.line_sizes = args.line_sizes
        self.cache_sizes = args.cache_sizes
        self.associativities = args.associativities
        self.trace_types = {item.lower() for item in args.trace_types}
        self.kind_filter = {item.lower() for item in args.kind_filter} if args.kind_filter else set()
        self.region_filter = {item.lower() for item in args.region_filter} if args.region_filter else set()
        self.total_records = 0
        self.total_cycles = 0
        self.total_instret = 0
        self.total_wait_cycles = 0.0
        self.total_eligible_accesses = 0
        self.kind_counts = Counter()
        self.trace_type_counts = Counter()
        self.policy_counts = Counter()
        self.reuse_hist = Counter()
        self.stride_hist = Counter()
        self.working_set_rows: List[Dict[str, Any]] = []
        self.address_time_ifetch: List[Tuple[int, int, str]] = []
        self.address_time_data: List[Tuple[int, int, str, str]] = []
        self.region_stats: Dict[str, RegionAccumulator] = {}
        self.global_last_seen: Dict[int, Tuple[int, int, str]] = {}
        self.prev_access: Optional[TraceRecord] = None
        self.window_queue: Deque[Tuple[List[int], List[int], Dict[int, List[int]]]] = deque()
        self.window_byte_counts: Counter[int] = Counter()
        self.window_word_counts: Counter[int] = Counter()
        self.window_line_counts: Dict[int, Counter[int]] = {size: Counter() for size in self.line_sizes}
        self.seen_lines_by_stream: Dict[Tuple[str, int], set] = defaultdict(set)
        self.cache_sims: Dict[Tuple[str, int, int, int], CacheSimulator] = {}
        self.cache_results: List[Dict[str, object]] = []
        self._sample_step = max(1, args.sample_step)
        self._max_records = args.max_records if args.max_records and args.max_records > 0 else None
        self._access_index = 0
        self._seen_input_records = 0
        self._total_streamable_eligible = 0

        for stream_kind in ("ifetch", "load", "store", "eligible"):
            for line_size in self.line_sizes:
                for capacity in self.cache_sizes:
                    for assoc in self.associativities:
                        cfg = CacheConfig(stream_kind, line_size, capacity, assoc)
                        self.cache_sims[cfg.key()] = CacheSimulator(
                            cfg,
                            args.assumed_hit_latency_cycles,
                            args.assumed_miss_penalty_cycles,
                        )

    def _region_matches(self, record: TraceRecord) -> bool:
        if not self.region_filter:
            return True
        return record.region_id.lower() in self.region_filter or record.region_name.lower() in self.region_filter

    def _kind_matches(self, record: TraceRecord) -> bool:
        if not self.kind_filter:
            return True
        return record.kind in self.kind_filter

    def _trace_type_matches(self, record: TraceRecord) -> bool:
        return record.trace_type.lower() in self.trace_types

    def _update_working_set(self, record: TraceRecord) -> None:
        byte_addrs = record.touched_bytes()
        word_addrs = [addr // 4 for addr in byte_addrs]
        line_addrs = {size: sorted({line_base(addr, size) for addr in byte_addrs}) for size in self.line_sizes}

        self.window_queue.append((byte_addrs, word_addrs, line_addrs))
        for b in byte_addrs:
            self.window_byte_counts[b] += 1
        for w in word_addrs:
            self.window_word_counts[w] += 1
        for size, lines in line_addrs.items():
            counter = self.window_line_counts[size]
            for line in lines:
                counter[line] += 1

        while len(self.window_queue) > self.args.window_accesses:
            old_bytes, old_words, old_lines = self.window_queue.popleft()
            for b in old_bytes:
                self.window_byte_counts[b] -= 1
                if self.window_byte_counts[b] <= 0:
                    del self.window_byte_counts[b]
            for w in old_words:
                self.window_word_counts[w] -= 1
                if self.window_word_counts[w] <= 0:
                    del self.window_word_counts[w]
            for size, lines in old_lines.items():
                counter = self.window_line_counts[size]
                for line in lines:
                    counter[line] -= 1
                    if counter[line] <= 0:
                        del counter[line]

        if self.total_records >= self.args.window_accesses and (self.total_records % self._sample_step == 0):
            row = {
                "index": self.total_records,
                "cycle": record.cycle,
                "instret": record.instret,
                "unique_bytes": len(self.window_byte_counts),
                "unique_words": len(self.window_word_counts),
            }
            for size in self.line_sizes:
                row[f"unique_lines_{size}"] = len(self.window_line_counts[size])
            self.working_set_rows.append(row)

    def _update_stride(self, record: TraceRecord) -> None:
        prev = self.prev_access
        if prev is None:
            self.prev_access = record
            return
        delta = record.address - prev.address
        self.stride_hist[f"bytes::{stride_bucket(delta)}"] += 1
        self.stride_hist[f"words::{word_stride(delta)}"] += 1
        self.prev_access = record

    def _update_reuse(self, record: TraceRecord) -> None:
        prev = self.global_last_seen.get(record.address)
        if prev is not None:
            prev_index, prev_cycle, prev_kind = prev
            distance = self._access_index - prev_index
            cycle_distance = max(1, record.cycle - prev_cycle)
            self.reuse_hist[(record.kind, "access", bucket_reuse(distance))] += 1
            self.reuse_hist[(record.kind, "cycle", bucket_reuse(cycle_distance))] += 1
            self.reuse_hist[("all", "access", bucket_reuse(distance))] += 1
            self.reuse_hist[("all", "cycle", bucket_reuse(cycle_distance))] += 1
        self.global_last_seen[record.address] = (self._access_index, record.cycle, record.kind)

    def _update_cache_sims(self, record: TraceRecord) -> None:
        if record.kind == "ifetch":
            stream_kinds = ["ifetch"]
        elif record.kind in {"load", "store"}:
            stream_kinds = [record.kind]
        else:
            stream_kinds = []
        if record.is_eligible:
            stream_kinds.append("eligible")

        for stream_kind in stream_kinds:
            for line_size in self.line_sizes:
                line_addr = line_base(record.address, line_size)
                self.seen_lines_by_stream[(stream_kind, line_size)].add(line_addr)
                for capacity in self.cache_sizes:
                    for assoc in self.associativities:
                        sim = self.cache_sims[(stream_kind, line_size, capacity, assoc)]
                        sim.access(line_addr)

    def _sample_trace_points(self, record: TraceRecord) -> None:
        if self.total_records % self._sample_step != 0:
            return
        x = record.cycle if self.args.x_axis == "cycle" else record.instret
        if record.kind == "ifetch":
            self.address_time_ifetch.append((x, record.address, record.region_name))
        elif record.kind in {"load", "store"}:
            self.address_time_data.append((x, record.address, record.kind, record.region_name))

    def observe(self, record: TraceRecord) -> None:
        self._seen_input_records += 1
        if not self._trace_type_matches(record):
            return
        if not self._kind_matches(record):
            return
        if not self._region_matches(record):
            return
        if self.args.from_cycle is not None and record.cycle < self.args.from_cycle:
            return
        if self.args.to_cycle is not None and record.cycle > self.args.to_cycle:
            return
        if self.args.from_instret is not None and record.instret < self.args.from_instret:
            return
        if self.args.to_instret is not None and record.instret > self.args.to_instret:
            return
        if self._max_records is not None and self._access_index >= self._max_records:
            return

        self.total_records += 1
        self._access_index += 1
        self.total_cycles = max(self.total_cycles, record.cycle)
        self.total_instret = max(self.total_instret, record.instret)
        self.kind_counts[record.kind] += 1
        self.trace_type_counts[record.trace_type] += 1
        self.policy_counts[record.cache_policy] += 1
        if record.is_eligible:
            self.total_eligible_accesses += 1

        region = self.region_stats.get(record.region_id)
        if region is None:
            region = RegionAccumulator(
                region_id=record.region_id,
                region_name=record.region_name,
                region_base=record.region_base,
                region_end=record.region_end,
                memory_class=record.memory_class,
                cache_policy=record.cache_policy,
                cache_eligibility=record.cache_eligibility,
                cacheability_reason=record.cacheability_reason,
                volatile=record.volatile,
                side_effect=record.side_effect,
                idempotent_read=record.idempotent_read,
                idempotent_write=record.idempotent_write,
            )
            self.region_stats[record.region_id] = region
        region.observe(record, self._access_index, self.line_sizes)

        self.total_wait_cycles += memory_class_latency(record.memory_class)
        self._update_reuse(record)
        self._update_stride(record)
        self._update_cache_sims(record)
        self._sample_trace_points(record)
        self._update_working_set(record)

    def finalize(self) -> None:
        for sim in self.cache_sims.values():
            sim.finalize()

    def region_rows(self) -> List[Dict[str, Any]]:
        rows = [
            region.to_row(self.line_sizes, self.total_records, self.total_wait_cycles, self.total_cycles, self.total_eligible_accesses)
            for region in self.region_stats.values()
        ]
        rows.sort(key=lambda row: (float(row["effective_cache_value_score"]), float(row["access_count"])), reverse=True)
        return rows

    def cache_rows(self) -> List[Dict[str, Any]]:
        rows: List[Dict[str, Any]] = []
        for sim in self.cache_sims.values():
            line_size = sim.config.line_size
            stream_kind = sim.config.stream_kind
            compulsory = len(self.seen_lines_by_stream[(stream_kind, line_size)])
            sim.compulsory_misses = compulsory
            rows.append(
                {
                    "stream_kind": stream_kind,
                    "line_size": line_size,
                    "capacity_bytes": sim.config.capacity_bytes,
                    "associativity": sim.config.associativity,
                    "accesses": sim.total_accesses,
                    "hits": sim.hits,
                    "misses": sim.misses,
                    "hit_rate": sim.hit_rate,
                    "miss_rate": sim.miss_rate,
                    "compulsory_miss_estimate": compulsory,
                    "capacity_conflict_miss_aggregate": max(0, sim.misses - compulsory),
                    "estimated_stall_cycles": sim.estimated_stall_cycles,
                    "estimated_saved_cycles": sim.estimated_saved_cycles,
                    "estimated_amat_cycles": sim.amat_cycles,
                }
            )
        rows.sort(key=lambda row: (row["stream_kind"], row["line_size"], row["capacity_bytes"], row["associativity"]))
        return rows


def load_trace(path: Path) -> Iterator[TraceRecord]:
    if not path.is_file():
        fail(f"input trace not found: {path}")
    with path.open("r", encoding="utf-8") as fh:
        for line_no, line in enumerate(fh, 1):
            if not line.strip():
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError as exc:
                fail(f"line {line_no}: invalid JSON: {exc}")
            yield TraceRecord.from_json(obj, line_no)


def write_csv(path: Path, rows: List[Dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def save_figure(fig, base_path: Path) -> None:
    if plt is None:
        fail("matplotlib is unavailable; cannot render plots")
    base_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(base_path.with_suffix(".png"), dpi=200, bbox_inches="tight")
    fig.savefig(base_path.with_suffix(".svg"), bbox_inches="tight")


def plot_address_time(points: List[Tuple[int, int, str]], title: str, output: Path, x_axis: str) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(12, 6))
    if points:
        xs = [x for x, _, _ in points]
        ys = [y for _, y, _ in points]
        ax.scatter(xs, ys, s=8, alpha=0.7, marker="o", color="black", edgecolors="none")
    else:
        ax.text(0.5, 0.5, "No matching records", ha="center", va="center", transform=ax.transAxes)
    ax.set_xlabel(x_axis)
    ax.set_ylabel("address")
    ax.set_title(title + " (measured)")
    save_figure(fig, output)


def plot_address_time_data(points: List[Tuple[int, int, str, str]], output: Path, x_axis: str) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(12, 6))
    if points:
        colors = {"load": "tab:blue", "store": "tab:orange"}
        markers = {"load": "o", "store": "s"}
        for kind in ("load", "store"):
            subset = [(x, y) for x, y, k, _ in points if k == kind]
            if not subset:
                continue
            xs = [x for x, _ in subset]
            ys = [y for _, y in subset]
            ax.scatter(xs, ys, s=10, alpha=0.7, label=kind, color=colors[kind], marker=markers[kind])
    else:
        ax.text(0.5, 0.5, "No matching records", ha="center", va="center", transform=ax.transAxes)
    ax.set_xlabel(x_axis)
    ax.set_ylabel("address")
    ax.set_title("address-time trace for data accesses (measured)")
    ax.legend(loc="best")
    save_figure(fig, output)


def plot_histogram(counter: Counter, title: str, xlabel: str, output: Path) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(10, 5))
    if counter:
        labels = list(counter.keys())
        values = [counter[k] for k in labels]
        ax.bar(labels, values, color="tab:blue")
        ax.tick_params(axis="x", rotation=45)
    else:
        ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel("count")
    save_figure(fig, output)


def plot_reuse_histogram(rows: List[Dict[str, Any]], output: Path) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(12, 6))
    if rows:
        kinds = sorted({row["kind"] for row in rows})
        buckets = ["<=1", "<=2", "<=4", "<=8", "<=16", "<=32", "<=64", "<=128", "<=256", ">256"]
        positions = list(range(len(buckets)))
        width = 0.8 / max(1, len(kinds))
        for i, kind in enumerate(kinds):
            subset = {row["bucket"]: row["count"] for row in rows if row["kind"] == kind and row["distance_type"] == "access"}
            values = [subset.get(bucket, 0) for bucket in buckets]
            ax.bar([p + i * width for p in positions], values, width=width, label=kind)
        ax.set_xticks([p + width * (len(kinds) - 1) / 2 for p in positions])
        ax.set_xticklabels(buckets, rotation=45)
    else:
        ax.text(0.5, 0.5, "No reuse data", ha="center", va="center", transform=ax.transAxes)
    ax.set_yscale("log")
    ax.set_title("reuse interval histogram (measured, log scale)")
    ax.set_xlabel("reuse-distance bucket")
    ax.set_ylabel("count")
    if rows:
        ax.legend(loc="best")
    save_figure(fig, output)


def plot_spatial_utilization(rows: List[Dict[str, Any]], line_sizes: Sequence[int], output: Path) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(10, 5))
    means = [next((float(row["mean"]) for row in rows if int(row["line_size"]) == size), 0.0) for size in line_sizes]
    p90s = [next((float(row["p90"]) for row in rows if int(row["line_size"]) == size), 0.0) for size in line_sizes]
    ax.plot(line_sizes, means, marker="o", label="mean utilization")
    ax.plot(line_sizes, p90s, marker="s", label="p90 utilization")
    ax.set_title("cache-line byte utilization (measured)")
    ax.set_xlabel("line size (bytes)")
    ax.set_ylabel("utilization")
    ax.set_ylim(0, 1.05)
    ax.legend(loc="best")
    save_figure(fig, output)


def plot_working_set(rows: List[Dict[str, Any]], output: Path, line_sizes: Sequence[int]) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(12, 6))
    if rows:
        xs = [int(row["index"]) for row in rows]
        ax.plot(xs, [int(row["unique_bytes"]) for row in rows], label="unique bytes", color="tab:blue")
        ax.plot(xs, [int(row["unique_words"]) for row in rows], label="unique words", color="tab:orange")
        for size in line_sizes:
            ax.plot(xs, [int(row[f"unique_lines_{size}"]) for row in rows], label=f"unique lines {size}B", alpha=0.5)
    else:
        ax.text(0.5, 0.5, "No working-set samples", ha="center", va="center", transform=ax.transAxes)
    ax.set_title("working set over time (measured)")
    ax.set_xlabel("access index")
    ax.set_ylabel("unique count")
    ax.legend(loc="best")
    save_figure(fig, output)


def plot_region_cache_value(rows: List[Dict[str, Any]], output: Path) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(14, 6))
    if rows:
        top = rows[:12]
        labels = [row["region_name"] for row in top]
        raw = [float(row["raw_cache_benefit_score"]) for row in top]
        effective = [float(row["effective_cache_value_score"]) for row in top]
        ax.barh(labels, raw, color="tab:blue", alpha=0.6, label="raw benefit")
        ax.barh(labels, effective, color="tab:green", alpha=0.8, label="effective value")
        for idx, row in enumerate(top):
            if float(row["cache_eligibility"]) <= 0.0:
                ax.text(0.99, idx, "UNCACHEABLE", transform=ax.get_yaxis_transform(), ha="right", va="center", fontsize=8, color="red")
    else:
        ax.text(0.5, 0.5, "No regions", ha="center", va="center", transform=ax.transAxes)
    ax.set_title("region cache value (estimated)")
    ax.set_xlabel("score")
    ax.legend(loc="best")
    save_figure(fig, output)


def plot_cache_curve(rows: List[Dict[str, Any]], output: Path) -> None:
    if plt is None:
        return
    fig, ax = plt.subplots(figsize=(12, 6))
    if rows:
        by_kind: Dict[str, List[Dict[str, Any]]] = defaultdict(list)
        for row in rows:
            by_kind[str(row["stream_kind"])].append(row)
        for kind, subset in sorted(by_kind.items()):
            xs = [int(row["capacity_bytes"]) for row in subset]
            ys = [float(row["miss_rate"]) for row in subset]
            ax.plot(xs, ys, marker="o", label=kind)
    else:
        ax.text(0.5, 0.5, "No cache results", ha="center", va="center", transform=ax.transAxes)
    ax.set_title("cache miss-rate curve (simulated)")
    ax.set_xlabel("capacity (bytes)")
    ax.set_ylabel("miss rate")
    if rows:
        ax.legend(loc="best")
    save_figure(fig, output)


def build_reuse_rows(counter: Counter) -> List[Dict[str, Any]]:
    rows = []
    for (kind, distance_type, bucket), count in sorted(counter.items()):
        rows.append({"kind": kind, "distance_type": distance_type, "bucket": bucket, "count": count})
    return rows


def build_stride_rows(counter: Counter) -> List[Dict[str, Any]]:
    rows = []
    total = sum(counter.values()) or 1
    for bucket, count in sorted(counter.items()):
        rows.append({"bucket": bucket, "count": count, "ratio": count / total})
    return rows


def build_line_util_rows(analyzer: TraceAnalyzer) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    for size in analyzer.line_sizes:
        utils: List[float] = []
        full_count = 0
        one_word_count = 0
        for region in analyzer.region_stats.values():
            line_map = region.line_bits.get(size, {})
            for mask in line_map.values():
                used = mask.bit_count()
                utils.append(used / float(size))
                if used == size:
                    full_count += 1
                if used <= 4:
                    one_word_count += 1
        if utils:
            util_sorted = sorted(utils)
            rows.append({
                "line_size": size,
                "mean": statistics.fmean(utils),
                "median": statistics.median(util_sorted),
                "p50": percentile(util_sorted, 50),
                "p90": percentile(util_sorted, 90),
                "full_line_utilization_rate": full_count / len(utils),
                "one_word_only_rate": one_word_count / len(utils),
            })
        else:
            rows.append({
                "line_size": size,
                "mean": 0.0,
                "median": 0.0,
                "p50": 0.0,
                "p90": 0.0,
                "full_line_utilization_rate": 0.0,
                "one_word_only_rate": 0.0,
            })
    return rows


def build_summary_text(analyzer: TraceAnalyzer, region_rows: List[Dict[str, Any]], cache_rows: List[Dict[str, Any]]) -> str:
    lines: List[str] = []
    lines.append(f"Schema: {SCHEMA_NAME} v{SCHEMA_VERSION}")
    lines.append(f"Records kept: {analyzer.total_records}")
    lines.append(f"IFetch / Load / Store / Other: {analyzer.kind_counts.get('ifetch', 0)} / {analyzer.kind_counts.get('load', 0)} / {analyzer.kind_counts.get('store', 0)} / {analyzer.kind_counts.get('other', 0)}")
    lines.append("")
    if region_rows:
        top = region_rows[0]
        lines.append(
            f"Top cache-value region: {top['region_name']} ({top['address_range']}) with effective score {float(top['effective_cache_value_score']):.1f}."
        )
    uncachable = [row for row in region_rows if float(row["cache_eligibility"]) <= 0.0]
    if uncachable:
        hot = uncachable[0]
        lines.append(
            f"Top uncacheable hot region: {hot['region_name']} with {hot['access_count']} accesses and reason: {hot['cacheability_reason']}."
        )
    lines.append("")
    if cache_rows:
        best = max(cache_rows, key=lambda row: float(row["estimated_saved_cycles"]))
        lines.append(
            f"Best simulated cache: {best['stream_kind']} line={best['line_size']}B cap={best['capacity_bytes']}B assoc={best['associativity']} -> miss rate {float(best['miss_rate']):.3f}, estimated saved cycles {float(best['estimated_saved_cycles']):.1f}."
        )
    lines.append("")
    lines.append("Measured vs simulated vs estimated:")
    lines.append("- measured: JSONL access trace, reuse, stride, line utilization, working-set snapshots")
    lines.append("- simulated: offline set-associative cache sweep")
    lines.append("- estimated: region value scores and saved cycles derived from trace-locality metrics")
    return "\n".join(lines) + "\n"


def build_summary_json(analyzer: TraceAnalyzer, region_rows: List[Dict[str, Any]], cache_rows: List[Dict[str, Any]], line_util_rows: List[Dict[str, Any]], reuse_rows: List[Dict[str, Any]], stride_rows: List[Dict[str, Any]]) -> Dict[str, Any]:
    return {
        "schema": SCHEMA_NAME,
        "schema_version": SCHEMA_VERSION,
        "record_type": "summary",
        "total_records": analyzer.total_records,
        "total_cycles": analyzer.total_cycles,
        "total_instret": analyzer.total_instret,
        "kind_counts": dict(analyzer.kind_counts),
        "trace_type_counts": dict(analyzer.trace_type_counts),
        "policy_counts": dict(analyzer.policy_counts),
        "region_count": len(region_rows),
        "cache_sweep_rows": len(cache_rows),
        "top_regions": region_rows[:8],
        "cache_sweep_best": max(cache_rows, key=lambda row: float(row["estimated_saved_cycles"])) if cache_rows else None,
        "line_utilization": line_util_rows,
        "reuse_histogram_rows": reuse_rows,
        "stride_histogram_rows": stride_rows,
    }


def ensure_output_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Trace-driven locality analysis for NPC JSONL traces")
    ap.add_argument("--input", required=True, type=Path, help="JSONL trace input path")
    ap.add_argument("--output-dir", required=True, type=Path, help="Output directory")
    ap.add_argument("--line-sizes", default="4,8,16,32,64", help="Comma-separated line sizes")
    ap.add_argument("--window-accesses", default=DEFAULT_WINDOW_ACCESSES, type=int, help="Sliding window length in accesses")
    ap.add_argument("--cache-sizes", default="64,128,256,512,1024,2048,4096", help="Comma-separated cache capacities")
    ap.add_argument("--associativities", default="1,2,4", help="Comma-separated associativities")
    ap.add_argument("--x-axis", choices=("cycle", "instret"), default="cycle", help="x-axis for address-time plots")
    ap.add_argument("--sample-step", type=int, default=DEFAULT_SAMPLE_STEP, help="Downsample plots every N kept records")
    ap.add_argument("--from-cycle", "--mtrace-from-cycle", dest="from_cycle", type=int, default=None)
    ap.add_argument("--to-cycle", "--mtrace-to-cycle", dest="to_cycle", type=int, default=None)
    ap.add_argument("--from-instret", "--mtrace-from-instret", dest="from_instret", type=int, default=None)
    ap.add_argument("--to-instret", "--mtrace-to-instret", dest="to_instret", type=int, default=None)
    ap.add_argument("--region", "--mtrace-region", dest="region_filter", action="append", default=[], help="Region filter (repeatable or comma-separated)")
    ap.add_argument("--kind", "--mtrace-kind", dest="kind_filter", action="append", default=[], help="Access kind filter (repeatable or comma-separated)")
    ap.add_argument("--trace-types", default="mtrace", help="Comma-separated trace types to include (default: mtrace)")
    ap.add_argument("--max-records", "--mtrace-max-records", dest="max_records", type=int, default=None, help="Maximum kept records after filtering")
    ap.add_argument("--assumed-miss-penalty-cycles", type=int, default=DEFAULT_ASSUMED_MISS_PENALTY_CYCLES)
    ap.add_argument("--assumed-hit-latency-cycles", type=int, default=DEFAULT_ASSUMED_HIT_LATENCY_CYCLES)
    return ap.parse_args(argv)


def flatten_csv_filters(values: List[str]) -> List[str]:
    out: List[str] = []
    for value in values:
        out.extend([item.strip().lower() for item in value.split(",") if item.strip()])
    return out


def main(argv: Optional[Sequence[str]] = None) -> None:
    args = parse_args(argv)
    args.line_sizes = parse_csv_ints(args.line_sizes)
    args.cache_sizes = parse_csv_ints(args.cache_sizes)
    args.associativities = parse_csv_ints(args.associativities)
    args.region_filter = flatten_csv_filters(args.region_filter)
    args.kind_filter = flatten_csv_filters(args.kind_filter)
    args.trace_types = [item.strip().lower() for item in args.trace_types.split(",") if item.strip()]
    if not args.line_sizes:
        fail("at least one line size is required")
    if not args.cache_sizes:
        fail("at least one cache size is required")
    if not args.associativities:
        fail("at least one associativity is required")

    analyzer = TraceAnalyzer(args)
    ensure_output_dir(args.output_dir)

    for record in load_trace(args.input):
        analyzer.observe(record)

    analyzer.finalize()

    reuse_rows = build_reuse_rows(analyzer.reuse_hist)
    stride_rows = build_stride_rows(analyzer.stride_hist)
    line_util_rows = build_line_util_rows(analyzer)
    region_rows = analyzer.region_rows()
    cache_rows = analyzer.cache_rows()

    write_csv(args.output_dir / "region_summary.csv", region_rows)
    write_csv(args.output_dir / "cache_sweep.csv", cache_rows)
    write_csv(args.output_dir / "reuse_histogram.csv", reuse_rows)
    write_csv(args.output_dir / "stride_histogram.csv", stride_rows)
    write_csv(args.output_dir / "line_utilization.csv", line_util_rows)
    write_csv(args.output_dir / "working_set.csv", analyzer.working_set_rows)

    summary_json = build_summary_json(analyzer, region_rows, cache_rows, line_util_rows, reuse_rows, stride_rows)
    (args.output_dir / "locality_summary.json").write_text(json.dumps(summary_json, indent=2, sort_keys=True), encoding="utf-8")
    (args.output_dir / "locality_summary.txt").write_text(build_summary_text(analyzer, region_rows, cache_rows), encoding="utf-8")

    plot_address_time(analyzer.address_time_ifetch, "IFetch address-time trace", args.output_dir / "address_time_ifetch", args.x_axis)
    plot_address_time_data(analyzer.address_time_data, args.output_dir / "address_time_data", args.x_axis)
    plot_reuse_histogram(reuse_rows, args.output_dir / "reuse_interval")
    plot_spatial_utilization(line_util_rows, analyzer.line_sizes, args.output_dir / "spatial_line_utilization")
    plot_histogram(Counter({row["bucket"]: row["count"] for row in stride_rows if row["bucket"]}), "stride distribution (measured)", "stride bucket", args.output_dir / "stride_distribution")
    plot_working_set(analyzer.working_set_rows, args.output_dir / "working_set_over_time", analyzer.line_sizes)
    plot_region_cache_value(region_rows, args.output_dir / "region_cache_value")
    plot_cache_curve(cache_rows, args.output_dir / "cache_miss_rate_curve")


if __name__ == "__main__":
    main()
