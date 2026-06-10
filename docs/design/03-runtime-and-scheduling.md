# 运行状态机和调度规则

## 目标

本文件定义运行时行为：

```text
一次只运行一个 Zone
所有入口共用同一套启动校验
停止只表示停止当前运行
不支持暂停/继续
不支持重启后恢复未完成运行
自动计划冲突时排队顺延
故障停机后按故障策略处理
```

## 运行入口

系统只有两类启动入口：

```text
手动启动 Zone
自动计划启动 Zone 序列
```

无论来自 Web、API、本地按键还是计划调度，都必须经过同一套业务校验。

流量计校准和正常流量测定是维护入口，不属于普通浇水入口。这些流程可以在 `pulsesPerLiter = 0` 或 Zone 正常流量未设置时运行，但必须有单独页面/接口、明确提示和用户确认。

## 运行状态

运行状态建议保持为小状态机：

```text
Idle
Starting
WaitingForFirstPulse
FlowStabilizing
Running
Stopping
FaultStopping
```

含义：

```text
Idle：
  没有 Zone 正在运行。

Starting：
  已通过启动校验，正在打开阀门和可选泵。

WaitingForFirstPulse：
  已打开输出，正在等待流量计第一个有效脉冲。

FlowStabilizing：
  已收到第一个有效脉冲，正在等待水流进入相对稳定状态。

Running：
  已进入稳定运行阶段，按时间运行并持续监测。

Stopping：
  用户停止或正常到时，正在关闭输出并写记录。

FaultStopping：
  故障触发停机，正在关闭输出、写记录、写事件并设置锁定。
```

## 启动前校验

启动任何 Zone 前必须检查：

```text
系统当前为 Idle
目标 Zone 存在且 enabled
目标 Zone 未被锁定
没有全局故障锁定
正常手动/自动浇水要求流量计 K-factor 已校准
如果 lowLevelEnabled，则低液位输入正常
如果来源是自动计划，则 autoMode 当前允许自动浇水
```

不通过时拒绝启动，并返回明确原因。

## 手动启动

手动启动参数：

```text
zoneId
durationSec
```

如果用户没有指定时长，使用 Zone 的 `defaultManualDurationSec`。

手动启动不进入自动计划队列。系统忙时直接拒绝，提示当前正在运行。

自动浇水关闭或暂停不影响手动启动。

## 自动计划执行

计划调度每分钟检查一次即可。检查条件：

```text
系统时间有效
autoMode 允许自动浇水
当天符合计划组周期规则
当前分钟匹配某个启用开始时间
计划组 enabled
计划组至少有一个 enabled Zone 且运行时长 > 0
```

匹配后生成一次计划运行实例：

```text
planGroupId
startTimeIndex
scheduledAt
zoneSteps[]
```

`zoneSteps` 按 Zone 1..6 顺序生成，只包含：

```text
Zone enabled
zoneDurationSec > 0
```

## 自动计划排队

如果计划触发时系统 Idle，立即开始该计划运行实例的第一个 Zone。

如果系统正在运行，计划运行实例进入等待队列。

队列规则：

```text
按 scheduledAt 先后排序
同一分钟多个计划组触发时，按 planGroupId 从小到大排序
队列只保存自动计划，不保存手动请求
等待超过 queuedPlanMaxDelayMin 后跳过
```

跳过过期计划时写轻量事件：

```text
queued_plan_expired
```

如果 `queuedPlanMaxDelayMin = 0`，表示冲突时不排队，直接跳过。

## Zone 启动顺序

启动一个 Zone：

```text
1. 进入 Starting。
2. 打开目标 Zone 电磁阀，进入吸合阶段。
3. 延迟 0.5~1 秒。
4. 如果 pumpStartEnabled，打开 PUMP_START_OUT。
5. 进入 WaitingForFirstPulse。
6. 在 firstPulseTimeoutSec 内等待第一个有效脉冲。
7. 收到第一个有效脉冲后，记录 firstPulseAt，进入 FlowStabilizing。
8. FlowStabilizing 持续到 firstPulseAt + flowStabilizeSec。
9. 稳定等待结束后进入 Running。
10. 超时无有效脉冲，进入 FaultStopping，故障为 no_water。
```

默认采用先开阀再开泵。这样避免泵先对封闭管路建立压力。

