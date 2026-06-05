# Two Flow Six Zone Irrigation Redesign

## Goal

本设计将灌溉业务核心重写为固定上限的专业模型：

```text
最多 2 个流量计
最多 6 路水路/电磁阀
每路水路任意归属 Flow 1 或 Flow 2
同一 Flow 下水路互斥运行
不同 Flow 下水路可并行运行
流量计使用 K + Offset 线性校准
```

本次重构不考虑旧配置、旧记录、旧 API、旧页面或旧数据结构兼容。设备会格式化后部署，新实现可以直接使用新的 namespace、schema、文件路径和 API 语义。旧的 4 路固定 `Zone -> Flow` 一一对应模型不保留。

项目仍然遵守既有边界：应用层只实现灌溉业务；WiFi、NTP、mDNS、Web、OTA、日志、文件系统等基础能力继续使用 `Esp32Base`。

## Terminology

术语固定如下：

```text
FlowMeter / Flow
  主管、水源组或阀组前的流量计。一个 Flow 可以管理多个 Zone。

Zone
  一个电磁阀控制的一整路水路。电磁阀后面的 9/12 管、8mm 分管、三通和喷头都属于该 Zone 的末端分支。

Valve
  Zone 的执行器。每个 Zone 固定一个阀门 PWM 输出。
```

不再把电磁阀后面的 8mm 小管称为系统级支路。系统级最小调度单位是 Zone。

## Hardware Model

推荐硬件结构：

```text
水塔
 -> 过滤器
 -> 增压泵
 -> Flow 1
 -> 分水器
 -> Valve 1 -> Zone 1
 -> Valve 2 -> Zone 2
 -> Valve 3 -> Zone 3
 -> Valve 4 -> Zone 4
 -> Valve 5 -> Zone 5
 -> Valve 6 -> Zone 6
```

如果现场存在两条真实独立主管，可配置为：

```text
主管 A -> Flow 1 -> Zone 1 / Zone 3 / Zone 5
主管 B -> Flow 2 -> Zone 2 / Zone 4 / Zone 6
```

也可以配置为：

```text
主管 A -> Flow 1 -> Zone 1
主管 B -> Flow 2 -> Zone 2 / Zone 3 / Zone 4 / Zone 5 / Zone 6
```

Zone 到 Flow 的归属不要求连续、不要求按编号分组，完全按真实水管拓扑配置。

Flow 1 和 Flow 2 应代表两条不同供水管上的不同计量点。不要在同一根主管上串联两个 Flow 后把它们当成两个独立组使用。

本系统不建模水塔、泵或供水源为独立软件资源。软件规则只基于 Flow 归属判断互斥和并行。现场部署时必须注意：如果 Flow 1 和 Flow 2 共用同一个水塔、同一个增压泵或同一条供水能力有限的主管，虽然软件允许不同 Flow 下的 Zone 并行，实际运行仍可能导致压力下降、喷头不均、低流量误报或泵负载异常。推荐连接方式是一个泵下默认顺序浇水；只有确认 Flow 1 和 Flow 2 对应的真实供水能力足够时，才配置同时运行的计划。

## Pin Model

引脚模型从固定 4 路改为：

```text
MaxFlowMeters = 2
MaxZones = 6
```

`Pins.h` 应只描述板级物理引脚：

```text
Flow1Input
Flow2Input
Valve1Output ... Valve6Output
I2C SDA/SCL
5 个本地按钮
状态灯
```

GPIO35/36/39 等输入专用脚只能用于流量计或按钮，不能用于阀门输出。阀门 PWM 输出必须使用可输出 GPIO，并通过 MOSFET、继电器模块或专用驱动器控制，不能由 ESP32 GPIO 直驱。

推荐固定引脚：

```text
Flow1Input = GPIO32
Flow2Input = GPIO35
Valve1Output = GPIO16
Valve2Output = GPIO14
Valve3Output = GPIO13
Valve4Output = GPIO27
Valve5Output = GPIO4
Valve6Output = GPIO5
```

GPIO4/GPIO5 在 ESP32 DevKit 上可作为输出使用；其中 GPIO5 是 ESP32 strapping pin，上电复位阶段会参与启动配置采样。阀门驱动必须保证 GPIO5 在复位采样期间不被外部强拉到错误电平；建议由 MOSFET/驱动输入的高阻状态和合适下拉保证默认关阀。如果后续板级验证发现启动受影响，再整体换 pin map。

## Local Interaction Model

本地按钮和 I2C 屏幕只做现场快捷操作和状态查看，不承担完整配置、校准、学习和计划编辑功能。完整配置仍通过 Web/API 完成，避免在小屏和少量按钮上做复杂菜单。

本地交互的核心目标只有两个：

```text
发现异常时能快速停水。
现场需要时能选择某一路 enabled Zone 手动浇水。
```

推荐按钮语义：

```text
Button 1: 上一个 enabled Zone
Button 2: 下一个 enabled Zone
Button 3: 启动/停止当前选中的 enabled Zone
Button 4: Stop All；如果正在校准或学习，则中止当前采样/学习并关阀
Button 5: 信息页切换，可选；如果硬件只装 4 个按钮，第一版可以不实现
```

本地操作不为 Zone 1/2 设计特殊快捷逻辑。首页、本地屏幕和本地按钮都以 `enabled Zone` 列表为准：启用的 Zone 都可以展示和操作，禁用的 Zone 不进入本地选择列表、不在本地屏幕主操作页展示、不能启动。Zone 设置页仍可显示所有 Zone 并重新启用禁用项。

如果当前选中的 Zone 被禁用，选择器自动切到下一个 enabled Zone；如果没有任何 enabled Zone，屏幕显示无启用水路，启动/停止当前 Zone 按钮无效，Stop All 仍然有效。

本地按钮必须调用和 Web/API 相同的业务服务校验：

```text
Zone disabled、ZoneFault、Flow disabled、Flow busy、leakProtectionActive、calibration/learning active 时拒绝启动。
手动启动不进入计划队列，Flow 忙时直接拒绝。
本地启动使用 manualDefaultDurationSec。
Stop All 立即关闭所有 Zone，并中止校准/学习。
```

I2C 屏幕第一版只显示运行状态：

