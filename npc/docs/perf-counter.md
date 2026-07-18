# NPC Performance Counter System

NPC 通过 `PerfDPIBundle` (Chisel) → `PerfMonitor` (C++) → TUI PerfPanel / summary dump
的路径，提供 26 个 polling-first 性能计数器以及 CPI / IPC / Stall% 派生指标。

## Configuration

`RUN_CONFIG_PERF` 通过 Makefile-var → env var → C++ `SimConfig::config_perf` 管道控制：

```bash
# 默认启用（无需显式指定）
make -C npc run IMG=path/to/program.bin

# 显式启用
make -C npc run RUN_CONFIG_PERF=on IMG=path/to/program.bin

# 禁用性能计数器
make -C npc run RUN_CONFIG_PERF=off IMG=path/to/program.bin

# 检查 Makefile→env var 管道（dry-run，不会启动仿真）
make -C npc -n run RUN_CONFIG_PERF=on RUN_CONFIG_TUI=off
```

**非法值会立即报错**——在 Verilator 启动之前终止：

```bash
$ make -C npc run RUN_CONFIG_PERF=banana
# [config] 无效的 RUN_CONFIG_PERF 值: "banana" (必须为 on 或 off)
```

## Naming Convention

Counter 对外名称使用 **dotted hierarchical** 风格：

```
domain.subdomain[.subdomain].metric
```

| 层级 | 说明 | 示例 |
|---|---|---|
| 顶级 domain | `core`, `inst`, `state`, `stall`, `mem`, `trap` | `core.cycle` |
| 子域 | 分类或 pipeline stage 特定 | `inst.class.alu` |
| metric | 单位指示 (`cycle`, `count`) | `core.cycle` (cycle 单位), `core.instret` (count 单位) |

**所有 26 个 counter 的定义仅维护在一处**：`npc/csrc/perf.cpp::kCounterTable`。
RTL 侧只暴露 `perf_*` 语义信号，不含 counter name 字符串。

## Polling vs Event-Triggered Boundary

| 机制 | 何时采样 | 负责什么 | 不负责什么 |
|---|---|---|---|
| **Polling** (`PerfMonitor::sampleCycle()`) | 每稳定时钟周期，posedge eval 后 | 全部 26 个 counter 的累加 | — |
| **Event-triggered DPI** (`dpi.cpp` callbacks) | 仅在 retire / mem access / exception 发生时 | 精确 metadata：PC、指令、地址，供 trace / difftest 使用 | 不做 counter 累加 |

两者**不重复统计同一事件**。Polling 负责计数，event-triggered DPI 只负责 metadata。

## Counter Table

> 所有 counter 定义来自 `npc/csrc/perf.cpp::kCounterTable` — 单一事实来源。

### Core

| # | Name | Unit | Description |
|---|---|---|---|
| 0 | `core.cycle` | cycle | Core execution cycles (running=1) |
| 1 | `core.instret` | count | Retired instructions |
| 2 | `core.busy.cycle` | cycle | Core busy cycles (not idle) |
| 3 | `core.stall.cycle` | cycle | Core stall cycles (busy but not committing) |

### Instruction Class

| # | Name | Unit | Description |
|---|---|---|---|
| 4 | `inst.class.alu.count` | count | ALU / integer compute instructions retired |
| 5 | `inst.class.load.count` | count | Load instructions retired |
| 6 | `inst.class.store.count` | count | Store instructions retired |
| 7 | `inst.class.branch.count` | count | Branch instructions retired |
| 8 | `inst.class.jal.count` | count | JAL instructions retired |
| 9 | `inst.class.jalr.count` | count | JALR instructions retired |
| 10 | `inst.class.csr.count` | count | CSR-access instructions retired |
| 11 | `inst.class.muldiv.count` | count | Multiply/divide instructions retired (hardwired 0 on RV32I) |

### Pipeline State

| # | Name | Unit | Description |
|---|---|---|---|
| 12 | `state.fetch.cycle` | cycle | Cycles IF stage was active |
| 13 | `state.decode.cycle` | cycle | Cycles ID stage was active |
| 14 | `state.execute.cycle` | cycle | Cycles EX stage was active |
| 15 | `state.memory.cycle` | cycle | Cycles MEM stage was active |
| 16 | `state.writeback.cycle` | cycle | Cycles WB stage was active |

### Stall

| # | Name | Unit | Description |
|---|---|---|---|
| 17 | `stall.ifetch.wait_resp.cycle` | cycle | IFU waiting for instruction-fetch response |
| 18 | `stall.mem.wait_resp.cycle` | cycle | LSU waiting for memory response |
| 19 | `stall.mem.req_blocked.cycle` | cycle | LSU request blocked (backpressure) |
| 20 | `stall.structural.shared_mem.cycle` | cycle | Structural hazard: IFU+LSU competing for shared memory |
| 21 | `stall.muldiv.busy.cycle` | cycle | Multi-cycle mul/div unit busy (hardwired 0 on RV32I) |

