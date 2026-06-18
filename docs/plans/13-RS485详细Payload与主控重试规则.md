# 13 RS485 详细 Payload 与主控重试规则

状态：成熟产品 RS485 详细协议草案。本文补充 `02-RS485通讯协议设计草案.md` 中尚未细化的 payload、字节序、主控重试、离线判定和疑似地址冲突处理。

关联文档：

- `02-RS485通讯协议设计草案.md`
- `03-从站运行状态机.md`
- `11-统一枚举与错误码表.md`
- `12-从站安全配置语义与持久化边界.md`

## 通用编码规则

- 多字节整数统一使用小端序。
- 无符号整数按 `uint8/uint16/uint32` 表达。
- 有符号整数按 `int16/int32` 表达。
- 时间单位写入字段名，常用 `_ms`、`_sec`。
- 电压单位使用 `_mv`。
- 流量单位使用 `_ml`、`_ml_min`。
- 位图从低位开始编号，bit0 对应第 1 路资源。
- 未使用字段必须置 0。
- 保留字段接收方必须忽略，发送方必须置 0。

## 帧确认规则

主控发送请求后，只接受满足以下条件的应答：

1. SOF 正确。
2. CRC 正确。
3. `VER` 支持。
4. `ADDR` 等于请求地址。
5. `SEQ` 等于请求序号。
6. `CMD` 等于请求命令。
7. Payload 长度符合命令定义。

不满足条件的帧：

- 不执行任何业务状态变化。
- 主控记录通信异常计数。
- 运行中从站的本地通信超时仍由从站自行判断。

## `PING`

请求 Payload：

```text
无
```

应答 Payload：

```text
result_code      uint8
uptime_sec       uint32
state            uint8
```

用途：

- 在线探测。
- 空闲从站低成本轮询。

## `GET_INFO`

请求 Payload：

```text
无
```

应答 Payload：

```text
result_code        uint8
hw_type            uint8
hw_revision        uint8
fw_major           uint8
fw_minor           uint8
fw_patch           uint8
dipswitch_addr     uint8
capability_flags   uint32
device_uid         uint32
```

`device_uid`：

- 如果从站固件或芯片无法提供唯一 ID，可使用 0。
- 主控不能依赖它解决地址冲突。
- 如果后续确认 STC8H8K64U 有可靠唯一 ID，可在基础库或板级能力中统一提供。

`capability_flags`：

| 位 | 含义 |
|---:|---|
| 0 | 支持 8 路阀输出 |
| 1 | 支持 4 路数字输入 |
| 2 | 支持 1 路流量计 |
| 3 | 支持低液位输入 |
| 4 | 支持 1 路干接点输出 |
| 5 | 支持系统电压 ADC |
| 6 | 支持本地安全配置持久化 |
| 7 | 支持阀门 PWM 保持 |

## `GET_CONFIG_SUMMARY`

请求 Payload：

```text
无
```

应答 Payload：

```text
result_code       uint8
config_valid      uint8
schema_version    uint8
reserved          uint8
config_version    uint32
config_crc16      uint16
config_status_flags uint16
```

`config_status_flags`：

| 位 | 含义 |
|---:|---|
| 0 | 本地存在安全配置 |
| 1 | 本地安全配置有效 |
| 2 | 最近一次配置应用失败 |
| 3 | 本地持久化状态异常 |

## `SET_SAFE_CONFIG`

请求 Payload：

```text
config_version          uint32
schema_version          uint8
payload_len             uint16
payload_crc16           uint16
payload                 N bytes
```

应答 Payload：

```text
result_code             uint8
config_valid            uint8
schema_version          uint8
reserved                uint8
config_version          uint32
config_crc16            uint16
```

规则：

- 从站处于 `STARTING`、`RUNNING`、`STOPPING` 时拒绝写入，返回 `ERR_BUSY` 或 `ERR_UNSAFE_STATE`。
- `payload_crc16` 必须覆盖 payload。
- payload 内部结构见下节。
- 配置应用后的状态语义见 `12-从站安全配置语义与持久化边界.md`。具体持久化介质和算法不由协议文档决定。

