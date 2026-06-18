# 06 Web 交互与 API 边界

## 交互原则

- Web 是完整配置、操作、维护、故障处理和记录查看入口。
- LCD2004 和本地按键只承担最小本地辅助。
- Web 不做通用硬件调试台。
- Web 不提供通用寄存器读写。
- Web 不提供任意 RS485 帧透传。
- Web 不提供通用 IO 映射 UI。

## 页面范围

| 页面 | 职责 |
|---|---|
| 首页 | 总览设备状态、当前运行、告警、从站在线状态 |
| 手动运行 | 选择从站、水路、运行时长，启动或停止 |
| 水源/从站 | 管理从站、地址、在线状态、水源名称 |
| 水路 | 配置主阀、分支水路、启用状态、名称、默认时长 |
| 输入输出 | 配置 `IN1..IN4` 的流量计/低液位/预留占位，以及自吸泵 |
| 流量 | 流量计校准、标准流速学习、流量保护参数 |
| 计划 | 自动计划、同站水路顺序、启用状态、跳过结果查看 |
| 告警与记录 | 查看运行记录、故障记录、配置变更和维护记录 |
| 系统设置 | 网络、时间、SHT30、RS485、备份恢复 |
| 维护 | 从站识别、清除故障、重新同步配置、诊断状态 |

## 首页

首页显示：

- 主控时间状态。
- RS485 总体状态。
- 当前运行中的从站和水路。
- 未处理告警数量。
- 最近一次故障摘要。
- 从站在线、离线、配置未同步、故障、锁定状态。

## 手动运行

启动流程：

1. 选择从站。
2. 选择已启用且未锁定的分支水路。
3. 输入运行时长。
4. 主控检查从站在线、配置同步、故障和锁定状态。
5. 主控下发 `START_ZONE`。
6. 页面显示启动中、运行中、停止中和结果。

规则：

- 其他从站运行中不阻止当前从站启动。
- 同一从站已有活动运行时禁止启动。
- 流量计计量未校准、标准流速未学习、电压检测未校准不阻断启动，但必须提示保护不完整。
- 停止后记录停止原因。
- 如果从站已因本地安全策略停机，页面显示从站上报的停止原因，不覆盖成用户停止。

## 从站页面

显示：

- 从站 ID。
- 拨码地址。
- 在线状态。
- 配置同步状态。
- 水源名称。
- 最近故障。
- 固件版本和硬件能力。

动作：

- 启用/禁用从站。
- 同步安全配置。
- 识别从站。
- 清除可恢复故障。
- 查看从站诊断。

## 输入输出页面

当前只覆盖：

- `IN1..IN4` 中 1 路流量计输入。
- `IN1..IN4` 中可选 1 路低液位输入。
- `IN1..IN4` 中未使用输入的 `disabled` 或 `reserved` 占位。
- 可选自吸泵干接点输出 `dry_contact_1`。
- `VALVE1..VALVE8` 的主阀/分支水路角色。
- 电压保护启用状态，以及启用时的阈值。

不覆盖：

- 主控板预留 ADC、脉冲输入、干接点、阀输出。
- 从站 H6 预留 I2C。
- 从站 H8/H9 预留 ADC。
- `SPARE_*` 引脚。

## 计划页面

计划配置包含：

- 计划名称。
- 启用状态。
- 执行时间。
- 重复规则。
- 目标从站和水路顺序。
- 每路运行时长。

运行规则：

- 同一从站同一时间只运行一路分支水路。
- 同一个水源下按顺序运行多个水路。
- 不同从站代表不同真实水源，可以同时运行。
- 离线、未同步配置、故障锁定或同站冲突导致未启动的计划项直接跳过并记录原因。
- 不提供排队或隐藏延后策略。

计划结果：

- `completed`：全部应执行水路完成。
- `partial`：部分完成，部分跳过或失败。
- `skipped`：全部应执行水路都未启动即跳过。
- `failed`：至少尝试启动或运行，但没有水路成功完成。
- `cancelled`：用户取消。

## API 资源边界

主要资源：

```text
SystemStatus
Station
Zone
InputConfig
FlowConfig
PumpConfig
SafeConfig
Schedule
RunSession
Alarm
Event
RunRecord
ScheduleRunRecord
```

API 只暴露产品业务对象，不暴露任意寄存器和通用 IO。

## 关键 API

```text
GET  /api/irrigation/status
GET  /api/irrigation/stations
GET  /api/irrigation/stations/{station_id}
POST /api/irrigation/stations/{station_id}/sync-config
POST /api/irrigation/stations/{station_id}/identify
POST /api/irrigation/stations/{station_id}/clear-fault

POST /api/irrigation/runs
POST /api/irrigation/runs/{run_id}/stop
POST /api/irrigation/stop-all

GET  /api/irrigation/schedules
POST /api/irrigation/schedules
PUT  /api/irrigation/schedules/{schedule_id}
DELETE /api/irrigation/schedules/{schedule_id}
POST /api/irrigation/schedules/{schedule_id}/enable
POST /api/irrigation/schedules/{schedule_id}/disable

GET  /api/irrigation/alarms
POST /api/irrigation/alarms/{alarm_id}/ack
POST /api/irrigation/alarms/{alarm_id}/clear
GET  /api/irrigation/events
GET  /api/irrigation/run-records
GET  /api/irrigation/schedule-records
```

## 高影响动作

以下动作必须二次确认：

- 停止全部。
- 删除计划。
- 清除锁定故障。
- 恢复出厂配置。
- 覆盖从站安全配置。

## LCD 和本地按键

LCD2004 只显示：

- 当前时间。
- 主控和 RS485 状态。
- 当前运行摘要。
- 未处理故障数量。
- 最短故障提示。

主控按键只用于：

- 查看有限状态信息。
- 必要时停止当前运行。
- 必要时触发识别。

LCD 和主控按键不承担完整配置、计划编辑、故障处理和记录查看。
