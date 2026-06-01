# 计划管理体系设计

从零开始的计划管理完整设计。计划属于 Zone，每个 Zone 最多 6 条计划。

## 一、三大核心概念

```
Plan Definition  ──调度器──►  Plan Execution  ──成功──►  Zone.start()  ──结束──►  Watering Record
(用户配置)                        (今日运行态)                                (执行态)              (历史事实)
```

| 概念 | 是什么 | 存储 | 生命周期 |
|------|--------|------|---------|
| Plan Definition | 计划怎么配 | 持久化（NVS） | 永久，用户创建/编辑/删除 |
| Plan Execution | 今天执行得怎么样 | 内存 | 每天重置 |
| ScheduleSkip | 哪天哪个计划被跳过 | 持久化（NVS） | 环形，自动清理过期 |
| Watering Record | 实际浇水的历史事实 | 文件系统环形记录 | 永久 |

## 二、Plan Definition（计划定义）

```
PlanDefinition
├── id                  // 全局唯一 ID，uint8_t，1-based
├── zoneId              // 属于哪个 Zone，不可变
├── name[16]            // 计划名称，如 "早浇"、"午浇"
├── enabled             // 启用/停用
├── timeHour            // 0-23
├── timeMinute          // 0-59
├── durationSec         // 1-14400，必填，非零
├── cycleDays           // 周期长度，1-30
├── cycleMask           // 周期内哪些天浇水，bitmap（bit0 = 第 1 天）
├── cycleStartYmd       // 周期起始日 YYYYMMDD，不可变
└── createdAt           // 创建时的 Unix 时间戳
```

**循环模型（只有一种）**：

```
cycleDays = N, cycleMask = bitmap
→ 以 cycleStartYmd 为起点，(today - cycleStartYmd) % cycleDays = dayInCycle
→ 如果 cycleMask 的 dayInCycle 位为 1，则今天执行

示例：
  每天执行：     cycleDays=1, cycleMask=0b1
  隔天执行：     cycleDays=2, cycleMask=0b1
  周一三五：     cycleDays=7, cycleMask=0b1010101 (bit0/2/4)
  浇 2 停 1：    cycleDays=3, cycleMask=0b011
  浇 1 停 3：    cycleDays=4, cycleMask=0b0001
```

**归属**：每个计划自带循环。同一个 Zone 的不同计划可以用不同的循环规则。

**每 Zone 最多 6 条计划。**

## 三、ScheduleSkip（跳过管理）

跳过最小单位：**单个计划的某一天（planId × ymd）**。不是 Zone 级别的一天。

```
ScheduleSkip
├── enum SkipReason : uint8_t {
│       SKIP_REASON_OTHER = 0,     // 默认/其他（扩展兜底，0 = 未初始化也合理）
│       SKIP_REASON_MANUAL = 1,    // 用户手动跳过（已经浇过了/其他个人原因）
│       SKIP_REASON_WEATHER = 2,   // 天气预报有雨
│       // 后续扩展从这里往后加：
│       // SKIP_REASON_SOIL_MOIST = 3,   // 土壤湿度足够
│       // SKIP_REASON_HOLIDAY = 4,      // 节假日
│       // SKIP_REASON_MAINTENANCE = 5,  // 设备维护
│   }
│
├── struct SkipEntry {
│       uint8_t  planId;     // 跳过的是哪个具体计划
│       uint32_t ymd;        // 哪一天
│       SkipReason reason;
│   }
│
├── entries[Capacity]       // 持久化，环形覆盖
│
├── isSkipped(planId, ymd) → bool
├── skip(planId, ymd, reason)
├── unskip(planId, ymd)
├── unskipZone(zoneId, ymd)           // 批量：取消某天该 Zone 所有计划的跳过
├── getEntries(zoneId, fromYmd, toYmd) → SkipEntry[]  // 日历视图数据
└── pruneBefore(ymd)                  // 清理过期条目
```

**容量**：保留最近 30 天过去 + 未来 30 天。超过自动清理。

**场景示例**：