```text
当前选中的 enabled Zone、归属 Flow、能否启动和 blockedReason
当前运行 Zone、实时流量、累计水量
队列数量
ZoneFault / FlowLeakFault 摘要
```

Button 5 的信息页切换只在运行页、Flow 页、队列页和故障页之间切换，不进入配置菜单。

不在第一版本地交互里实现 Flow 参数编辑、Zone 基线编辑、计划编辑、Flow 校准、Zone 学习或校准样本输入。这些操作需要输入数字、实际水量和确认流程，放在 Web 页面更可靠。

## Configuration Model

配置拆成 Flow 配置和 Zone 配置。参数来源枚举先定义一次，Flow 参数和 Zone 学习参数共用：

```cpp
enum class ParameterSource : uint8_t {
    NONE = 0,
    MANUAL = 1,
    SINGLE_POINT = 2,
    MULTI_POINT = 3,
    LEARNED = 4,
};

enum class FlowFaultAction : uint8_t {
    RECORD_ONLY = 1,
    STOP_ZONE = 2,
};
```

Flow 配置：

```cpp
struct FlowMeterCalibrationProfile {
    ParameterSource source;
    int32_t kUlPerMinPerHz;
    int32_t offsetMilliHz;
    uint32_t warningFreqMilliHz;
    uint32_t minValidFreqMilliHz;
    uint32_t maxValidFreqMilliHz;
    uint16_t pressurizeSec;
    uint16_t sampleWindowSec;
    uint32_t updatedAt;
};

struct FlowMeterConfig {
    uint8_t flowId;             // 1..2
    uint8_t pulsePin;           // read-only hardware pin
    bool enabled;
    FlowMeterCalibrationProfile activeCalibration;
    bool hasPendingCalibration;
    FlowMeterCalibrationProfile pendingCalibration;
    bool hasRollbackCalibration;
    FlowMeterCalibrationProfile rollbackCalibration;
};
```

K + Offset 只使用固定点数持久化，不使用 `float` 作为配置存储格式：

```text
kUlPerMinPerHz       = K * 1,000,000
offsetMilliHz        = Offset * 1,000
warningFreqMilliHz / minValidFreqMilliHz / maxValidFreqMilliHz 使用 mHz
```

示例：

```text
K = 0.1749 L/min/Hz      -> kUlPerMinPerHz = 174900
Offset = 3.136 Hz        -> offsetMilliHz = 3136
K = 60 / 245 L/min/Hz    -> kUlPerMinPerHz = 244897, offsetMilliHz = 0
```

Zone 配置：

```cpp
struct ZoneFlowBaselineProfile {
    ParameterSource source;
    uint32_t learnedFlowMlPerMin;
    uint16_t lowFlowPermille;
    uint16_t highFlowPermille;
    uint16_t flowFaultConfirmSec;
    FlowFaultAction lowFlowAction;
    FlowFaultAction highFlowAction;
    uint16_t noPulseTimeoutSec;
    uint32_t updatedAt;
};

struct ZoneConfig {
    uint8_t zoneId;             // 1..6
    char name[32];
    uint8_t valvePin;           // read-only hardware pin
    uint8_t flowId;             // 1 or 2
    bool enabled;
    bool hasLearnedBaseline;
    ZoneFlowBaselineProfile activeBaseline;
    bool hasPendingBaseline;
    ZoneFlowBaselineProfile pendingBaseline;
    bool hasRollbackBaseline;
    ZoneFlowBaselineProfile rollbackBaseline;
};
```

待应用参数和回退参数是新模型的一部分，不继承旧校准结构。每个 Flow 保存一套正在使用的 `activeCalibration`，可选保存 `pendingCalibration` 和 `rollbackCalibration`；每个 Zone 保存一套正在使用的 `activeBaseline`，可选保存 `pendingBaseline` 和 `rollbackBaseline`。这样手工修改、单点校准、多点校准和自动学习都走同一套待应用参数流程，不需要为来源设计多套存储格式。

布尔字段语义固定如下，避免和启用状态混淆：

```text
Flow.activeCalibration 永远有效；Flow.enabled 只表示该 Flow 是否参与运行。
Flow.hasPendingCalibration 表示是否有待应用的计量校准参数。
Flow.hasRollbackCalibration 表示是否有可回退的上一套计量校准参数。
Zone.hasLearnedBaseline=false 表示该 Zone 尚未学习正常流量，此时不启用高低流量比例判断。
Zone.hasPendingBaseline 表示是否有待应用的正常流量基线。
Zone.hasRollbackBaseline 表示是否有可回退的上一套正常流量基线。
```

第一版 Web 不提供 `flowId = 0`，启用的 Zone 必须归属一个启用的 Flow。这样缺水保护和运行记录始终有流量依据。

存储边界也必须拆开，避免把 Flow 计量参数塞回 Zone 配置：

```text
FlowConfigStore
  保存 FlowMeterConfig[2]，namespace 使用 irr_flow_v1。

ZoneConfigStore
  保存 ZoneConfig[6]，namespace 使用 irr_zone_v1。

SystemConfigStore
  只保存系统级参数，例如最长浇水时长、计划宽限期、排队最长等待、待机漏水窗口和默认手动时长。
```

Flow 校准参数属于流量计，不属于某个电磁阀；Zone 正常流量基线属于 Zone，不属于 Flow。这样才能支持 `1/3/5 -> Flow 1`、`2/4/6 -> Flow 2` 或 `1 -> Flow 1`、`2..6 -> Flow 2` 这类任意拓扑。

默认配置：

```text
Flow 1 enabled, activeCalibration 有默认值
Flow 2 disabled, activeCalibration 有默认值
Flow hasPendingCalibration=false, hasRollbackCalibration=false
Flow 默认 kUlPerMinPerHz=244897, offsetMilliHz=0
Flow 默认 warningFreqMilliHz=4000, minValidFreqMilliHz=500, maxValidFreqMilliHz=0
Flow 默认 pressurizeSec=5, sampleWindowSec=2
Zone 1..6 -> Flow 1
Zone 1/2 enabled
Zone 3..6 disabled
Zone 学习默认 hasLearnedBaseline=false, hasPendingBaseline=false, hasRollbackBaseline=false
Zone 学习默认 lowFlowPermille=100, highFlowPermille=3000, flowFaultConfirmSec=15, lowFlowAction=STOP_ZONE, highFlowAction=STOP_ZONE, noPulseTimeoutSec=10
```