## Safe Config Payload

`payload` 固定字段顺序：

```text
station_addr              uint8
input_role[4]             uint8[4]
input_active_level[4]     uint8[4]
input_debounce_ms[4]      uint16[4]
flow_input_index          uint8
flow_edge                 uint8
low_level_enabled         uint8
low_level_input_index     uint8
low_level_active_level    uint8
low_level_confirm_ms      uint16
pump_enabled              uint8
pump_start_delay_ms       uint16
pump_stop_delay_ms        uint16
master_valve_index        uint8
master_start_delay_ms     uint16
master_stop_delay_ms      uint16
branch_valve_bitmap       uint8
valve_pull_in_ms          uint16
valve_pwm_freq_hz         uint16
valve_hold_percent        uint8
comm_timeout_ms           uint16
max_run_sec               uint16
voltage_low_warn_mv       uint16
voltage_low_stop_mv       uint16
voltage_high_stop_mv      uint16
reserved                  uint8[8]
```

编码规则：

- `input_role`: 0=disabled, 1=flow_meter, 2=low_level, 3=reserved。
- `input_active_level`: 0=low, 1=high。
- `flow_input_index`: 1..4。
- `flow_edge`: 0=rising, 1=falling。
- `low_level_input_index`: 0=不启用，1..4。
- `master_valve_index`: 0=无主阀，1..8。
- `branch_valve_bitmap`: bit0 对应 VALVE1，bit7 对应 VALVE8。
- `valve_hold_percent`: 0..100。

## `GET_SAFE_CONFIG`

请求 Payload：

```text
无
```

应答 Payload：

```text
result_code             uint8
config_valid            uint8
schema_version          uint8
payload_len             uint16
config_version          uint32
config_crc16            uint16
payload                 N bytes
```

用途：

- Web 维护页核对从站实际配置。
- 实机调试时确认从站保存内容。

## `START_ZONE`

请求 Payload：

```text
run_id                    uint32
config_version            uint32
zone_index                uint8
duration_sec              uint16
expected_flow_ml_min      uint32
low_flow_percent          uint8
high_flow_percent         uint8
abnormal_confirm_sec      uint16
first_flow_timeout_sec    uint16
no_flow_timeout_sec       uint16
stabilize_sec             uint16
flags                     uint16
```

`flags`：

| 位 | 含义 |
|---:|---|
| 0 | 启用低流量停机；0 表示低流量只告警 |
| 1 | 高流量停机 |
| 2 | 无水停机 |
| 3 | 低液位停机 |

应答 Payload：

```text
result_code       uint8
run_id            uint32
state             uint8
stop_reason       uint8
```

规则：

- `expected_flow_ml_min=0` 表示该水路未学习标准流速，从站不做低/高流量判断。
- 无水、低液位、通信超时、运行超时，以及已启用电压保护并确认电压严重异常不依赖标准流速。
- 从站接受命令后进入 `STARTING`。

## `STOP`

请求 Payload：

```text
run_id            uint32
reason            uint8
```

应答 Payload：

```text
result_code       uint8
run_id            uint32
state             uint8
stop_reason       uint8
```

规则：

- `reason` 通常为 `USER_STOP`。
- 如果 `run_id` 不匹配但从站正在运行，返回 `ERR_BAD_VALUE`，不停止，除非后续明确增加强制停止标志。
- 维护页 `stop-all` 需要逐个从站发送当前 `run_id` 的 `STOP`。

## `GET_STATUS`

请求 Payload：

```text
无
```

应答 Payload：

