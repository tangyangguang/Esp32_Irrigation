# Two Flow Six Zone Irrigation Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 重构 ESP32 灌溉业务为最多 2 个 Flow、最多 6 个 Zone 的新模型，Zone 可任意归属 Flow，同 Flow 互斥，不同 Flow 可并行，流量计使用固定点 K+Offset 校准。

**Architecture:** 删除旧的固定 4 路、每路一个流量计、启动阶段补偿模型。新业务核心分为 Flow 计量、Valve 输出、Zone 运行、Schedule 队列、Calibration 校准、Record/Event 记录、Web/API 七块；不增加 Supply/Pump 资源，不做旧配置/旧记录/API 兼容。

**Tech Stack:** ESP32 / PlatformIO / Arduino C++，基础能力使用 `/Users/tyg/dir/claude_dir/Esp32Base` 和 `ESP32BASE_PROFILE_FULL`，业务文件位于 `include/`、`src/domain/`、`src/storage/`、`src/web/`、`docs/`。

---

## Scope Rules

本计划只实现当前确认的新模型：

```text
MaxFlowMeters = 2
MaxZones = 6
Zone.flowMeterId = 1 or 2
同一 Flow 下 Zone 互斥
不同 Flow 下 Zone 可并行
Flow 参数 = K + Offset + 有效频率范围 + 建压时间 + 采样窗口
Zone 学习参数 = 正常流量 + 高低阈值 + 连续确认秒数 + 无脉冲超时
```

明确不做：

```text
不做 Supply/Pump 软件资源
不做旧 NVS key 迁移
不读取旧记录文件
不保留旧 API 字段兼容
不实现停水等待后自动恢复补浇
```

不同 Flow 可并行的前提写在安装说明和页面提示中：Flow 1、Flow 2 应接在两条不同供水管的不同计量点上。如果共用一个泵或供水能力不足，用户应把计划错开。

## File Map

核心文件调整如下：

```text
include/Pins.h
  定义 2 路流量计输入、6 路阀门输出、I2C、5 个按钮、状态灯和 PWM 参数。

src/domain/ZoneTypes.h
  定义 MaxFlowMeters、MaxZones、新 Flow/Zone 配置、待应用参数、回退参数、运行状态、结果枚举。

src/domain/FlowMeter.*
  重写为 Flow 1/2 脉冲计数和固定点 K+Offset 流量计算。

src/domain/ValveController.*
  扩展为 6 路 PWM 阀门输出，保持现有吸合/保持策略。

src/domain/ZoneManager.* / Zone.* / ZoneTaskRunner.*
  重写 Zone 状态机、Flow 占用、同 Flow 互斥、不同 Flow 并行。

src/domain/ZoneScheduler.* / PlanExecutionTracker.*
  改为按 Zone 触发，并实现同 Flow 冲突的内存排队。

src/domain/FlowCalibration.*
  删除旧启动补偿校准，重写 Flow K+Offset 校准和 Zone 正常流量学习。

src/storage/FlowConfigStore.* / ZoneConfigStore.* / SystemConfigStore.* / PlanStore.* / RecordStore.* / FaultStateStore.*
  使用新 schema 和新 namespace；不做旧格式迁移。

src/domain/BusinessEventLog.*
  继续封装 Esp32BaseAppEventLog，新增业务事件类型。

src/web/IrrigationWeb.*
  重写业务页面和 API 字段，保持 Esp32Base Web 输出、POST、confirm、escape 规则。

scripts/check-web-structure.mjs
  更新结构检查到 2 Flow / 6 Zone / K+Offset 新模型，删除旧启动补偿和固定 4 路断言。

README.md / PROJECT_PLAN.md / docs/*.md / docs/road-management/*.md
  同步入口文档；仍有价值的旧 road-management 文档必须改写为新模型，不能继续被结构检查脚本当成当前依据。
```

