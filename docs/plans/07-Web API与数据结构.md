# 07 Web API 与数据结构

状态：成熟产品 Web 交互与业务接口草案。本文定义主控 Web 页面与固件业务对象之间的接口边界，不定义 RS485 二进制帧细节、route 数量或具体存储实现。

关联文档：

- `01-新版硬件启用范围与系统边界.md`
- `02-RS485通讯协议设计草案.md`
- `03-从站运行状态机.md`
- `04-配置模型.md`
- `05-Web交互模型.md`
- `06-固件架构.md`
- `11-统一枚举与错误码表.md`

## 设计目标

- Web 是完整配置、操作、维护、故障处理和记录查看入口。
- API 面向主控业务对象，不直接暴露 RS485 帧。
- 主控负责把 Web/API 操作转换成配置保存、从站配置同步、运行调度和状态轮询。
- API 结构要稳定，适合后续网页、手机浏览器和本地维护工具共同使用。
- 不做通用物联网平台式抽象，只覆盖本产品需要的水源、从站、水路、输入输出、计划、告警和维护。

## 基础库边界

主控 Web/API 基于 `Esp32Base` 实现。

应优先复用：

- Web 服务和路由注册能力。
- Web 认证和基础系统页面。
- 静态资源和基础 UI 样式。
- 配置管理能力。
- 文件和导出能力。
- 事件记录能力。
- 系统诊断日志。
- OTA 和维护入口。

本项目负责：

- 灌溉业务页面。
- 灌溉业务 API。
- 业务配置模型。
- 水路运行调度。
- 从站管理。
- RS485 主站协议适配。
- 业务事件命名、展示和导出。

## API 总体规则

路径建议统一放在 `/api/irrigation/*` 下。

返回格式统一使用 JSON：

```json
{
  "ok": true,
  "data": {},
  "error": null
}
```

错误格式：

```json
{
  "ok": false,
  "data": null,
  "error": {
    "code": "station_offline",
    "message": "从站离线，不能启动运行",
    "details": {}
  }
}
```

规则：

- `GET` 只读取状态或配置。
- `POST` 用于执行动作。
- `PUT` 用于整体保存配置对象。
- `PATCH` 用于局部修改配置对象。
- 删除类动作必须使用 `POST` 或 `DELETE` 并在 Web 页面二次确认。
- 所有修改配置和运行控制的 API 必须经过 Web 认证。
- API 不返回 Web Auth 密码、Wi-Fi 密码或其他敏感字段。

## 核心数据对象

### SystemStatus

主控系统状态。

```text
device_name
firmware_version
time_status
current_time
network_status
rs485_baud
temperature_c
humidity_percent
station_count
online_station_count
running_session_count
active_alarm_count
storage_status
```

用途：

- 首页总览。
- 系统设置页。
- 维护页。

### Station

从站业务对象。

```text
station_id
rs485_address          1..15
enabled
station_name
water_source_name
notes
online
dipswitch_address
firmware_version
hardware_version
config_version
config_crc
config_sync_state      synced / pending / failed / unknown
last_seen_at
last_error
```

规则：

- `rs485_address` 来自从站拨码地址。
- Web 不修改从站地址。
- `station_id` 是主控内部 ID，用于配置、记录和页面引用。
- 地址冲突时，相关从站不可运行。

### StationRuntime

从站实时状态。

```text
station_id
state                  booting / unconfigured / idle / starting / running / stopping / fault / locked / offline
run_id
running_zone_index
elapsed_sec
target_duration_sec
flow_pulse_count
flow_pulse_rate_min
flow_ml
flow_ml_min
flow_estimation_quality
low_level_active
voltage_mv
pump_output
master_valve_output
branch_valve_output
fault_flags
stop_reason
comm_quality
updated_at
```

用途：

- 首页运行状态。
- `flow_estimation_quality` 表示 `flow_ml` 和 `flow_ml_min` 是否可靠，可为 `valid`、`reference_only`、`unavailable`。
- 手动运行页。
- 从站状态页。
- 告警判断。

### ZoneConfig

水路配置。

