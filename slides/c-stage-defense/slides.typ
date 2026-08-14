#import "@preview/touying:0.7.4": *
#import themes.simple: *

#show: simple-theme.with(
  aspect-ratio: "16-9",
  footer: ["一生一芯" C 阶段结业考核],
  config-info(
    title: ["一生一芯" C 阶段结业考核],
    subtitle: [从功能正确到数据驱动的 NPC 微结构优化],
    author: [TBD],
    date: [2026-07-31],
    institution: [ysyx-workbench],
  ),
)

#set text(font: "Noto Sans CJK SC", size: 13pt)
#set par(justify: false, leading: 0.32em)

#title-slide[
  = “一生一芯”C 阶段结业考核
  == 从功能正确到数据驱动的 NPC 微结构优化

  RV32 NPC · Chisel · Verilator · NEMU · AM · ysyxSoC · perf · synth / STA

  姓名：TBD · 学号：TBD · 日期：2026-07-31

  // Speaker notes:
// - Main point: 这不是通用 RISC-V 介绍，而是当前仓库的真实实现与优化证据。
// - Time: 20 s
// - Likely question: 现在到底处于什么阶段？
// - Answer: C6 之后，已进入 B1/B2/B3；第一版 blocking I-cache RTL 已接入，正在用 locality + perf 准备下一步 DSE。
//
]

== 目录与当前进度

- 目录
  - 项目协同架构
  - NPC 微架构
  - 工程构建与验证
  - 考核题目与个人特色

- 当前进度
  - #link("https://ysyx.oscc.cc/docs/2306/basic/1.9.html")[B3] 性能瓶颈分析完成
  - 第一版 blocking I-cache RTL 与 cache-specific perf observability 已就位

  // Speaker notes:
// - Main point: 先把整套协同闭环讲清，再讲 NPC 和数据驱动优化。
// - Time: 30 s
// - Likely question: 为什么不直接讲 cache？
// - Answer: 因为现在不只是“想做 cache”，而是已经有 locality 证据、真实 RTL 和 cache-specific perf 观测链路。
//

== 一生一芯软硬件协同架构（1/3）

#image("assets/architecture_1.svg", width: 98%)

- AM 把应用和机器实现解耦
- 这一层决定“程序怎么写”而不是“硬件怎么搭”
  - （软件层与硬件层解耦）

  // Speaker notes:
// - Main point: 先讲软件层与 AM 边界。
// - Time: 45 s
// - Likely question: AM 只是运行库吗？
// - Answer: 不是，它同时定义了 TRM / IOE / CTE / VME 接口。
//

== 一生一芯软硬件协同架构（2/3）

#image("assets/architecture_2.svg", width: 98%)

- NEMU 是 ISA 级参考模型
- NPC 是当前仓库的 RTL 实现
- DiffTest / Trace 在两者之间做功能对齐

  // Speaker notes:
// - Main point: NEMU 负责参考，NPC 负责实现，DiffTest 负责对齐。
// - Time: 50 s
// - Likely question: 为什么 NEMU 不是“编译器”？
// - Answer: 它是模拟器 / 调试器，不是代码生成器。
//

== 一生一芯软硬件协同架构（3/3）

#image("assets/architecture_3.svg", width: 90%)

- Verilator 把 RTL 变成可执行的 C++ 模型
- ysyxSoC 把 NPC 放进更接近真实芯片的总线和外设环境

  // Speaker notes:
// - Main point: 仿真与 SoC 环境是为了把“能跑”推进到“能测、能优”。
// - Time: 50 s
// - Likely question: 为什么要同时有 standalone 和 ysyxSoC？
// - Answer: 前者用于纯 CPU 调试，后者用于系统级真实性。
//

== 程序从源码到 NPC 执行

- 源码 → AM 构建 → ELF/BIN → `IMG` → Verilator 仿真 → NPC 执行 → Trace / Perf
- 关键开关：`RUN_CONFIG_SIM_MODE` / `RUN_CONFIG_PERF` / `RUN_CONFIG_WAVE` / `RUN_CONFIG_DIFFTEST`
- `NPC_CONFIG_*` 由 `RUN_CONFIG_*` 转写到仿真二进制环境

  // Speaker notes:
