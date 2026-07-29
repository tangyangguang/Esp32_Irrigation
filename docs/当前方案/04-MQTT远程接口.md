# 04 MQTT 远程接口

## 目标与边界

MQTT 只把已经在本地 Web 中实现的日常能力安全映射到公网，不增加新的硬件动作、浇水模式、计划结构或配置副本。首次安装、WiFi、Web 认证、MQTT 凭据、流量校准、区域基准、保护参数、系统设置、日志、文件和 OTA 继续使用本地维护入口。

MQTT 远程入口允许：

- 查看设备、浇水、流量、自动总控、下一计划、存储故障和全部计划。
- 按当前保存的计划或本次六路时长启动普通手动浇水，并停止普通浇水。
- 无限期暂停、定时暂停和恢复自动浇水。
- 新建、完整修改、启停和删除现有 8 个每日计划。

所有动作调用 `IrrigationApp` 的现有公开入口。MQTT 断开、凭据错误、Broker 故障或 NTP 不可用不得影响本地自动调度、Web、保护逻辑和安全停机。

## 连接与私密配置

基础库使用单 Broker MQTT 3.1.1、Clean Session 和 MQTTS。设备 Client ID 及 Topic 设备段由 eFuse MAC 生成。每台设备使用独立 Broker 用户和 ACL。

带密码的本地配置统一保存在 Git 忽略的 `firmware/platformio.local.ini`；CA 保存在 Git 忽略的 `firmware/mqtt-ca.local.crt`。构建前脚本只在 `.pio` 构建目录生成静态 C++ 头文件，不输出配置值。仓库只提交 `platformio.example.ini` 模板。

## Topic

设本地配置前缀为 `irrigation`，设备 ID 为 `{deviceId}`：

| Topic | 方向 | QoS | retain | 内容 |
| --- | --- | --- | --- | --- |
| `irrigation/{deviceId}/availability` | 设备→Broker | 1 | 是 | `online`；LWT 为 `offline` |
| `irrigation/{deviceId}/meta` | 设备→Broker | 1 | 是 | 设备 ID、六路名称和启用状态、当前单路时长上限 |
| `irrigation/{deviceId}/state` | 设备→Broker | 1 | 是 | 当前业务摘要、设备时间、自动调度、异常水流观测和故障状态 |
| `irrigation/{deviceId}/run` | 设备→Broker | 1 | 是 | 当前普通浇水的完整步骤、进度、流量和水量；空闲时为 `active=false` |
| `irrigation/{deviceId}/latest` | 设备→Broker | 1 | 是 | 最近一次普通浇水的来源、时间、结果、停止原因和汇总 |
| `irrigation/{deviceId}/plan/{id}` | 设备→Broker | 1 | 是 | 单个计划完整状态和配置 revision |
| `irrigation/{deviceId}/command` | 客户端→设备 | 1 | 否 | 唯一命令入口 |
| `irrigation/{deviceId}/result` | 设备→Broker | 1 | 否 | 命令结果和最新 revision |

设备只订阅一个 `command` Topic。重连后分批发布当前 state、run、latest、meta 和 8 个计划；不保存过时遥测，不建立第二套离线队列。浇水活动期间 state 和 run 最多每 5 秒更新一次，任务结束后立即把 run 置为空闲并更新 latest；其它状态按变化发布。run 省略为零的步骤统计和没有发生的流量报警，避免六路任务超过 1024 字节业务 payload，不改变字段语义。

`state.ready` 表示灌溉业务能否安全接受控制命令；`state.ready_reason` 在就绪时固定为 `none`，未就绪时提供稳定、非敏感的机器可读原因，例如文件系统不可用、没有有效配置副本、默认配置写入失败或启动检查失败。远程客户端必须展示该原因并锁定控制操作，不能把未就绪误报为漏水，也不能通过 MQTT 暴露格式化文件系统等维护能力。