```text
station_id
zone_index             1..8
role                   disabled / branch_zone / master_valve
enabled
name
default_duration_sec
manual_allowed
schedule_allowed
lock_state
baseline_ml_min
baseline_status        unknown / learned / disabled
```

规则：

- 一个从站最多一个主阀。
- 主阀不作为可运行水路。
- 分支水路至少启用一路，从站才可进入可运行配置。

### InputConfig

输入配置。

```text
station_id
input_index            1..4
role                   disabled / flow_meter / low_level / reserved
active_level           low / high
debounce_ms
edge                   rising / falling
```

规则：

- 一个从站必须且只能配置一个流量计输入。
- 低液位输入可选。
- 流量计输入不使用普通去抖。
- `reserved` 只表示 `IN1..IN4` 中该路暂不参与当前灌溉逻辑，不等于启用从站 H6/H8/H9 或 `SPARE_*` 预留接口。

### PumpConfig

自吸泵配置。

```text
station_id
enabled
output                 dry_contact_1
start_delay_ms
stop_delay_ms
```

规则：

- 自吸泵可选。
- 自吸泵和低液位互不强制依赖。
- 如果二者都配置，从站启动自吸泵前必须确认低液位未触发。

### FlowMeterConfig

流量计配置。

```text
station_id
input_index
edge
pulses_per_liter
sample_window_sec
first_flow_timeout_sec
no_flow_timeout_sec
stabilize_sec
standby_leak_confirm_sec
calibration_status     missing / calibrated
```

规则：

- 流量计是每个从站的必需配置。
- 未完成计量校准的从站仍可参与正常灌溉运行，但水量估算不可用或仅供参考。

### Schedule

自动计划。

```text
schedule_id
enabled
name
time_rule
items[]
skip_policy
created_at
updated_at
```

`items[]`：

```text
station_id
zone_index
duration_sec
order
```

规则：

- 同一从站的多个水路按顺序运行。
- 不同从站可以并行运行。
- 被锁定、禁用、离线、配置未同步或已触发明确安全阻断条件的目标应跳过并记录原因。
- 标准流速未学习、流量计计量未校准、电压检测未校准属于增强能力未就绪，不应作为计划跳过原因。

### RunSession

运行任务。

```text
run_id
source                 manual / schedule / calibration / baseline_learning
station_id
zone_index
target_duration_sec
state                  starting / running / stopping / stopped / fault
started_at
stopped_at
elapsed_sec
flow_pulse_count
flow_pulse_rate_min
flow_ml
flow_ml_min
flow_estimation_quality
stop_reason
fault_flags
```

规则：

- 同一从站同一时间最多一个活动 `RunSession`。
- 不同从站允许多个 `RunSession` 同时存在。
- 页面停止运行时，停止原因应区分用户停止、本地保护停机、通信失败和计划结束。
- 流量计计量未校准时，`flow_estimation_quality` 不能为 `valid`，页面不得把水量估算显示为准确值。

## API 分组

### 总览

```text
GET /api/irrigation/status
```

返回：

- `SystemStatus`
- 当前所有活动 `RunSession`
- 从站在线摘要
- 未处理告警摘要

用途：

- 首页。
- 顶部状态栏。
- 周期刷新。

### 从站

```text
GET   /api/irrigation/stations
GET   /api/irrigation/stations/{station_id}
PATCH /api/irrigation/stations/{station_id}
POST  /api/irrigation/stations/{station_id}/identify
POST  /api/irrigation/stations/{station_id}/sync-config
POST  /api/irrigation/stations/{station_id}/clear-fault
```

说明：

- `PATCH` 只允许修改 `enabled`、`station_name`、`water_source_name`、`notes` 等主控业务字段。
- `identify` 触发从站状态灯识别流程。
- `sync-config` 生成并下发从站安全配置。
- `clear-fault` 只清除可恢复故障。

### 从站实时状态

```text
GET /api/irrigation/stations/{station_id}/runtime
GET /api/irrigation/stations/runtime
```

说明：

- 单个接口用于详情页。
- 批量接口用于首页和运行页。
- 状态来自主控最近一次轮询结果，不由 Web 直接访问从站。