// - Main point: 运行路径必须从 Makefile 变量出发，而不是从 README 想象。
// - Time: 60 s
// - Likely question: 哪些配置属于 make 层，哪些进入仿真层？
// - Answer: `RUN_CONFIG_*` 在 make 层整理，映射成 `NPC_CONFIG_*` 给仿真程序。
//

== 总体微架构

#image("assets/microarchitecture.svg", width: 98%)

- `Top.pc_r` 只在 `WBU.done` 时经 `pcTargetOut` 提交下一条 PC，因此当前结构是 *single-instruction-in-flight*，不是传统五级并行流水
- `IFU → I-cache → LSU` 与 `MEMU → LSU` 是两条独立访存 client path；`LSU` 是唯一对外 `AXI4 master` 出口

  // Speaker notes:
// - Main point: 阶段名是生命周期描述，不代表并行流水线。
// - Time: 90 s
// - Likely question: 为什么性能表里还有 IF/ID/EX/MEM/WB？
// - Answer: 那是性能归因分段，不是并行结构。
//

== 控制流、CSR 与 RT-Thread 支撑

#image("assets/trap_flow.svg", width: 100%)

```scala
when(prevStageData.ecallEnable) {
  io.csrWritePort1.writeAddress := ControlAndStatusRegisterFile.CSR_MEPC.U
  io.csrWritePort2.writeAddress := ControlAndStatusRegisterFile.CSR_MCAUSE.U
}
```

- `mtvec / mepc / mcause / mstatus` 已在 CSR 文件中接通
- `mret` 通过 `epcRecoverEnable` 恢复到 `mepc`

  // Speaker notes:
// - Main point: ecall 和 mret 的 trap 闭环已经落在真实代码里。
// - Time: 75 s
// - Likely question: 这里实现了哪些 CSR？
// - Answer: mstatus / mtvec / mepc / mcause / mtval，以及只读 mvendorid / marchid。
//

== 顶层连接（Chisel）

Chisel 顶层做的不是“把几个 stage 摆在一起”，而是把 *阶段内部结果 → stage bundle → 共享访存接口* 这条链路真正接起来。

```scala
val ifu = Module(new IFUnit(xLen))
val idu = Module(new IDUnit(xLen))
val exu = Module(new EXUnit(xLen))
val memu = Module(new MEMUnit(xLen))
val wbu = Module(new WBUnit(xLen))
val lsu = Module(new LoadAndStoreUnit(xLen))

DecoupledIOConnect(ifu.io.nextStage, idu.io.prevStage, DecoupledIOConnect.Pipeline)
DecoupledIOConnect(idu.io.nextStage, exu.io.prevStage, DecoupledIOConnect.Pipeline)
DecoupledIOConnect(exu.io.nextStage, memu.io.prevStage, DecoupledIOConnect.Pipeline)
DecoupledIOConnect(memu.io.nextStage, wbu.io.prevStage, DecoupledIOConnect.Pipeline)
lsu.io.memBus <> master
```

- 五个 stage 各自是独立 FSM，通过 `IF_ID_Bundle / ID_EX_Bundle / EX_MEM_Bundle / MEM_WB_Bundle` 串起来，每一段都只把“这一拍算完的结果”交给下一段
- 数据流可以看一个代表例子：`ALU.io.alu` 不会直接跑到 Top，而是先进入 `EX_MEM_Bundle.aluOutput`，再进入 `MEM_WB_Bundle.aluOutput`，最后才由 `WBU` 选择写回 `gprWritePort.writeData`
- 控制流走的是同一条路：`EXU` 先产出 `pcTarget`，经过 `EX_MEM_Bundle` 和 `MEM_WB_Bundle` 传到 `WBU.io.pcTargetOut`，Top 再在 `done` 时回写 `pc_r`
- 所以这一页最想强调的结论其实只有一句：执行单元的结果先在 stage 之间层层传递，*不会直接跳到顶层端口*

#pagebreak()