## Task 1: Constants And Pins

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/include/Pins.h`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/ZoneTypes.h`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/scripts/check-web-structure.mjs`

- [ ] **Step 1: Replace old hardware constants**

Set the board model to two flow inputs and six valve outputs. The first four valve pins and two flow pins keep the current board assignment:

```cpp
static constexpr uint8_t Valve1 = 16;
static constexpr uint8_t Valve2 = 14;
static constexpr uint8_t Valve3 = 13;
static constexpr uint8_t Valve4 = 27;

static constexpr uint8_t Flow1 = 32;
static constexpr uint8_t Flow2 = 35;

static constexpr uint8_t MaxFlowMeters = 2;
static constexpr uint8_t MaxZones = 6;
static constexpr uint8_t DefaultZoneEnabledMask = 0x03;
```

Before writing `Valve5` and `Valve6`, confirm the real board wiring. Do not assign provisional GPIO4/GPIO5, input-only pins, or boot-sensitive pins without a board-level decision. If the fifth and sixth valve pins are not confirmed, stop this task and ask for the hardware pin map instead of committing guessed firmware.

- [ ] **Step 2: Replace domain limits**

In `ZoneTypes.h`, replace `MaxZones = IrrigationPins::MaxRoads` with:

```cpp
static constexpr uint8_t MaxFlowMeters = IrrigationPins::MaxFlowMeters;
static constexpr uint8_t MaxZones = IrrigationPins::MaxZones;
static constexpr uint8_t MaxPlansPerZone = 6;
static constexpr uint8_t TotalPlanSlots = MaxZones * MaxPlansPerZone;
static constexpr uint8_t ScheduleQueueCapacity = 12;
```

- [ ] **Step 3: Update structural check**

Change `scripts/check-web-structure.mjs` assertions from fixed 4-road expectations to:

```js
assert(pins.includes('MaxFlowMeters = 2'), 'hardware model should expose two flow meters');
assert(pins.includes('MaxZones = 6'), 'hardware model should expose six zones');
assert(pins.includes('DefaultZoneEnabledMask = 0x03'), 'default hardware enable mask should enable zone 1 and 2');
```

- [ ] **Step 4: Verify**

Run:

```bash
node scripts/check-web-structure.mjs
pio run
```

Expected after this task: no commit until the firmware compiles. If the only blocker is unconfirmed `Valve5`/`Valve6` GPIO, stop and ask for the pin map.

- [ ] **Step 5: Commit**

```bash
git add include/Pins.h src/domain/ZoneTypes.h scripts/check-web-structure.mjs
git commit -m "refactor: define two-flow six-zone hardware model"
```

## Task 2: New Configuration Types And Storage

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/ZoneTypes.h`
- Add: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/FlowConfigStore.h`
- Add: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/FlowConfigStore.cpp`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/ZoneConfigStore.h`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/ZoneConfigStore.cpp`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/SystemConfigStore.h`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/SystemConfigStore.cpp`

- [ ] **Step 1: Replace old flow parameter structs**

Delete `FlowParameters` fields:

```cpp
startupPulseLimit
startupEstimatedMl
stablePulsePerLiter
```

Add:

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

struct FlowMeterConfig {
    uint8_t id;
    uint8_t pulsePin;
    bool enabled;
    FlowMeterCalibrationProfile activeCalibration;
    bool hasPendingCalibration;
    FlowMeterCalibrationProfile pendingCalibration;
    bool hasRollbackCalibration;
    FlowMeterCalibrationProfile rollbackCalibration;
};