```text
result_code          uint8
state                uint8
fault_flags          uint32
run_id               uint32
active_zone          uint8
valve_output_bitmap  uint8
input_bitmap         uint8
raw_input_bitmap     uint8
flow_pulse_count     uint32
flow_pulse_rate_min  uint32
flow_rate_ml_min     uint32
flow_value_flags     uint16
voltage_mv           uint16
elapsed_sec          uint16
target_duration_sec  uint16
uptime_sec           uint32
config_version       uint32
last_stop_reason     uint8
```

说明：

- `valve_output_bitmap` bit0..bit7 对应 VALVE1..VALVE8。
- `input_bitmap` 表示经过有效电平转换后的逻辑激活状态。
- `raw_input_bitmap` 表示原始电平读取状态。
- `active_zone=0` 表示当前无分支水路运行。
- `flow_pulse_count` 当前运行中表示本次累计，空闲时表示最近一次运行累计。
- `flow_pulse_rate_min` 表示当前窗口脉冲率，流量计计量未校准时仍可用于判断是否有流量。
- `flow_rate_ml_min` 仅在计量有效时表示估算流速；计量无效时必须为 0 或仅作无效值处理。

`flow_value_flags`：

| 位 | 含义 |
|---:|---|
| 0 | 流量计输入已配置 |
| 1 | `pulses_per_liter` 已校准 |
| 2 | `flow_rate_ml_min` 有效 |
| 3 | 本次累计水量可估算 |

## `CLEAR_FAULT`

请求 Payload：

```text
clear_flags       uint32
```

应答 Payload：

```text
result_code       uint8
state             uint8
fault_flags       uint32
```

规则：

- 只能清除可恢复故障。
- 存储错误、配置无效、地址/总线冲突等不能通过普通清除绕过。
- 清除后从站仍需满足配置有效、输出关闭等条件才可进入 `IDLE`。

## `FACTORY_RESET_CONFIG`

请求 Payload：

```text
confirm_code      uint32
```

应答 Payload：

```text
result_code       uint8
state             uint8
```

规则：

- 只清空从站本地安全配置。
- 不修改拨码地址。
- 执行前主控 Web 必须二次确认。
- 执行后从站进入 `UNCONFIGURED`，全部输出关闭。

## 主控重试规则

建议默认：

```text
request_timeout_ms = 150
max_retries_idle = 2
max_retries_running = 1
offline_fail_count = 3
conflict_suspect_count = 3
```

规则：

- 空闲从站请求失败可重试 2 次。
- 运行中从站优先快速轮询，不宜长时间阻塞总线，单次失败重试 1 次。
- `SET_SAFE_CONFIG` 属于配置应用请求，主控必须把它与普通状态查询区分处理，不能在从站运行中执行。
- 连续失败达到 `offline_fail_count` 后，主控标记从站离线或通信异常。
- 正在运行的从站通信异常时，主控记录事件；从站本地按 `comm_timeout_ms` 安全停机。
- 主控不得因为自己未收到应答就假定从站已经停机。

## 轮询优先级

同一时间总线上只允许一个未完成请求。

轮询优先级：

1. 正在运行的从站。
2. 正在启动或停止的从站。
3. 有未处理故障的从站。
4. 在线空闲从站。
5. 离线重试从站。

建议周期：

- 运行中：`500 ms..1 s`。
- 启动/停止中：`200 ms..500 ms`，直到状态稳定。
- 空闲在线：`1 s..3 s`。
- 离线：退避到 `3 s`、`5 s`、`10 s`。

## 疑似地址冲突或总线冲突

RS485 总线上多个从站使用相同地址时，主控通常不能可靠区分具体设备，只能识别通信异常模式。

判定为疑似冲突的条件：

- 同一地址连续出现 CRC 错误或乱码。
- 同一请求偶尔返回不同 `fw_version`、`capability_flags`、`config_version`。
- 同一地址应答长度不稳定。
- 离线/在线状态在短时间内异常抖动。

处理：

- 标记该地址 `station_address_conflict`。
- 禁止该地址运行。
- Web 提示用户逐个接入从站或检查拨码地址。
- 不尝试通过广播或强制命令修复地址。

## 方案层通信评估项