如果用户启用 Flow 2，可任意把 Zone 归属到 Flow 1 或 Flow 2。

## Flow Calculation

Flow 使用专业线性校准模型：

```text
Q = K * (f + Offset)
```

含义：

```text
Q       瞬时流量，单位 L/min
f       当前采样窗口脉冲频率，单位 Hz
K       斜率，单位 L/min/Hz
Offset  频率偏移量，单位 Hz
```

实现上每个采样窗口计算：

```text
if pulseDelta == 0:
  flowUlPerMin = 0
else:
  freqMilliHz = pulseDelta * 1,000,000 / elapsedMs
  if freqMilliHz < minValidFreqMilliHz:
    mark sample below metering range
    flowUlPerMin = 0
  else if maxValidFreqMilliHz > 0 and freqMilliHz > maxValidFreqMilliHz:
    mark sample invalid
    flowUlPerMin = 0
  else:
    effectiveMilliHz = freqMilliHz + offsetMilliHz
    flowUlPerMin = max(0, kUlPerMinPerHz * effectiveMilliHz / 1000)

volumeUl += flowUlPerMin * elapsedMs / 60000
```

所有乘法中间值使用 `int64_t`，避免高频脉冲或长采样窗口造成溢出。`pulseDelta == 0` 时流量必须强制为 0；即使 `offsetMilliHz` 为正，也不能在无脉冲窗口中产生虚假流量。

低流量计量分三档：

```text
pulseDelta == 0
  完全没有原始脉冲，参与无水/停水判断。

0 < freqMilliHz < minValidFreqMilliHz
  有原始脉冲，但低于最低可计量频率；不按 0 水处理，只标记“低于计量下限”，水量估算为 0 或仅作不可靠显示。

minValidFreqMilliHz <= freqMilliHz < warningFreqMilliHz
  可以按 K+Offset 估算水量，但页面提示“低于厂家建议可靠量程，水量可能不准”。
```

`warningFreqMilliHz` 是可靠量程提示阈值，默认 `4000`，约等于用户当前流量计 245 脉冲/L 时的 1 L/min。`minValidFreqMilliHz` 是最低可计量频率，默认 `500`，用于支持只有 3-5 个小喷头的低流量水路。`maxValidFreqMilliHz = 0` 表示不启用上限检查；启用上限时只标记样本异常，不直接推断具体故障原因。

无水/停水保护只看原始脉冲是否持续为 0，不看 `flowUlPerMin` 是否为 0。也就是说：只要有脉冲，即使频率低于计量下限，也不能直接判定为无水停机；但可以提示该 Zone 水量计量不可靠。

如果 `Offset = 0`，模型退化为普通 P/L 模型：

```text
K = 60 / pulsesPerLiter
```

但新系统的持久化参数只保存 `K` 和 `Offset`，不再保存旧的 `stablePulsePerLiter`、`startupPulseLimit`、`startupEstimatedMl`。

建议参数范围：

```text
kUlPerMinPerHz: 1..2000000
offsetMilliHz: -50000..50000
minValidFreqMilliHz: 0..60000
warningFreqMilliHz: 0..60000
maxValidFreqMilliHz: 0 或 1000..200000
```

超出范围、计算结果非有限或运行时换算结果异常的配置必须拒绝保存。

## Startup And Pressurize Handling

K + Offset 只负责稳定流动阶段的频率到流量换算，不负责处理开阀暂态。

每个 Flow 配置 `pressurizeSec`。Zone 启动后：

```text
pressurizeSec 内：
  可以用 K + Offset 粗略累计水量
  不参与 Zone 正常流量学习
  不触发低流量或高流量异常

pressurizeSec 后：
  进入稳定监测
  实时流量用于 Zone 学习、异常判断和记录统计
```

无脉冲启动保护从开阀后开始记录原始脉冲，但在 `pressurizeSec` 内不确认故障。进入稳定阶段后，如果连续 `noPulseTimeoutSec` 没有任何原始脉冲，才确认无水/停水。默认 `noPulseTimeoutSec=10` 秒，可在 Zone 学习/安全参数里手工修改。该值的工程含义是允许水路建压和短暂气泡，但不要让增压泵在无水状态下长时间运行；如果泵有独立缺水保护，可适当放宽，否则不建议超过 15-20 秒。

第一版缺水策略固定为异常即停，不等待来水恢复：

```text
稳定阶段持续无脉冲超过 noPulseTimeoutSec
  -> 关闭当前 Zone
  -> 写入无流量异常记录
  -> 不自动恢复、不自动补浇
```

不设计“停水等待、来水后继续”的状态机。该模式会增加泵空转、电磁阀长时间开启和补浇语义不清的风险。

## Runtime Rules

启动 Zone 时执行：

```text
1. Zone 必须启用。
2. Zone 归属的 Flow 必须启用。
3. 同一个 Flow 下不能已有其他 Zone 运行。
4. 没有全局漏水保护或恢复出厂保护阻塞。
5. 目标时长必须大于 0 且不超过系统上限。
6. 打开 Zone 阀门。
7. 标记该 Flow 被此 Zone 占用。
```

同一个 Flow 下互斥：

```text
Zone 1 -> Flow 1
Zone 3 -> Flow 1

Zone 1 运行时，Zone 3 不能启动。
```

不同 Flow 下可并行：

```text
Zone 1 -> Flow 1
Zone 2 -> Flow 2

Zone 1 和 Zone 2 可同时运行。
```

运行中的 Zone 从其归属 Flow 读取脉冲、实时流量和累计水量。由于同一 Flow 下互斥，该 Flow 的读数可以明确归属到当前 Zone。

## Schedule Queueing

计划触发必须尊重 Flow 互斥规则。多个计划同时触发且目标 Zone 不能同时运行时，第一版采用内存队列顺序执行，而不是直接跳过。

队列规则：

```text
1. 计划到点后生成一个待执行任务。
2. 任务按 scheduledEpoch 排序。
3. scheduledEpoch 相同时按 zoneId 升序排序。
4. 如果目标 Zone 归属的 Flow 空闲，则立即启动。
5. 如果 Flow 忙，则任务留在内存队列等待。
6. 当前 Zone 完成后，调度器重新扫描队列并启动可运行任务。
```