== 顶层连接（Chisel） (cont'd)

- 真正暴露成 SoC 总线接口的，*只有* 访存请求：
  - `IFU` 发的是 `lsuIfetchReq`，不是 AXI
  - `MEMU` 发的是 `lsuMemReq`，也不是 AXI
  - `LSU` 仲裁之后才驱动 `io.memBus.ar/aw/w/...`，所以它是 *唯一 AXI4 master 出口*
- 离开 CPU 以后，这条访存路径一路串上去：`LSU.io.memBus` → `Top.master` → `ysyx_25070190.io_master` → `CPU.masterNode` → `ysyxSoCASIC` 的 `xbar`
- 这里名字能直接对上。因为 Top 用 `desiredName = "ysyx_25070190"` 生成 Verilog，而 `ysyxSoC/src/CPU.scala` 又用同名 `BlackBox` 去实例化它；所以 SoC 看到的 AXI4 顶层引脚，本质上就是这条 LSU 访存路径被摊平后的结果。（Chisel特性重命名Top module，#link("https://ysyx.oscc.cc/docs/2306/basic/1.11.html")[B5]讲义提及）

- 关键文件：`npc/vsrc-chisel/.../Top.scala`、`stage/IFUnit.scala`、`stage/EXUnit.scala`、`stage/MEMUnit.scala`、`stage/WBUnit.scala`、`LoadAndStoreUnit.scala`、`ysyxSoC/src/CPU.scala`

  // Speaker notes:
// - Main point: 顶层由 Chisel 组合各个 stage，而不是黑盒拼接。
// - Time: 45 s
// - Likely question: 谁负责 pc 更新？
// - Answer: `WBU` 产出 `pcTargetOut`，顶层在 `done` 时回写 `pc_r`。
//

== Makefile 包含与调用关系

#image("assets/build_flow.svg", width: 80%)

```make
chisel-gen:
	@$(MAKE) -C $(CHISEL_DIR) $(CHISEL_TARGET)
	@rm -rf $(NPC_GEN_DIR)
	@cp -r $(CHISEL_GEN_OUT) $(NPC_GEN_DIR)

run: $(BIN) git_commit_sim
	IMG=$(IMG) $(RUN_ARGS) $(BIN)
```

- `abstract-machine/Makefile` 是 AM 的核心构建 hub
- `platform/ysyxsoc.mk` → `make -C $(NPC_HOME) run`
- `platform/npc.mk` → `RUN_CONFIG_SIM_MODE=standalone`
- `microbench/Makefile` 只有 `NAME / SRCS / include $(AM_HOME)/Makefile`

  // Speaker notes:
// - Main point: 真正的构建关系是 include + recursive make 组合，不是单个入口。
// - Time: 80 s
// - Likely question: root Makefile 有什么作用？
// - Answer: 只是 tracer / git_commit 机制，不负责普通构建。
//

== 一次 NPC 构建运行的时序

1. NPC 自己的三个入口：
  - `make -C npc run` → `chisel-gen` → Mill/Chisel → generated SV → Verilator → `npc-runner` → 用 `IMG=<bin>` 启动仿真
  - `make -C npc synth` → `chisel-clean` → `chisel-gen RUN_CONFIG_DPI=off` → `scripts/synth.sh` → `make -C yosys-sta syn sta` → 产出 `synth_summary.json / optimization_hotspots.txt`
  - `make -C npc perf` → 先 synth 固定 RTL 快照，再重建 DPI-on 模拟器，跑 microbench，最后聚合 `perf.json`
2. AM 平台怎么把程序送进 NPC：
  - 共同前半段都是 `源文件 → .o → ELF → objcopy 得到 .bin → insert-arg.py 写 mainargs`
  - `ARCH=riscv32e-npc` 走 `abstract-machine/scripts/platform/npc.mk`：最后递归执行 `make -C npc run IMG=<bin> RUN_CONFIG_SIM_MODE=standalone`
  - `ARCH=riscv32e-ysyxsoc` 走 `abstract-machine/scripts/platform/ysyxsoc.mk`：最后同样递归执行 `make -C npc run IMG=<bin>`，但不显式传 `SIM_MODE`，因此落到默认的 `ysyxsoc` SoC 模式
3. 总结：两条 AM 路径的关键区别不在“前面怎么编译 C 程序”，而在“最后把同一个程序交给 bare NPC 还是交给 `ysyxSoCFull` 整个平台去跑”

  // Speaker notes:
// - Main point: 这一页要说明两件事：NPC 自己有 run / synth / perf 三条入口；AM 平台最终都会把程序变成 bin，再递归进 NPC 仿真。
// - Time: 65 s
// - Likely question: 为什么 perf 先 synth？
// - Answer: 因为要把性能数据和后端数据放在同一份 RTL 快照上比较；而 standalone 和 ysyxsoc 的差别，则是在最后选择哪个仿真顶层。
//

== 定量优化方法论

- `执行时间 = 动态指令数 × CPI × 单周期时间`
- 在这套 NPC 里，这不是纸面公式：动态指令数看 `instret`，CPI / IPC 看 `perf.json`，单周期时间看综合后的 `WNS / Fmax`
- 真正决定优化顺序的不是“哪个点看起来能改”，而是 Amdahl 定律：`Speedup = 1 / ((1 - f) + f / S)`，先抓占比最大的那一段时间 `f`
- 对当前 RTL 来说，最大的 `f` 不是算术单元，而是 *instruction supply* 和访存等待：所以后面才会先看 `ifetch wait_resp`、`mem wait_resp`、locality 和 I-cache
- 这也把优化分成两类：
  - *降 CPI*：减少等待和结构冲突，比如 I-cache、LSU 访存组织、共享存储口仲裁、store 路径串行开销
  - *降单周期时间 / 省面积*：缩短关键路径、为后续结构留预算，比如 CSR 读口简化、写回 mux 裁剪、共享加法器、stage payload 压缩
- 所以我现在的做法不是“看到哪就改哪”，而是先用 perf 找出大头，再用 synth / STA 判断这些改动值不值得、代价有多大

  // Speaker notes:
// - Main point: 公式只负责告诉我“时间由哪三部分组成”，Amdahl 负责告诉我“现在先改哪一部分最值”。
// - Time: 60 s
// - Likely question: 你为什么不只看仿真周期？
// - Answer: 因为真正的优化要同时看前端 CPI、后端时序，以及这两个方向之间的面积代价和收益上限。
//

== 前端性能证据链

#image("assets/perf_stage.svg", width: 90%)

#table(
  columns: (1.2fr, 1fr),
  [*指标*], [*数值*],
  [cycles], [32,418,542],
  [instret], [990,316],
  [IPC], [0.0305],
  [Stall%], [93.9%],
  [IF wait_resp], [11,102,170],
  [MEM wait_resp], [7,729,865],
)

