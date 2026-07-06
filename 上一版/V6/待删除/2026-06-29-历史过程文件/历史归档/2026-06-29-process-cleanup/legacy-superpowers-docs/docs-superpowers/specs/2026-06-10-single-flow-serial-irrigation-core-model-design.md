# Single Flow Serial Irrigation Core Model

## Goal

本设计将灌溉控制器核心模型收敛为家用/小型灌溉场景的稳定方案：

```text
1 个流量计，必配，不提供禁用配置
最多 6 路 Zone，每路 Zone 对应 1 个电磁阀
同一时间只允许 1 路 Zone 运行
可选 1 路自吸泵启动输出，默认不启用
可选 1 路低液位输入，默认不启用
```

本设计取代此前默认的 `2 Flow / 6 Zone` 模型。第二流量计、多供水域、同供水域多 Zone 并发、动态流量管理均不进入当前核心模型。

核心目标是可靠确认实际出水、记录当前 Zone 的真实浇水量，并降低家庭自来水、低压水塔、小型自吸泵等场景下的压力和诊断复杂度。

## Professional Review Summary

从专业灌溉设备角度看，多个流量计和多个 Flow Zone 是存在的，但它们对应的是多个真实水力单元、多个主管、多个主阀或大型商业系统。把这种能力直接作为家庭设备默认模型，会增加配置、调度、校准、告警和 UI 复杂度。

本项目的目标是家用/小型自动浇水控制器。成熟且更合适的默认拓扑是：

```text
水源
 -> 过滤/必要的减压或防回流
 -> 可选自吸泵或主执行器
 -> 1 个流量计
 -> 分水器
 -> Zone 1 电磁阀
 -> Zone 2 电磁阀
 -> ...
 -> Zone 6 电磁阀
```

系统一次只打开一个 Zone，因此流量计读数天然归属于当前 Zone。这样可以清楚判断：

```text
有没有实际出水
当前 Zone 的实时流量
当前 Zone 的累计浇水量
当前 Zone 是否无水、低流量、高流量或异常漏水
```

此模型也更适合 3 米左右水塔、家庭自来水、小型自吸泵等供水能力有限的场景。

设计依据：

```text
Hunter / Hydrawise 家用流量计指南将流量计放在 master valve 和 zone valves 之间；只有阀组不集中时才需要多个流量计。
Rain Bird flow sensor 指南建议每个 master valve 下游、各 station/zone valves 上游安装一个 flow sensor。
Hunter ACC2 / Rain Bird LXD 等专业控制器支持多个 Flow Zone 和多站点流量管理，但这是大型/商业水力单元能力，不适合作为本项目默认复杂度。
Rain Bird / Hunter 的泵启动说明均把泵作为外部继电器/接触器控制对象，控制器输出 Pump/Master Valve 信号，不直接给泵主回路供电。
```

参考资料：

```text
https://www.hunterirrigation.com/support/hydrawise-flow-meter-install-guide
https://www.rainbird.com/sites/default/files/media/documents/2023-02/man_esp-lxd-flowsensorinstallationguide_-_sd211_update_-_feb_2023.pdf
https://www.hunterirrigation.com/support/acc2-set-flow-manager
https://wifi.rainbird.com/articles/wiring-a-pump-start-relay-to-the-rc2-controller/
https://www.hunterindustries.com/support/x2-connecting-pump-start-relay
```

## Terminology

```text
FlowMeter
  唯一流量计，安装在所有 Zone 电磁阀上游。系统必须接入并使用它。

Zone
  一个电磁阀控制的一整路水路。系统最多支持 6 个 Zone。

Valve
  Zone 的执行器。每个 Zone 固定 1 路阀门输出。

PumpStart
  可选自吸泵启动输出。它只提供控制信号，不直接承载泵主电流。

LowLevel
  可选低液位输入。它只用于水桶/水肥桶等泵供水场景的缺液保护。
```

## Hardware Interfaces

板级接口固定为：

```text
FLOW_IN x1
VALVE_OUT_1 ... VALVE_OUT_6
PUMP_START_OUT x1
LOW_LEVEL_IN x1
```

### FLOW_IN

`FLOW_IN` 是唯一流量计输入，必接。系统不提供关闭流量计的运行模式。没有有效流量计时，设备不能满足“确认有水”和“统计浇水量”的产品目标。