例子：

```text
08:00 Zone 1 计划 5 分钟，Zone 1 -> Flow 1
08:00 Zone 2 计划 10 分钟，Zone 2 -> Flow 1
08:00 Zone 3 计划 3 分钟，Zone 3 -> Flow 1

执行顺序：
08:00-08:05 Zone 1
08:05-08:15 Zone 2
08:15-08:18 Zone 3
```

如果：

```text
Zone 1 -> Flow 1
Zone 2 -> Flow 2
Zone 3 -> Flow 1
```

则：

```text
08:00 Zone 1 和 Zone 2 可同时开始
Zone 3 等 Zone 1 完成后开始
```

队列边界：

```text
手动启动不排队，资源忙时直接拒绝。
队列只保存在内存，不做持久化恢复。
设备重启后按计划宽限期重新评估，不恢复重启前内存队列。
队列容量固定，建议 12 个任务；满时记录 queue_full 并拒绝新任务。
排队任务最长等待 queuedPlanMaxDelaySec，默认 3600 秒；超过后记录 queue_expired 并丢弃。
排队任务仍受单次最长浇水时长、Zone 启用状态、Flow 启用状态、漏水保护和恢复出厂保护约束。
```

`queuedPlanMaxDelaySec` 是系统级可配置项。默认 3600 秒适合多数分区浇水；如果 6 个 Zone 每路都要浇 15-20 分钟，建议把该值调到 7200 秒，避免后面的计划因排队过期。

## Flow Calibration

Flow 校准分两种。

快速单点校准：

```text
1. 用户选择 Flow 和该 Flow 下一个实际出水的 Zone。
2. 系统打开该 Zone 并开始采集脉冲和时长。
3. 用户接水后输入实际水量。
4. 计算平均流量 Q 和平均频率 f。
5. 设置 Offset = 0。
6. 设置 K = Q / f。
```

该模式等价于用实测 P/L 换算 K，适合快速部署。

专业多点校准：

```text
1. 对同一个 Flow 采集至少 2 个不同流量点。
2. 每个点保存实际水量、采集时长、总脉冲、平均 Q、平均 f。
3. 2 点时直接求直线。
4. 3 点及以上时用线性回归拟合 Q = a*f + b。
5. K = a。
6. Offset = b / a。
7. 显示每个样本的预测误差和总体误差。
```

拟合实现也使用固定点/整数中间值，不把 `float` 或 `double` 作为业务参数、记录字段或持久化字段。页面可以显示换算后的小数，但存储和运行逻辑只保存固定点整数。

2 点公式：

```text
K = (Q2 - Q1) / (f2 - f1)
Offset = Q1 / K - f1
```

校准约束：

```text
K 必须 > 0
Offset 可为正或负
K * (f + Offset) 小于 0 时运行时按 0 流量处理
样本流量范围过窄时提示 Offset 可信度不足
```

多点校准质量门槛：

```text
有效样本实际流量 >= 1 L/min
有效样本采集时长 >= 30 秒
有效样本实际水量 >= 1 L
至少 2 个有效样本才能计算 Offset
3 个及以上有效样本才推荐启用 Offset
最大频率和最小频率差建议 >= 30%
拟合平均误差 > 10% 时不建议应用
abs(offsetMilliHz) 不应大于典型工作频率的 50%，否则提示参数可疑
```

如果样本数量不足或频率范围过窄，系统可以退回到 `offsetMilliHz = 0` 的快速校准模型，只保存由样本平均得到的 `kUlPerMinPerHz`。

用户应优先采集明显不同的流量点，例如少喷头、中等喷头、多喷头。流量点过于集中时，可以保存结果，但页面必须显示该结果只是局部拟合。

校准生成的待应用参数也必须包含 `warningFreqMilliHz`、`minValidFreqMilliHz` 和 `maxValidFreqMilliHz`。单点校准默认沿用当前 Flow 的有效频率范围；多点校准可把最低可信样本频率作为待应用 `minValidFreqMilliHz`，但页面必须允许用户手工覆盖。

## Calibration Redesign Scope

本轮重构必须同步重写全部校准功能。校准不能继续沿用旧的 Zone 级启动补偿模型：

```text
startupPulseLimit
startupEstimatedMl
stablePulsePerLiter
```

这些字段、相关待应用参数、回退参数、页面文案、API 参数和计算逻辑都删除。新校准体系拆成两类对象：Flow 参数校准和 Zone 流量学习。

Flow 参数必须支持三种修改来源：

```text
manual       用户手工修改并保存待应用参数
singlePoint  单点采样生成 Offset=0 的待应用参数
multiPoint   多点采样拟合 K+Offset 的待应用参数
```

Flow 参数页面必须允许手工修改：

```text
kUlPerMinPerHz
offsetMilliHz
warningFreqMilliHz
minValidFreqMilliHz
maxValidFreqMilliHz
pressurizeSec
sampleWindowSec
```

手工修改必须先保存为待应用参数，不直接覆盖当前参数。应用待应用参数时保存当前参数为回退参数。Flow 参数按 Flow 独立保存：

```text
Flow 1 activeCalibration / pendingCalibration / rollbackCalibration
Flow 2 activeCalibration / pendingCalibration / rollbackCalibration
```

Zone 学习参数也必须支持自动学习和手工修改：

```text
learnedFlowMlPerMin
lowFlowPermille
highFlowPermille
noPulseTimeoutSec
```

这些参数属于 Zone，不属于 Flow。Zone 基线参数按 Zone 独立保存：

```text
Zone 1 activeBaseline / pendingBaseline / rollbackBaseline
...
Zone 6 activeBaseline / pendingBaseline / rollbackBaseline
```

自动学习只生成待应用值；手工编辑也只保存待应用值。只有用户明确应用待应用参数后，当前运行参数才改变。应用待应用参数时必须要求目标 Flow 或 Zone 当前空闲。

校准页面建议分区：

```text
Flow 校准
  Flow 1/2 当前参数、待应用参数、回退参数
  手工编辑 K/Offset/有效频率范围/建压时间/采样窗口
  单点采样
  多点采样
  拟合误差和可信度

Zone 学习
  Zone 1..6 当前学习参数、待应用参数、回退参数
  手工编辑正常流量/高低阈值/无脉冲超时
  自动学习
```