- 分层 performance counters 通过 `PerfSignalCollector` / `PerfMonitor` 汇总（当前 `123` 个事件）
- `RUN_CONFIG_PERF=on` 开启
- `perf.json` 与 `perf.txt` 是当前仓库的真实输出

  // Speaker notes:
// - Main point: 时间主要消耗在取指等待与访存等待。
// - Time: 85 s
// - Likely question: 为什么 IPC 这么低？
// - Answer: 这是多周期、顺序执行设计，不能拿流水线的 IPC 直接类比。
//

== 后端综合 / STA 与微结构优化

#image("assets/backend_area.svg", width: 90%)

#pagebreak()

#table(
  columns: (1.25fr, 1fr),
  [*指标*], [*数值*],
  [area], [14,972.87 µm²],
  [budget], [23,000 µm²],
  [utilization], [65.1%],
  [cells], [7,253],
  [WNS], [3.38 ns],
  [derived Fmax], [151 MHz],
  [data_reg2reg Fmax], [920 MHz],
)

- 当前已从“凭感觉优化”转向“前后端共同驱动”
- 候选方向：CSR 多读口简化 / 写回 mux 裁剪 / 共享加法器
- 这里讲的是“候选”，不是“已经完成的优化”

  // Speaker notes:
// - Main point: 优化决策来自前端证据 + 后端实现证据的交集。
// - Time: 85 s
// - Likely question: 现在最该先优化什么？
// - Answer: 先释放面积，再为后续 I-cache 留预算。
//

// NOTE: slides use current RTL semantics rather than stale docs.

== 从访存轨迹中发现 Instruction Locality

#text(size: 10.5pt, fill: rgb("#475569"))[*Evidence → Opportunity* · 仅看 “instruction cache 值不值得做”，不把 locality-only sweep 当成最终 architecture 结论。]

