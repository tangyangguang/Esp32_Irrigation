# 配置和数据模型

## 目标

本文件定义新系统第一版需要持久化的数据。原则：

```text
配置少而清楚
运行状态不混进配置
记录和事件分开
不保留旧项目字段、旧 key、旧存储格式
```

## 配置分类

第一版只保留 5 类用户配置：

```text
系统配置
Zone 配置
计划组配置
流量计和阀门参数
故障策略
```

## 系统配置

系统配置描述全局行为。

```text
autoMode
  enabled：自动浇水开启
  disabled：自动浇水关闭
  disabled_until：自动浇水暂停到指定时间后自动开启

autoResumeAt
  仅 autoMode = disabled_until 时有效

pumpStartEnabled
  是否启用自吸泵启动输出，默认 false

lowLevelEnabled
  是否启用低液位输入，默认 false

queuedPlanMaxDelayMin
  自动计划排队最长等待时间，默认 60 分钟
```

默认值：

```text
autoMode = disabled
pumpStartEnabled = false
lowLevelEnabled = false
queuedPlanMaxDelayMin = 60
```

自动浇水默认关闭。用户完成 Zone、计划和流量计配置后，再手动开启自动浇水。

## Zone 配置

硬件固定提供 6 路电磁阀输出，但现场不一定全部接水路。每个 Zone 都有独立配置：

```text
zoneId：1..6
enabled：是否启用
name：显示名称
defaultManualDurationSec：手动浇水默认时长
normalFlowMlPerMin：正常流量基线
lowFlowPercent：低流量阈值百分比
highFlowPercent：高流量阈值百分比
flowFaultConfirmSec：流量异常确认时间
```

默认值：

```text
Zone 1 enabled = true
Zone 2..6 enabled = false
name = 水路1..水路6
defaultManualDurationSec = 300
normalFlowMlPerMin = 0
lowFlowPercent = 60
highFlowPercent = 160
flowFaultConfirmSec = 10
```

`normalFlowMlPerMin = 0` 表示尚未建立基线。没有基线时：

```text
无水检测仍然工作
累计水量仍然工作
低流量/高流量基线检测不启用
页面提示该 Zone 尚未学习/设置正常流量
```

## 计划组配置

最多 4 个计划组。每个计划组是一套浇水方案，可以有最多 4 个开始时间。

```text
planGroupId：1..4
enabled：是否启用
name：显示名称
startTimes[4]：一天中的开始时间，单位分钟，0..1439；未使用项为 disabled
cycleLengthDays：周期长度，1..31
cycleAnchorDate：周期第 1 天对应的日期
activeDayMask：周期内哪些天浇水
zoneDurationsSec[6]：Zone 1..6 每次运行时长，0 表示跳过
```

执行规则：

```text
计划组禁用：不触发
当天不是周期浇水日：不触发
开始时间未启用：不触发
Zone 禁用：执行时跳过
Zone 运行时长为 0：执行时跳过
```

默认值：

```text
所有计划组 disabled
startTimes 全部 disabled
cycleLengthDays = 1
activeDayMask = 第 1 天启用
zoneDurationsSec 全部 0
```

## 流量计和阀门参数

流量计必配。第一版至少支持单点校准。

```text
mlPerPulse
  每个脉冲对应的毫升数，由校准得到

flowSampleWindowSec
  实时流速滑动窗口，默认 5 秒

flowUpdateIntervalMs
  实时流速更新间隔，默认 1000 ms

noWaterConfirmSec
  启动后无有效流量确认时间，默认 15 秒

idleLeakConfirmSec
  待机异常流量确认时间，默认 30 秒

valvePullInMs
  电磁阀全功率吸合时间，默认 3000 ms

valveHoldDutyPercent
  电磁阀保持占空比，默认 60

valvePwmFrequencyHz
  电磁阀保持 PWM 频率，默认 20000 Hz
```

默认值：

```text
mlPerPulse = 0
flowSampleWindowSec = 5
flowUpdateIntervalMs = 1000
noWaterConfirmSec = 15
idleLeakConfirmSec = 30
valvePullInMs = 3000
valveHoldDutyPercent = 60
valvePwmFrequencyHz = 20000
```

`mlPerPulse = 0` 表示流量计尚未校准。未校准时：

```text
系统可以检测有无脉冲
不能给出可信水量
不能给出可信流量
自动计划应拒绝开启，提示必须完成流量计校准
手动测试可允许，但页面必须明确标注未校准
```

## 故障策略

只保留少量用户可理解的策略项。

```text
noWaterLockZone
  无水停机后是否锁定当前 Zone，默认 true

highFlowAction
  高流量处理：warn / stop，默认 stop

highFlowLockZone
  高流量停机后是否锁定当前 Zone，默认 true

lowFlowAction
  低流量处理：warn / stop，默认 warn

lowFlowLockZone
  低流量停机后是否锁定当前 Zone，默认 false
```

固定规则不做配置：

```text
无水一定停机
低液位一定停机但不锁定 Zone
待机漏水一定全局锁定
流量计异常一定全局锁定
```

## 运行状态

运行状态不是用户配置。它可以保存在内存，必要时少量持久化，但不进入配置模型。

```text
当前是否运行
当前运行 Zone
当前来源：手动 / 自动计划
当前计划组和开始时间
当前 Zone 开始时间
当前 Zone 计划运行时长
当前 Zone 已运行时间
当前累计脉冲
当前估算水量
当前实时流量
等待队列
```

第一版不支持暂停/继续，也不支持重启后恢复未完成计划。因此运行状态不需要保存“剩余进度”。

## 故障锁定状态

故障锁定是状态，不是配置，但需要持久化，避免重启后丢失安全保护。

```text
globalLockReason
zoneLockReasons[6]
lockedAt
lastFaultCode
```

用户清除故障时，只清除锁定状态，不删除浇水记录和事件日志。

## 浇水记录

实际开阀运行后才写浇水记录。浇水记录是业务主记录，尽量多保留。

```text
recordId
zoneId
source：manual / schedule
planGroupId：自动计划来源时有效
startedAt
endedAt
plannedDurationSec
actualDurationSec
pulseCount
estimatedVolumeMl
avgFlowMlPerMin
minFlowMlPerMin
maxFlowMlPerMin
stopReason
faultCode
```

停止原因建议枚举：

```text
completed
user_stop
no_water
high_flow
low_flow
low_level
flow_meter_fault
```

## 轻量事件日志

事件日志只记录故障、警告和重要状态变化。正常计划开始、正常完成、正常手动浇水不写事件日志。

建议事件字段：

```text
time
level：info / warning / fault
code
zoneId：可选
planGroupId：可选
value：可选数值
```

建议事件：

```text
auto_enabled
auto_disabled
auto_disabled_until
no_water_stop
high_flow_warn
high_flow_stop
low_flow_warn
low_flow_stop
low_level_stop
idle_leak_lock
flow_meter_fault
zone_fault_cleared
global_fault_cleared
queued_plan_expired
system_restarted_during_run
```

事件日志使用基础库能力，默认保留 512 条，最大可配 1024 条，环形覆盖。
