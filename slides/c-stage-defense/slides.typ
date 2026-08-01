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
// - Answer: C6 之后，已进入 B1/B2/B3，正在做局部性分析与 I-cache 初步定调。
//
]

== 目录与当前进度

- 项目协同架构
- NPC 微架构
- 工程构建与验证
- 考核题目与个人特色

- 当前进度：B3 性能瓶颈分析完成，局部性分析与 I-cache 初步定调中

  // Speaker notes:
// - Main point: 先把整套协同闭环讲清，再讲 NPC 和数据驱动优化。
// - Time: 30 s
// - Likely question: 为什么不直接讲 cache？
// - Answer: 因为现在已经有 locality 数据在收敛 I-cache 候选，不能只写成抽象的下一步。
//

== 一生一芯软硬件协同架构（1/3）

#image("assets/architecture_1.svg", width: 98%)

- AM 把应用和机器实现解耦
- 这一层决定“程序怎么写”而不是“硬件怎么搭”

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

#image("assets/microarch.svg", width: 90%)

- 单发射、顺序执行、多周期状态机式 NPC
- `IFU / IDU / EXU / MEMU / WBU` 都是独立 FSM
- LSU 是唯一 AXI4 总线出口，统一管理取指与数据访存

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

```scala
val ifu = Module(new IFUnit(xLen))
val idu = Module(new IDUnit(xLen))
DecoupledIOConnect(ifu.io.nextStage, idu.io.prevStage, DecoupledIOConnect.Pipeline)
```

- `npc/vsrc-chisel/src/main/scala/top/srcres258/ysyx/npc/Top.scala`

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

- `make -C npc run` → `chisel-gen` → Mill/Chisel → generated SV → Verilator → C++ binary → `IMG`
- `make -C npc perf` → 先 synth，再重建 DPI-on 模拟器，再跑 microbench，再聚合 perf
- 运行模式：standalone / ysyxSoC

  // Speaker notes:
// - Main point: perf 流是串行化的，不能拿 DPI-free RTL 直接做性能结论。
// - Time: 55 s
// - Likely question: 为什么 perf 先 synth？
// - Answer: 因为要把性能数据和后端数据放在同一份 RTL 快照上比较。
//

== 定量优化方法论

- `执行时间 = 动态指令数 × CPI × 单周期时间`
- 动态指令数来自程序与编译
- CPI / IPC 反映微结构
- 单周期时间来自综合与 STA

  // Speaker notes:
// - Main point: 不能只盯着 IPC，也不能只盯着综合频率。
// - Time: 45 s
// - Likely question: 你为什么不只看仿真周期？
// - Answer: 因为真正的优化要同时看前端 CPI 和后端时序。
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

- 109 个 counter 通过 `PerfSignalCollector` / `PerfMonitor` 汇总
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

== 局部性分析与 I-cache 初步定调

- C6 / B1 / B2 / B3 已完成，进入 I-cache 候选收敛
- NPC 后端已完成取指与访存局部性分析
- microbench 的 address-time / reuse interval / spatial utilization / miss-rate curve 已经把设计空间摊开
- `cache_sweep` 首轮最佳点：4KB / 64B / 4-way，miss rate 0.32%

// #box(width: 100%, height: 2.6cm, stroke: 1.5pt + rgb("#94a3b8"), radius: 6pt)[
//   #align(center + horizon)[
//     #text(size: 20pt, weight: "bold")[Locality Analysis / I-cache Initial Tuning]
//   ]
// ]

- Current process: Locality Analysis / I-cache Initial Tuning

- 64B line utilization 98.1%，说明大 line 依然不浪费
- 结论：I-cache 先从高 locality 热路径定 line size / capacity，再落 RTL 验证
- 下一步补充 itrace / basic-block 热点分析

  // Speaker notes:
// - Main point: locality sweep 已经给出 I-cache 的首轮候选和数量级。
// - Time: 60 s
// - Likely question: 为什么这个 I-cache 方向可信？
// - Answer: 因为 hit rate、line utilization 和 reuse distance 三类证据都指向同一结论。

== Locality state

`microbench` 程序 (`test` 规模)

#v(0.12cm)
#grid(
  columns: (1fr, 1fr, 1fr),
  gutter: 0.12cm, 
  image("solid-assets/address_time_ifetch.png", width: 100%),
  image("solid-assets/address_time_data.png", width: 100%),
  image("solid-assets/reuse_interval.png", width: 100%),
  image("solid-assets/spatial_line_utilization.png", width: 100%),
  image("solid-assets/cache_miss_rate_curve.png", width: 100%),
  image("solid-assets/working_set_over_time.png", width: 100%),
  image("solid-assets/stride_distribution.png", width: 100%),
  image("solid-assets/region_cache_value.png", width: 100%)
)

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

方向 A：复杂应用

- 应用名称
- 软件栈
- 运行路径
- 关键设备
- 性能表现
- 最困难的问题

方向 B：工具 / 优化 / Bug

- 109 项性能计数器系统
- TUI 性能面板
- perf JSON schema
- 自动闭合检查
- 综合与 STA 报告聚合
- 数据驱动的面积—性能优化

  // Speaker notes:
// - Main point: 先保留结构，等后续再决定故事线。
// - Time: 20 s
// - Likely question: 最终选哪条线？
// - Answer: 先不定，留给后续 agent / 实际材料。
//

== 证据与复盘

- 问题现象
- 原因定位
- 使用的工具
- 关键证据
- 修复方案
- 修复前后对比
- 经验总结

PLACEHOLDER — 根据后续 agent 工作结果补充

  // Speaker notes:
// - Main point: 这页是复盘框架，不写虚构 bug。
// - Time: 20 s
// - Likely question: 有什么典型 bug 可讲？
// - Answer: 目前先留空，等后续证据补齐。
//

== 总结

1. 建立了 AM、NEMU、NPC 与 ysyxSoC 的完整协同闭环
2. NPC 已支持复杂软件运行，并具备系统化的性能观测能力
3. 已从“凭感觉优化”转向“前后端数据共同驱动”，局部性分析正在收敛 I-cache 候选

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