#grid(
  columns: (1fr, 1fr, 1fr),
  gutter: 0.14cm,
  [
    #box(width: 100%, inset: 8pt, fill: rgb("#f8fafc"), stroke: 1.2pt + rgb("#cbd5e1"), radius: 8pt)[
      #text(weight: "bold", size: 11.5pt)[Hot address regions]
      #image("solid-assets/address_time_ifetch.png", width: 100%)
      #text(size: 9.6pt)[*Question:* instruction fetch 会不会反复回到少数地址段？]\
      #text(size: 9.6pt)[*Conclusion:* 会。measured IFetch trace 长时间停留在少数热点带。]
    ]
  ],
  [
    #box(width: 100%, inset: 8pt, fill: rgb("#f8fafc"), stroke: 1.2pt + rgb("#cbd5e1"), radius: 8pt)[
      #text(weight: "bold", size: 11.5pt)[Working-set locality]
      #image("solid-assets/working_set_over_time.png", width: 100%)
      #text(size: 9.6pt)[*Question:* 活跃 footprint 是否明显小于字节级完整映像？]\
      #text(size: 9.6pt)[*Conclusion:* 是。按 cache line 聚合后更小、更平滑，热点集合明显收敛。]
    ]
  ],
  [
    #box(width: 100%, inset: 8pt, fill: rgb("#f8fafc"), stroke: 1.2pt + rgb("#cbd5e1"), radius: 8pt)[
      #text(weight: "bold", size: 11.5pt)[Spatial opportunity]
      #image("solid-assets/spatial_line_utilization.png", width: 100%)
      #text(size: 9.6pt)[*Question:* 相邻 instruction 将来是否值得一起抓进同一 line？]\
      #text(size: 9.6pt)[*Conclusion:* 值得。line size 到 `64 B` 时利用率仍接近满载。]
    ]
  ],
)

#v(0.12cm)
#box(width: 100%, inset: 9pt, fill: rgb("#eff6ff"), stroke: 1.6pt + rgb("#2563eb"), radius: 8pt)[
  #text(weight: "bold", size: 12pt)[Conclusion]\
  #text(size: 11pt)[局部性分析不是直接给出“最佳 cache 参数”，而是证明：当前 workload 的 instruction supply 确实存在值得硬件利用的重复 fetch 结构。]\
  #text(size: 9.2pt, fill: rgb("#64748b"))[Locality-only sweep ≠ final cache choice; refill cost, traffic, area, timing, and total miss time still belong to the next step.]
]

  // Speaker notes:
// - Main point: 为什么值得做 I-cache？不是因为“cache 通常有用”，而是 perf 已经告诉我们 instruction supply 是主要 stall 来源，而 PC trace / working-set / spatial evidence 说明这些 fetch 不是完全随机的。
// - Time: 55 s
// - Likely question: 为什么不直接把 locality sweep 的最低 miss-rate 当成最终参数？
// - Answer: 因为 miss rate 只覆盖一个维度，后面还要结合 refill cost、lower-memory traffic、area、timing 和 total miss time 一起看。

== 从 Locality Insight 到真实 I-cache RTL

#text(size: 10.5pt, fill: rgb("#475569"))[*Implementation → Observation → Next* · I-cache 已经不是“下一步”，而是当前 NPC 顶层里真实接在 `IFU` 和 `LSU` 之间的 RTL module。]