流量计输入应按脉冲型流量计设计，使用外部上拉、输入保护、滤波和中断/计数逻辑。具体 K 值、Offset、最小有效频率、采样窗口仍属于后续计量参数设计。

### VALVE_OUT

最多支持 6 路电磁阀输出。每一路电磁阀对应一个 Zone。

阀门输出可以继续使用 MOSFET 驱动和吸合/保持策略，但调度层必须保证任意时刻最多只有一个 Zone 输出处于运行态。停止、异常保护、系统重启和看门狗恢复时必须默认关阀。

### PUMP_START_OUT

`PUMP_START_OUT` 是可选输出，默认不启用。启用后，设备在运行 Zone 时同步管理自吸泵启动/停止。

该输出是泵启动控制信号，不是泵电源输出。推荐硬件形态为：

```text
ESP32 GPIO
 -> 光耦或晶体管/MOSFET 驱动
 -> 干接点继电器或开集电极/开漏控制输出
 -> 外部泵启动继电器、接触器或泵控制模块
```

主板不直接承载 220V/110V 交流泵或大电流直流泵主回路。若现场使用直流自吸泵，也应由外部功率模块按泵的堵转电流、保险、TVS、散热和线径单独设计。

不做 PWM 调速。泵控制只有开/关两个状态。调压、恒压、变频和泵内部保护属于泵或外部泵控制器职责。

### LOW_LEVEL_IN

`LOW_LEVEL_IN` 是可选输入，默认不启用。它与 `PUMP_START_OUT` 分开配置：

```text
泵控制可以启用或不启用
低液位输入可以启用或不启用
二者不强制绑定
```

推荐支持两线制浮球液位开关，作为干接点/磁簧开关输入。只做低液位保护，不做连续液位测量，也不管理水塔或水桶补水。

推荐电气定义：

```text
水位正常：触点闭合，LOW_LEVEL_IN 被拉到低电平
低液位或线缆断开：触点断开，LOW_LEVEL_IN 为高电平
```

这样低液位、线断、接触不良都按不安全状态处理。接口可设计为 2 针：

```text
LEVEL_IN
GND
```

如需兼容少量三线低压传感器，可预留 3 针：

```text
3V3
LEVEL_IN
GND
```

但正式支持范围仍限定为干接点或开漏输出。不得把 5V/12V/24V 推挽输出直接接入 ESP32 输入。

## Configuration Model

系统配置中不再出现多个 Flow 或 Zone 归属 Flow 的配置。

固定能力：

```text
flowMeter.enabled = true
maxZones = 6
zoneConcurrency = serial_only
```

可配置能力：

```text
pumpStart.enabled: bool, default false
lowLevel.enabled: bool, default false
lowLevel.activePolicy: fixed fail-safe policy
```

`lowLevel.activePolicy` 当前不建议暴露成普通用户配置。硬件安装应统一为“水位正常闭合，低液位断开”。如果未来确实要兼容相反动作的传感器，应作为安装向导中的高级选项，并要求现场测试确认。

Zone 配置只需要描述当前水路本身：

```text
zoneId: 1..6
enabled
name
valveOutput
defaultDuration
flowBaseline for this Zone
fault policy for this Zone
```

不再需要：

```text
flowId
flow enabled/disabled
不同 Flow 并行规则
同 Flow 互斥队列
Flow 级漏水阻断归属关系
```

## Runtime Model

### Serial Rule

任意时刻只允许一个 Zone 运行。无论手动启动、计划启动、Web/API 启动、本地按键启动，都必须通过同一运行仲裁：

```text
如果已有 Zone 正在运行，则拒绝新的 Zone 启动或进入串行队列
不允许两个 Zone 同时输出
不提供并发数量配置
不提供组合浇水配置
```

是否采用内存队列属于调度细节，但执行层必须保持串行。

### Start Sequence

推荐启动顺序：

```text
1. 校验没有其他 Zone 正在运行。
2. 校验目标 Zone enabled 且无阻断故障。
3. 如果 lowLevel.enabled，则读取低液位输入；低液位时拒绝启动并报警。
4. 打开目标 Zone 电磁阀。
5. 如果 pumpStart.enabled，则启动 PUMP_START_OUT。
6. 进入出水确认窗口。
7. 使用 FLOW_IN 判断是否有有效水流。
8. 有效出水后进入正常浇水计量。
```