struct ZoneConfig {
    uint8_t zoneId;
    char name[NameMaxBytes];
    uint8_t valvePin;
    uint8_t flowMeterId;
    bool enabled;
    bool hasLearnedBaseline;
    ZoneFlowBaselineProfile activeBaseline;
    bool hasPendingBaseline;
    ZoneFlowBaselineProfile pendingBaseline;
    bool hasRollbackBaseline;
    ZoneFlowBaselineProfile rollbackBaseline;
};
```

- [ ] **Step 2: Set defaults**

Defaults:

```text
Flow 1: enabled, pulsePin=Flow1, activeCalibration has defaults
Flow 2: disabled, pulsePin=Flow2, activeCalibration has defaults
Flow hasPendingCalibration=false, hasRollbackCalibration=false
Flow calibration defaults: k=244897, offset=0, warningFreq=4000, minValidFreq=500, maxValidFreq=0, pressurizeSec=5, sampleWindowSec=2
Zone 1..6: flowMeterId=1
Zone 1/2: enabled
Zone 3..6: disabled
Zone baseline: hasLearnedBaseline=false, hasPendingBaseline=false, hasRollbackBaseline=false, low=100, high=3000, flowFaultConfirmSec=15, lowFlowAction=STOP_ZONE, highFlowAction=STOP_ZONE, noPulseTimeoutSec=10
```

- [ ] **Step 3: Use new namespaces**

Use:

```text
FlowConfigStore namespace: irr_flow_v1
ZoneConfigStore namespace: irr_zone_v1
```

Do not read old namespaces. If schema is missing or invalid, write defaults and log a schema reset event.

`FlowConfigStore` owns only `FlowMeterConfig[2]` and Flow calibration pending/rollback state. `ZoneConfigStore` owns only `ZoneConfig[6]` and Zone baseline pending/rollback state. Do not store Flow K+Offset parameters inside `ZoneConfigStore`; otherwise arbitrary `Zone -> Flow` assignment will duplicate and desynchronize calibration parameters.

- [ ] **Step 4: Add queue config**

Add to `SystemConfig`:

```cpp
uint16_t scheduleGraceSec;
uint16_t queuedPlanMaxDelaySec;
uint16_t idleLeakWindowSec;
uint16_t idleLeakPulseThreshold;
uint32_t maxWateringDurationSec;
uint32_t manualDefaultDurationSec;
```

Default `queuedPlanMaxDelaySec = 3600`.
Default `idleLeakWindowSec = 15`.
Default `idleLeakPulseThreshold = 5`.

After adding `queuedPlanMaxDelaySec`, update `platformio.ini` `ESP32BASE_APP_CONFIG_MAX_FIELDS` if the App Config registration count exceeds the old value. The current old value is `19`, which only covers the pre-redesign field set.

- [ ] **Step 5: Verify**

Run:

```bash
node scripts/check-web-structure.mjs
pio run
```

Expected: `check-web-structure` includes this assertion after the config rewrite:

```js
assert(zoneTypes.includes('kUlPerMinPerHz') &&
       zoneTypes.includes('offsetMilliHz') &&
       zoneTypes.includes('warningFreqMilliHz') &&
       zoneTypes.includes('minValidFreqMilliHz') &&
       zoneTypes.includes('maxValidFreqMilliHz'),
       'flow config should use fixed-point K+Offset with valid frequency bounds');
assert(!zoneTypes.includes('startupPulseLimit') &&
       !zoneTypes.includes('startupEstimatedMl') &&
       !zoneTypes.includes('stablePulsePerLiter'),
       'old two-stage startup pulse calibration fields must be removed from config types');
