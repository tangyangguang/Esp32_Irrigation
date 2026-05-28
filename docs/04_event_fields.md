# 事件字段语义

`EventStore::Event` 是固定长度环形事件记录。系统事件用于内部事实流和联调诊断；正式业务历史记录以 `RecordStore` 的浇水任务记录为准。

通用字段：

- `type`：事件类型。
- `source`：事件来源，取值为 `system`、`button`、`web`、`plan`。
- `road`：关联路号；`0` 表示整机事件或不适用。
- `code`：事件内的枚举码。
- `value1` / `value2`：按事件类型解释的两个数值槽位。
- `text`：短文本原因，最多 31 字符。

## 类型对照

| type | code | value1 | value2 | 说明 |
|---|---:|---:|---:|---|
| `boot` | 是否 WDT 复位 | 启动计数 | 重启日志计数 | 应用初始化完成；`text` 为 reset reason。 |
| `config_changed` | 0 | 配置值 | 0 | Web 保存配置时 `value1` 为启用路 mask；按键锁定时 `value1` 为 1/0。 |
| `plan_changed` | 0 | 计划索引或日期 | 周期天数或 0 | 保存计划、跳过计划或取消跳过计划。 |
| `water_start` | `RecordStore::TaskType` | 目标秒数 | 计划槽或 `0xFF` | `road` 为目标水路；`source` 区分按钮、Web/API 或计划。 |
| `water_stop` | `RecordStore::Result` 或计划结果值 | 目标秒数或计划结果值 | 脉冲增量或目标秒数 | 路级任务停止时保存执行结果；计划调度结果事件的 `source=plan`，`text` 说明启动或跳过原因。 |
| `water_error` | `RecordStore::Result` | 目标秒数 | 脉冲增量 | 路级水流异常停止。 |
| `leak_alert` | 0 | 窗口内脉冲增量 | 告警阈值 | 待机漏水或阀门粘连告警。 |
| `alert_clear` | 0 | 0 | 0 | 告警清除；`source` 区分 Web 或系统来源。 |
| `factory_reset_requested` | 是否清记录 | 0 | 0 | Web 请求时 `code=1` 表示同时清空记录和事件；BOOT 长按固定为 0。 |
| `factory_reset_executed` | 0 | 是否成功 | 0 | 仅在恢复出厂保留事件时写入。 |
| `wifi_status_changed` | `Esp32BaseWiFi::State` | RSSI | 0 | 应用启动和 WiFi 关键状态变化。 |
| `ota_status_changed` | `Esp32BaseOta::Status` | 已写入字节 | 总字节 | OTA 开始、成功、失败。 |

## 计划触发结果

计划触发结果的正式业务状态保存在 `PlanResultStore`，用于近期计划页面和 `/api/v1/plans/recent`。事件流中的 `water_stop` 只作为补充诊断。

当前计划结果：

- `started`
- `skipped_manual`
- `skipped_road_disabled`
- `skipped_road_busy`
- `rejected`
- `config_invalid`
- `factory_reset_pending`
- `leak_alert`

## 约束

- 新增事件类型时，必须同时更新本文件、`EventStore::typeName()` 和相关 JSON 验证项。
- 新增计划触发结果时，必须同时更新 `PlanResultStore::resultName()`、`PlanResultStore::resultLabel()` 和 Web/API 展示。
- 新增浇水执行结果时，必须同时更新 `RecordStore::resultName()`、历史记录页面和记录 JSON。