### Memory

| # | Name | Unit | Description |
|---|---|---|---|
| 22 | `mem.load.req.count` | count | Load requests fired to LSU |
| 23 | `mem.store.req.count` | count | Store requests fired to LSU |
| 24 | `mem.mmio.req.count` | count | MMIO/peripheral requests (CLINT plus UART/GPIO/Keyboard/VGA/SPI controller) |

说明：该计数器统计 CLINT 读写请求，以及通过 LSU 发往 SoC 外设（UART、GPIO、Keyboard、VGA、SPI controller）的请求次数；不包含普通存储器区域（如 SRAM/PSRAM/SDRAM/MROM/Flash）的访问。

### Trap

| # | Name | Unit | Description |
|---|---|---|---|
| 25 | `trap.exception.count` | count | Exceptions taken |

### Derived Metrics

仿真结束时 (`simExec()` 返回前) 或 TUI PerfPanel 中显示：

- **CPI** = `core.cycle / core.instret`（`instret == 0` 时显示 0）
- **IPC** = `core.instret / core.cycle`（`cycle == 0` 时显示 0）
- **Stall%** = `core.stall.cycle / core.cycle × 100%`（`cycle == 0` 时显示 0）

## TUI Usage

TUI PerfPanel 展示 7 行分组数据：Core、Derived (CPI/IPC/Stall%)、Inst Class、Pipeline State、Stall、Memory、Trap。

启动命令（需要真实交互终端；通过 `script` / 非交互式捕获会报告 `Terminal too small (0x0)`）：

```bash
make -C npc run \
  RUN_CONFIG_TUI=on \
  RUN_CONFIG_PERF=on \
  RUN_SDB_ENABLED=false \
  IMG=path/to/program.bin
```

Panel 通过 `TuiFrameModel::perfValues[26]` 读取，不直接访问 DPI。

## Verification — Microbench Smoke

禁用 trace / waveform / NVBoard 后运行 microbench test 规模，结束时打印 Perf Counter Summary：

```bash
nix develop --command make -C am-kernels/benchmarks/microbench \
  ARCH=riscv32e-ysyxsoc run mainargs=test \
  CONFIG_ITRACE=off CONFIG_MTRACE=off CONFIG_FTRACE=off \
  CONFIG_DTRACE=off CONFIG_ETRACE=off \
  CONFIG_WAVE=off CONFIG_NVBOARD=off
```

预期输出末尾包含：

```
─── Perf Counter Summary ───
  CPI  = x.xxxx  IPC  = x.xxxx  Stall% = xx.x%
  Domain totals:
    core:   cycle=...  instret=...  ...
    inst-class: alu=...  load=...  ...
    ...
```

> **注意**：默认 `ysyxsoc.mk` 会同时开启所有 5 个 trace + waveform + NVBoard，
> 造成极大的磁盘 I/O，仿真看起来像死掉。禁用这些特性后可正常跑完并输出汇总。

## How to Add a Counter

1. **定义**：在 `npc/csrc/perf.cpp::kCounterTable` 末尾追加一行 `{ ... }`，包含 `name` / `unit` / `definition` / `rawSource`。
2. **索引**：在 `npc/include/perf.hpp::Idx` 命名空间末尾添加 `constexpr size_t` 常量。
3. **RTL 信号**（如需要新信号）：在 `npc/vsrc-chisel/.../PerfDPIBundle.scala` 对应子 bundle 中添加 `Output(Bool())` 字段。
4. **信号接入**：在 `npc/vsrc-chisel/.../PerfSignalCollector.scala` 中连接实际硬件信号到 bundle 字段。
5. **采样**：如果新计数器落入已有 domain（如 `core`, `inst`, `state`, `stall`, `mem`, `trap`），在对应的 `accumulate*` 函数中添加 `m_values[Idx::NEW_COUNTER] += ...`。如果需要新 domain，增加对应的 accumulate 方法并在 `PerfMonitor::sampleCycle()` 模板中调用。
6. **更新表大小**：`PerfCounters::kNumCounters` 及其 `static_assert` 会自动通过 compile-time array size 检查一致性。
7. **重新生成 RTL + 构建**：
   ```bash
   nix develop --command make -C npc chisel-gen
   nix develop --command make -C npc
   ```

**不要做的**：
- 不要在 event-triggered DPI callback (`dpi.cpp`) 中对同一事件重复累加。
- 不要在多处重复维护 counter name 字符串——`kCounterTable` 是唯一来源。
- 不要修改生成目录 `npc/vsrc/generated/` 或 `npc/build/` 中的文件。
