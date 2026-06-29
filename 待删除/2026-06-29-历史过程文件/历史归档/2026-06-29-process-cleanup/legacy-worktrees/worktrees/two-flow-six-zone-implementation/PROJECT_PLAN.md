# Esp32_Irrigation Project Plan

本项目当前目标是一次干净重构：最多 2 个 Flow 流量计、最多 6 个 Zone 电磁阀水路，Zone 可任意归属 Flow，同 Flow 互斥，不同 Flow 可并行。旧 4 路、每路一个流量计、启动补偿校准、旧 API 和旧存储格式都不作为兼容目标。

## Current Authority

当前实现依据按优先级为：

1. [详细设计](docs/superpowers/specs/2026-06-05-two-flow-six-zone-irrigation-design.md)
2. [实施计划](docs/superpowers/plans/2026-06-05-two-flow-six-zone-irrigation-redesign.md)
3. [当前需求摘要](docs/01_requirements_v1.md)

`old-docs/` 和旧 `docs/road-management/` 内容只作为历史背景；后续需要时按新模型改写，不做旧行为兼容。

## Target Architecture

- `FlowMeterService`：Flow 1/2 脉冲计数、固定点 K+Offset 计算、低于计量下限和样本无效标记。
- `ValveService`：6 路阀门 PWM、吸合/保持、安全关阀。
- `ZoneService`：Zone 状态机、Flow 占用、互斥、启动/停止、运行统计。
- `ScheduleService`：Zone 计划触发，同 Flow 冲突进入内存队列。
- `CalibrationService`：Flow K+Offset 校准、Zone 正常流量学习。
- `LocalControlService`：本地 enabled Zone 选择、启动/停止、Stop All 和同键二次确认。
- `DisplayService`：I2C 屏幕状态显示，不做配置、校准、学习输入。
- `RecordService`：固定环形浇水记录。

## Implementation Tasks

实施按计划文档逐项推进：

1. Constants and pins
2. New configuration types and storage
3. Flow Meter K+Offset service
4. Valve, Zone runtime, LocalControl and DisplayService
5. Schedule queue
6. Calibration and learning
7. Records and events
8. Web and API
9. Entry docs and final validation

每个任务完成后至少运行计划中指定的验证命令。涉及硬件、功耗、流量、RTC、LCD、OTA 的结论必须明确区分“已实机验证”和“未实机验证”。

## Verification

```bash
node scripts/check-web-structure.mjs
pio run
```

最终实机验收至少覆盖：6 路阀门输出、2 路 Flow 输入、同 Flow 互斥、不同 Flow 并行、K+Offset 校准、Zone 学习、无脉冲停机、低/高流量动作、待机漏水、本地同键二次确认、记录恢复和 72 小时稳定运行。