先开阀再启动泵，可以避免泵对封闭管路瞬间憋压。若后续特定阀门或泵要求相反顺序，应基于实机验证调整为可控延迟策略，而不是开放复杂用户配置。

### Run Sequence

运行期间持续监测：

```text
实时流量
累计水量
无脉冲超时
低流量
高流量
可选低液位输入
用户 Stop / Stop All
```

因为系统只运行一个 Zone，所有流量统计都直接归属于当前 Zone，无需在多个 Zone 之间拆分估算。

### Stop Sequence

推荐停止顺序：

```text
1. 如果 pumpStart.enabled，关闭 PUMP_START_OUT。
2. 短延迟释放水力状态。
3. 关闭当前 Zone 电磁阀。
4. 固化本次记录：zoneId、开始/结束时间、运行时长、累计水量、最小/最大/平均流量、停止原因。
```

异常停止和 Stop All 必须优先让系统进入无输出状态。若实现上为保证安全需要先关阀再关泵，必须通过实际泵/阀验证后在实现计划中明确。

## Fault Handling

### No Water

出水确认窗口内没有有效流量，判定为无水或泵未出水：

```text
关闭泵
关闭阀
记录 no_water 故障
报警
阻止当前 Zone 后续自动运行，直到用户确认或清除
```

这适用于自来水断水、水塔无水、泵不上水、阀未打开、流量计故障等情况。设备只负责灌溉侧检测，不管理水塔补水。

### Low Level

仅当 `lowLevel.enabled` 时启用。

启动前低液位：

```text
拒绝启动
记录 low_level
报警
不启动泵
不打开阀或立即确保关阀
```

运行中低液位：

```text
关闭泵
关闭阀
记录 low_level_stop
报警
```

低液位输入未启用时，系统不读取该输入，也不因为输入状态阻止泵启动。此时仍依靠流量计的无水检测保护。

### Leak / Unexpected Flow

没有任何 Zone 运行时，如果流量计出现持续脉冲，应判定为待机漏水或阀门未关严：

```text
记录 idle_leak
报警
阻止自动启动，直到用户确认或清除
```

因为只有一个流量计和一个运行域，漏水阻断是全局的，不需要按 Flow 归属拆分。

## Scope Boundaries

当前模型明确不做：

```text
多个流量计
多个供水域
同一时间多个 Zone 同开
按目标流量动态组合 Zone
水塔液位管理或补水控制
水桶补水控制
泵 PWM 调速
恒压控制
连续液位测量
天气联动内置到核心控制器
```

这些能力如未来需要，应作为新设计重新确认，不在当前核心模型里预留复杂配置。

## Validation Requirements

软件验证：

```text
任意入口启动 Zone 时都不能并发运行
已有 Zone 运行时，新启动请求被拒绝或排队但不并发
流量计读数只归属当前运行 Zone
pumpStart.enabled=false 时不输出 PUMP_START
pumpStart.enabled=true 时按启动/停止序列输出 PUMP_START
lowLevel.enabled=false 时忽略 LOW_LEVEL_IN
lowLevel.enabled=true 时低液位阻止启动或触发停机
待机流量触发全局 idle_leak
```

硬件/实机验证：

```text
唯一流量计在目标水流范围内计数稳定
6 路电磁阀逐路吸合/保持/关闭可靠
任意复位、断电、看门狗恢复后默认关阀、关泵
PUMP_START 只能驱动外部泵启动继电器/接触器，不承载泵主电流
LOW_LEVEL_IN 对低液位、断线、抖动都有安全响应
自来水、水塔低压、自吸泵供水三类场景下无水报警可靠
```

涉及泵、电磁阀、流量计、水塔低压和液位输入的结论必须区分“已实机验证”和“未实机验证”。

## Self Review

本设计已按以下点复审：

```text
没有保留旧 2 Flow 默认模型
没有引入多 Zone 并发配置
没有把水塔管理越界放入灌溉控制器
流量计保持必配，满足有水检测和水量统计目标
自吸泵控制和低液位输入保持可选且相互独立
硬件接口数量与目标能力一致：1 Flow、6 Valve、1 PumpStart、1 LowLevel
```
