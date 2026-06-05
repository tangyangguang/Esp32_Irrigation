# 业务事件字段语义

灌溉业务事件统一写入 `Esp32BaseAppEventLog`。本项目不另建事件存储；基础库继续提供固定容量环形存储、分页读取、CSV 导出和清空能力。

业务事件只记录能帮助用户理解“为什么启动、排队、跳过、停止、保护或参数改变”的事实。启动、WiFi、NTP、OTA、重启、文件系统和健康状态等系统层信息归 `Esp32Base` 日志、健康和诊断页面。

## 字段约定

- `level`：`info`、`warn`、`error`。
- `source`：业务来源，例如 `web`、`api`、`button`、`schedule`、`monitor`、`runtime`、`storage`。
- `type`：业务事件类型。
- `reason`：结构化原因。
- `object`：关联对象，例如 `flow:1`、`zone:3`、`plan:123`、`system:irrigation`。
- `code`：业务短码，通常保存枚举值。
- `value1` / `value2` / `value3`：业务数值槽，含义由 `type` 决定。
- `text`：短说明。业务页面应使用业务文案解释事件，不直接暴露基础库内部字段。

## 当前事件类型

| type | level | source | reason | object | code/value | 说明 |
|---|---|---|---|---|---|---|
| `flow_mutex_rejected` | `info` | `web`/`api`/`button`/`schedule` | `flow_busy` | `zone:<id>` | `value1` 为 flowId，`value2` 为占用该 Flow 的 activeZoneId | Zone 启动被同 Flow 互斥规则拒绝。 |
| `schedule_queued` | `info` | `schedule` | `flow_busy` | `plan:<id>` | `value1` 为 zoneId，`value2` 为 flowId | 计划到点但目标 Flow 忙，任务进入 RAM 队列。 |
| `schedule_queue_started` | `info` | `schedule` | `flow_available` | `plan:<id>` | `value1` 为 zoneId，`value2` 为 flowId | 排队计划在 Flow 空闲后启动。 |
| `schedule_queue_full` | `warn` | `schedule` | `capacity` | `system:irrigation` | `value1` 为队列容量 | 队列满，新的计划任务被拒绝。 |
| `schedule_queue_expired` | `warn` | `schedule` | `queued_too_long` | `plan:<id>` | `value1` 为 zoneId，`value2` 为等待秒数 | 排队超过 `queuedPlanMaxDelaySec` 后被丢弃。 |
| `flow_no_pulse_stop` | `error` | `monitor` | `no_pulse_timeout` | `zone:<id>` | `value1` 为 flowId，`value2` 为 noPulseTimeoutSec，`value3` 为停机前脉冲数 | Zone 稳定阶段无原始脉冲超时并被关闭。 |
| `flow_low_fault` | `warn`/`error` | `monitor` | `low_flow` | `zone:<id>` | `code` 为 action，`value1` 为 flowId，`value2` 为当前流量，`value3` 为阈值 | 有效计量样本连续低于 Zone 正常流量阈值。 |
| `flow_high_fault` | `warn`/`error` | `monitor` | `high_flow` | `zone:<id>` | `code` 为 action，`value1` 为 flowId，`value2` 为当前流量，`value3` 为阈值 | 有效计量样本连续高于 Zone 正常流量阈值。 |
| `idle_leak_detected` | `error` | `monitor` | `idle_flow` | `flow:<id>` | `value1` 为窗口内脉冲数，`value2` 为阈值，`value3` 为窗口秒 | 没有 Zone 运行时某个 Flow 检测到异常脉冲。 |
| `flow_pending_calibration_saved` | `info` | `web`/`api` | `manual`/`single_point`/`multi_point` | `flow:<id>` | `value1` 为 kUlPerMinPerHz，`value2` 为 offsetMilliHz | Flow 待应用 K+Offset 参数已保存。 |
| `flow_params_applied` | `info` | `web`/`api` | `apply_pending` | `flow:<id>` | `value1` 为旧 k，`value2` 为新 k，`value3` 为新 offset | Flow 待应用参数被应用，旧参数进入 rollback。 |
| `flow_calibration_rolled_back` | `info` | `web`/`api` | `rollback` | `flow:<id>` | `value1` 为恢复后的 k，`value2` 为恢复后的 offset | Flow 参数回退。 |
| `zone_pending_baseline_saved` | `info` | `web`/`api` | `manual`/`learned` | `zone:<id>` | `value1` 为 learnedFlowMlPerMin，`value2` 为 lowFlowPermille，`value3` 为 highFlowPermille | Zone 待应用正常流量基线已保存。 |
| `zone_learning_applied` | `info` | `web`/`api` | `apply_pending` | `zone:<id>` | `value1` 为 learnedFlowMlPerMin | Zone 待应用基线被应用，旧基线进入 rollback。 |
| `zone_baseline_rolled_back` | `info` | `web`/`api` | `rollback` | `zone:<id>` | `value1` 为恢复后的 learnedFlowMlPerMin | Zone 正常流量基线回退。 |
| `zone_fault_cleared` | `info` | `web`/`api` | `manual_clear` | `zone:<id>` | `code` 为清除前 ZoneErrorCode | 用户清除指定 ZoneFault。 |
| `flow_leak_fault_cleared` | `info` | `web`/`api` | `manual_clear` | `flow:<id>` | `code` 为清除前 FlowFaultCode | 用户清除指定 FlowLeakFault。 |
| `record_store_recovered` | `warn` | `storage` | `meta_rebuilt` | `system:irrigation` | `value1` 为恢复记录数，`value2` 为 nextRecordId | 启动时从 committed 浇水记录重建 RecordStore 元数据。 |
| `record_store_fault` | `error` | `storage` | `meta_save_failed`/`append_failed` | `system:irrigation`/`zone:<id>` | `code` 为失败场景，`value1` 为 recordId | 浇水记录写入或元数据保存失败。 |
| `config_schema_reset` | `warn` | `storage` | `invalid_or_missing` | `config:<scope>` | `value1` 为被重置的对象数量 | 新 schema 缺失或非法，系统按默认值重建配置。 |
| `web_route_fault` | `error` | `web` | `registration_failed` | `system:irrigation` | `value1` 为失败路由数量 | 业务页面或 API 路由注册失败。 |
| `factory_reset` | `warn`/`info`/`error` | `button`/`runtime` | `requested`/`executed`/`failed` | `system:irrigation` | `code` 表示执行结果 | 本地 GPIO0/BOOT 长按触发的恢复出厂请求和执行结果。 |

## 规则

- `RECORD_ONLY` 的低/高流量异常只写事件和记录观察标记，不设置 ZoneFault。
- `STOP_ZONE` 的低/高流量异常必须写事件、关闭 Zone，并在记录中使用对应停止结果。
- 待机漏水事件对象必须是 `flow:<id>`，不能伪装成 `zone:<id>`。
- Flow 校准事件对象必须是 `flow:<id>`；Zone 学习事件对象必须是 `zone:<id>`。
- 事件字段必须使用 `flowId`、`zoneId`、`planId` 等新模型命名，不使用 `road`、`candidateFlow`、`previousFlow` 等旧字段。
