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
    float kLitersPerMinutePerHz;
    float offsetHz;
    uint16_t pressurizeSec;
    uint16_t sampleWindowSec;
};
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
elapsedSec = elapsedMs / 1000
f = pulseDelta / elapsedSec
flowMlPerMin = max(0, K * (f + Offset)) * 1000
volumeMl += flowMlPerMin * elapsedSec / 60
```

如果 `Offset = 0`，模型退化为普通 P/L 模型：

```text
K = 60 / pulsesPerLiter
```

但新系统的持久化参数只保存 `K` 和 `Offset`，不再保存旧的 `stablePulsePerLiter`、`startupPulseLimit`、`startupEstimatedMl`。

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

用户应优先采集明显不同的流量点，例如少喷头、中等喷头、多喷头。流量点过于集中时，可以保存结果，但页面必须显示该结果只是局部拟合。

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
result
stopSource
```

记录必须保存 `flowMeterId` 快照。这样即使以后 Zone 改归属，历史记录仍能解释当时使用哪个 Flow 计量。

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
K
Offset
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
K+Offset 快速校准计算正确
K+Offset 多点拟合计算正确
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