#v(0.12cm)
#grid(
  rows: (0.75fr, 1fr),
  [
    #grid(
      columns: (1.45fr, 1fr),
      gutter: 0.2cm,
      [
        #box(width: 100%, inset: 10pt, fill: rgb("#f8fafc"), stroke: 1.2pt + rgb("#cbd5e1"), radius: 8pt)[
            #text(weight: "bold", size: 12pt)[Request path in the current RTL]
            #set text(font: "DejaVu Sans Mono", size: 9.2pt)

            ```
            PC
              ↓
            IFU
              │ cpuReq
              ▼
            I-cache (tag / valid / data)
              ├─ hit                → cpuResp
              ├─ miss (cacheable)   → lowerReq / lowerResp → refill → cpuResp
              └─ bypass             → lowerReq / lowerResp → forward → cpuResp
            ```

            blocking rule: state != idle 时不接收新请求
            lower-memory side: single outstanding transaction only
            #set text(font: "Noto Sans CJK SC", size: 9.1pt)
            #v(0.08cm)
            #text(size: 9.1pt)[当前是 *blocking frontend*：hit 直接返回；miss / bypass 都会占住前端直到 lower memory 响应。]
            #text(size: 9.1pt)[当前 line size 只有 `4 B`，第一版 RTL 先把“重复 PC 命中”做成真实可测行为。]
          ]
      ],
      [
        #box(width: 100%, inset: 8pt, fill: rgb("#f8fafc"), stroke: 1.2pt + rgb("#cbd5e1"), radius: 8pt)[
          #text(weight: "bold", size: 12pt)[Current RTL]
          #set text(size: 9pt)
          #table(
            columns: (1fr, 1.15fr),
            align: left,
            [*Capacity*], [32 B (`8 × 4 B`)],
            [*Line size*], [4 B (`1` instruction / line)],
            [*Associativity*], [1-way (direct-mapped)],
            [*Replacement*], [index conflict → overwrite],
            [*Frontend*], [blocking, single outstanding],
            [*Refill*], [single-word, one lower req per line],
            [*Cacheable*], [Flash / PSRAM / SDRAM],
            [*Bypass*], [MROM / SRAM / MMIO / unknown],
          )
        ]

      ],
    )
  ],
  [
    #box(width: 100%, inset: 8pt, fill: rgb("#f8fafc"), stroke: 1.2pt + rgb("#cbd5e1"), radius: 8pt)[
      #text(weight: "bold", size: 12pt)[Observable behavior now]
      #set text(size: 9.2pt)
      - `icache.request / hit / miss / bypass`
      - `icache.lower_req / lower_resp / refill`
      - `icache.refill_word / miss_wait / total_miss_time`
      - counters are part of a hierarchical perf table with `123` current events
      #set text(font: "DejaVu Sans Mono", size: 9.6pt)
      #v(0.06cm)
      #text(weight: "bold", size: 10pt, font: "Noto Sans CJK SC")[10-second hit / miss story]
      0x3000_0000  → miss   → lower req/resp → line valid
      0x3000_0000  → hit    → served by cache
      0x3000_0004  → new line (current RTL line = 4 B)
      #set text(font: "Noto Sans CJK SC", size: 10pt)
      #v(0.04cm)
      #text(size: 9.2pt)[Next: 让 CacheSim / DSE 回答“下一版 cache 应该长成什么样”。]
    ]
  ]
)

// Speaker notes:
// - Main point: 现在到底实现了什么？答案是：一个 direct-mapped、4 B line、8 entries、blocking、single-outstanding 的第一版 I-cache，cacheable 区域只包含 Flash / PSRAM / SDRAM，其余一律 bypass。
// - Time: 65 s
// - Likely question: 为什么不直接把 locality sweep 的 64 B line 候选做成当前 RTL？
// - Answer: 因为当前第一版 RTL 的目标是先建立真实 hit/miss/bypass/refill 观测链路；更宽 line 是否值得，要再结合 refill traffic、miss penalty、area、timing 与 total miss time 做下一轮判断。

== 考核题目

PLACEHOLDER — 考核前三天收到题目后替换

题目：
[收到邮件后填写]

问题背景：
[占位]

需要说明的核心机制：
1. [...]
2. [...]
3. [...]

涉及模块：
[...]

验证方法：
[测试 / 波形 / DiffTest / assertion]

  // Speaker notes:
// - Main point: 这里保持完全可替换，不虚构题目。
// - Time: 15 s
// - Likely question: 还没收到题目时讲什么？
// - Answer: 这页直接说明占位即可。
//

== 题目实现与验证

PLACEHOLDER — 题目收到后再填入

- 关键代码
- 时序图
- 波形
- 测试结果
- 遇到的问题
- 结论

  // Speaker notes:
// - Main point: 这页是后填内容，不应在当前阶段编造结果。
// - Time: 15 s
// - Likely question: 为什么保留空白页？
// - Answer: 为了答辩时直接替换，不破坏整体结构。
//

== 个人特色展示

方向 B：工具 / 优化 / Bug