```
Zone 1 有三个计划：
  计划 ID=1 "早浇"  07:00
  计划 ID=2 "午浇"  13:00
  计划 ID=3 "晚浇"  19:00

场景 1: 明天下雨，只跳过午浇
  → SkipEntry { planId=2, ymd=明天, WEATHER }
  结果：早浇正常，午浇跳过，晚浇正常

场景 2: 今天已经手动浇过了，跳过今天所有
  → SkipEntry { planId=1, ymd=今天, MANUAL }
  → SkipEntry { planId=2, ymd=今天, MANUAL }
  → SkipEntry { planId=3, ymd=今天, MANUAL }
```

## 四、Plan Execution Tracker（执行跟踪）

防止同一个计划在同一分钟重复触发。内存态，掉电即失。

```
PlanExecutionTracker（每 Zone 一个实例）
│
├── enum ExecutionStatus {
│       NOT_EVALUATED,
│       STARTED,              // 成功启动
│       SKIPPED_CALENDAR,     // ScheduleSkip 跳过
│       SKIPPED_DISABLED,     // Zone 停用
│       SKIPPED_BUSY,         // Zone 忙
│       SKIPPED_ERROR,        // Zone 异常
│       SKIPPED_LEAK,         // 系统漏水告警
│       SKIPPED_RESET,        // 恢复出厂待处理
│       SKIPPED_CYCLE,        // 循环规则不匹配
│       REJECTED              // Zone.start() 被拒绝
│   }
│
├── struct PlanDayState {
│       uint8_t planSlot;         // 0-5
│       bool handled;             // 今天是否已处理
│       ExecutionStatus status;   // 今天的结果
│   }
│
├── g_states[MaxPlansPerZone]   // 内存
│
├── isHandledToday(planSlot) → bool
├── markHandled(planSlot, status)
├── resetNewDay()               // 新的一天，全部重置
└── getAllToday() → PlanDayState[]
```

**与 ScheduleSkip 的区别**：

| | ScheduleSkip | PlanExecutionTracker |
|---|---|---|
| 是什么 | 用户主动设置的跳过 | 调度器自动记录的"今天已处理" |
| 存储 | 持久化（NVS） | 内存，掉电即失 |
| 粒度 | planId × ymd | planSlot × today |
| 生命周期 | 环形清理，保留 30 天 | 每天重置 |

## 五、ZoneScheduler（每路调度器）

```
ZoneScheduler
├── Zone* zone                          // 绑定的 Zone
├── plans[MaxPlansPerZone]              // 该 Zone 的计划定义
├── tracker: PlanExecutionTracker       // 执行跟踪（内存）
├── scheduleSkip: ScheduleSkip*         // 引用全局 ScheduleSkip 实例
│
├── begin(now)
│
├── tick(now)                           // 每分钟调用
│   ├── 1. 时间同步检查：未同步 → 返回
│   ├── 2. 新的一天？→ tracker.resetNewDay()
│   └── 3. 遍历该 Zone 的所有计划
│       ├── 计划启用？
│       ├── timeHour/timeMinute 匹配当前分钟？
│       ├── 循环规则匹配？→ 不匹配 → tracker.mark(SKIPPED_CYCLE)
│       ├── tracker.isHandledToday? → 跳过
│       ├── ScheduleSkip.isSkipped(planId, today)? → tracker.mark(SKIPPED_CALENDAR)
│       ├── Zone.isError()? → tracker.mark(SKIPPED_ERROR)
│       ├── Zone.isBusy()? → tracker.mark(SKIPPED_BUSY)
│       ├── 系统漏水告警? → tracker.mark(SKIPPED_LEAK)
│       ├── 恢复出厂待处理? → tracker.mark(SKIPPED_RESET)
│       └── 否则 → zone.start(PLAN, targetSec, planId, now)
│           ├── 成功 → tracker.mark(STARTED)
│           └── 失败 → tracker.mark(REJECTED)
│
├── skipPlan(planId, ymd, reason)       // 委托给 ScheduleSkip
├── unskipPlan(planId, ymd)
├── getTodayResults() → PlanDayState[]
├── getPlan(planSlot) → PlanDefinition
├── setPlan(planSlot, PlanDefinition)
├── count() → uint8_t
└── hasFreeSlot() → bool
```

**每个 ZoneScheduler 只调度自己的 Zone，不关心其他 Zone。**

## 六、ZoneManager（系统协调器）

