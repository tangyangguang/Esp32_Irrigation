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

Flow 1 和 Flow 2 应代表不同计量点。不要在同一根主管上串联两个 Flow 后把它们当成两个独立组使用。

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

## Configuration Model

配置拆成 Flow 配置和 Zone 配置。

Flow 配置：

```cpp
struct FlowMeterConfig {
    uint8_t id;                 // 1..2
    uint8_t pulsePin;           // read-only hardware pin
    bool enabled;
    int32_t kUlPerMinPerHz;
    int32_t offsetMilliHz;
    uint16_t pressurizeSec;
    uint16_t sampleWindowSec;
};
```

K + Offset 只使用固定点数持久化，不使用 `float` 作为配置存储格式：

```text
kUlPerMinPerHz  = K * 1,000,000
offsetMilliHz   = Offset * 1,000
```

示例：

```text
K = 0.1749 L/min/Hz      -> kUlPerMinPerHz = 174900
Offset = 3.136 Hz        -> offsetMilliHz = 3136
K = 60 / 245 L/min/Hz    -> kUlPerMinPerHz = 244897, offsetMilliHz = 0
```

Zone 配置：

```cpp
struct ZoneConfig {
    uint8_t id;                 // 1..6
    char name[32];
    uint8_t valvePin;           // read-only hardware pin
    uint8_t flowMeterId;        // 1 or 2
    bool enabled;
    uint32_t learnedFlowMlPerMin;
    uint16_t lowFlowPermille;
    uint16_t highFlowPermille;
    uint16_t noPulseTimeoutSec;
};
```

第一版 Web 不提供 `flowMeterId = 0`，启用的 Zone 必须归属一个启用的 Flow。这样缺水保护和运行记录始终有流量依据。

默认配置：

```text
Flow 1 enabled
Flow 2 disabled
Zone 1..6 -> Flow 1
Zone 1/2 enabled
Zone 3..6 disabled
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
  effectiveMilliHz = freqMilliHz + offsetMilliHz
  flowUlPerMin = max(0, kUlPerMinPerHz * effectiveMilliHz / 1000)

volumeUl += flowUlPerMin * elapsedMs / 60000
```

所有乘法中间值使用 `int64_t`，避免高频脉冲或长采样窗口造成溢出。`pulseDelta == 0` 时流量必须强制为 0；即使 `offsetMilliHz` 为正，也不能在无脉冲窗口中产生虚假流量。

如果 `Offset = 0`，模型退化为普通 P/L 模型：

```text
K = 60 / pulsesPerLiter
```

但新系统的持久化参数只保存 `K` 和 `Offset`，不再保存旧的 `stablePulsePerLiter`、`startupPulseLimit`、`startupEstimatedMl`。

建议参数范围：

```text
kUlPerMinPerHz: 1..2000000
offsetMilliHz: -50000..50000
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

无脉冲启动保护可以在 `pressurizeSec` 后再进入严格判断，也可以使用独立的 `noPulseTimeoutSec` 作为启动后最大等待。实现时应避免刚开阀的前几秒误报缺水。

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
排队任务仍受单次最长浇水时长、Zone 启用状态、Flow 启用状态、漏水保护和恢复出厂保护约束。
```

## Flow Calibration

Flow 校准分两种。

快速单点校准：

```text
1. 用户选择 Flow。
2. 系统开始采集脉冲和时长。
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

## Calibration Redesign Scope

本轮重构必须同步重写全部校准功能。校准不能继续沿用旧的 Zone 级启动补偿模型：

```text
startupPulseLimit
startupEstimatedMl
stablePulsePerLiter
```

这些字段、相关候选参数、上一套参数、页面文案、API 参数和计算逻辑都删除。新校准体系拆成两类对象：Flow 参数校准和 Zone 流量学习。

Flow 参数必须支持三种修改来源：

```text
manual       用户手工修改并保存候选参数
singlePoint  单点采样生成 Offset=0 的候选参数
multiPoint   多点采样拟合 K+Offset 的候选参数
```

Flow 参数页面必须允许手工修改：

```text
kUlPerMinPerHz
offsetMilliHz
pressurizeSec
sampleWindowSec
```

手工修改必须走候选参数流程，不直接覆盖当前参数。应用候选时保存上一套 Flow 参数，便于回退。Flow 参数候选和上一套参数按 Flow 独立保存：

```text
Flow 1 current / candidate / previous
Flow 2 current / candidate / previous
```

Zone 学习参数也必须支持自动学习和手工修改：

```text
learnedFlowMlPerMin
lowFlowPermille
highFlowPermille
noPulseTimeoutSec
```

这些参数属于 Zone，不属于 Flow。Zone 学习候选和上一套参数按 Zone 独立保存：

```text
Zone 1 current / candidate / previous
...
Zone 6 current / candidate / previous
```

自动学习只生成候选值；手工编辑也只保存候选值。只有用户明确应用候选后，当前运行参数才改变。应用候选时必须要求目标 Flow 或 Zone 当前空闲。

校准页面建议分区：

```text
Flow 校准
  Flow 1/2 当前参数、候选参数、上一套参数
  手工编辑 K/Offset/建压时间/采样窗口
  单点采样
  多点采样
  拟合误差和可信度

