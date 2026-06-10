# 流量计稳定态校准和 Zone 流量参数

## 目标

本文件定义系统如何得到可信的水量、实时流速和每个 Zone 的异常判断基准。

设计原则：

```text
流量计必配
自动计划和普通手动浇水必须在流量计稳定态校准后才能运行
水量只用于观察、记录和异常判断，不用于按水量停止
流量计稳定态参数是全局参数，不按 Zone 保存
启动阶段水量和正常流量是 Zone 参数，因为每路管长、喷头、空气和压力损失不同
低流量/高流量只在稳定运行阶段判断
所有测定类操作都必须由用户明确发起和确认保存
```

## 参数边界

### 全局流量计参数

流量计保存一套稳定态参数：

```text
stablePulsesPerLiter
calibratedAt
calibrationSampleCount
calibrationTotalStablePulseCount
calibrationTotalStableActualMl
```

`stablePulsesPerLiter` 表示稳定状态下每升水对应多少个脉冲。这个参数属于流量计本身，用于所有 Zone 的稳定阶段水量和实时流速计算。

`stablePulsesPerLiter = 0` 表示尚未完成稳定态校准。此时系统可以做维护测定，但不能执行自动计划和普通手动浇水。

### Zone 启动阶段参数

每个 Zone 保存自己的启动阶段参数：

```text
startupWindowSec
startupEstimatedMl
startupPulseCount
startupMeasuredAt
```

启动阶段从第一个有效脉冲开始计时，不从开阀或开泵时刻开始计时。第一个有效脉冲之前的时间只用于判断是否无水，不参与低/高流量判断。

`startupEstimatedMl` 用于累计水量补偿。它不是流量计全局参数，因为不同 Zone 的管长、阀门位置、喷头、滴头、空气和泵建立压力过程不同。

### Zone 正常流量参数

每个 Zone 保存自己的正常流量参数：

```text
normalFlowMlPerMin
lowFlowPercent
highFlowPercent
flowFaultConfirmSec
normalFlowMeasuredAt
```

`normalFlowMlPerMin` 用于低流量和高流量判断。它必须由用户明确测定或手工输入后保存，系统不能在普通运行中自动悄悄修改。

## 启动阶段测定

启动阶段测定的目的，是得到某个 Zone 在启动窗口内的大致出水量，用于总水量估算。

推荐流程：

```text
1. 用户选择一个已启用 Zone。
2. 用户设置或确认 startupWindowSec，默认 10 秒。
3. 用户准备量杯或量桶，并确认开始。
4. 系统打开该 Zone，按正常顺序开阀、可选开泵。
5. 系统等待第一个有效脉冲。
6. 从第一个有效脉冲开始计时 startupWindowSec。
7. 到时后关闭泵和阀。
8. 用户输入实际接到的水量。
9. 系统保存 startupEstimatedMl、startupPulseCount、startupMeasuredAt。
```

有效性检查：

```text
必须收到至少 1 个有效脉冲
用户输入水量必须 > 0
测定期间不能触发低液位
如果首脉冲超时，测定失败
```

启动阶段水量允许存在小误差。它只影响短时运行和总水量中的启动段补偿；稳定态参数才是长期累计水量准确性的关键。

## 稳定态校准

稳定态校准的目的，是得到全局 `stablePulsesPerLiter`。

校准使用完整长样本，但计算时扣除所选 Zone 的启动阶段估算：

```text
stablePulseCount = totalPulseCount - startupPulseCount
stableActualMl = totalActualMl - startupEstimatedMl
stablePulsesPerLiter = stablePulseCount * 1000 / stableActualMl
```

推荐流程：

```text
1. 用户选择一个已完成启动阶段测定的 Zone。
2. 用户准备 5L 或 10L 量桶；至少不低于 2L。
3. 用户确认开始校准。
4. 系统打开该 Zone，并记录总脉冲数和运行时长。
5. 用户接水到足够容量后停止校准。
6. 系统关闭泵和阀。
7. 用户输入实际接到的总水量。
8. 系统扣除该 Zone 的启动阶段估算，得到稳定阶段样本。
9. 用户可重复多条样本。
10. 系统显示每条样本的偏差和合并后的建议参数。
11. 用户确认后保存全局 stablePulsesPerLiter。
```

多样本合并使用总量加权：

```text
stablePulsesPerLiter = sum(stablePulseCount) * 1000 / sum(stableActualMl)
```

保存前必须检查：

```text
每条样本 stablePulseCount > 0
每条样本 stableActualMl > 0
校准时长不能太短
推荐总稳定阶段水量至少 5L
多条样本之间偏差过大时，不建议保存
```

样本偏差显示建议：

