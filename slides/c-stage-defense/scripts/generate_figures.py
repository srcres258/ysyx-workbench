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


def yield_flow_svg() -> str:
    body = [
        text(40, 40, 'yield-os：一次 yield 如何穿过 User → AM → NEMU', 'title'),
        text(40, 67, '以当前仓库的真实代码为准：yield 通过 ecall 进入 CTE，保存 architectural state，scheduler 只返回下一份 Context*，真正的切换在 trap.S 的 restore + mret。', 'subtitle'),
    ]

    lanes = [
        (110, 72, '#f8fafc', '#cbd5e1', 'yield-os / user program'),
        (192, 88, '#eff6ff', '#bfdbfe', 'AM API'),
        (290, 98, '#fff7ed', '#fdba74', 'RISC-V / NEMU trap'),
        (398, 104, '#f8fafc', '#cbd5e1', 'AM CTE / trap entry'),
        (512, 88, '#eff6ff', '#bfdbfe', 'yield-os scheduler'),
        (610, 102, '#f8fafc', '#cbd5e1', 'restore + resume'),
    ]
    for y, h, fill, stroke, label in lanes:
        body.append(f'<rect x="34" y="{y}" width="1132" height="{h}" rx="14" ry="14" fill="{fill}" stroke="{stroke}" stroke-width="1.1"/>')
        body.append(text(58, y + 30, label, 'label'))

    body.append(rect(250, 122, 760, 48, 'box', 'f(A) / f(B)  →  yield()', None))
    body.append(rect(250, 212, 760, 54, 'soft', 'yield(): RV32E 用 a5=-1；RV32I 用 a7=-1；然后 ecall', '-1 是 AM software convention；ECALL 才是 trap mechanism'))

    body.append(f'<rect x="250" y="312" width="760" height="54" rx="12" ry="12" fill="#fff7ed" stroke="#ea580c" stroke-width="1.5"/>')
    body.append(text(630, 336, 'NEMU / architectural trap', 'label', 'middle'))
    body.append(text(630, 358, 'mepc ← ECALL PC    mcause ← 11 (ECALL from M-mode)    PC ← mtvec', 'mono', 'middle', line_height=18))

    body.append(f'<rect x="250" y="420" width="340" height="58" rx="12" ry="12" fill="#f8fafc" stroke="#334155" stroke-width="1.5"/>')
    body.append(text(420, 444, 'Context snapshot', 'label', 'middle'))
    body.append(text(420, 466, 'gpr[]  |  mcause  |  mstatus  |  mepc', 'mono', 'middle', line_height=18))
    body.append(f'<rect x="640" y="420" width="370" height="58" rx="12" ry="12" fill="#eff6ff" stroke="#2563eb" stroke-width="1.5"/>')
    body.append(text(825, 444, '__am_irq_handle()', 'label', 'middle'))
    body.append(text(825, 466, 'decode EVENT_YIELD  →  call schedule(ev, prev)', 'small', 'middle'))

    body.append(rect(250, 528, 760, 56, 'soft', 'current->cp = prev  →  pick next PCB  →  return current->cp', 'first yield: pcb_boot → pcb[0]；after that: pcb[0] ↔ pcb[1]'))

    body.append(f'<rect x="250" y="632" width="220" height="56" rx="12" ry="12" fill="#fff7ed" stroke="#ea580c" stroke-width="1.7"/>')
    body.append(text(360, 656, 'mv sp, a0', 'label', 'middle'))
    body.append(text(360, 678, 'switch trap frame source', 'small', 'middle'))
    body.append(f'<rect x="510" y="632" width="500" height="56" rx="12" ry="12" fill="#f8fafc" stroke="#334155" stroke-width="1.5"/>')
    body.append(text(760, 656, 'restore mstatus / mepc → pop GPR → mret', 'label', 'middle'))
    body.append(text(760, 678, 'resume the selected execution flow (stack + PC together)', 'small', 'middle'))

    body.append(arrow(630, 170, 630, 212, 'thin'))
    body.append(text(648, 196, 'ordinary call', 'small'))
    body.append(f'<line x1="630" y1="266" x2="630" y2="312" stroke="#ea580c" stroke-width="3.0" marker-end="url(#arrow)"/>')
    body.append(text(648, 292, 'ECALL trap', 'small'))
    body.append(f'<line x1="630" y1="366" x2="630" y2="420" stroke="#ea580c" stroke-width="3.0" marker-end="url(#arrow)"/>')
    body.append(text(648, 398, 'PC ← mtvec', 'small'))
    body.append(arrow(825, 478, 825, 528, 'thin'))
    body.append(text(842, 506, 'return next Context *', 'small'))
    body.append(f'<line x1="630" y1="584" x2="630" y2="632" stroke="#2563eb" stroke-width="2.6" marker-end="url(#arrow)"/>')
    body.append(text(648, 614, 'Context * dataflow', 'small'))
    body.append(f'<line x1="760" y1="688" x2="760" y2="744" stroke="#ea580c" stroke-width="3.0" marker-end="url(#arrow)"/>')
    body.append(text(778, 720, 'MRET', 'small'))
    body.append(f'<path d="M 1010 660 C 1110 660, 1128 540, 1128 238 C 1128 144, 1038 144, 1010 144" stroke="#64748b" stroke-width="1.5" fill="none" stroke-dasharray="7 5" marker-end="url(#arrow)"/>')
    body.append(text(1048, 612, 'next round', 'small'))

    body.append(text(60, 740, '箭头语义：细实线 = 普通调用；粗橙色 = trap / mret；蓝色 = Context* 数据流。', 'small'))
    return svg_doc(1200, 780, "\n  ".join(body))


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