### 普通请求超时

当前不能确定：

- `request_timeout_ms=150` 在实际线长、实际接线、实际从站数量下是否足够。

不能确定的原因：

- RS485 线缆长度、接地、端接、电源噪声和从站数量会直接影响应答稳定性。
- 当前值来自协议估算，不是目标整机实测结果。

后续实施阶段需要验证：

1. 使用默认 `19200 bps`。
2. 连接计划支持的最大从站数量，至少包含 2 个正在运行从站和若干空闲从站。
3. 连续执行 `PING`、`GET_STATUS`、`GET_CONFIG_SUMMARY` 各 1000 次。
4. 记录请求耗时最大值、平均值、超时次数、CRC 错误次数。
5. 在线缆最长、阀门吸合、自吸泵动作、电源波动场景下重复测试。

方案层通过标准：

- 普通请求 1000 次中无连续 3 次失败。
- 运行中从站状态刷新不超过 `1 s`。
- 主控不会因为某个地址超时阻塞其他运行中从站超过 `1 s`。

在实测完成前的产品策略：

- 保持默认 `19200 bps`。
- `request_timeout_ms` 不低于 `150 ms`。
- 运行中从站本地 `comm_timeout_ms` 不低于主控最坏轮询周期的 3 倍。

### 配置应用请求

当前不能确定：

- 从站完成本地配置应用和状态确认所需的实际时间。

后续实施阶段需要验证：

1. 从站处于 `IDLE`，所有输出关闭。
2. 主控连续下发 100 次不同 `config_version` 的 `SET_SAFE_CONFIG`。
3. 从站完成配置校验、应用和状态确认。
4. 主控记录每次请求耗时、失败原因和重试次数。

方案层通过标准：

- 配置应用成功后，主控和从站报告同一个 `config_version` 与 `config_crc16`。
- 配置应用失败时，主控标记 `config_sync_state=failed`，禁止运行。
- 配置应用失败不能让从站进入半配置可运行状态。

在实测完成前的产品策略：

- `SET_SAFE_CONFIG` 不允许在从站运行中执行。
- 应用失败不允许启动该从站。
- 主控显示配置未同步，不允许启动该从站。

### 多从站并行轮询

当前不能确定：

- 多从站并行运行时，单主站轮询是否满足页面显示和从站通信超时安全窗口。

后续实施阶段需要验证：

1. 至少接入 4 个从站，其中 2 个同时运行，2 个空闲。
2. 两个运行从站分别触发正常流量状态上报。
3. 主控按轮询优先级运行 24 小时。
4. 记录每个从站最大状态间隔、超时次数、丢包次数和 Web 状态过期次数。

方案层通过标准：

- 运行中从站最大状态间隔不超过 `1 s`，启动/停止中不超过 `500 ms`。
- 单个从站异常不导致其他运行从站状态间隔超过 `1 s`。
- 24 小时内没有误触发通信超时停机。

在实测完成前的产品策略：

- 默认运行中轮询周期取 `1 s` 内保守值。
- 离线从站退避轮询，不能长期占用总线。

### 疑似地址或总线冲突

当前不能确定：

- 现场同地址冲突、A/B 反接、端接异常和噪声造成的错误模式能否被稳定区分。

后续实施阶段需要验证：

1. 人为设置两个从站同一拨码地址。
2. 执行扫描和 `GET_INFO`、`GET_STATUS` 轮询。
3. 分别测试 A/B 断开、A/B 短时干扰、端接电阻缺失。
4. 记录 CRC 错误、应答长度异常、版本矛盾、在线抖动。

方案层通过标准：

- 主控能把相关地址标记为 `station_address_conflict` 或总线异常。
- 被标记地址不能启动运行。
- Web 提示用户逐个接入或检查拨码地址。

在实测完成前的产品策略：

- 只要疑似冲突计数达到 `conflict_suspect_count`，就禁止该地址运行。
- 不通过广播或强制命令尝试自动修复。