### 水路配置

```text
GET /api/irrigation/stations/{station_id}/zones
PUT /api/irrigation/stations/{station_id}/zones
```

规则：

- 保存时整体校验该从站 8 路水路配置。
- 如果保存内容影响从站安全配置，标记 `config_sync_state=pending`。
- 如果该从站正在运行，禁止修改主阀、当前水路、PWM 和安全相关参数。

### 输入输出配置

```text
GET /api/irrigation/stations/{station_id}/io
PUT /api/irrigation/stations/{station_id}/io
```

包含：

- `InputConfig[]`
- `PumpConfig`
- 主阀延时配置。
- 阀门 PWM 配置。
- 电压检测启用状态；启用时包含电压检测阈值。

保存校验：

- 必须有且只有一个流量计输入。
- 低液位最多一个。
- 同一输入不能重复分配。
- 自吸泵输出只有 `dry_contact_1`。
- 主阀如果启用，必须引用 `VALVE1..VALVE8` 中一个未作为分支水路重复使用的输出。
- 电压检测未启用或未校准时，不要求电压阈值作为运行准入条件。

### 流量

```text
GET  /api/irrigation/stations/{station_id}/flow
PUT  /api/irrigation/stations/{station_id}/flow
POST /api/irrigation/stations/{station_id}/flow/calibration/start
POST /api/irrigation/stations/{station_id}/flow/calibration/finish
POST /api/irrigation/stations/{station_id}/zones/{zone_index}/baseline/start
POST /api/irrigation/stations/{station_id}/zones/{zone_index}/baseline/finish
```

说明：

- 校准和标准流速学习都属于维护运行。
- 维护运行也必须遵守低液位、电压、通信超时、运行超时和手动停止规则。
- `finish` 保存结果前需要校验运行期间没有保护停机。

### 手动运行

```text
POST /api/irrigation/runs/manual/start
POST /api/irrigation/runs/{run_id}/stop
GET  /api/irrigation/runs/active
GET  /api/irrigation/runs/{run_id}
```

启动请求：

```json
{
  "station_id": "station-1",
  "zone_index": 1,
  "duration_sec": 600
}
```

启动前主控检查：

- 从站启用。
- 从站在线。
- 配置已同步。
- 从站不在故障或锁定状态。
- 目标水路启用。
- 目标水路允许手动运行。
- 目标水路未锁定。
- 流量计输入已配置。
- 流量计计量未校准不阻断启动，只影响水量估算质量。
- 同一从站当前没有活动运行。

### 计划

```text
GET    /api/irrigation/schedules
POST   /api/irrigation/schedules
GET    /api/irrigation/schedules/{schedule_id}
PUT    /api/irrigation/schedules/{schedule_id}
DELETE /api/irrigation/schedules/{schedule_id}
POST   /api/irrigation/schedules/{schedule_id}/enable
POST   /api/irrigation/schedules/{schedule_id}/disable
```

规则：

- 计划保存时校验目标从站和水路存在。
- 计划执行时再次校验在线、锁定、校准和配置同步状态。
- 计划跳过、失败、被保护停止都必须记录事件。

### 告警与记录

```text
GET  /api/irrigation/alarms
POST /api/irrigation/alarms/{alarm_id}/ack
GET  /api/irrigation/events
GET  /api/irrigation/runs/history
GET  /api/irrigation/events/export.csv
```

记录类型：

- 运行开始。
- 正常停止。
- 用户停止。
- 计划跳过。
- 通信异常。
- 低液位停机。
- 无水停机。
- 已启用电压保护并确认电压严重异常停机。
- 配置变更。
- 配置同步结果。
- 流量计校准。
- 标准流速学习。
- 故障清除。

记录边界原则：

- 低频、可解释的业务事件包括保护触发、计划跳过、配置同步结果、故障清除。
- 完整运行历史、累计用水量、统计报表、校准明细等长期业务数据属于运行历史记录。
- 系统诊断日志不混入业务事件列表。
- 业务事件 token 命名遵守 `10-基础库能力边界与命名原则.md`。

### 系统设置