校准 API 也按新对象重写，不保留旧路径语义：

```text
Flow config save
Zone config save
Flow calibration pending save/apply/rollback
Flow calibration sample start/stop/save/clear/fit
Zone baseline pending save/apply/rollback
Zone baseline learning start/stop
```

API 路径重新命名为新模型，不沿用旧校准路径语义。不提供旧 `startupPulseLimit`、`startupEstimatedMl`、`stablePulsePerLiter` 的兼容入口。

校准采集期间的安全规则：

```text
Flow 校准采集期间，不允许普通手动/计划浇水启动。
Zone 学习期间，不允许普通手动/计划浇水启动。
停止全部必须中止当前校准或学习并关阀。
待机漏水检测不参与校准采集判断。
校准采样只允许同时打开一个 Zone。
采样 Zone 必须 enabled。
采样 Zone 必须归属目标 Flow。
目标 Flow 必须 enabled 且当前空闲。
系统不能处于漏水保护、恢复出厂保护或阀门错误状态。
```

Flow 单点或多点采样必须选择一个实际出水的 Zone 作为采样执行水路；样本归属 Flow，而不是归属 Zone。只有所选 Zone 归属目标 Flow 时，才能用于该 Flow 校准。

## Zone Flow Learning

Flow 校准解决“流量计如何把脉冲换算成流量”。Zone 学习解决“这一路水路正常应该是多少流量”。

Zone 学习流程：

```text
1. 单独启动目标 Zone。
2. 跳过归属 Flow 的 pressurizeSec。
3. 在稳定窗口内采集平均流量。
4. 保存 learnedFlowMlPerMin。
5. 默认 lowFlowPermille = 100。
6. 默认 highFlowPermille = 3000。
7. 默认 flowFaultConfirmSec = 15。
8. 默认 lowFlowAction = STOP_ZONE。
9. 默认 highFlowAction = STOP_ZONE。
```

Zone 学习必须在页面提供用户操作入口。用户行为是：选择 Zone，点击开始学习，系统自动打开该 Zone；等待稳定采样后停止；页面显示采集到的平均流量并保存为待应用基线。用户也可以手工编辑 `learnedFlowMlPerMin`、`lowFlowPermille`、`highFlowPermille`、`flowFaultConfirmSec`、`lowFlowAction`、`highFlowAction`、`noPulseTimeoutSec`，手工编辑同样先保存为待应用基线。

异常判断：

```text
无脉冲超时：
  稳定阶段持续无脉冲，关闭该 Zone 并记录缺水/无流量。

低流量：
  有效计量样本的当前流量连续 flowFaultConfirmSec 秒 < learnedFlowMlPerMin * lowFlowPermille / 1000。
  lowFlowAction=STOP_ZONE 时关闭该 Zone；lowFlowAction=RECORD_ONLY 时只记录事件和记录标记。

高流量：
  有效计量样本的当前流量连续 flowFaultConfirmSec 秒 > learnedFlowMlPerMin * highFlowPermille / 1000。
  highFlowAction=STOP_ZONE 时关闭该 Zone；highFlowAction=RECORD_ONLY 时只记录事件和记录标记。
```

如果某个 Zone 尚未学习正常流量，系统仍可用无脉冲保护和总流量记录，但不启用高低流量比例判断。

低/高流量比例判断只使用 `minValidFreqMilliHz <= freqMilliHz` 且未超过 `maxValidFreqMilliHz` 的有效计量样本。`pulseDelta > 0` 但低于计量下限的样本只设置 `belowMeteringRangeObserved`，不累计低流量确认时间；超过上限的样本只设置 `sampleInvalidObserved`，不累计高流量确认时间。这样可以区分“确实没水”“有很小水流但计量不可靠”“有效计量下的异常低/高流量”，避免少量小喷头或低水塔压力被误判为停水。

`flowFaultConfirmSec` 是 Zone 级可配置项，默认 15 秒。该值用于过滤喷头瞬时波动、气泡和压力短时变化；不建议低于 5 秒。低流量默认阈值为正常流量的 10%，目的是适应水塔低水位时仍能慢速浇水；高流量默认阈值为正常流量的 300%，目的是只在明显脱管、爆管或喷头大量脱落时触发。低/高流量默认动作都是 `STOP_ZONE`，但可改成 `RECORD_ONLY`。

## Idle Leak Detection

待机时没有任何 Zone 运行。如果 Flow 1 或 Flow 2 出现持续脉冲：

```text
记录对应 Flow 存在异常流动
关闭全部阀门
进入漏水保护
```

待机漏水检测使用两个系统级可配置项：

```text
idleLeakWindowSec 默认 15 秒
idleLeakPulseThreshold 默认 5 个脉冲
```

当没有任何 Zone 运行时，任一 Flow 在 `idleLeakWindowSec` 窗口内累计原始脉冲数达到 `idleLeakPulseThreshold`，判定为待机异常流动。不要因为单个偶发脉冲立即进入漏水保护。

由于 Flow 可能管理多个 Zone，待机漏水只能定位到 Flow 组，不能精确定位到某个 Zone。页面应显示类似：

```text
Flow 1 检测到待机流动，请检查该 Flow 下的电磁阀和管路。
```

因此异常状态存储不能继续使用只表示 Zone 错误的旧模型。新实现使用 `FaultStateStore` 管理：

```text
ZoneFault[6]
  保存 Zone 级锁定异常，例如无脉冲停机、低流量停机、高流量停机、配置无效。

FlowLeakFault[2]
  保存 Flow 级待机漏水保护，例如 Flow 1 待机窗口内检测到异常脉冲。

leakProtectionActive
  任一 FlowLeakFault 有效时为 true，阻止普通浇水启动，直到用户人工清除。
```

不要把 Flow 级待机漏水硬塞成某个 Zone 的 `LEAK_DETECTED`，否则页面、事件和清除逻辑都会误导用户。

## Records And Events

浇水记录使用新格式，不迁移旧记录。

每条记录保存：

