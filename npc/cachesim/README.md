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