```
ZoneManager
├── zones[MaxZones]: Zone[]
├── schedulers[MaxZones]: ZoneScheduler[]
├── scheduleSkip: ScheduleSkip
├── leakAlertActive: bool
├── factoryResetPending: bool
│
├── begin(now)
│   ├── for i in zones: zones[i].begin(config[i], now)
│   └── for i in schedulers: schedulers[i].begin(now)
│
├── tick(now)
│   ├── 每 10ms:  for i in zones: zones[i].tick(pulseCount, now)
│   ├── 每分钟:   for i in schedulers: schedulers[i].tick(now)
│   └── 漏水监控:  if allIdle(): checkLeakForAll()
│
├── startZone(zoneId, MANUAL, source, targetSec, now)
├── stopZone(zoneId, source, now)
├── stopAll(source, result, now)
├── clearZoneError(zoneId)
├── skipSchedule(planId, ymd, reason)
│   └── scheduleSkip.skip(planId, ymd, reason)
└── getZoneStatus() → ZoneStatus[]
```

## 七、Watering Record（浇水记录）

```
WateringRecord
├── recordId              // 自增 ID
├── zoneId                // 属于哪个 Zone
├── taskType              // MANUAL / PLAN
├── startSource           // web_page / http_api / button / scheduler
├── planId                // 计划 ID（手动任务为 0xFF）
├── targetSec             // 目标时长
├── startedMs             // 开始时间戳（系统时间）
├── endedMs               // 结束时间戳
├── stopSource            // 停止来源
├── stopScope             // 本路 / 全部
├── result                // COMPLETED / USER_STOPPED / FLOW_ERROR / LEAK_PROTECTED / FACTORY_RESET
├── startedPulseCount     // 启动时脉冲
├── endedPulseCount       // 结束时脉冲
├── estimatedMilliliters  // 估算水量
└── configSnapshot        // 启动时的配置快照
    ├── startupPulseLimit
    ├── startupEstimatedMl
    └── stablePulsePerLiter
```

一条记录 = 一个 Zone 的一次实际浇水任务。`planId` 字段让记录可反查触发来源。`configSnapshot` 保证历史水量计算不受后续配置修改影响。

## 八、完整结构总览

```
ZoneManager
├── Zone[MaxZones]
│   ├── config: ZoneConfig (id, name, pins, enabled, flow params, timeout params, suppressError)
│   ├── state: DISABLED | IDLE | STARTING | RUNNING | ERROR
│   ├── runner: ZoneTaskRunner (active, task, runtime, finished)
│   ├── lastErrorCode
│   └── leakAlert
│
├── ZoneScheduler[MaxZones]
│   ├── plans[6]: PlanDefinition (id, zoneId, name, time, duration, cycle, createdAt)
│   ├── tracker: PlanExecutionTracker (内存，今天是否已处理)
│   └── scheduleSkip → ScheduleSkip (引用)
│
├── ScheduleSkip (持久化，环形)
│   └── entries[]: { planId, ymd, reason }
│
├── leakAlertActive
└── factoryResetPending
```

## 九、数据流

```
用户创建/修改计划 → ZoneScheduler.plans[] 更新（持久化）

每分钟调度:
  ZoneScheduler.tick()
    ├── 遍历该 Zone 的所有计划
    ├── 判断：启用？时间匹配？循环匹配？已处理？已跳过？Zone 可用？
    ├── 满足条件 → Zone.start(PLAN, targetSec, planId)
    └── 记录结果到 PlanExecutionTracker

Zone.start():
    ├── 检查 Zone 状态（必须 IDLE）
    ├── 初始化 ZoneTaskRunner
    └── 开阀

Zone 执行中:
    ├── 每 tick 更新 lastPulseCount / lastPulseMs
    ├── 时长到 → finish(COMPLETED)
    ├── 无脉冲超时 → finish(FLOW_ERROR)
    └── 被停止 → finish(USER_STOPPED)

Zone.finish():
    ├── 关阀
    ├── 写 WateringRecord（含 planId、configSnapshot）
    ├── 按需写 Esp32BaseAppEventLog 业务事件
    └── Zone 状态回 IDLE（或 ERROR）

用户手动跳过:
    ZoneManager.skipSchedule(planId, ymd, MANUAL)
    → ScheduleSkip.skip(planId, ymd, MANUAL)

日历视图查询:
    ScheduleSkip.getEntries(zoneId, monthStart, monthEnd) → 跳过的日期
    RecordStore.read(zoneId, monthStart, monthEnd) → 实际浇水的日期
```