def context_switch_svg() -> str:
    body = [
        text(40, 40, 'Context switch：scheduler 选择状态，trap.S 恢复状态', 'title'),
        text(40, 67, '视觉焦点放在 `mv sp, a0`：scheduler 只决定“恢复哪一份 Context”，trap.S 才真正把机器状态和 execution stack 从 A 切到 B。', 'subtitle'),
    ]

    body.append(f'<rect x="70" y="120" width="270" height="430" rx="14" ry="14" fill="#f8fafc" stroke="#334155" stroke-width="1.4"/>')
    body.append(f'<rect x="860" y="120" width="270" height="430" rx="14" ry="14" fill="#f8fafc" stroke="#334155" stroke-width="1.4"/>')
    body.append(text(205, 152, 'PCB A', 'label', 'middle'))
    body.append(text(995, 152, 'PCB B', 'label', 'middle'))

    for x, tag, arg in [(95, 'Context A', 'mepc = f\na0 = 1'), (885, 'Context B', 'mepc = f\na0 = 2')]:
        body.append(f'<rect x="{x}" y="182" width="220" height="228" rx="12" ry="12" fill="#ffffff" stroke="#cbd5e1" stroke-width="1.1"/>')
        body.append(text(x + 110, 206, 'private stack', 'label', 'middle'))
        body.append(f'<line x1="{x+22}" y1="236" x2="{x+198}" y2="236" stroke="#94a3b8" stroke-width="1" stroke-dasharray="5 4"/>')
        body.append(text(x + 110, 256, 'call frames / locals', 'small', 'middle'))
        body.append(f'<rect x="{x+22}" y="292" width="176" height="96" rx="10" ry="10" fill="#eff6ff" stroke="#2563eb" stroke-width="1.3"/>')
        body.append(text(x + 110, 316, tag, 'label', 'middle'))
        body.append(text(x + 110, 342, 'gpr[]\nmcause\nmstatus', 'mono', 'middle', line_height=18))
        body.append(text(x + 110, 378, arg, 'mono', 'middle', line_height=18))

    body.append(rect(455, 176, 290, 82, 'soft', 'schedule(prev)', 'store prev; pick the other PCB; return next Context *'))
    body.append(rect(495, 294, 210, 54, 'warn', 'a0 = next Context *', 'return value back to trap.S'))
    body.append(f'<rect x="450" y="392" width="300" height="82" rx="12" ry="12" fill="#fff7ed" stroke="#ea580c" stroke-width="1.6"/>')
    body.append(text(600, 434, 'mv sp, a0', 'label', 'middle'))
    body.append(text(600, 458, 'BEFORE: sp → Context A', 'small', 'middle'))
    body.append(text(600, 480, 'AFTER:  sp → Context B', 'small', 'middle'))

    body.append(rect(430, 504, 340, 68, 'box', 'restore mstatus / mepc → pop GPR → mret', 'PC ← next.mepc；a0 ← next.a0'))

    body.append(arrow(315, 340, 455, 220))
    body.append(text(368, 274, 'prev', 'small'))
    body.append(f'<line x1="745" y1="220" x2="885" y2="340" stroke="#2563eb" stroke-width="2.5" marker-end="url(#arrow)"/>')
    body.append(text(760, 274, 'next', 'small'))
    body.append(f'<line x1="600" y1="348" x2="600" y2="392" stroke="#2563eb" stroke-width="2.5" marker-end="url(#arrow)"/>')
    body.append(f'<line x1="750" y1="544" x2="885" y2="380" stroke="#ea580c" stroke-width="2.8" marker-end="url(#arrow)"/>')

    body.append(text(94, 610, 'kcontext() 先人工造出 trap frame：mepc=entry, a0=arg, mstatus=0x1800。', 'small'))
    body.append(text(94, 636, '因此它不是直接调用 f()；第一次 mret 才让这个 C 函数真正拿到 CPU。', 'small'))
    body.append(text(94, 670, 'sp 的切换焦点只有一句：scheduler returns a Context *; trap.S switches to it with `mv sp, a0`.', 'small'))
    return svg_doc(1200, 730, "\n  ".join(body))


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
        'trap_flow.svg': trap_svg(),
        'yield_flow.svg': yield_flow_svg(),
        'build_flow.svg': build_flow_svg(),
        'context_switch.svg': context_switch_svg(),
        'perf_stage.svg': perf_svg(perf),
        'backend_area.svg': backend_svg(synth),
    }
    for name, content in outputs.items():
        (ASSETS / name).write_text(content)
        print(f'wrote {ASSETS / name}')


if __name__ == '__main__':
    main()
