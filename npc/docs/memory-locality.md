# NPC Memory Locality Analysis

This document describes the JSONL trace format and the offline locality pipeline
used by `npc/scripts/mtrace_analyzer.py`.

## 1. Where the trace comes from

Current NPC tracing is split into three layers:

- `mtrace` — architecture-level load/store events from `dpi_onMemAccess()`
- `dtrace` — device/bus events from IOMap and direct DPI device backends
- `etrace` — trap events

For locality analysis, the primary input is `build/mtrace.jsonl`.
The JSONL stream is emitted by NPC when `NPC_CONFIG_TRACE_FORMAT=jsonl` or
`both` is enabled.

## 2. Event levels

Three levels are kept distinct:

- `architecture_access`: one instruction-semantic access
- `bus_transaction`: an observed bus-level request/response action
- `device_access`: a concrete C++ device model read/write

The current JSONL trace marks each record with `trace_type` and `event_level`.
For locality analysis, do not mix levels without an explicit filter.

## 3. JSONL schema

Each line is one JSON object.

Required top-level fields:

- `schema = "npc.mtrace"`
- `schema_version = 1`
- `record_type = "access"`

Important per-record fields:

- `seq`, `access_id`, `transaction_id`
- `cycle`, `instret`, `pc`
- `access_kind`, `direction`, `initiator`, `device`
- `address`, `address_u64`, `size_bytes`, `byte_mask`, `data`
- `region_id`, `region_name`, `region_base`, `region_end`, `region_offset`
- `memory_class`, `cache_policy`, `cache_eligibility`, `cacheability_reason`
- `volatile`, `side_effect`, `idempotent_read`, `idempotent_write`

Fields that are not reliably available are emitted as `null`.

## 4. Cacheability rules

The analyzer treats cacheability as a static property of the region, not of the
observed hit pattern.

Suggested policy mapping:

- `cacheable` — ordinary RAM
- `read_only_cacheable` — ROM / Flash reads
- `write_through_preferred` — framebuffer-like memory
- `uncacheable` — MMIO registers, timers, keyboard, UART, etc.

The key point is:

> high temporal locality does not imply that caching is architecturally safe.

## 5. Cache eligibility vs cache benefit

These are separate:

- `cache_eligibility` — static legality/safety (`0.0`, `0.5`, `1.0`)
- `raw_cache_benefit_score` — dynamic locality score from the trace
- `effective_cache_value_score = cache_eligibility × raw_cache_benefit_score`

An RTC register can have excellent locality and still have an effective value of
zero because it is volatile.

## 6. Temporal locality

The analyzer computes reuse intervals with two views:

- access distance: number of intervening memory accesses
- cycle distance: number of intervening cycles

Short-range reuse is weighted more heavily:

- within 4 accesses
- within 16 accesses
- within 64 accesses
- within 256 accesses

## 7. Spatial locality

The analyzer computes, for each candidate line size:

- unique bytes touched per line
- mean / median / p50 / p90 utilization
- full-line utilization rate
- one-word-only rate

The raw trace stores only the address and access size; line utilization is
derived in Python so line-size exploration remains flexible.

## 8. Offline cache simulator

`mtrace_analyzer.py` includes a simple set-associative LRU simulator.

Supported parameters:

- line sizes: 4 / 8 / 16 / 32 / 64 B
- capacities: configurable sweep
- associativity: direct-mapped, 2-way, 4-way, etc.

Limitations:

- compulsory misses are estimated from first-touch line addresses
- capacity/conflict miss separation is reported as an aggregate
- the simulator is trace-driven and does not model RTL refill details

## 9. Running the pipeline

Example:

```bash
make -C npc mtrace IMG=build/your-benchmark.elf
make -C npc locality
make -C npc locality-report IMG=build/your-benchmark.elf
make -C npc test-locality
```

`make locality` reads an existing JSONL file and writes:

- `locality_summary.json`
- `locality_summary.txt`
- `region_summary.csv`
- `cache_sweep.csv`
- `reuse_histogram.csv`
- `stride_histogram.csv`
- `line_utilization.csv`
- `working_set.csv`

It also writes PNG and SVG plots for the locality figures.

## 10. Interpreting the plots

- `address_time_ifetch` — instruction fetch address over time
- `address_time_data` — load/store address over time
- `reuse_interval` — reuse-distance histogram
- `spatial_line_utilization` — line-size sensitivity
- `stride_distribution` — common stride classes
- `working_set_over_time` — working set growth and shrinkage
- `region_cache_value` — cacheability vs observed benefit
- `cache_miss_rate_curve` — cache sweep over size / associativity

The legend and text use three labels consistently:

- measured
- simulated
- estimated

## 11. Using the results for I-cache design

The goal is not to claim a cache is already correct. The goal is to rank which
line sizes, capacities, and associativities are worth exploring first.

Useful signals:

- high IFetch locality with safe cacheability
- strong reuse but low cache eligibility (MMIO / timer registers)
- diminishing returns as cache capacity grows

## 12. Future RTL counters

When a real cache is added to RTL, the trace-driven estimate should be replaced
or cross-checked with perf counters such as:

- I-side hits / misses / refills / evictions
- D-side hits / misses / refills / evictions
- line fill stalls
- write-back / write-through traffic
- prefetch or replay counters if added later

The current Python tool is a design-space explorer, not a hardware oracle.
