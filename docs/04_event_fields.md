# 业务事件字段语义

本项目不再维护自己的事件存储。灌溉业务事件统一写入 `Esp32BaseAppEventLog`，由基础库提供固定容量环形存储、分页读取、`/esp32base/app-events`、`/esp32base/api/app-events`、CSV 导出和 POST 清空能力。

业务事件只记录能帮助用户理解灌溉业务行为的重要事实，不记录启动、WiFi、NTP、OTA、重启、文件系统、健康状态等设备系统日志。这些系统层信息归 `Esp32Base` 的日志、健康和诊断页面。

## 字段约定

- `level`：`info`、`warn`、`error`，页面显示为“信息 / 注意 / 故障”。
- `source`：业务来源 token，例如 `web`、`api`、`button`、`schedule`、`monitor`、`runtime`。
- `type`：业务事件类型。
- `reason`：结构化原因。
- `object`：关联业务对象，例如 `zone:1`、`plan:123`、`system:irrigation`。
- `code`：业务自定义短码，通常保存对应枚举值。
- `value1` / `value2` / `value3`：业务数值槽，含义由 `type` 决定。
- `text`：短说明。业务页面会用业务文案解释事件，不展示基础库内部字段。

## 当前事件类型

| type | level | source | reason | object | code/value | 说明 |
|---|---|---|---|---|---|---|
| `schedule_skipped` | `info`/`warn` | `schedule`/`web`/`api` | 计划跳过或未执行原因 | `plan:<id>` | `code` 为计划观察状态或跳过原因；`value1` 可为日期或水路 | 计划因为手动跳过、天气、周期、水路停用、忙碌、漏水保护、配置无效等原因没有启动。 |
| `schedule_unskipped` | `info` | `web`/`api` | `manual`/`weather`/`other` | `plan:<id>` | `value1` 为日期 | 取消某天计划跳过。 |
| `schedule_tracker_fault` | `error` | `schedule` | `persist_failed` | `plan:<id>` | `code` 为计划观察状态；`value1` 为水路号；`value2` 为计划 ID | 计划执行跟踪写入 NVS 失败。运行期仍用 RAM 防止重复执行，并在后续调度 tick 重试保存。 |
| `record_store_recovered` | `warn` | `storage` | `meta_rebuilt` | `system:irrigation` | `code`/`value1` 为恢复后记录数；`value2` 为下一个记录 ID | 启动时从定长记录文件重建浇水记录元数据。 |
| `record_store_fault` | `error` | `storage` | `meta_save_failed` / `append_failed` | `system:irrigation` / `zone:<id>` | `meta_save_failed`：`code`/`value2` 为槽位、`value1` 为记录 ID；`append_failed`：`code` 为 `TaskResult`、`value1` 为水路号、`value2` 为计划 ID | 浇水记录写入或记录元数据保存失败。任务状态机仍完成关阀和安全处理，但历史记录可能缺失。 |
| `flow_fault` | `error` | `monitor` | `flow_start_timeout` / `flow_no_pulse_timeout` | `zone:<id>` | `code` 为 `TaskResult`；`value1` 目标秒；`value2` 脉冲数；`value3` 是否锁定 | 浇水任务因启动无水流或运行中断流停止。 |
| `leak_detected` | `error` | `monitor` | `idle_flow` | `zone:<id>` | `value1` 实际脉冲数；`value2` 阈值；`value3` 窗口秒 | 某一路待机状态检测到异常流量，疑似漏水、阀门粘连或流量计输入干扰。 |
| `zone_locked` | `error` | `monitor` | 水路异常原因 | `zone:<id>` | `code` 为 `ZoneErrorCode`；`value1` 为 `TaskResult` | 水路进入异常锁定，需要人工清除。 |
| `alert_cleared` | `info` | `web` | `zone` / `all_zones` | `zone:<id>` / `zone:all` | `value1` 可为水路号 | 用户清除单路或全部告警。 |
| `safety_stop` | `warn` | `monitor`/`runtime` | 停止原因 | `zone:<id>` | `code` 为 `TaskResult` | 安全策略主动停止浇水，例如漏水保护或恢复出厂保护。 |
| `factory_reset` | `warn`/`info`/`error` | `web`/`button`/`runtime` | `requested` / `executed` / `failed` | `system:irrigation` | `code`/`value1` 表示是否清空记录或执行结果 | 恢复出厂请求和执行结果。清空业务记录时会清空应用事件。 |

## 新增事件规则

新增事件前必须先判断：

> 这个事件是否能帮助用户理解灌溉业务为什么执行、跳过、停止或进入保护？

如果答案是否，就不要写入业务事件日志，应交给 `Esp32Base` 系统日志或普通调试日志。