第一个有效脉冲之前的时间属于等待出水，只用于无水判断。第一个有效脉冲之后的 `flowStabilizeSec` 只用于屏蔽低流量/高流量判断；期间的脉冲仍然计入累计水量。

## Running 监测

Running 状态持续执行：

```text
累计运行时间
累计脉冲和估算水量
按滑动窗口更新实时流量
检查运行时长是否到达
检查用户停止请求
检查低液位
检查无流量
检查低流量
检查高流量
```

正常到时：

```text
进入 Stopping
stopReason = completed
```

用户停止：

```text
需要二次确认
确认后进入 Stopping
stopReason = user_stop
当前自动计划剩余 Zone 取消
等待队列清空
```

## 流量异常确认

无水：

```text
WaitingForFirstPulse 超过 firstPulseTimeoutSec 没有第一个有效脉冲
必须停机
```

运行中无流量：

```text
Running 中连续无有效脉冲超过 runningNoPulseTimeoutSec
按 no_water 处理
必须停机
```

FlowStabilizing 阶段不做低流量/高流量判断。低流量和高流量只在 Running 稳定阶段判断。

低流量：

```text
Zone 有 normalFlowMlPerMin 时启用
flowMlPerMin 持续低于 normalFlowMlPerMin * lowFlowPercent / 100
持续超过 flowFaultConfirmSec 后成立
按 lowFlowAction 处理
```

高流量：

```text
Zone 有 normalFlowMlPerMin 时启用
flowMlPerMin 持续高于 normalFlowMlPerMin * highFlowPercent / 100
持续超过 flowFaultConfirmSec 后成立
按 highFlowAction 处理
```

低液位：

```text
仅 lowLevelEnabled 时启用
运行中触发低液位立即停机
```

待机漏水：

```text
Idle 时检测到持续流量超过 idleLeakConfirmSec
设置全局锁定
写 idle_leak_lock 事件
不生成浇水记录
```

## 停止输出顺序

停止当前运行时：

```text
1. 关闭 PUMP_START_OUT。
2. 关闭当前 Zone 电磁阀。
3. 关闭阀门 PWM 输出。
4. 固化浇水记录。
5. 回到 Idle 或继续队列中的下一个计划实例。
```

故障停机时也使用同一输出关闭顺序，但额外执行：

```text
写轻量事件日志
按故障策略设置 Zone 锁定或全局锁定
取消当前自动计划剩余 Zone
清空等待队列
```

## 自动计划内的 Zone 串行

自动计划运行实例包含多个 Zone step。

每个 Zone 正常完成后：

```text
如果还有下一个 Zone step，启动下一个 Zone
如果没有，计划运行实例完成，回到 Idle
如果队列里有等待计划，检查是否过期，未过期则启动下一个计划实例
```

如果某个 Zone 被用户停止或故障停机：

```text
当前计划运行实例终止
剩余 Zone step 取消
等待队列清空
```

## 自动浇水状态

`autoMode` 只影响自动计划触发和队列，不影响手动启动。

```text
enabled：
  自动计划可触发。

disabled：
  自动计划到点直接跳过，不入队，不补跑。

disabled_until：
  当前时间早于 autoResumeAt 时跳过自动计划。
  当前时间达到或晚于 autoResumeAt 后自动转为 enabled。
```

自动浇水关闭或暂停期间，不为每次跳过写事件日志，避免刷屏。只在用户改变自动浇水状态时写轻量事件。

## 设备重启

设备启动时：

```text
所有阀门输出关闭
PUMP_START_OUT 关闭
运行状态重置为 Idle
未完成运行不恢复
已经错过的自动计划不补跑
保留故障锁定状态
```

如果系统检测到上次重启发生在运行期间，可以写轻量事件：

```text
system_restarted_during_run
```

## 用户停止二次确认

由于系统一次只运行一个 Zone，用户停止只对应当前运行。

Web/API：

```text
页面必须弹出确认
API 必须使用 POST
API 参数需包含 confirm=true 或等价确认字段
```

本地按键：

```text
第一次按下停止键：进入确认状态，屏幕提示将停止当前运行
确认窗口内再次按下：执行停止
超时或切换页面：取消确认
```

确认窗口建议 5 秒。