```text
zoneId
flowId
kUlPerMinPerHzSnapshot
offsetMilliHzSnapshot
warningFreqMilliHzSnapshot
minValidFreqMilliHzSnapshot
maxValidFreqMilliHzSnapshot
pressurizeSecSnapshot
sampleWindowSecSnapshot
learnedFlowMlPerMinSnapshot
lowFlowPermilleSnapshot
highFlowPermilleSnapshot
flowFaultConfirmSecSnapshot
lowFlowActionSnapshot
highFlowActionSnapshot
startedAt
endedAt
targetSec
actualSec
startPulse
endPulse
estimatedMl
avgFlowMlPerMin
minFlowMlPerMin
maxFlowMlPerMin
lastPulseAtSec
firstNoPulseAtSec
faultConfirmedAtSec
noPulseTimeoutSecSnapshot
pulsesBeforeFault
estimatedMlBeforeFault
lowFlowObserved
highFlowObserved
belowMeteringRangeObserved
sampleInvalidObserved
result
stopSource
```

记录必须保存 `flowId` 和关键 Flow/Zone 参数快照。这样即使以后 Zone 改归属或 Flow 重新校准，历史记录仍能解释当时使用哪个 Flow、哪组 K+Offset 和哪组异常阈值计量。

缺水/无脉冲异常记录不保存多次停水恢复片段。第一版缺水即停，因此只需要保存第一次进入无脉冲观察、最后一次有脉冲、确认异常并停机的时间点，以及停机前脉冲和估算水量。

低/高流量如果配置为 `RECORD_ONLY`，任务不停止，最终任务结果仍按实际结束原因记录，例如 `COMPLETED` 或 `USER_STOPPED`；同时记录 `lowFlowObserved`、`highFlowObserved`、`belowMeteringRangeObserved`、`sampleInvalidObserved` 等标志。只有 `STOP_ZONE` 动作真正关闭阀门时，任务结果才写成对应的故障停止结果。

结果和异常枚举建议固定为：

```text
TaskResult
  NONE
  COMPLETED
  USER_STOPPED
  FLOW_NO_PULSE_TIMEOUT
  FLOW_LOW_STOPPED
  FLOW_HIGH_STOPPED
  LEAK_PROTECTED
  FACTORY_RESET_PROTECTED
  CONFIG_INVALID
  REJECTED

ZoneErrorCode
  NONE
  FLOW_NO_PULSE_TIMEOUT
  FLOW_LOW
  FLOW_HIGH
  CONFIG_INVALID

FlowFaultCode
  NONE
  IDLE_LEAK
```

旧的 `FLOW_START_TIMEOUT` 不再单独保留。启动建压和无脉冲检测统一由 `pressurizeSec + noPulseTimeoutSec` 处理，故障结果统一为 `FLOW_NO_PULSE_TIMEOUT`。

业务事件至少覆盖：

```text
Zone 启动被同 Flow 互斥拒绝
计划任务进入队列
计划队列任务启动
计划队列满
计划队列任务过期
无流量停止
低流量异常触发，并记录 action=STOP_ZONE 或 RECORD_ONLY
高流量异常触发，并记录 action=STOP_ZONE 或 RECORD_ONLY
待机漏水
Flow K+Offset 参数应用
Flow K+Offset 待应用参数保存
Flow K+Offset 参数回退
Zone 正常流量学习应用
Zone 正常流量待应用参数保存
Zone 正常流量参数回退
```

## Web And API

业务页面重做为新模型。

页面结构固定为：

```text
/irrigation
  运行总览、Flow 状态、Zone 快捷启动/停止、当前计划队列、当前故障。

/irrigation/flows
  Flow 1/2 启用状态、输入 GPIO、当前/待应用/回退校准参数、安装提示。

/irrigation/zones
  Zone 1..6 启用状态、名称、阀门 GPIO、归属 Flow、正常流量基线和安全阈值。

/irrigation/plans
  Zone 计划列表、计划创建/编辑/删除/启用/停用、排队和最近执行结果。

/irrigation/settings
  系统级业务设置：默认手动时长、最长浇水时长、计划宽限期、排队最长等待、待机漏水窗口。

/irrigation/calibration
  Flow K+Offset 校准、采样、拟合、Zone 正常流量学习。

/irrigation/records
  浇水记录查询。

/irrigation/events
  业务事件查询。
```

`/index` 可以作为 Esp32Base 首页入口重定向到 `/irrigation`，但业务页面以 `/irrigation` 为主，不继续围绕旧首页路径设计。

运行总览：

```text
显示 Flow 1/2 当前状态和流量
显示所有 enabled Zone 的运行状态和手动启动/停止入口；不只显示默认启用的 Zone 1/2
隐藏 disabled Zone 的主操作入口；Zone 设置页仍显示全部 Zone 供重新启用
运行中的 Zone 显示归属 Flow 的实时流量和累计水量
只提供符合互斥规则的启动操作
显示每个 Zone 不能启动的原因，例如 disabled、fault、leak_protected、flow_disabled、flow_busy、calibration_active
显示计划队列中等待的任务、目标 Zone、归属 Flow、已等待时间和过期剩余时间
```

Flow 设置页：

```text
Flow 1/2 启用状态
输入 GPIO 只读显示
kUlPerMinPerHz
offsetMilliHz
minValidFreqMilliHz
maxValidFreqMilliHz
pressurizeSec
sampleWindowSec
校准入口
当前参数、待应用参数、回退参数分区显示
Flow 被运行中的 Zone 使用时，禁用保存/应用/回退按钮
```

Zone 设置页：

```text
Zone 1..6 启用状态
名称
阀门 GPIO 只读显示
归属 Flow：Flow 1 / Flow 2
学习流量
高低流量阈值
低/高流量连续确认秒数
低/高流量异常动作：停机 / 只记录
无脉冲超时
当前基线、待应用基线、回退基线分区显示
运行中的 Zone 禁止修改配置或应用/回退基线
```

系统设置页必须提供：

```text
计划排队最长等待 queuedPlanMaxDelaySec
待机漏水检测窗口 idleLeakWindowSec
待机漏水检测脉冲阈值 idleLeakPulseThreshold
```

系统设置页和 Esp32Base AppConfig 必须使用同一个 `SystemConfigStore` 和同一组 `irr_sys_v1` 标量 key。可以在 `/irrigation/settings` 内渲染业务友好的设置表单，也可以链接到 Esp32Base AppConfig 页面；但不能做两套互相不同步的设置存储。

计划页必须提供：