Zone 学习
  Zone 1..6 当前学习参数、候选参数、上一套参数
  手工编辑正常流量/高低阈值/无脉冲超时
  自动学习
```

校准 API 也按新对象重写，不保留旧路径语义：

```text
Flow candidate save/apply/restore
Flow calibration sample start/stop/save/clear/fit
Zone learning start/stop/save candidate/apply/restore
```

可以继续使用 `/api/v1/calibration/*` 前缀，但请求字段和语义必须是新模型。不提供旧 `startupPulseLimit`、`startupEstimatedMl`、`stablePulsePerLiter` 的兼容入口。

校准采集期间的安全规则：

```text
Flow 校准采集期间，不允许普通手动/计划浇水启动。
Zone 学习期间，不允许普通手动/计划浇水启动。
停止全部必须中止当前校准或学习并关阀。
待机漏水检测不参与校准采集判断。
校准采样只允许同时打开一个 Zone。
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
5. 默认 lowFlowPermille = 700。
6. 默认 highFlowPermille = 1300。
```

异常判断：

```text
无脉冲超时：
  稳定阶段持续无脉冲，关闭该 Zone 并记录缺水/无流量。

低流量：
  当前流量 < learnedFlowMlPerMin * lowFlowPermille / 1000。

高流量：
  当前流量 > learnedFlowMlPerMin * highFlowPermille / 1000。
```

如果某个 Zone 尚未学习正常流量，系统仍可用无脉冲保护和总流量记录，但不启用高低流量比例判断。

## Idle Leak Detection

待机时没有任何 Zone 运行。如果 Flow 1 或 Flow 2 出现持续脉冲：

```text
记录对应 Flow 存在异常流动
关闭全部阀门
进入漏水保护
```

由于 Flow 可能管理多个 Zone，待机漏水只能定位到 Flow 组，不能精确定位到某个 Zone。页面应显示类似：

```text
Flow 1 检测到待机流动，请检查该 Flow 下的电磁阀和管路。
```

## Records And Events

浇水记录使用新格式，不迁移旧记录。

每条记录保存：

```text
zoneId
flowMeterId
kUlPerMinPerHzSnapshot
offsetMilliHzSnapshot
pressurizeSecSnapshot
sampleWindowSecSnapshot
learnedFlowMlPerMinSnapshot
lowFlowPermilleSnapshot
highFlowPermilleSnapshot
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
result
stopSource
```

记录必须保存 `flowMeterId` 和关键 Flow/Zone 参数快照。这样即使以后 Zone 改归属或 Flow 重新校准，历史记录仍能解释当时使用哪个 Flow、哪组 K+Offset 和哪组异常阈值计量。

缺水/无脉冲异常记录不保存多次停水恢复片段。第一版缺水即停，因此只需要保存第一次进入无脉冲观察、最后一次有脉冲、确认异常并停机的时间点，以及停机前脉冲和估算水量。

业务事件至少覆盖：

```text
Zone 启动被同 Flow 互斥拒绝
无流量停止
低流量停止
高流量停止
待机漏水
Flow K+Offset 参数应用
Zone 正常流量学习应用
```

## Web And API

业务页面重做为新模型。

首页：

```text
显示 Flow 1/2 当前状态和流量
显示 enabled Zone 的运行状态
运行中的 Zone 显示归属 Flow 的实时流量和累计水量
只提供符合互斥规则的启动操作
```

Flow 设置页：

```text
Flow 1/2 启用状态
输入 GPIO 只读显示
kUlPerMinPerHz
offsetMilliHz
pressurizeSec
sampleWindowSec
校准入口
```

Zone 设置页：

```text
Zone 1..6 启用状态
名称
阀门 GPIO 只读显示
归属 Flow：Flow 1 / Flow 2
学习流量
高低流量阈值
无脉冲超时
```

校准页：

```text
Flow K+Offset 校准
Zone 正常流量学习
样本误差和拟合可信度
```

所有状态改变操作必须使用 POST、鉴权、二次确认。Web/API 输出继续做 HTML/JSON escape。

## Storage

使用新的 namespace 和文件路径：

```text
Flow 配置 namespace: irr_flow_v1
Zone 配置 namespace: irr_zone_v1
记录文件: /irr/records_v1.bin
事件仍使用 Esp32BaseAppEventLog 或新业务事件字段
```

旧配置、旧 namespace、旧记录文件不读取、不迁移、不清理。设备部署前由用户格式化。

## Implementation Boundary

旧模块可删除或重写。目标是清晰的新业务核心，而不是在旧接口上做兼容适配。

建议模块边界：

```text
FlowMeterService
  管理 Flow 1/2 脉冲、K+Offset、实时流量、累计水量。

ValveService
  管理 6 路阀门 PWM、吸合/保持、安全关阀。

ZoneService
  管理 Zone 状态机、Flow 占用、互斥、启动/停止、运行统计。

CalibrationService
  管理 Flow 校准样本、K+Offset 拟合、Zone 流量学习。

ScheduleService
  面向 Zone 触发计划，不直接操作 Flow 或 Valve。

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
Zone 学习能保存正常流量
无脉冲、低流量、高流量能停止对应 Zone
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