远程交互必须遵守本地 Web 的同一套用户规则和日常页面认知：只展示已启用水路；计划和手动时长使用 `meta.maximum_zone_duration_minutes`；手动浇水可从已配置计划填入临时副本但不修改原计划；停止整次浇水、手动启动、恢复自动浇水、无限期暂停、定时暂停和删除计划均显示明确确认。Mac 控制台的首页、运行态、手动浇水和计划管理直接采用本地 Web 的标题、状态优先级、说明、字段、禁用规则与响应式结构，不另建一套产品逻辑；校准、区域基准、系统设置、日志、OTA 和完整记录列表仍不进入远程日常入口。

`state.watering` 提供当前运行摘要；`run` 提供任务和当前水路用时、剩余时间、水流建立状态、当前/预期流量、脉冲、估算水量和步骤顺序；`latest` 提供首页最近一次结果所需的确定信息。`state.time` 提供可信状态、来源、当前 epoch 和 RTC 降级，远程页面以设备 epoch 为锚点在本机连续走秒；`state.unexpected_flow` 提供报警、观测就绪、窗口时长、脉冲和窗口平均估算流量。客户端不能因为状态未获取而自行猜测或启用操作。

## 命令信封

```json
{
  "v": 1,
  "id": "client-unique-id",
  "action": "stop",
  "args": {}
}
```

- `v` 固定为 `1`。
- `id` 为 1～48 字节，供本次启动内重复过滤和结果关联。
- 命令必须是 QoS 1、非 retained；retained 命令始终拒绝。
- 基础库使用 Clean Session，设备重启先安全关闭全部输出；第一版不为命令 ID 增加 NVS 写入。

### 计划完整保存

```json
{
  "v": 1,
  "id": "plan-save-12",
  "action": "set_plan",
  "revision": 12,
  "args": {
    "id": 1,
    "name": "早晨浇水",
    "enabled": true,
    "starts": [360, 1080],
    "durations": [10, 8, 0, 5, 0, 0]
  }
}
```

`starts` 是 0～1439 的本地 UTC+8 当日分钟，最多 4 项；`durations` 必须恰好 6 项，单位分钟。设备复制当前完整配置、替换指定计划，再复用现有跨计划冲突、区域、时长和运行上限校验及原子保存。`revision` 不等于当前值时返回 `revision_conflict`，不能覆盖 Web 或另一客户端的新修改。

其它 action：

- `delete_plan`：必须携带 `revision`，`args.id` 为计划 ID。
- `pause_automatic`：`args.resume_at=0` 表示无限期，否则为可信 epoch 恢复时间。
- `resume_automatic`。
- `start_plan`：`args.id` 指定当前已保存计划，按普通手动来源执行。
- `start_manual`：`args.durations` 为本次六路分钟数组，不修改计划。
- `stop`：停止当前普通浇水；空闲时幂等成功，校准或学习活动时拒绝。

## 结果与一致性

```json
{
  "v": 1,
  "id": "plan-save-12",
  "ok": true,
  "code": "saved",
  "revision": 13
}
```

结果码稳定区分解析失败、版本不支持、参数错误、重复、业务未就绪、忙、计划不存在、revision 冲突、配置校验/写入失败和硬件失败。Broker PUBACK 只表示 Broker 收到 MQTT 报文，不等于业务成功；客户端以 `result` 和随后 retained 状态为准。

计划保存继续使用现有完整副本、跨字段校验、临时文件、回读验证、备份和原子替换。浇水期间保存只影响下一次任务，当前会话继续使用启动快照。

## 资源与验收

- 不增加业务任务、锁、动态离线队列和第二套状态机。
- 命令和每条状态 JSON 上限 1024 字节；state、run、latest、meta、单计划和结果分别发布。
- 后台状态发布在 QoS 1 in-flight 少于 3 时每轮最多提交一条，给命令结果保留容量。
- 真实发布必须验证 Flash、静态 RAM、MQTT task stack、TLS 握手峰值、稳定连接 heap 和连续重连后的 minimum heap。
- 必须实机验证 Broker/WiFi 重启、modem sleep、重复命令、Web/MQTT revision 冲突、OTA 暂停恢复和 MQTT 故障下本地计划继续执行。