```text
Zone 1..6 的计划列表
计划启用/停用
开始时间、目标时长、星期/日期规则
计划触发后的排队状态和最近执行结果
```

故障页或首页故障区必须提供：

```text
ZoneFault 清除入口
FlowLeakFault 清除入口
清除前二次确认
```

校准页：

```text
Flow K+Offset 校准
Zone 正常流量学习
样本误差和拟合可信度
采样/学习运行中显示当前 Flow、采样 Zone、脉冲数、频率、估算水量、已用时间
采样/学习运行中禁用普通手动启动、计划启动、Flow/Zone 参数应用
```

所有状态改变操作必须使用 POST、鉴权、二次确认。Web/API 输出继续做 HTML/JSON escape。

页面交互规则：

```text
手动启动
  页面允许用户选择 Zone 和时长。
  如果目标 Zone 禁用、存在 ZoneFault、全局漏水保护中、目标 Flow 禁用、目标 Flow 忙、校准/学习进行中，则按钮禁用并显示原因。
  提交后如果状态在服务端已变化，API 返回 409 和 reason；页面刷新状态并显示失败原因。
  手动启动不进入计划队列。

停止
  单 Zone 停止只关闭该 Zone。
  Stop All 关闭所有 Zone，并中止正在进行的校准采样或 Zone 学习。
  Stop All 必须二次确认。

Flow 配置
  `pulsePin` 只读。
  Flow 禁用前必须确认没有 enabled Zone 归属该 Flow；否则拒绝保存。
  Flow 参数手工编辑只保存 pendingCalibration，不直接覆盖 activeCalibration。
  apply/rollback 只允许目标 Flow 空闲且没有校准/学习进行中。

Zone 配置
  `valvePin` 只读。
  `flowId` 下拉只列出 enabled Flow。
  enabled Zone 必须归属 enabled Flow。
  运行中的 Zone 不允许改名、禁用、改 Flow、保存基线或应用/回退基线。
  手工编辑基线只保存 pendingBaseline，不直接覆盖 activeBaseline。

计划
  计划保存时必须指定有效 Zone。
  如果 Zone 禁用，允许保存 disabled 计划，不允许启用该计划。
  如果目标 Zone 归属的 Flow 禁用，不允许启用该计划。
  删除计划必须二次确认。
  计划到点后如果 Flow 忙，页面显示 queued；如果超过 queuedPlanMaxDelaySec，显示 expired。

校准
  同一时间只能有一个 Flow 校准或 Zone 学习任务。
  Flow 校准采样必须选择目标 Flow 和一个归属该 Flow 的 enabled Zone。
  单点/多点样本必须先 stop sample，再输入实际水量并保存样本。
  fit 只生成 pendingCalibration。
  Zone 学习只生成 pendingBaseline。

故障清除
  ZoneFault 清除只清目标 Zone。
  FlowLeakFault 清除清指定 Flow 的待机漏水保护。
  清除前页面必须展示故障发生时间、故障类型和二次确认。
```

页面和 API 使用同一套业务服务校验，页面禁用按钮只是用户体验，不能代替服务端校验。

API 使用干净的新路径，不保留旧字段或旧路径语义：

```text
GET  /api/v1/status
GET  /api/v1/flows
GET  /api/v1/zones
GET  /api/v1/plans
GET  /api/v1/flows/history?flowId=1
GET  /api/v1/records
GET  /api/v1/events

POST /api/v1/zones/start
POST /api/v1/zones/stop
POST /api/v1/zones/stop-all

POST /api/v1/flows/config
POST /api/v1/zones/config
POST /api/v1/plans/config
POST /api/v1/plans/delete

POST /api/v1/faults/zone/clear
POST /api/v1/faults/leak/clear

POST /api/v1/flows/calibration/pending
POST /api/v1/flows/calibration/apply
POST /api/v1/flows/calibration/rollback
POST /api/v1/flows/calibration/samples/start
POST /api/v1/flows/calibration/samples/stop
POST /api/v1/flows/calibration/samples/save
POST /api/v1/flows/calibration/samples/clear
POST /api/v1/flows/calibration/fit

POST /api/v1/zones/baseline/pending
POST /api/v1/zones/baseline/apply
POST /api/v1/zones/baseline/rollback
POST /api/v1/zones/baseline/learning/start
POST /api/v1/zones/baseline/learning/stop
```

所有 ID 使用请求体或 query 中的 `flowId`、`zoneId`。不要再使用 `road`、`channel`、`line`、`candidateFlow`、`previousFlow` 这类旧模型或含义不清的字段名。

API 返回约定：

```text
GET
  只读，返回 JSON。

POST
  修改状态，必须鉴权。
  页面表单和 API 客户端使用相同字段名。
  成功返回 ok=true；页面表单可重定向回来源页。
  参数错误返回 400 和稳定 reason。
  状态冲突返回 409 和稳定 reason，例如 flow_busy、calibration_active、zone_fault、flow_disabled。
```

`/api/v1/status` 至少返回：

```text
flows[]
  flowId, enabled, pulsePin, activeZoneId, flowMlPerMin, frequencyMilliHz,
  belowMeteringRange, sampleInvalid, totalPulses

zones[]
  zoneId, name, enabled, valvePin, flowId, state, canStart, blockedReason,
  running, elapsedSec, targetSec, estimatedMl, fault

queue[]
  planId, zoneId, flowId, scheduledEpoch, queuedEpoch, expiresEpoch

faults
  zoneFaults[], flowLeakFaults[], leakProtectionActive
```

`GET /api/v1/records` 分页读取 `/irr/records_v1.bin` 中的浇水记录；`GET /api/v1/events` 分页读取 `Esp32BaseAppEventLog` 中的灌溉业务事件。两者都是只读 JSON，不另建第二套事件存储。

## Storage

使用新的 namespace 和文件路径：

```text
Flow 配置 namespace: irr_flow_v1
Zone 配置 namespace: irr_zone_v1
System 配置 namespace: irr_sys_v1
Plan 配置 namespace: irr_plan_v1
Fault 状态 namespace: irr_fault_v1
Record 元数据 namespace: irr_record_v1
记录文件: /irr/records_v1.bin
事件仍使用 Esp32BaseAppEventLog，灌溉业务只新增事件类型和字段，不另建事件存储
```

