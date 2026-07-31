# slides/c-stage-defense 页面审查表

| 页码 | 标题 | 目标时长 | 核心结论 | 数据来源 | 状态 |
| --- | --- | ---: | --- | --- | --- |
| 1 | 标题页 | 15s | 项目定位 | - | Ready |
| 2 | 目录与当前进度 | 25s | 讲述主线 | 仓库状态 | Ready |
| 3 | 一生一芯软硬件协同架构（1/3） | 35s | AM 与软件层 | `abstract-machine/*` | Ready |
| 4 | 一生一芯软硬件协同架构（2/3） | 40s | NEMU/NPC 对齐 | `nemu/*`, `npc/*` | Ready |
| 5 | 一生一芯软硬件协同架构（3/3） | 40s | Verilator / ysyxSoC / device | `ysyxSoC/*` | Ready |
| 6 | 程序从源码到 NPC 执行 | 50s | 变量驱动执行路径 | `npc/Makefile`, `platform/*.mk` | Ready |
| 7 | 总体微架构 | 60s | 顺序多周期，不是流水线 | `Top.scala`, `IFUnit.scala`, `IDUnit.scala`, `LoadAndStoreUnit.scala` | Ready |
| 8 | 控制流、CSR 与 RT-Thread 支撑 | 55s | ecall/mret trap 闭环（图） | `ControlUnit.scala`, `PCTargetController.scala` | Ready |
| 9 | 控制流、CSR 与 RT-Thread 支撑 | 40s | ecall/mret trap 闭环（代码） | `WBUnit.scala`, `ControlAndStatusRegisterFile.scala` | Ready |
| 10 | 顶层连接（Chisel） | 35s | stage 组合与 pc 更新 | `Top.scala` | Ready |
| 11 | Makefile 包含与调用关系 | 55s | include + recursive make | `Makefile`, `abstract-machine/Makefile`, `platform/*.mk`, `am-kernels/*/Makefile` | Ready |
| 12 | 一次 NPC 构建运行的时序 | 40s | chisel-gen → run/perf | `npc/Makefile`, `scripts/*.py` | Ready |
| 13 | 定量优化方法论 | 35s | 时间 = 指令数 × CPI × 周期 | 观察模型 | Ready |
| 14 | 前端性能证据链 | 45s | stage / stall 归因 | `npc/build/perf/perf.json`, `PerfSignalCollector.scala` | Ready |
| 15 | 前端性能证据链 | 40s | counter 表与结论 | `perf.json`, `perf.txt` | Ready |
| 16 | 后端综合 / STA 与微结构优化 | 55s | 面积 / 频率 / 关键路径 | `npc/build/synth/synth_summary.json`, `synth_summary.txt` | Ready |
| 17 | 后端综合 / STA 与微结构优化 | 45s | 候选优化方向 | `synth_summary.txt` | Ready |
| 18 | 当前阶段与下一步 | 45s | I-cache 之前 | 仓库状态 | Ready |
| 19 | 考核题目 | 15s | 占位页 | - | TODO |
| 20 | 题目实现与验证 | 15s | 占位页 | - | TODO |
| 21 | 个人特色展示 | 20s | 占位页 | - | TODO |
| 22 | 证据与复盘 | 20s | 占位页 | - | TODO |
| 23 | 总结 | 25s | 三条结论 | 全部 | Ready |
| 24 | 附录：CSR 与异常寄存器表 | 15s | 备答 | `ControlAndStatusRegisterFile.scala` | Ready |
| 25 | 附录：AXI4 与 counter 闭合检查 | 15s | 备答 | `LoadAndStoreUnit.scala`, `perf.cpp`, `PerfSignalCollector.scala` | Ready |
