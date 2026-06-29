# Zone Management 水路管理领域

> Current status: historical background only. The current implementation target is the 2 Flow / 6 Zone redesign in `docs/superpowers/specs/2026-06-05-two-flow-six-zone-irrigation-design.md`. Files in this directory may still describe the old 4-road, startup-compensation, or old API model until Task 9 rewrites them. Do not use this directory as the current implementation authority.

本目录存放「水路（Zone）管理」领域的设计讨论、方案确认和实现规划。每个文件对应一个子主题。

> Zone = 水路。灌溉行业标准术语（Rain Bird、Hunter、Orbit 等品牌统一使用）。

## 文件索引

| 文件 | 主题 | 状态 |
| --- | --- | --- |
| [01-core-proposition.md](01-core-proposition.md) | 核心命题：单路自治，N 倍扩展 | ✅ 已确认 |
| [02-zone-unit-design.md](02-zone-unit-design.md) | Zone 单元设计：状态机、实体、接口 | ✅ 已确认 |
| [03-plan-management.md](03-plan-management.md) | 计划管理体系：定义、调度、跳过、记录 | ✅ 已确认 |
| [04-flow-calibration.md](04-flow-calibration.md) | 流量计现场校准：原始脉冲明细、稳定识别、参数拟合 | ✅ 已确认 |

## 使用方式

- 每个子话题独立成文件，保持精简
- 方案确认后再整合到主项目文档
- 历史讨论保留在此，便于追溯决策过程