```

No compile errors from deleted config fields are allowed. If a deleted field is still referenced, fix the reference in the same task before committing.

- [ ] **Step 6: Commit**

```bash
git add src/domain/ZoneTypes.h src/storage/FlowConfigStore.* src/storage/ZoneConfigStore.* src/storage/SystemConfigStore.* platformio.ini
git commit -m "refactor: replace irrigation config schema"
```

## Task 3: Flow Meter K+Offset Service

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/FlowMeter.h`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/FlowMeter.cpp`

- [ ] **Step 1: Replace road APIs with flow APIs**

Expose APIs shaped around Flow IDs:

```cpp
void begin();
void handle();
void configureFlow(uint8_t flowId, const Irrigation::FlowMeterCalibrationProfile& params);
uint32_t pulseCount(uint8_t flowId);
uint32_t flowMillilitersPerMinute(uint8_t flowId);
bool flowRateReady(uint8_t flowId);
bool sampleInvalid(uint8_t flowId);
uint32_t consumeVolumeMicroliters(uint8_t flowId);
```

- [ ] **Step 2: Implement fixed-point calculation**

Use this exact runtime rule:

```cpp
if (pulseDelta == 0) {
    flowUlPerMin = 0;
} else if (freqMilliHz < params.minValidFreqMilliHz) {
    belowMeteringRange = true;
    flowUlPerMin = 0;
} else if (params.maxValidFreqMilliHz > 0 && freqMilliHz > params.maxValidFreqMilliHz) {
    sampleInvalid = true;
    flowUlPerMin = 0;
} else {
    const int64_t effectiveMilliHz = static_cast<int64_t>(freqMilliHz) + params.offsetMilliHz;
    const int64_t raw = static_cast<int64_t>(params.kUlPerMinPerHz) * effectiveMilliHz / 1000;
    flowUlPerMin = raw > 0 ? static_cast<uint32_t>(raw) : 0;
}
volumeUl += static_cast<uint64_t>(flowUlPerMin) * elapsedMs / 60000ULL;
```

No-water protection must use raw pulse presence, not `flowUlPerMin`. If `pulseDelta > 0` but frequency is below `minValidFreqMilliHz`, mark the sample as below metering range and show unreliable volume, but do not treat it as no water.

- [ ] **Step 3: Keep pulse capture for calibration**

Keep capture start/stop ability, but rename `road` parameters to `flowId` and reject IDs outside `1..2`.

- [ ] **Step 4: Verify**

Run:

```bash
pio run
```

Expected: FlowMeter compiles and no remaining references to `setStablePulsePerLiter`.

- [ ] **Step 5: Commit**

```bash
git add src/domain/FlowMeter.*
git commit -m "refactor: calculate flow with fixed-point K offset"
```

## Task 4: Valve And Zone Runtime

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/ValveController.*`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/Zone.*`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/ZoneManager.*`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/ZoneTaskRunner.*`

- [ ] **Step 1: Expand valves to six outputs**

Valve IDs are `1..6`. Keep pull-in and hold PWM:

```text
pull-in: 100% for ValvePullInMs
hold: ValveHoldDutyPercent
stop: 0%
```

- [ ] **Step 2: Add Flow occupancy**

`ZoneManager` keeps:

```cpp
uint8_t activeZoneByFlow[MaxFlowMeters + 1];
```

Start rule:

```text
Zone enabled
Flow enabled
activeZoneByFlow[flowMeterId] == 0
duration valid
no leak/reset protection
```

- [ ] **Step 3: Allow cross-Flow parallel**

If `Zone 1 -> Flow 1` and `Zone 2 -> Flow 2`, both may run. Do not add a global pump lock.

- [ ] **Step 4: Stop on no pulse**

After `pressurizeSec`, if no pulse exceeds `noPulseTimeoutSec`, close only the current Zone, clear Flow occupancy, write result `FLOW_NO_PULSE_TIMEOUT`.

Do not keep the old separate `FLOW_START_TIMEOUT` state. Startup and running no-pulse protection are both represented by `FLOW_NO_PULSE_TIMEOUT`; the record timestamps explain whether it happened immediately after pressurize or later during watering.

- [ ] **Step 5: Verify**

Run:

```bash
pio run
```

Bench tests after firmware flash:

```text
Zone 1 and Zone 3 both assigned Flow 1: second start rejected.
Zone 1 assigned Flow 1 and Zone 2 assigned Flow 2: both can run.
Stop all closes all six valve outputs.
```

- [ ] **Step 6: Commit**

```bash
git add src/domain/ValveController.* src/domain/Zone.* src/domain/ZoneManager.* src/domain/ZoneTaskRunner.*
git commit -m "refactor: run zones by flow occupancy"
```

## Task 5: Schedule Queue

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/ZoneScheduler.*`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/PlanExecutionTracker.*`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/PlanStore.*`

