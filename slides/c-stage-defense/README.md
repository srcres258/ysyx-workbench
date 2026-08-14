# "一生一芯" C 阶段结业考核演示文稿

## 版本

- Typst: `0.14.2`
- Touying: `0.7.4`

## 编译

```bash
python3 slides/c-stage-defense/scripts/generate_figures.py
slides/c-stage-defense/scripts/render_microarchitecture.sh
typst compile slides/c-stage-defense/slides.typ slides/c-stage-defense/slides.pdf
```

## 字体

- 正文/中文：`Noto Sans CJK SC`
- 等宽字体：`DejaVu Sans Mono`

## 图形生成

图形来源现在分为三类：

- `scripts/generate_figures.py`：数据驱动 SVG 图表与非 TikZ 架构图
- `microarchitecture.tex` + `scripts/render_microarchitecture.sh`：NPC 总体微架构 TikZ → SVG
- `slides.typ`：最终 Typst 演示文稿整合

`generate_figures.py` 读取仓库当前数据生成：

- `npc/build/perf/perf.json`
- `npc/build/synth/synth_summary.json`

输出到 `assets/`。\
NPC 总体微架构图单独由下列命令生成：

```bash
slides/c-stage-defense/scripts/render_microarchitecture.sh
```

当前环境使用 `xelatex` 生成 PDF，再由 `pdftocairo -svg` 渲染为 `assets/microarchitecture.svg`。如果未来 dev shell 提供 `dvisvgm`，也可以切回 `XeLaTeX → XDV → dvisvgm` 的同源流程。

## 占位页替换位置

- 第 13–16 页：考核题目、实现与验证、个人特色、证据与复盘
- 这些页目前保留为可直接替换的版式，不虚构题目或成果

## 讲解时长

- 正文建议总时长：`11–13 min`
- 单页建议：`40–70 s`
- 核心图页：`60–90 s`

## 说明

- 这份 deck 以当前仓库事实为准，明确区分“已实现 / 候选 / 占位”
- I-cache、D-cache、B4 流水线都未写成已完成功能