```text
GET   /api/irrigation/settings
PATCH /api/irrigation/settings
POST  /api/irrigation/settings/backup
POST  /api/irrigation/settings/restore
```

说明：

- 网络、认证、OTA、系统诊断等基础能力优先使用 `Esp32Base` 内置页面和 API。
- 本项目系统设置只保存灌溉业务需要的全局参数，例如 RS485 波特率、轮询周期、事件保留策略、SHT30 显示开关和 LCD 最小显示开关。
- 备份恢复只覆盖灌溉业务配置，不应包含 Web Auth 密码和 Wi-Fi 密码。

### 维护

```text
GET  /api/irrigation/maintenance/diagnostics
POST /api/irrigation/maintenance/scan-stations
POST /api/irrigation/maintenance/stop-all
POST /api/irrigation/maintenance/resync-all
```

规则：

- `stop-all` 是高影响操作，Web 必须二次确认。
- `stop-all` 对所有正在运行的从站逐个发送停止命令；若某从站通信失败，记录通信失败，并依赖从站本地通信超时安全停机。
- `scan-stations` 只扫描地址和在线状态，不修改从站拨码地址。

## 配置保存和同步流程

配置修改分为三类：

| 类型 | 保存位置 | 是否需要同步从站 |
|---|---|---|
| 主控显示、网络、记录策略 | 主控 | 否 |
| 从站名称、水源名称、备注 | 主控 | 否 |
| 输入输出、阀门、泵、低液位、电压、安全超时 | 主控 + 从站 | 是；未启用的可选能力同步禁用状态，不要求对应预留接口或增强参数有效 |

需要同步从站的配置保存流程：

1. Web 提交配置。
2. 主控校验业务规则。
3. 主控保存业务配置草稿。
4. 主控生成安全配置包。
5. 主控标记从站 `config_sync_state=pending`。
6. 主控下发 `SET_SAFE_CONFIG`。
7. 从站校验、保存并返回版本和 CRC。
8. 主控确认后标记 `config_sync_state=synced`。

失败处理：

- 同步失败不删除主控配置。
- 同步失败的从站不能启动运行。
- Web 需要显示“配置未同步”并提供重新同步入口。

## 运行状态刷新

Web 页面建议采用轮询。

建议刷新周期：

- 首页：`1 s..3 s`。
- 运行详情页：`500 ms..1 s`。
- 普通配置页：保存后刷新，不做高频轮询。
- 告警和记录页：`5 s..10 s` 或手动刷新。

说明：

- 当前产品不要求 WebSocket。
- 页面看到的状态来自主控缓存；主控缓存来自 RS485 周期轮询。
- 如果主控长时间没有从站新状态，需要明确显示“状态过期”。

## 错误码建议

Web/API 错误码统一定义见 `11-统一枚举与错误码表.md`。本节保留当前 API 需要暴露的稳定字符串。

```text
station_disabled
station_offline
station_address_conflict
station_config_pending
station_config_invalid
station_fault
station_locked
station_busy
zone_disabled
zone_locked
zone_not_manual_allowed
flow_meter_missing
flow_meter_uncalibrated
low_level_active
voltage_fault
rs485_timeout
command_rejected
validation_failed
storage_failed
auth_required
permission_denied
```

错误码应稳定，页面文案可以后续调整。

## 不支持的 API 方向

当前产品不支持：

- Web 直接发送任意 RS485 帧。
- Web 直接操作从站 GPIO。
- 通用寄存器读写页面。
- 从站主动推送到 Web。
- 一个从站多个流量计。
- 一个从站内部多水路并行运行。
- 未认证用户执行配置或运行命令。
- 通过 Web 修改从站拨码地址。

## 方案边界

- 业务数据模型、生命周期、事件、运行历史和告警边界见 `15-主控数据模型与生命周期边界.md`。
- Web 页面以稳定业务接口为主；具体 JSON 路由、HTML helper、静态资源组织和 route 数量属于实施阶段设计。
- 配置备份的业务语义是“导出和恢复灌溉业务配置”；具体文件格式、版本迁移实现和基础库 API 使用属于实施阶段设计。
