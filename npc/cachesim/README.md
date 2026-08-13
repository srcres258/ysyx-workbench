# NPC CacheSim

Rigorous Rust reference model for NPC I-cache architectural behavior.

## Observed current authoritative semantics

These are taken from the current repository, not inferred from old docs.

- NPC RTL source of truth:
  - `npc/vsrc-chisel/src/main/scala/top/srcres258/ysyx/npc/cache/InstructionCache.scala`
  - `npc/vsrc-chisel/src/main/scala/top/srcres258/ysyx/npc/Config.scala`
  - `npc/vsrc-chisel/src/main/scala/top/srcres258/ysyx/npc/util/SoCMemoryRanges.scala`
- Current default geometry is **4B blocks × 8 lines × 1 way**, not 16 lines.
- The cache is **blocking**, **single-outstanding**, **direct-mapped**, and refills a cacheable miss by issuing **independent 32-bit lower requests** from `line_base + 0, +4, ...` until the full line is present.
- The line becomes valid only after the full successful refill completes.
- Current positive instruction-cacheable allowlist is:
  - Flash `0x3000_0000..=0x3fff_ffff`
  - PSRAM `0x8000_0000..=0x803f_ffff`
  - SDRAM `0xa000_0000..=0xa7ff_ffff`
- Everything else bypasses, including SRAM and MROM.
- The current processor model assumes one architectural PC in the dynamic NEMU trace corresponds to one logical CPU-side I-cache request. This is valid for the current non-speculative blocking frontend, and is **not** claimed for arbitrary future speculative frontends.

## Important discrepancies found during implementation

- `npc/docs/icache.md` is stale for geometry-related defaults; the current default is 4B×8, not 4B×16.
- `npc/docs/perf-counter.md` is stale; current I-cache counters extend through `icache.total_miss_time.cycle` at index 122.
- `npc/scripts/perf_aggregator.py` still contains the older strict check `icache.lower_req.count <= icache.miss.count + icache.bypass.count`, which is only valid for 4B lines. For multi-word lines, the correct structural relationship is `lower_requests = bypasses + refill_words` under successful execution.
- ysyxSoC diplomacy metadata is more device-like than NPC's explicit I-cache allowlist. CacheSim follows the **NPC RTL** behavior because it is the intended performance-reference target.

## CLI

```bash
cargo run --release -- simulate \
  --trace trace.pctrace.bz2 \
  --elf program.elf \
  --bin flash.bin \
  --machine ysyxsoc-current \
  --block-bytes 4 \
  --lines 8 \
  --ways 1 \
  --replacement lru \
  --output result.json \
  --output-txt result.txt

cargo run --release -- compare \
  --cachesim-json result.json \
  --perf-json ../build/perf/perf.json
```

`--output-txt` is optional. When provided, cachesim emits a human-readable text
summary alongside the JSON report; when omitted, no text summary file is
written.

## Scope boundaries

- Structural counters are exact for the modeled architecture.
- Cycle timing is optional and calibration-driven.
- No instruction decoding is used for cache behavior.
- No speculative fetches, prefetching, burst refill, coherence, or self-modifying-code support is claimed.

## Output semantics (schema v2)

CacheSim now separates four kinds of statistics explicitly:

- **Exact structural/workload metrics**: exact counters and byte totals derived directly from the simulated request stream and current refill model.
- **Derived locality/traffic metrics**: rates, per-1k normalizations, traffic amplification, observed footprint, and refill-utilization metrics computed from exact counts.
- **Configuration-derived quantities**: cache geometry, words per line, lower transactions per full refill, capacity bytes, and stable `config_id`.
- **Calibrated/estimated timing metrics**: optional timing/TMT metrics derived only when a timing calibration file is available.

When a denominator is zero, derived rates are emitted as `null` in JSON and shown as `N/A` in the text summary.

### Request-rate naming

- `rates.hit_share_all_requests = hits / requests`
- `rates.miss_share_all_requests = misses / requests`
- `rates.bypass_share_all_requests = bypasses / requests`

These are **shares of all requests**, not the primary cache-effectiveness rates.

