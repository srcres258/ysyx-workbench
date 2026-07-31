# "一生一芯" C 阶段结业考核演示文稿

## 版本

- Typst: `0.14.2`
- Touying: `0.7.4`

## 编译

```bash
python3 slides/c-stage-defense/scripts/generate_figures.py
typst compile slides/c-stage-defense/slides.typ slides/c-stage-defense/slides.pdf
```

## 字体

- 正文/中文：`Noto Sans CJK SC`
- 等宽字体：`DejaVu Sans Mono`

## 图形生成

所有图表和架构图都由 `scripts/generate_figures.py` 从仓库当前数据生成：

- `npc/build/perf/perf.json`
- `npc/build/synth/synth_summary.json`

生成结果输出到 `assets/`。

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
