# 事件字段语义

`EventStore::Event` 是固定长度环形事件记录。通用字段含义如下：

- `type`：事件类型。
- `source`：事件来源，取值为 `system`、`button`、`web`、`plan`。
- `road`：关联路号；`0` 表示整机会话、全局事件或不适用。
- `code`：事件内的枚举码，通常保存执行模式或停止原因。
- `value1` / `value2`：按事件类型解释的两个数值槽位。
- `text`：短文本原因，最多 31 字符。

## 类型对照

| type | code | value1 | value2 | 说明 |
|---|---:|---:|---:|---|
| `boot` | 0 | 0 | 0 | 应用初始化完成。 |
| `config_changed` | 0 | 配置值 | 0 | Web 保存配置时 `value1` 为启用路 mask；按键锁定时 `value1` 为 1/0。 |
| `plan_changed` | 0 | 计划索引或日期 | 周期天数或 0 | 保存计划时为计划索引和周期天数；跳过计划时为 `ymd` 和 0。 |
| `water_start` | 执行模式 | 第 1 路目标秒数 | 第 2 路目标秒数 | `code` 使用 `SettingsStore::ExecutionMode`。 |
| `water_stop` | 停止原因 | 第 1 路目标秒数 | 第 2 路目标秒数 | `code` 使用 `WateringSession::StopReason`；单路停止只填写对应路槽位。 |
| `water_error` | 停止原因 | 脉冲数或第 1 路目标秒数 | 超时秒数或第 2 路目标秒数 | 路级无脉冲异常时 `road>0`，`value1` 为本路脉冲增量、`value2` 为超时秒数；会话级错误时 `road=0`，两个值为目标秒数。 |
| `leak_alert` | 0 | 窗口内脉冲增量 | 告警阈值 | 待机漏水或阀门粘连告警。 |
| `alert_clear` | 0 | 0 | 0 | 告警清除；`source` 区分 Web 或系统来源。 |
| `factory_reset_requested` | 是否清记录 | 0 | 0 | Web 请求时 `code=1` 表示同时清空记录和事件；BOOT 长按固定为 0。 |
| `factory_reset_executed` | 是否清记录 | 是否成功 | 0 | `code=1` 表示同时清空记录和事件，`value1=1` 表示各清理步骤均成功。 |

## 约束

- 新增事件类型时，必须同时更新本文件、`EventStore::typeName()` 和相关 CSV/JSON 验证项。
- 新增 `StopReason` 或 `ExecutionMode` 时，必须确认 `code` 的导出解释仍然准确。