- 前端性能观测：分层 counters 目前累计到 `123` 个事件；在 microbench 上退休了 `202,571,599` 条指令，`IPC = 0.0327`，`93.4%` 周期都落在 stall 里
- 后端综合链路：`make synth` / `synth-search` / `synth-exp-*` / `synth-flow-diff`，当前 `100 MHz` 约束下做到 `150 MHz` 的 `Fmax`，`WNS = +3.36 ns`、`TNS = 0`
- 报告阅读顺序：先看面积预算 `14,761.4 / 23,000 µm² = 64.2%`，再看 `Top Contributors`、`Cell Class`、`High-Fanout Nets`、`Constraint Coverage`
- 结论落点：把“哪里慢、哪里大、哪里不闭合”翻成微结构动作，比如 `ifetch wait_resp = 47.6%`、`mem wait_resp = 9.0%`，优先补流水和优化访存组织
- 自动闭合检查：`perf` 侧还能看 `RS2` 有 `56.4%` 未使用、`0` 个结构/`muldiv` stall；工具把这些守恒关系固定住，避免“看起来对、实际不对”

  // Speaker notes:
// - Main point: 这一页只讲方向 B，用具体数字说明工具链如何把性能和综合结果串起来。
// - Time: 20 s
// - Likely question: 为什么不选复杂应用？
// - Answer: 这个阶段我更有可验证的成果是工具、分析和优化闭环，适合做成主线。
//

== 证据链与复盘

- 现象：microbench 只有 `IPC = 0.0327`，而且 `93.4%` 的周期都在 stall；另一方面综合后 `Fmax = 150 MHz`、`WNS = +3.36 ns`，说明问题主要不在“跑不起来”，而在“跑得慢”
- 定位：用 `perf.txt`（microbench 主证据）、`perf.json`（补充回归）、`synth_summary.json`、`optimization_hotspots.txt` 和 locality 报告交叉确认，优先盯 `ifetch wait_resp = 47.6%`、`mem wait_resp = 9.0%`、`in2out` 最坏路径
- 证据：`14,761.4 µm²` 面积里，顺序逻辑占 `59.1%`，组合逻辑占 `32.8%`；`Top 3` 高扇出网分别是 `70 / 32 / 32`
- 方案：按证据选择补流水、拆长路径、减少无效状态、调整寄存器 / 缓存 / 总线组织；例如 `RS2` `56.4%` 未使用、`ALU+PC` 约 `19.0%` 并发，能直接指导算术和发射组织
- 对比：优化前后对比 CPI / IPC、Fmax、面积、stall 分布和热点排序；当前已经做到 `100 MHz -> 150 MHz` 的 timing headroom 和 `64.2%` 面积利用率
- 复盘：先让工具说话，再决定改结构；每次改动都保留可复查的文本证据，便于把一次修复沉淀成下次优化的判断标准

  // Speaker notes:
// - Main point: 复盘不讲空泛结论，只讲“现象—证据—决策”三段式，并把数字写进结论里。
// - Time: 20 s
// - Likely question: 这页和前一页有什么区别？
// - Answer: 前一页讲能力，这一页讲怎么把能力变成可验证的优化判断。
//

== 总结

1. 建立了 AM、NEMU、NPC 与 ysyxSoC 的完整协同闭环
2. NPC 已支持复杂软件运行，并具备系统化的性能观测能力
3. 已从“凭感觉优化”转向“前后端数据共同驱动”，并把 locality insight 落成了第一版可观测的 I-cache RTL

正确性决定“能不能运行”，性能证据决定“下一步该优化什么”。

  // Speaker notes:
// - Main point: 结论只保留三条，不再堆模块名。
// - Time: 30 s
// - Likely question: I-cache 定调后下一步最关键的风险是什么？
// - Answer: cache 参数与面积 / 关键路径预算之间的权衡。
//

== 附录：CSR 与异常寄存器表

#table(
  columns: (1fr, 1.6fr, 1fr),
  [*CSR*], [*作用*], [*当前状态*],
  [mstatus], [特权状态], [已实现],
  [mtvec], [trap 入口], [已实现],
  [mepc], [异常返回地址], [已实现],
  [mcause], [异常原因], [已实现],
  [mtval], [额外陷阱信息], [已实现],
  [mvendorid / marchid], [只读身份信息], [已实现],
)

== 附录：AXI4 与 counter 闭合检查

#table(
  columns: (1fr, 2fr),
  [*主题*], [*证据*],
  [AXI4 读写], [LSU 统一管理 IFetch 与数据访存],
  [取指闭合], [`ifetch.request == lsu_req == axi_ar == axi_r == response`],
  [结构闭合], [`inst-class-sum == instret`],
  [阶段闭合], [`state-sum == core.busy`],
  [风险提示], [当前 RAW hold slack 仍是检查项],
)