- [ ] **Step 1: Queue only plan starts**

Manual starts never enter the queue. Plan starts create:

```cpp
struct QueuedPlanTask {
    bool used;
    uint32_t scheduledEpoch;
    uint32_t queuedEpoch;
    uint32_t planId;
    uint8_t zoneId;
    uint8_t planSlot;
    uint32_t durationSec;
};
```

- [ ] **Step 2: Sort by scheduled time and zone**

Start the earliest queued task whose Flow is currently free. If times are equal, lower `zoneId` wins.

- [ ] **Step 3: Expire stale queued tasks**

If `now - queuedEpoch > queuedPlanMaxDelaySec`, drop the task and log `queue_expired`.

- [ ] **Step 4: Verify**

Run:

```bash
pio run
```

Bench scenario:

```text
08:00 Zone1/2/3 all Flow1 -> Zone1, then Zone2, then Zone3.
08:00 Zone1 Flow1 and Zone2 Flow2 -> start together.
Queue capacity exceeded -> queue_full event.
```

- [ ] **Step 5: Commit**

```bash
git add src/domain/ZoneScheduler.* src/domain/PlanExecutionTracker.* src/storage/PlanStore.*
git commit -m "refactor: queue planned watering by flow"
```

## Task 6: Calibration And Learning

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/FlowCalibration.*`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/ZoneConfigStore.*`

- [ ] **Step 1: Delete old startup compensation logic**

Remove all use of:

```text
startupPulseLimit
startupEstimatedMl
stablePulsePerLiter
```

- [ ] **Step 2: Implement manual pending calibration save**

Manual Flow edit validates and saves pending calibration:

```text
kUlPerMinPerHz 1..2000000
offsetMilliHz -50000..50000
minValidFreqMilliHz 0..60000
warningFreqMilliHz 0..60000
maxValidFreqMilliHz 0 or 1000..200000
pressurizeSec bounded by max watering duration
sampleWindowSec >= 1
```

- [ ] **Step 3: Implement single-point Flow calibration**

Single-point calibration must choose both a target Flow and one enabled Zone that belongs to that Flow. The sample opens only that Zone, because a Flow by itself cannot produce water.

Formula:

```text
Q_LPM = actualMl * 60 / durationMs
f_Hz = pulses * 1000 / durationMs
K = Q_LPM / f_Hz
Offset = 0
```

Persist as fixed point:

```text
kUlPerMinPerHz = K * 1000000
offsetMilliHz = 0
```

- [ ] **Step 4: Implement multi-point Flow calibration**

Fit:

```text
Q = a*f + b
K = a
Offset = b / a
```

Reject apply when `a <= 0`. Warn but allow save when sample frequency span is narrow. Save the fitted `minValidFreqMilliHz` as the lower reliable sample frequency unless the user manually overrides it.

- [ ] **Step 5: Implement Zone learning**

Zone learning opens exactly one Zone, skips `pressurizeSec`, samples stable flow, then saves pending baseline:

```text
learnedFlowMlPerMin = average stable flow
lowFlowPermille = 100
highFlowPermille = 3000
flowFaultConfirmSec = existing active baseline or default 15
lowFlowAction = existing active baseline or default STOP_ZONE
highFlowAction = existing active baseline or default STOP_ZONE
noPulseTimeoutSec = existing active baseline or default 10
```

- [ ] **Step 6: Verify**

Run:

```bash
pio run
```

Manual calculation check with user data:

```text
7.0 L, 204 s, 1688 pulses -> around 241 pulses/L when Offset=0
K=60/241 ~= 0.24896 L/min/Hz -> kUlPerMinPerHz around 248960
```

- [ ] **Step 7: Commit**

```bash
git add src/domain/FlowCalibration.* src/storage/ZoneConfigStore.*
git commit -m "refactor: calibrate flow meters with K offset"
```

