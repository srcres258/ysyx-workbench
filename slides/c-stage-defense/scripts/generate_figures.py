#!/usr/bin/env python3
from __future__ import annotations

import json
import math
from pathlib import Path
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[3]
ASSETS = ROOT / "slides" / "c-stage-defense" / "assets"
PERF = ROOT / "npc" / "build" / "perf" / "perf.json"
SYNTH = ROOT / "npc" / "build" / "synth" / "synth_summary.json"

FONT_SIZES = {
    "title": 24,
    "subtitle": 14,
    "label": 15,
    "small": 13,
    "mono": 12,
}

DEFAULT_LINE_HEIGHTS = {
    "title": 30,
    "subtitle": 20,
    "label": 19,
    "small": 21,
    "mono": 18,
}


def load_json(path: Path) -> dict:
    return json.loads(path.read_text())


def perf_map(perf: dict) -> dict[str, int | float]:
    mapping: dict[str, int | float] = {
        "cycles": perf["cycles"],
        "instret": perf["instret"],
        "ipc": perf["ipc"],
    }
    for counter in perf.get("perf_counters", []):
        if isinstance(counter, dict) and "name" in counter:
            mapping[counter["name"]] = counter.get("value", 0)
    return mapping


def svg_doc(width: int, height: int, body: str) -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <defs>
    <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" fill="#4b5563"/>
    </marker>
    <style>
      .title {{ font: 700 24px 'Noto Sans CJK SC', sans-serif; fill: #111827; }}
      .subtitle {{ font: 500 14px 'Noto Sans CJK SC', sans-serif; fill: #374151; }}
      .label {{ font: 600 15px 'Noto Sans CJK SC', sans-serif; fill: #111827; }}
      .small {{ font: 13px 'Noto Sans CJK SC', sans-serif; fill: #374151; }}
      .mono {{ font: 12px 'DejaVu Sans Mono', monospace; fill: #111827; }}
      .box {{ fill: #f8fafc; stroke: #334155; stroke-width: 1.5; rx: 12; ry: 12; }}
      .soft {{ fill: #eff6ff; stroke: #2563eb; stroke-width: 1.5; rx: 12; ry: 12; }}
      .warn {{ fill: #fff7ed; stroke: #ea580c; stroke-width: 1.5; rx: 12; ry: 12; }}
      .muted {{ fill: #f3f4f6; stroke: #9ca3af; stroke-width: 1.2; rx: 12; ry: 12; stroke-dasharray: 6 4; }}
      .arrow {{ stroke: #4b5563; stroke-width: 2.1; fill: none; marker-end: url(#arrow); }}
      .thin {{ stroke: #64748b; stroke-width: 1.4; fill: none; marker-end: url(#arrow); }}
      .axis {{ stroke: #94a3b8; stroke-width: 1; }}
    </style>
  </defs>
  {body}
</svg>'''


def text_lines(s: str) -> list[str]:
    lines = s.splitlines()
    return lines or [""]


def resolved_line_height(cls: str, line_height: int | None = None) -> int:
    if line_height is not None:
        return line_height
    return DEFAULT_LINE_HEIGHTS.get(cls, DEFAULT_LINE_HEIGHTS["small"])


def text_height(s: str, cls="small", line_height: int | None = None) -> int:
    lines = text_lines(s)
    font_size = FONT_SIZES.get(cls, FONT_SIZES["small"])
    return font_size + max(0, len(lines) - 1) * resolved_line_height(cls, line_height)


def rect(x, y, w, h, klass, title, subtitle=None):
    body = [f'<rect class="{klass}" x="{x}" y="{y}" width="{w}" height="{h}"/>']
    body.append(text(x + w / 2, y + 24, title, "label", "middle", line_height=19))
    if subtitle:
        sy = y + h - 18 - text_height(subtitle, "small") + FONT_SIZES["small"]
        body.append(text(x + w / 2, sy, subtitle, "small", "middle"))
    return "\n  ".join(body)


def arrow(x1, y1, x2, y2, klass="arrow"):
    return f'<line class="{klass}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"/>'


def text(x, y, s, cls="small", anchor="start", line_height=22):
    lines = text_lines(s)
    line_height = resolved_line_height(cls, line_height)
    if len(lines) <= 1:
        return f'<text class="{cls}" x="{x}" y="{y}" text-anchor="{anchor}">{escape(s)}</text>'
    spans = [f'<tspan x="{x}" y="{y}">{escape(lines[0])}</tspan>']
    for line in lines[1:]:
        spans.append(f'<tspan x="{x}" dy="{line_height}">{escape(line)}</tspan>')
    return f'<text class="{cls}" text-anchor="{anchor}">' + "".join(spans) + '</text>'


def text_width(s: str, cls="small") -> int:
    font_size = FONT_SIZES.get(cls, FONT_SIZES["small"])

    def char_width(ch: str) -> float:
        if ch.isspace():
            return font_size * 0.35
        if ord(ch) < 128:
            return font_size * 0.62
        return font_size * 1.0

    widths = [sum(char_width(ch) for ch in line) for line in text_lines(s)]
    return math.ceil(max(widths, default=0))


def bar_chart(title, items, max_value, width=1200, height=700, kind="cycle"):
    top = [text(40, 38, title, "title")]
    chart_x, chart_y = 60, 90
    bar_w = 700
    row_h = 54
    top.append(arrow(chart_x, chart_y - 15, chart_x + bar_w, chart_y - 15, "thin"))
    top.append(text(chart_x, chart_y - 28, "0", "small"))
    top.append(text(chart_x + bar_w, chart_y - 28, f"{max_value:,}", "small", "end"))
    for i, (name, value, color) in enumerate(items):
        y = chart_y + i * row_h
        top.append(text(chart_x - 10, y + 20, name, "label", "end"))
        bw = 0 if max_value <= 0 else bar_w * value / max_value
        top.append(f'<rect x="{chart_x}" y="{y}" width="{bw:.1f}" height="26" rx="7" ry="7" fill="{color}"/>')
        top.append(text(chart_x + bw + 12, y + 19, f"{value:,}", "mono"))
    return svg_doc(width, height, "\n  ".join(top))


def architecture_svg() -> str:
    body = [text(40, 40, '一生一芯项目软硬件协同架构', 'title'), text(40, 66, '当前事实：AM 解耦软件与机器；NEMU 负责参考模型；NPC 负责 RTL；ysyxSoC 提供接近真实芯片的总线与外设环境。', 'subtitle')]
    body.append(rect(90, 110, 1020, 68, 'soft', '应用 / RT-Thread / microbench', '软件层'))
    body.append(arrow(600, 178, 600, 228))
    body.append(rect(90, 230, 1020, 68, 'box', 'Abstract Machine', 'AM 抽象层'))
    body.append(arrow(600, 298, 600, 348))
    body.append(rect(90, 350, 1020, 70, 'warn', 'ISA / 运行时接口边界', 'ABI · TRM · IOE · CTE · VME'))
    body.append(rect(100, 460, 430, 110, 'box', 'NEMU', '参考模型 / 调试器'))
    body.append(rect(560, 460, 430, 110, 'soft', 'NPC', 'RTL 处理器实现'))
    body.append(arrow(315, 570, 315, 620))
    body.append(arrow(775, 570, 775, 620))
    body.append(rect(90, 625, 1020, 68, 'muted', 'DiffTest / Trace', '功能对齐与观测'))
    body.append(arrow(600, 693, 600, 742))
    body.append(rect(90, 745, 1020, 74, 'box', 'Verilator C++ 仿真', 'RTL → 可执行模型'))
    body.append(arrow(600, 819, 600, 868))
    body.append(rect(90, 870, 1020, 74, 'box', 'ysyxSoC', 'AXI / APB / SRAM / SDRAM / Flash / UART / SPI'))
    body.append(arrow(600, 944, 600, 995))
    body.append(rect(90, 997, 1020, 68, 'soft', '设备与存储', 'SRAM · SDRAM · Flash · UART · SPI · GPIO · VGA'))
    body.append(text(112, 520, 'NEMU ↔ NPC 通过 DiffTest 对齐', 'small'))
    body.append(text(580, 520, 'NPC → ysyxSoC AXI4 master', 'small'))
    return svg_doc(1200, 1120, "\n  ".join(body))


def architecture_software_svg() -> str:
    body = [text(40, 40, '协同架构 (1/3)：软件层与 AM 边界', 'title')]
    body.append(rect(80, 120, 320, 82, 'soft', '应用 / RT-Thread / microbench', '软件层'))
    body.append(rect(470, 120, 250, 82, 'box', 'Abstract Machine', 'AM 抽象层'))
    body.append(rect(770, 120, 330, 82, 'warn', 'ISA / 运行时接口边界', 'TRM · IOE · CTE · VME'))
    body.append(arrow(400, 161, 470, 161))
    body.append(arrow(720, 161, 770, 161))
    body.append(text(100, 255, 'AM 的作用：', 'label'))
    body.append(text(100, 282, '• 把应用与机器实现解耦；', 'small'))
    body.append(text(100, 306, '• 让同一个程序可以在 NEMU、NPC、FPGA 风格平台上复用；', 'small'))
    body.append(text(100, 330, '• 当前的 benchmark 直接服务于性能分析，而不仅是正确性。', 'small'))
    return svg_doc(1200, 390, "\n  ".join(body))


def architecture_ref_svg() -> str:
    body = [text(40, 40, '协同架构 (2/3)：NEMU 参考模型与 NPC RTL', 'title')]
    body.append(rect(90, 128, 360, 98, 'box', 'NEMU', '参考模型 / 调试器'))
    body.append(rect(750, 128, 360, 98, 'soft', 'NPC', 'RTL 处理器实现'))
    body.append(rect(480, 130, 220, 92, 'muted', 'DiffTest / Trace', 'ISA 级对齐'))
    body.append(arrow(450, 177, 480, 177))
    body.append(arrow(700, 177, 750, 177))
    body.append(text(120, 270, 'NEMU 提供：', 'label'))
    body.append(text(120, 296, '• ISA 级参考执行；', 'small'))
    body.append(text(120, 320, '• DiffTest 时作为 REF；', 'small'))
    body.append(text(120, 344, '• 便于定位功能错误和 CSR / trap 差异。', 'small'))
    body.append(text(760, 270, 'NPC 提供：', 'label'))
    body.append(text(760, 296, '• 当前仓库的真实 RTL；', 'small'))
    body.append(text(760, 320, '• 可跑微基准与 ysyxSoC 场景；', 'small'))
    body.append(text(760, 344, '• 通过 trace / perf 与参考模型对齐。', 'small'))
    return svg_doc(1200, 390, "\n  ".join(body))


def architecture_soc_svg() -> str:
    body = [text(40, 40, '协同架构 (3/3)：Verilator、ysyxSoC 与设备层', 'title')]
    body.append(rect(70, 130, 280, 90, 'box', 'Verilator C++ 仿真', 'RTL → 可执行模型'))
    body.append(rect(390, 130, 250, 90, 'soft', 'ysyxSoC', 'AXI / APB 总线系统'))
    body.append(rect(690, 130, 120, 90, 'box', 'SRAM', 'local'))
    body.append(rect(830, 130, 120, 90, 'box', 'SDRAM', 'main'))
    body.append(rect(970, 130, 120, 90, 'box', 'Flash', 'boot'))
    body.append(rect(390, 260, 120, 74, 'box', 'UART', 'serial'))
    body.append(rect(530, 260, 120, 74, 'box', 'SPI', 'flash / xip'))
    body.append(rect(670, 260, 120, 74, 'box', 'GPIO', 'misc'))
    body.append(rect(810, 260, 120, 74, 'box', 'VGA', 'display'))
    body.append(rect(950, 260, 120, 74, 'box', 'PSRAM', 'ext'))
    body.append(arrow(350, 175, 390, 175))
    body.append(arrow(640, 175, 690, 175))
    body.append(arrow(810, 175, 830, 175))
    body.append(arrow(950, 175, 970, 175))
    body.append(text(70, 375, '这一步的意义：', 'label'))
    body.append(text(70, 401, '• 让 NPC 进入更接近芯片的总线 / 外设环境；', 'small'))
    body.append(text(70, 425, '• 兼顾功能验证、trace 观测与性能基准；', 'small'))
    body.append(text(70, 449, '• 也为后续 cache / 总线优化提供真实约束。', 'small'))
    return svg_doc(1200, 500, "\n  ".join(body))


def microarch_svg() -> str:
    body = [text(40, 40, 'NPC 微架构：单发射、顺序执行、多周期状态机', 'title'), text(40, 66, '五个阶段用于生命周期描述和性能归因，不等价于五级流水线并行。', 'subtitle')]
    body.append(rect(70, 135, 150, 78, 'soft', 'PC / Next-PC', '执行控制'))
    body.append(rect(260, 135, 140, 78, 'box', 'IFU', '取指 FSM'))
    body.append(rect(440, 135, 140, 78, 'box', 'IDU', '译码 / 读寄存器'))
    body.append(rect(620, 135, 140, 78, 'box', 'EXU', 'ALU / 分支 / 地址'))
    body.append(rect(800, 135, 160, 78, 'box', 'MEMU / LSU', 'AXI4 仲裁与访存'))
    body.append(rect(1000, 135, 120, 78, 'box', 'WBU', '写回 / trap'))
    body.append(arrow(220, 174, 260, 174))
    body.append(arrow(400, 174, 440, 174))
    body.append(arrow(580, 174, 620, 174))
    body.append(arrow(760, 174, 800, 174))
    body.append(arrow(960, 174, 1000, 174))
    body.append(rect(70, 260, 160, 92, 'muted', 'Control FSM', 'executing · pc_r'))
    body.append(rect(270, 260, 150, 92, 'box', 'GPR', '2R1W'))
    body.append(rect(450, 260, 170, 92, 'box', 'CSR', 'mstatus / mtvec / mepc / mcause'))
    body.append(rect(660, 260, 160, 92, 'box', 'PCTargetController', 'jump / branch / ecall / mret'))
    body.append(rect(860, 260, 150, 92, 'soft', 'AXI4 Master', '唯一总线出口'))
    body.append(rect(1040, 260, 110, 92, 'box', 'ysyxSoC', '外设总线'))
    body.append(arrow(215, 300, 270, 300))
    body.append(arrow(420, 300, 450, 300))
    body.append(arrow(620, 300, 660, 300))
    body.append(arrow(820, 300, 860, 300))
    body.append(arrow(1010, 300, 1040, 300))
    body.append(text(80, 395, '关键事实：', 'label'))
    body.append(text(80, 421, '• IFU/IDU/EXU/MEMU/WBU 都是独立 FSM；', 'small'))
    body.append(text(80, 444, '• executing=1 时串行推进；无并行流水寄存器；', 'small'))
    body.append(text(80, 467, '• LSU 统一管理 IFetch 与数据访存；', 'small'))
    body.append(text(80, 490, '• CSR / ecall / mret 已接入 RT-Thread 路径。', 'small'))
    return svg_doc(1200, 560, "\n  ".join(body))


def trap_svg() -> str:
    body = [text(40, 40, 'ecall → trap → mret：CSR / RT-Thread 支撑链', 'title')]
    xs = [60, 240, 420, 600, 780, 960]
    titles = ['ecall', '写 mepc / mcause', '跳 mtvec', 'RT-Thread 入口', '处理中', 'mret 恢复']
    subs = ['ControlUnit 识别', 'WBUnit 写 CSR', 'PCTargetController', '异常入口', '执行处理', 'epcRecover']
    colors = ['warn', 'box', 'soft', 'box', 'box', 'soft']
    for x, title, sub, cls in zip(xs, titles, subs, colors):
        body.append(rect(x, 130, 150, 96, cls, title, sub))
    for x1, x2 in zip(xs[:-1], xs[1:]):
        body.append(arrow(x1 + 150, 178, x2, 178))
    body.append(text(70, 285, '真实代码路径：', 'label'))
    body.append(text(70, 311, 'ControlUnit.scala 识别 ecall / mret', 'small'))
    body.append(text(70, 334, 'WBUnit.scala 写 mepc + mcause', 'small'))
    body.append(text(70, 357, 'PCTargetController.scala 选择 tvec / epc', 'small'))
    body.append(text(70, 380, 'ControlAndStatusRegisterFile.scala 暴露 mstatus/mtvec/mepc/mcause', 'small'))
    return svg_doc(1200, 430, "\n  ".join(body))


def build_flow_svg() -> str:
    body = [text(40, 40, 'Makefile 调用关系：include 与递归 make 的真实路径', 'title')]
    body.append(rect(50, 120, 185, 70, 'box', 'root Makefile', 'tracer / git_commit'))
    body.append(rect(270, 120, 255, 70, 'soft', 'abstract-machine/Makefile', '核心构建 hub'))
    body.append(rect(560, 120, 220, 70, 'box', 'scripts/platform/*.mk', '选择 NEMU / NPC / ysyxSoC'))
    body.append(rect(820, 120, 150, 70, 'box', 'NPC / NEMU', 'run / gdb'))
    body.append(rect(1000, 120, 150, 70, 'box', 'AM app', 'microbench / cpu-tests'))
    body.append(arrow(235, 155, 270, 155))
    body.append(arrow(525, 155, 560, 155))
    body.append(arrow(780, 155, 820, 155))
    body.append(arrow(975, 155, 1000, 155))
    body.append(rect(560, 250, 220, 78, 'muted', 'NPC mode', 'RUN_CONFIG_SIM_MODE=standalone'))
    body.append(rect(820, 250, 150, 78, 'muted', 'ysyxSoC mode', 'default'))
    body.append(rect(1000, 250, 150, 78, 'muted', 'NEMU mode', 'CONFIG_TARGET_AM'))
    body.append(text(70, 290, '关键事实：', 'label'))
    body.append(text(70, 316, '• root Makefile 只负责 tracer / git commit，不负责真正构建；', 'small'))
    body.append(text(70, 339, '• abstract-machine/Makefile include scripts/$(ARCH).mk，并递归构建 am/klib；', 'small'))
    body.append(text(70, 362, '• platform/ysyxsoc.mk → make -C npc run；platform/npc.mk → standalone NPC；', 'small'))
    body.append(text(70, 385, '• microbench 仅有 3 行：NAME、SRCS、include $(AM_HOME)/Makefile。', 'small'))
    return svg_doc(1200, 420, "\n  ".join(body))


def perf_svg(perf: dict) -> str:
    perf = perf_map(perf)
    items = [
        ("IF", perf["state.fetch.cycle"], "#3b82f6"),
        ("ID", perf["state.decode.cycle"], "#60a5fa"),
        ("EX", perf["state.execute.cycle"], "#93c5fd"),
        ("MEM", perf["state.memory.cycle"], "#f59e0b"),
        ("WB", perf["state.writeback.cycle"], "#fbbf24"),
    ]
    left_margin = 50
    label_gap = 24
    stage_label_right = left_margin + max(text_width(name, 'label') for name, _, _ in items)
    stage_bar_x = stage_label_right + label_gap
    stage_bar_w = 650
    body = []
    body.append(text(40, 40, '前端证据链：stage 归因 + stall 归因', 'title'))
    body.append(text(40, 67, '来源：npc/build/perf/perf.json（最新 perf run）', 'subtitle'))
    maxv = max(v for _, v, _ in items)
    x, y = stage_bar_x, 110
    body.append(text(50, y - 18, 'Stage cycles', 'label'))
    for i, (name, value, color) in enumerate(items):
        yy = y + i * 54
        body.append(text(stage_label_right, yy + 19, name, 'label', 'end'))
        bw = stage_bar_w * value / maxv
        body.append(f'<rect x="{x}" y="{yy}" width="{bw:.1f}" height="26" rx="7" ry="7" fill="{color}"/>')
        body.append(text(x + bw + 12, yy + 19, f'{value:,}', 'mono'))
    body.append(text(40, 410, 'Stall breakdown', 'label'))
    stall_items = [
        ('ifetch wait_resp', perf['stall.ifetch.wait_resp.cycle'], '#ef4444'),
        ('mem wait_resp', perf['stall.mem.wait_resp.cycle'], '#f97316'),
        ('req_blocked', perf['stall.mem.req_blocked.cycle'], '#94a3b8'),
        ('shared_mem', perf['stall.structural.shared_mem.cycle'], '#94a3b8'),
        ('muldiv_busy', perf['stall.muldiv.busy.cycle'], '#94a3b8'),
    ]
    stall_total = perf['core.stall.cycle']
    stall_label_right = left_margin + max(text_width(name, 'small') for name, _, _ in stall_items)
    sx, sy = stall_label_right + label_gap, 440
    stall_bar_w = 650
    for i, (name, value, color) in enumerate(stall_items):
        yy = sy + i * 36
        body.append(text(stall_label_right, yy + 18, name, 'small', 'end'))
        bw = stall_bar_w * (value / stall_total if stall_total else 0)
        body.append(f'<rect x="{sx}" y="{yy}" width="{bw:.1f}" height="20" rx="6" ry="6" fill="{color}"/>')
        body.append(text(sx + bw + 12, yy + 15, f'{value:,} ({value / stall_total * 100:.1f}%)', 'mono'))
    return svg_doc(1200, 640, "\n  ".join(body))


def backend_svg(synth: dict) -> str:
    body = [text(40, 40, '后端证据链：面积 / 频率 / 关键路径', 'title'), text(40, 67, '来源：npc/build/synth/synth_summary.json（最新 synth run）', 'subtitle')]
    area = synth['area_um2']
    budget = synth['area_budget_um2']
    util = area / budget * 100 if budget else 0
    area_summary = f'{area:.2f} µm² / {budget} µm²  ({util:.1f}%)'
    body.append(rect(60, 110, 500, 190, 'box', 'Area vs budget', area_summary))
    body.append(f'<rect x="100" y="190" width="420" height="34" rx="8" ry="8" fill="#e5e7eb"/>')
    body.append(f'<rect x="100" y="190" width="{420 * util / 100:.1f}" height="34" rx="8" ry="8" fill="#2563eb"/>')
    body.append(text(100, 170, '预算利用率', 'label'))
    footer_y = 110 + 190 - 18
    detail_text = 'data_reg2reg Fmax 920 MHz；reg2reg 在当前报表中为 N/A'
    detail_y = footer_y - text_height(area_summary, 'small') - 10
    label_y = detail_y - text_height(detail_text, 'small') - 10
    body.append(text(100, label_y, 'WNS 3.38 ns  →  Fmax 151 MHz', 'label'))
    body.append(text(100, detail_y, detail_text, 'small'))
    body.append(rect(600, 110, 540, 190, 'box', 'Area by cell class', 'sequential dominates the mapped netlist'))
    classes = synth['area_by_cell_class']
    rows = [('sequential', classes['sequential']['area_um2'], '#2563eb'), ('combinational', classes['combinational']['area_um2'], '#60a5fa'), ('mux', classes['mux']['area_um2'], '#f59e0b'), ('buffer/inverter', classes['buffer/inverter']['area_um2'], '#94a3b8')]
    mx = max(v for _, v, _ in rows)
    for i, (name, value, color) in enumerate(rows):
        yy = 155 + i * 34
        body.append(text(620, yy + 14, name, 'small'))
        body.append(f'<rect x="730" y="{yy - 2}" width="{250 * value / mx:.1f}" height="18" rx="6" ry="6" fill="{color}"/>')
        body.append(text(990, yy + 12, f'{value:.1f} µm²', 'mono', 'end'))
    body.append(rect(60, 340, 1080, 170, 'muted', 'Current conclusion', 'Setup meets 100 MHz; the current RTL now has an area headroom story, but the raw summary still carries hold slack as a separate check item.'))
    body.append(text(90, 388, f'cells = {synth["cell_count"]:,}', 'label'))
    body.append(text(250, 388, f'global Fmax = {synth["final_mhz"]} MHz', 'label'))
    body.append(text(500, 388, f'WNS = {synth["wns_ns"]:.2f} ns', 'label'))
    body.append(text(670, 388, f'tns = {synth["tns_ns"]:.1f} ns', 'label'))
    body.append(text(820, 388, f'budget = {util:.1f}% used', 'label'))
    body.append(text(90, 426, 'Optimization candidates from the data: CSR read simplification, writeback mux trimming, shared adder/resource pressure relief.', 'small'))
    return svg_doc(1200, 540, "\n  ".join(body))


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    perf = load_json(PERF)
    synth = load_json(SYNTH)
    outputs = {
        'architecture.svg': architecture_svg(),
        'architecture_1.svg': architecture_software_svg(),
        'architecture_2.svg': architecture_ref_svg(),
        'architecture_3.svg': architecture_soc_svg(),
        'microarch.svg': microarch_svg(),
        'trap_flow.svg': trap_svg(),
        'build_flow.svg': build_flow_svg(),
        'perf_stage.svg': perf_svg(perf),
        'backend_area.svg': backend_svg(synth),
    }
    for name, content in outputs.items():
        (ASSETS / name).write_text(content)
        print(f'wrote {ASSETS / name}')


if __name__ == '__main__':
    main()
