# 流量计校准和 Zone 流量基线

## 目标

本文件定义第一版如何得到可信的水量和流速数据，以及如何建立每个 Zone 的正常流量基线。

设计原则：

```text
流量计必配
自动计划必须在流量计校准后才能开启
水量只用于观察、记录和异常判断，不用于按水量停止
每个 Zone 单独建立正常流量基线
低流量/高流量基于 Zone 基线判断
第一版只做单点流量计校准，不做复杂多点校准
```

## 流量计校准

### 校准目的

校准用于得到：

```text
mlPerPulse
```

也就是每个流量计脉冲代表多少毫升水。

有了 `mlPerPulse` 后，系统才能计算：

```text
累计水量
实时流速
Zone 正常流量基线
低流量/高流量判断
```

### 校准入口

校准是维护操作，不属于普通手动浇水。

校准入口必须：

```text
只能从 Web 页面/API 进入
使用 POST
要求用户确认
明确提示会打开指定 Zone 放水
要求现场准备量杯、量桶或已知容积容器
```

本地按键不做流量计校准。

### 校准流程

推荐流程：

```text
1. 用户选择用于校准的 Zone。
2. 系统检查该 Zone enabled，且没有全局锁定、Zone 锁定、低液位。
3. 用户点击开始校准。
4. 系统打开该 Zone，并在需要时启动泵。
5. 用户接水到量桶。
6. 用户点击停止校准。
7. 系统关闭泵和阀，记录校准期间脉冲数。
8. 用户输入实际接到的水量。
9. 系统计算 mlPerPulse = actualMl / pulseCount。
10. 用户确认保存。
```

校准过程中不要求已有 `mlPerPulse`。这是普通启动校验的例外。

### 校准有效性检查

保存前必须检查：

```text
pulseCount > 0
actualMl > 0
校准时长不能太短
校准期间不能发生低液位、无脉冲异常或用户强制停止
计算出的 mlPerPulse 在合理范围内
```

第一版建议：

```text
校准时长至少 20 秒
推荐接水量至少 2L
更推荐 5L 或 10L
```

如果样本太小，页面提示结果可能不准，不建议保存。

### 校准记录

保存校准参数时记录：

```text
mlPerPulse
calibratedAt
samplePulseCount
sampleActualMl
sampleDurationSec
sampleZoneId
```

这些不是浇水记录，只是流量计校准参数元数据。

## 实时流速计算

实时流速使用滑动窗口计算。

默认：

```text
flowSampleWindowSec = 5
flowUpdateIntervalMs = 1000
```

公式：

```text
flowMlPerMin = windowPulseCount * mlPerPulse * 60 / windowSeconds
```

如果窗口内脉冲数为 0：

```text
flowMlPerMin = 0
```

滑动窗口用于降低短时水压波动和低流量脉冲离散带来的抖动。异常判断不得只基于单次瞬时值，必须结合持续确认时间。

## Zone 正常流量基线

### 基线目的

每个 Zone 的喷头数量、管长、滴头数量和压力损失不同，正常流量不同。因此低流量和高流量应按 Zone 基线判断。

每个 Zone 保存：

```text
normalFlowMlPerMin
lowFlowPercent
highFlowPercent
flowFaultConfirmSec
baselineUpdatedAt
```

默认：

```text
normalFlowMlPerMin = 0
lowFlowPercent = 60
highFlowPercent = 160
flowFaultConfirmSec = 10
```

`normalFlowMlPerMin = 0` 表示尚未建立基线。此时：

```text
不启用低流量/高流量基线检测
仍启用无水检测
仍记录水量和流速
页面提示该 Zone 尚未建立正常流量基线
```

### 基线建立方式

第一版支持两种方式：

```text
手动输入正常流量
运行学习正常流量
```

#### 手动输入

用户可以直接输入 `normalFlowMlPerMin`。适用于用户已经知道该水路正常流量，或使用外部量测工具。

#### 运行学习

运行学习流程：

```text
1. 用户选择一个已启用 Zone。
2. 系统要求流量计已校准。
3. 用户确认该 Zone 当前水路、喷头、滴头状态正常。
4. 系统打开该 Zone。
5. 先等待出水稳定。
6. 采集一段稳定运行期间的流量。
7. 计算平均流量，作为建议基线。
8. 用户确认保存。
```

默认参数：

```text
baselineStabilizeSec = 15
baselineSampleSec = 30
```

学习期间如果发生无水、低液位或用户停止，则学习失败，不保存基线。

### 基线计算

学习时建议：

```text
跳过前 baselineStabilizeSec
在 baselineSampleSec 内每秒采样 flowMlPerMin
丢弃明显为 0 的无效样本
计算有效样本平均值
```

如果有效样本不足：

```text
学习失败
提示水流不稳定或样本不足
```

第一版不做复杂统计，不做自动异常点剔除算法。只需要保证样本时间足够，并在页面提示用户确保现场水路状态正常。

## 异常判断

### 无水

无水不依赖 Zone 基线。只要启动后或运行中超过 `noWaterConfirmSec` 没有有效流量，即判定无水。

### 低流量

低流量需要 Zone 有基线：

```text
lowThreshold = normalFlowMlPerMin * lowFlowPercent / 100
```

如果 `flowMlPerMin` 持续低于 `lowThreshold` 超过 `flowFaultConfirmSec`，低流量成立。

默认策略：

```text
报警，不停机
```

### 高流量

高流量需要 Zone 有基线：

```text
highThreshold = normalFlowMlPerMin * highFlowPercent / 100
```

如果 `flowMlPerMin` 持续高于 `highThreshold` 超过 `flowFaultConfirmSec`，高流量成立。

默认策略：

```text
停机，并锁定当前 Zone
```

### 待机漏水

待机漏水不依赖 Zone 基线。系统 Idle 时如果持续检测到流量超过 `idleLeakConfirmSec`，触发全局锁定。

## 用户界面要求

Web 页面应提供：

```text
流量计校准
查看当前 mlPerPulse 和校准时间
Zone 基线状态列表
手动输入 Zone 正常流量
运行学习 Zone 正常流量
低流量/高流量百分比设置
```

页面必须清楚区分：

```text
流量计校准：决定水量和流速是否可信
Zone 基线：决定低流量/高流量判断是否可用
```

本地屏幕只显示：

```text
流量计是否已校准
当前实时流速
当前累计水量
Zone 是否已有基线
```

本地按键不做校准和基线学习。