```text
deviationPercent = (samplePulsesPerLiter - mergedPulsesPerLiter) * 100 / mergedPulsesPerLiter
```

如果样本波动明显，页面应提示用户检查水压、管路空气、量桶读数、接水过程和流量计安装方向。

## 总水量计算

每次运行记录总脉冲、稳定阶段脉冲和估算水量。

运行阶段划分：

```text
WaitingForFirstPulse：等待第一个有效脉冲
StartupWindow：从第一个有效脉冲开始，到 startupWindowSec 结束
Running：稳定运行阶段
```

总水量公式：

```text
estimatedVolumeMl = startupVolumeMl + stablePulseCount * 1000 / stablePulsesPerLiter
```

启动阶段水量：

```text
如果本次完整经过 StartupWindow：
  startupVolumeMl = startupEstimatedMl

如果本次在 StartupWindow 内停止：
  startupVolumeMl = startupEstimatedMl * actualStartupMs / (startupWindowSec * 1000)

如果该 Zone 尚未测定 startupEstimatedMl：
  startupVolumeMl = 0
```

首脉冲之前的少量出水不单独建模，归入启动阶段估算误差。这个取舍可以保持用户操作简单，同时把影响最大的稳定态参数校准准确。

## 实时流速计算

实时流速只在稳定运行阶段用于显示和异常判断。

默认参数：

```text
flowSampleWindowSec = 5
flowUpdateIntervalMs = 1000
```

公式：

```text
flowMlPerMin = windowPulseCount * 60000 / (stablePulsesPerLiter * windowMs)
```

窗口内没有脉冲时：

```text
flowMlPerMin = 0
```

滑动窗口用于降低短时水压波动和低流量脉冲离散造成的抖动。低流量和高流量判断必须结合 `flowFaultConfirmSec`，不能只基于一次瞬时值。

## 测定正常流量

正常流量用于判断某个 Zone 是否低流量或高流量。它不是自动调整功能，而是用户明确触发的维护操作。

推荐命名：

```text
测定正常流量
```

流程：

```text
1. 用户选择一个已启用 Zone。
2. 系统要求 stablePulsesPerLiter 已校准。
3. 用户确认该 Zone 当前管路、喷头、滴头状态正常。
4. 系统打开该 Zone。
5. 等待第一个有效脉冲。
6. 跳过该 Zone 的 StartupWindow。
7. 在稳定运行阶段采集一段流速样本，建议 30 秒。
8. 系统显示平均流量、最小流量、最大流量。
9. 用户确认后保存 normalFlowMlPerMin。
```

测定期间如果发生首脉冲超时、低液位或用户停止，则测定失败，不保存正常流量。

用户也可以手工输入 `normalFlowMlPerMin`。适用于用户已经用外部工具测得某路正常流量。

## 异常判断

### 无水

无水不依赖正常流量参数。

```text
WaitingForFirstPulse 超过 firstPulseTimeoutSec 没有第一个有效脉冲：无水
Running 中连续无有效脉冲超过 runningNoPulseTimeoutSec：无水
```

无水一定停机。

### 低流量

低流量需要该 Zone 有 `normalFlowMlPerMin`：

```text
lowThreshold = normalFlowMlPerMin * lowFlowPercent / 100
```

Running 稳定阶段中，如果 `flowMlPerMin` 持续低于 `lowThreshold` 超过 `flowFaultConfirmSec`，低流量成立。

默认策略：

```text
报警，不停机
```

### 高流量

高流量需要该 Zone 有 `normalFlowMlPerMin`：

```text
highThreshold = normalFlowMlPerMin * highFlowPercent / 100
```

Running 稳定阶段中，如果 `flowMlPerMin` 持续高于 `highThreshold` 超过 `flowFaultConfirmSec`，高流量成立。

默认策略：

```text
停机，并锁定当前 Zone
```

### 待机漏水

待机漏水不依赖正常流量参数。系统 Idle 时如果持续检测到流量超过 `idleLeakConfirmSec`，触发全局锁定。

## 用户界面要求

Web 页面应提供：

```text
查看 stablePulsesPerLiter 和校准时间
启动阶段测定
稳定态校准，多样本列表和偏差提示
Zone 正常流量状态列表
手工输入 Zone 正常流量
测定 Zone 正常流量
低流量/高流量百分比设置
```

页面必须清楚区分：

```text
流量计稳定态校准：决定稳定阶段水量和实时流速是否可信
Zone 启动阶段测定：补偿每路启动阶段水量
Zone 正常流量：决定低流量/高流量判断是否可用
```

本地屏幕只显示：

```text
流量计是否已校准
当前实时流速
当前累计水量
Zone 是否已测定启动阶段
Zone 是否已测定正常流量
```

本地按键不做校准、启动阶段测定和正常流量测定。
