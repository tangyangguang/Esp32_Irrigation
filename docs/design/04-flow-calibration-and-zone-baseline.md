# 流量计校准和 Zone 正常流量

## 目标

本文件定义系统如何得到可信的水量、实时流速和每个 Zone 的异常判断基准。

设计原则：

```text
流量计必配
自动计划和普通手动浇水必须在流量计校准后才能运行
水量只用于观察、记录和异常判断，不用于按水量停止
流量计 K-factor 是全局参数，不按 Zone 保存
启动过程只做流速异常判断的稳定等待，不单独做水量补偿
每个 Zone 只保存自己的正常流量，用于低流量/高流量判断
所有测定类操作都必须由用户明确发起和确认保存
```

## 参数边界

### 全局流量计参数

流量计保存一套 K-factor 参数：

```text
pulsesPerLiter
calibratedAt
calibrationSampleCount
calibrationTotalPulseCount
calibrationTotalActualMl
```

`pulsesPerLiter` 表示每升水对应多少个脉冲。这个参数属于流量计本身，用于所有 Zone 的累计水量和实时流速计算。

`pulsesPerLiter = 0` 表示尚未完成校准。此时系统可以做维护测定，但不能执行自动计划和普通手动浇水。

### 全局稳定等待参数

启动后保存一套全局稳定等待参数：

```text
flowStabilizeSec
```

它从第一个有效脉冲开始计时，只用于屏蔽低流量/高流量判断。稳定等待期间产生的脉冲仍然计入本次浇水总量。

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

## 流量计校准

流量计校准的目的，是得到全局 `pulsesPerLiter`。

校准使用完整接水样本。用户从开阀开始接水，停止后输入总水量，系统按全程脉冲和全程水量计算：

```text
pulsesPerLiter = totalPulseCount * 1000 / actualMl
```

推荐流程：

```text
1. 用户选择一个已启用 Zone。
2. 用户准备 5L 或 10L 量桶；至少不低于 2L。
3. 用户确认开始校准。
4. 系统打开该 Zone，并记录总脉冲数和运行时长。
5. 用户接水到足够容量后停止校准。
6. 系统关闭泵和阀。
7. 用户输入实际接到的总水量。
8. 系统计算该样本的 pulsesPerLiter。
9. 用户可重复多条样本。
10. 系统显示每条样本的偏差和合并后的建议参数。
11. 用户确认后保存全局 pulsesPerLiter。
```

校准必须有最大运行时间保护：

```text
maintenanceMaxDurationSec = 600
```

如果达到最大时间仍未停止，系统自动关闭泵和阀，本次校准失败，不保存样本。

多样本合并使用总量加权：

```text
pulsesPerLiter = sum(totalPulseCount) * 1000 / sum(actualMl)
```

保存前必须检查：

```text
每条样本 totalPulseCount > 0
每条样本 actualMl > 0
校准时长不能太短
校准时长不能超过 maintenanceMaxDurationSec
推荐总水量至少 5L
多条样本之间偏差过大时，不建议保存
```

样本偏差显示建议：

```text
samplePulsesPerLiter = samplePulseCount * 1000 / sampleActualMl
deviationPercent = (samplePulsesPerLiter - mergedPulsesPerLiter) * 100 / mergedPulsesPerLiter
```

如果样本波动明显，页面应提示用户检查水压、管路空气、量桶读数、接水过程和流量计安装方向。

## 总水量计算

每次运行记录总脉冲和估算水量。

公式：

```text
estimatedVolumeMl = pulseCount * 1000 / pulsesPerLiter
```

这包含本次运行从开始到结束的全部有效脉冲。启动稳定等待不是水量补偿模型，不需要用户为每个 Zone 测试启动阶段水量。

## 实时流速计算

实时流速使用滑动窗口计算。

默认参数：

```text
flowSampleWindowSec = 5
flowUpdateIntervalMs = 1000
```

公式：

```text
flowMlPerMin = windowPulseCount * 60000 / (pulsesPerLiter * windowMs)
```

窗口内没有脉冲时：

```text
flowMlPerMin = 0
```

滑动窗口用于降低短时水压波动和低流量脉冲离散造成的抖动。低流量和高流量判断必须结合 `flowFaultConfirmSec`，不能只基于一次瞬时值。

启动过程规则：

```text
WaitingForFirstPulse：等待第一个有效脉冲，超时为无水
FlowStabilizing：第一个有效脉冲后等待 flowStabilizeSec，不做低/高流量判断
Running：稳定运行阶段，允许低/高流量判断
```

## 测定正常流量

正常流量用于判断某个 Zone 是否低流量或高流量。它不是自动调整功能，而是用户明确触发的维护操作。

推荐命名：

```text
测定正常流量
```

流程：

```text
1. 用户选择一个已启用 Zone。
2. 系统要求 pulsesPerLiter 已校准。
3. 用户确认该 Zone 当前管路、喷头、滴头状态正常。
4. 系统打开该 Zone。
5. 等待第一个有效脉冲。
6. 等待 flowStabilizeSec。
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

### 流量计异常

流量计异常只用于输入电气或计数异常，例如脉冲频率超过合理硬上限、输入抖动风暴、计数器状态异常。启动后无脉冲或运行中无脉冲按 `no_water` 处理，不按流量计异常处理。

## 用户界面要求

Web 页面应提供：

```text
查看 pulsesPerLiter 和校准时间
流量计校准，多样本列表和偏差提示
Zone 正常流量状态列表
手工输入 Zone 正常流量
测定 Zone 正常流量
低流量/高流量百分比设置
```

页面必须清楚区分：

```text
流量计校准：决定累计水量和实时流速是否可信
Zone 正常流量：决定低流量/高流量判断是否可用
```

本地屏幕只显示：

```text
流量计是否已校准
当前实时流速
当前累计水量
Zone 是否已测定正常流量
```

本地按键不做流量计校准和正常流量测定。
