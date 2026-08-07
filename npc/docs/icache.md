# NPC Instruction Cache — Design & Verification

NPC 的指令缓存（I-cache）位于 IFU 与 LSU 的 IFetch 接口之间，在不改变 MEMU/AXI 拓扑的前提下，降低重复取指的 AXI 流量和总仿真周期。

## Cache Geometry

| 参数 | 值 |
|---|---|
| Block size | 4 bytes (1 RV32 指令) |
| Entries | 16 |
| Associativity | Direct-mapped |
| Tag width | 26 bits |
| Index width | 4 bits (addr[5:2]) |
| Offset width | 2 bits (addr[1:0], always 0 for word-aligned fetch) |
| Total tag storage | 16 × 26 = 416 FFs |
| Total data storage | 16 × 32 = 512 FFs |
| Total valid bits | 16 FFs |
| Estimated area overhead | ~944 FFs + combin. logic |
| Storage type | Register (Vec of Reg) — no SRAM macro, no SyncReadMem |
| Hit latency | 1 cycle (request fire → next cycle cpuResp valid) |
| Miss latency | 3 cycles + lower-memory wait (s_send_mem_req → s_wait_mem_resp → s_cpu_resp) |
| Miss policy | Blocking, single outstanding — no concurrent misses |

## Bypass Rules

I-cache 使用**正向 allowlist** 决定 cacheability：只有明确列为 cacheable 的地址区域会被缓存，其他所有地址默认 bypass。

| Region | Address Range | Cacheable | Rationale |
|---|---|---|---|
| SPI XIP Flash | `0x30000000–0x3fffffff` | ✅ yes | Main code storage; typically read-only at runtime |
| PSRAM | `0x80000000–0x803fffff` | ✅ yes | Fast on-chip PSRAM; code potentially placed here |
| SDRAM | `0xa0000000–0xa7ffffff` | ✅ yes | External DDR; large code footprint |
| MROM | `0x20000000–0x20000fff` | ❌ bypass | Boot ROM; executed once, caching futile |
| SRAM | `0x0f000000–0x0f001fff` | ❌ bypass | Dual-purpose (code + data); caching risks coherency |
| MMIO (all) | various | ❌ bypass | Side effects; reading UART status clears flags |
| Unknown | anything else | ❌ bypass | Safe default; unknown devices may have side effects |

**Refill gate**: Only cacheable misses with AXI OKAY (0) response are refilled. Error responses (SLVERR=2, DECERR=3) and bypass requests are forwarded without modifying cache state.

## Blocking & Single Outstanding

The cache implements a 4-state FSM:

```
s_idle → (hit?) → s_cpu_resp → s_idle
s_idle → (miss/bypass) → s_send_mem_req → s_wait_mem_resp → s_cpu_resp → s_idle
```

No new requests are accepted while a miss is in-flight. This is intentional — it guarantees:
- At most one outstanding AXI transaction per IFetch miss
- Deterministic refill ordering
- No duplicate requests due to backpressure or replayed IFU requests

The IFU sees the cache as a drop-in replacement for the original IFU→LSU connection.

## Limitations

1. **No burst refill**: Only 1 instruction (4 B) fetched per miss. Consecutive instructions in the same cache line (if block size were larger) would still generate separate misses.
2. **No prefetch**: The cache does not speculatively fetch the next cache line.
3. **No associativity**: Conflict misses occur when two different tags map to the same index. A tight loop spanning >16 unique addresses that all share the same index will thrash.
4. **No D-cache**: Data memory accesses go through LSU directly without caching.
5. **Blocking only**: Miss under miss is not supported; IFU stalls until the miss is resolved.
6. **No write-back**: The I-cache is read-only; there is no dirty-eviction path.

## Perf Counter Interpretation

The I-cache exposes 9 perf counters (indices 109–117) via DPI. See `npc/docs/perf-counter.md` for the full table.

### Key Derived Metrics

| Metric | Formula | Interpretation |
|---|---|---|
| Hit rate | `icache.hit / icache.request` | Fraction of IFU requests served from cache. Near 1.0 on tight loops; drops on large code footprints. |
| Miss rate | `icache.miss / icache.request` | Fraction requiring lower-memory access. Drives AXI traffic. |
| Bypass rate | `icache.bypass / icache.request` | Fraction of IFU requests to non-cacheable addresses. High on boot code (MROM, MMIO) or mixed code/data SRAM. |
| AXI load reduction | `1 − (icache.lower_req / icache.request)` | Effective AXI traffic reduction from caching. 0% = no cache effect; 50% = half the AXI throughput. |
| Miss response ratio | `icache.lower_resp / icache.miss` | Should be ≈1.0 for single-outstanding. >1.0 indicates pipeline replays or backpressure causing multiple resp per miss. |
| Hit-per-response ratio | `icache.response / icache.request` | Should be ≈1.0 — every request gets a response. <1.0 indicates lost requests (bug). |

### Closure Invariants

When `PERF_CHECK_STRICT=on`, the following invariants are verified (fail-closed on violation):

| Invariant | Slack | Rationale |
|---|---|---|
| `icache.request == icache.hit + icache.miss + icache.bypass` | 1 | Every request is classified exactly once |
| `icache.response == icache.request` | 1 | Every request eventually gets a response |
| `icache.lower_req <= icache.miss + icache.bypass` | 0 | Only miss and bypass generate lower-memory requests |
| `icache.refill <= icache.miss` | 0 | Refills only from misses (and not all misses — errors skip refill) |

### Comparing Baseline to I-Cache

When comparing before/after I-cache builds, focus on:

- **`core.cycle`**: Should decrease — fewer wait cycles
- **`stall.ifetch.wait_resp.cycle`**: Should decrease significantly — cache hits bypass AXI wait
- **`ifetch.axi_ar.fire.count` / `ifetch.axi_r.fire.count`**: Should approach `icache.lower_req.count` rather than `core.instret`
- **`core.instret`**: Should remain stable — same workload, same retired instructions
- **IPC**: Should increase — fewer cycles for the same instructions
- **`icache.hit.count`**: Core cache-effectiveness metric
- **`icache.bypass.count`**: Non-cacheable regions; should be near-zero on SDRAM-based workloads

## Chisel Implementation

- Source: `npc/vsrc-chisel/src/main/scala/top/srcres258/ysyx/npc/cache/InstructionCache.scala`
- Spec: `npc/vsrc-chisel/src/test/scala/top/srcres258/ysyx/npc/InstructionCacheSpec.scala`
- Top integration: `NPCWithSoC` and `NPCStandalone` in `Top.scala`
- Perf DPI: `PerfDPIBundle.scala → PerfICacheDPIBundle` (9 signals, DPI-only, zero synthesis impact)
- Assertions: Tag/data/valid stability, single outstanding request, response payload stability

## Verification

- **Chisel unit tests**: `./mill ChiselYSYXCpu.test` — 12 scenarios (cold miss/hit, conflict, bypass, reset, backpressure, error/no-refill, bypass isolation, duplicate request prevention, hit-path lowerReq suppression)
- **Functional regression**: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run`
- **Perf regression**: `make -C npc perf` + `make -C npc perf PERF_CHECK_STRICT=on`
- **Synthesis**: `make -C npc synth` at 100 MHz, 23,000 µm² budget