- `rates.cacheable_hit_rate = hits / cacheable_requests`
- `rates.cacheable_miss_rate = misses / cacheable_requests`
- `exact.cacheable_requests = hits + misses`

These are the primary I-cache effectiveness metrics because they exclude bypass traffic.

### Current instruction-side byte model

The current simulator assumes one architectural I-side demand corresponds to **4 bytes**:

- `exact.instruction_demand_bytes = requests * 4`
- `exact.cacheable_instruction_demand_bytes = cacheable_requests * 4`
- `exact.bypass_bytes = bypasses * 4`

The current refill transport model is **independent-word refill** with 32-bit lower requests:

- `exact.refill_bytes = exact.refill_words * 4`
- `exact.lower_fetch_bytes = exact.refill_bytes + exact.bypass_bytes`
- `exact.lower_fetch_bytes = exact.lower_requests * 4`

### Traffic amplification

CacheSim uses the term **traffic amplification** to mean lower-memory fetch traffic relative to architectural demand bytes for the measured trace window:

- `traffic.lower_traffic_amplification = lower_fetch_bytes / instruction_demand_bytes`
- `traffic.refill_traffic_amplification = refill_bytes / cacheable_instruction_demand_bytes`

Interpretation:

- `< 1.0`: reuse reduces lower-memory traffic below demanded bytes
- `= 1.0`: roughly one lower byte transferred per demanded byte
- `> 1.0`: overfetch and/or refill re-traffic exceed demanded bytes

This is **not** a bandwidth-utilization metric because no bandwidth model is implied.

### Observed footprint metrics

CacheSim reports trace-window footprint observations, not sliding-window working-set estimates:

- `exact.unique_dynamic_pcs`
- `exact.unique_cacheable_pcs`
- `exact.unique_bypass_pcs`
- `exact.unique_cacheable_blocks`
- `exact.cacheable_block_footprint_bytes = unique_cacheable_blocks * block_bytes`
- `locality.capacity_to_observed_footprint_ratio = capacity_bytes / cacheable_block_footprint_bytes`

`cacheable_block_footprint_bytes` is therefore an **observed executable block footprint** for the chosen block size, not a statement that all blocks must fit simultaneously.

### Refill line-utilization semantics

CacheSim tracks refill utilization per **cache-line incarnation**.

For each successfully allocated line, it records which 4-byte words in that line were ever demanded before:

- eviction, or
- simulation end.

Repeated execution of the same word counts once for utilization, but still contributes normally to hit counts.

Reported fields include:

- `exact.line_fill_count`
- `exact.refill_words_fetched`
- `exact.refill_words_used`
- `exact.useful_refill_bytes`
- `exact.unused_refill_words`
- `exact.unused_refill_bytes`
- `traffic.refill_word_utilization = refill_words_used / refill_words_fetched`
- `traffic.refill_overfetch_ratio = unused_refill_bytes / refill_bytes`
- `traffic.avg_unique_words_used_per_fill`

Important limitation: unused refill bytes are **unused within the measured trace/window**. A resident line at trace end may have been used later if execution had continued.

### 3C miss semantics

Raw miss classes are exposed in `miss_3c` and normalized in `miss_3c_metrics`.

Important limitation: **3C miss counts are defined relative to the selected block size.** A compulsory miss count for 4B lines is not directly identical in meaning to a compulsory miss count for 32B lines.

For block-size DSE, compare 3C counts together with:

- miss rates
- lower bytes per request
- traffic amplification
- refill utilization
- timing/TMT

### Timing/TMT semantics

If no complete calibration is available, timing metrics remain uncalibrated:

- `timing.calibrated = false`
- timing-derived numeric fields are `null`

CacheSim does **not** synthesize default miss penalties.

### JSON compatibility notes

- Schema version is now `2`.
- The exact structural bridge used by `cachesim compare` remains under `exact.*`.
- Legacy `exact.hit_rate` and `exact.miss_rate` are still emitted for compatibility, but they retain the original denominator of **all requests** and should be interpreted as all-request shares.