旧配置、旧 namespace、旧记录文件不读取、不迁移、不清理。设备部署前由用户格式化。

存储介质边界：

```text
NVS / Esp32BaseConfig
  只保存小型、低频变更、单条 <= 256 字节的配置或状态。
  FlowConfigStore 按 Flow 单独保存，key 为 f1/f2。
  ZoneConfigStore 按 Zone 单独保存，key 为 z1..z6。
  SystemConfigStore 保存系统级标量字段，作为 AppConfig 和业务设置页的同一份真实配置。
  PlanStore 按计划槽位单独保存，key 为 z<zone>_<slot>，另有 meta 保存 nextPlanId。
  FaultStateStore 保存 ZoneFault[6] 和 FlowLeakFault[2]；如果结构超过 256 字节，必须拆成 zone/flow 分 key 保存。
  RecordStore 只在 NVS 保存 head/count/nextId 等小元数据。

LittleFS / Esp32BaseFs
  保存定长环形浇水记录文件 /irr/records_v1.bin。
  创建记录文件前必须确保 /irr 目录存在。
  记录读写必须使用 Esp32BaseFs::readBytesAt() / writeBytesAt()。
  不直接 include LittleFS.h，不直接使用 Arduino File。

RAM only
  Schedule 内存队列。
  Flow 实时脉冲窗口、流量图表历史、累计的运行中水量。
  Flow 校准采样过程中的原始样本和拟合中间值。
```

`Esp32BaseConfig::CONFIG_BLOB_MAX_LEN` 为 256 字节，因此不得把 `FlowMeterConfig[2]`、`ZoneConfig[6]`、全部计划槽位或记录数组作为一个大 blob 写入 NVS。凡是可能超过 256 字节的数据，要么按对象拆 key，要么放入 LittleFS 定长文件。

Flash 写入频率原则：

```text
允许立即写 Flash：
  用户保存配置、应用/回退校准参数、修改计划、清除故障。
  一次浇水任务结束后追加一条记录。
  计划开始/完成等需要防重复执行的低频状态。

禁止周期性写 Flash：
  每秒流量、每个脉冲、运行中累计水量、流量图表点、队列状态。
```

浇水记录文件使用固定大小环形文件：

```text
RecordStore::WateringRecord 必须是 standard-layout/trivially-copyable。
每条记录包含 magic/version/size/recordId/commitMagic/crc32。
编译期 static_assert 单条记录大小和总文件大小，防止字段增加后意外超出预算。
写入顺序为：清 commitMagic -> 写整条记录和 crc -> 写 commitMagic。
启动时扫描记录文件，忽略 commitMagic 或 crc 无效的槽位，并从有效记录恢复 head/count/nextId。
```

这种设计允许掉电发生在记录写入中间时只丢失当前未提交记录，不破坏已有记录。Record 元数据可以从文件恢复，因此 NVS 元数据损坏不应导致记录文件整体不可读。

系统配置存储不做“双真实来源”。AppConfig 页面和业务设置 API 都写同一组 `irr_sys_v1` 标量 key；`SystemConfigStore` 启动时从这些 key 组装 RAM 配置并校验，缺失或非法则回默认值并记录 schema reset 事件。

计划存储不使用单个 `PlanDefinition[TotalPlanSlots]` 大 blob。每个计划槽位单独保存，单个槽位结构必须小于 256 字节；`nextPlanId`、版本和计数等元数据单独保存。设备重启后可以根据计划表和宽限期重新评估；内存队列不持久化。

校准样本默认只保存在 RAM。应用后的 `activeCalibration`、`pendingCalibration`、`rollbackCalibration` 按 Flow 持久化；应用后的 `activeBaseline`、`pendingBaseline`、`rollbackBaseline` 按 Zone 持久化。不要把校准采样的每秒明细或原始脉冲流持续写入 Flash。

维护/格式化边界：

```text
Factory reset / clear irrigation data 必须清除：
  irr_flow_v1
  irr_zone_v1
  irr_sys_v1
  irr_plan_v1
  irr_fault_v1
  irr_record_v1
  /irr/records_v1.bin

不清除 Esp32Base 自有 namespace，WiFi、Web auth、OTA、FileLog、AppEventLog 仍由 Esp32Base 管理。
```

## Implementation Boundary

旧模块可删除或重写。目标是清晰的新业务核心，而不是在旧接口上做兼容适配。

建议模块边界：

```text
FlowMeterService
  管理 Flow 1/2 脉冲、K+Offset、实时流量、累计水量、低于计量下限和样本无效状态。

ValveService
  管理 6 路阀门 PWM、吸合/保持、安全关阀。

ZoneService
  管理 Zone 状态机、Flow 占用、互斥、启动/停止、运行统计。

CalibrationService
  管理 Flow 校准样本、K+Offset 拟合、Zone 流量学习。

ScheduleService
  面向 Zone 触发计划，不直接操作 Flow 或 Valve。

LocalControlService
  管理 5 个本地按钮，复用 ZoneService 的启停校验，不实现配置菜单。

DisplayService
  管理 I2C 屏幕状态显示，只显示运行、队列和故障摘要。

RecordService
  保存新格式浇水记录。
```

## Validation

软件验证：

```text
node scripts/check-web-structure.mjs
pio run
```

台架验证：

```text
2 路 Flow 输入能计数
6 路 Valve PWM 输出正常
同一 Flow 下 Zone 启动互斥
不同 Flow 下 Zone 可并行
同一 Flow 下计划冲突能按队列顺序执行
手动启动在 Flow 忙时拒绝且不入队
K+Offset 快速校准计算正确
K+Offset 多点拟合计算正确
无脉冲窗口不因正 Offset 产生虚假流量
有脉冲但低于计量下限时不触发无水停机
低于计量下限时不累计低流量停机确认
高频无效样本不累计高流量停机确认
Zone 学习能保存正常流量
无脉冲能停止对应 Zone
低流量、高流量按动作配置停止或只记录
待机 Flow 脉冲触发漏水保护
```

实机验证必须单独标注：

```text
电磁阀实际吸合和保持可靠性
驱动模块温升
流量计在真实喷头流量下的 K+Offset 可信度
真实水路 pressurizeSec 是否合适
增压泵缺水场景
72 小时连续运行
```

构建通过不等于硬件验证通过。