## Task 7: Records And Events

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/RecordStore.*`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/domain/BusinessEventLog.*`
- Replace: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/ZoneErrorStore.*`
- Add: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/FaultStateStore.h`
- Add: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/storage/FaultStateStore.cpp`

- [ ] **Step 1: Replace record schema**

Use new record path:

```text
/irr/records_v1.bin
```

Every watering record snapshots:

```text
zoneId
flowMeterId
kUlPerMinPerHz
offsetMilliHz
warningFreqMilliHz
minValidFreqMilliHz
maxValidFreqMilliHz
pressurizeSec
sampleWindowSec
learnedFlowMlPerMin
lowFlowPermille
highFlowPermille
flowFaultConfirmSec
lowFlowAction
highFlowAction
noPulseTimeoutSec
targetSec
actualSec
startPulse
endPulse
estimatedMl
avg/min/max flow
lastPulseAtSec
firstNoPulseAtSec
faultConfirmedAtSec
pulsesBeforeFault
estimatedMlBeforeFault
lowFlowObserved
highFlowObserved
belowMeteringRangeObserved
sampleInvalidObserved
result
stopSource
```

Result/error taxonomy:

```text
TaskResult:
  COMPLETED
  USER_STOPPED
  FLOW_NO_PULSE_TIMEOUT
  FLOW_LOW_STOPPED
  FLOW_HIGH_STOPPED
  LEAK_PROTECTED
  FACTORY_RESET_PROTECTED
  CONFIG_INVALID
  REJECTED

ZoneErrorCode:
  FLOW_NO_PULSE_TIMEOUT
  FLOW_LOW
  FLOW_HIGH
  CONFIG_INVALID

FlowFaultCode:
  IDLE_LEAK
```

If low/high flow action is `RECORD_ONLY`, do not set a Zone error and do not change the final task result to low/high stopped; only set the observation flag and write an event. If action is `STOP_ZONE`, close the Zone and use `FLOW_LOW_STOPPED` or `FLOW_HIGH_STOPPED`.

- [ ] **Step 1b: Replace ZoneErrorStore with FaultStateStore**

`FaultStateStore` stores both Zone-level errors and Flow-level idle leak protection:

```cpp
struct ZoneFault {
    bool active;
    Irrigation::ZoneErrorCode code;
    uint32_t occurredEpoch;
    uint32_t occurredUptimeMs;
    Irrigation::StopSource source;
    Irrigation::TaskResult result;
};

struct FlowLeakFault {
    bool active;
    uint8_t flowMeterId;
    uint32_t occurredEpoch;
    uint32_t occurredUptimeMs;
    uint16_t windowSec;
    uint16_t pulseCount;
};
```

Flow-level idle leak must not be stored as a fake Zone error. Clearing leak protection clears `FlowLeakFault[2]`; clearing a Zone error clears only that Zone.

- [ ] **Step 2: Use Esp32BaseAppEventLog only**

Add event labels:

```text
flow_mutex_rejected
schedule_queued
schedule_queue_started
schedule_queue_full
schedule_queue_expired
flow_no_pulse_stop
flow_low_fault
flow_high_fault
idle_leak_detected
flow_pending_calibration_saved
flow_params_applied
flow_calibration_rolled_back
zone_pending_baseline_saved
zone_learning_applied
zone_baseline_rolled_back
```

- [ ] **Step 3: Verify**

Run:

```bash
pio run
```

- [ ] **Step 4: Commit**

```bash
git add src/storage/RecordStore.* src/domain/BusinessEventLog.* src/storage/FaultStateStore.* src/storage/ZoneErrorStore.*
git commit -m "refactor: record irrigation flow model events"
```

## Task 8: Web And API

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/src/web/IrrigationWeb.*`

- [ ] **Step 1: Replace pages**

Pages:

```text
/irrigation
/irrigation/flows
/irrigation/zones
/irrigation/calibration
/irrigation/records
/irrigation/events
```

- [ ] **Step 2: Replace API payloads**

Required API groups:

```text
GET  /api/v1/status
GET  /api/v1/flow/history?flowId=1
POST /api/v1/zone/start
POST /api/v1/zone/stop
POST /api/v1/zones/stop-all
POST /api/v1/flow/save-pending-calibration
POST /api/v1/flow/apply-pending-calibration
POST /api/v1/flow/rollback-calibration
POST /api/v1/calibration/flow/start
POST /api/v1/calibration/flow/stop
POST /api/v1/calibration/flow/save-sample
POST /api/v1/calibration/flow/clear-samples
POST /api/v1/calibration/flow/fit
POST /api/v1/zone-learning/start
POST /api/v1/zone-learning/stop
POST /api/v1/zone-learning/save-pending-baseline
POST /api/v1/zone-learning/apply-pending-baseline
POST /api/v1/zone-learning/rollback-baseline
```

All changing operations use POST, auth, and JavaScript confirm on pages.

- [ ] **Step 3: Add install warning text**

On Flow settings, show the practical rule:

```text
Flow 1 和 Flow 2 应接在不同供水管的不同计量点上。若共用同一泵或水塔供水能力有限，请把计划错开运行。
```

Do not add a software pump lock.

- [ ] **Step 4: Verify**

Run:

```bash
node scripts/check-web-structure.mjs
pio run
```

- [ ] **Step 5: Commit**

```bash
git add src/web/IrrigationWeb.* scripts/check-web-structure.mjs
git commit -m "refactor: rebuild irrigation web for flow zone model"
```

## Task 9: Entry Docs And Final Validation

**Files:**
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/README.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/PROJECT_PLAN.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/01_requirements_v1.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/02_next_implementation_plan.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/03_web_validation_checklist.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/04_event_fields.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/road-management/README.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/road-management/02-zone-unit-design.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/road-management/03-plan-management.md`
- Modify: `/Users/tyg/dir/claude_dir/Esp32_Irrigation/docs/road-management/04-flow-calibration.md`

- [ ] **Step 1: Replace old product wording**

Replace fixed 4-road wording with:

```text
最多 2 个流量计、最多 6 路电磁阀；每路 Zone 可归属 Flow 1 或 Flow 2；同 Flow 互斥，不同 Flow 可并行。
```

- [ ] **Step 2: Update validation checklist**

Checklist must include:

```text
2 Flow pulse input
6 Valve PWM output
same Flow mutex
cross Flow parallel
queue order and expiry
K+Offset manual/single/multi-point
Zone learning
no-pulse stop
below metering range does not trigger no-water stop
low/high flow default action stops zone
low/high flow can be configured record-only
idle leak detection
install warning for shared pump
```

- [ ] **Step 3: Final software verification**

Run:

```bash
node scripts/check-web-structure.mjs
pio run
git status --short
```

Expected:

```text
check-web-structure passed
pio run success
only intended docs/code files changed
```

- [ ] **Step 4: Commit**

```bash
git add README.md PROJECT_PLAN.md docs/ scripts/check-web-structure.mjs
git commit -m "docs: document two-flow six-zone irrigation model"
```

## Final Hardware Validation

软件完成后必须单独标注实机验证结果：

```text
1. 6 路阀门吸合和 PWM 保持可靠，驱动温升正常。
2. Flow 1/2 输入在真实线缆和上拉下计数稳定。
3. 用户实际喷头组合下，K+Offset 校准误差可接受。
4. 只有 1 个 Flow 接入时，所有 Zone 归属 Flow 1 并顺序运行。
5. 2 个 Flow 分别接入不同供水管时，不同 Flow 并行运行水压可接受。
6. 共用同一泵时，计划错开后无低流量误报。
7. 缺水、停泵、拔掉流量计信号时能安全关阀。
8. 72 小时运行无异常重启、记录损坏或阀门误动作。
```
