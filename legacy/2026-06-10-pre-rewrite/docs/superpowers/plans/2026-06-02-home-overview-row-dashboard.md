# Home Overview Row Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the home page table with a row-based operational dashboard showing global weather, per-zone runtime state, flow charts with axes, and today's per-zone plan progress.

**Architecture:** Keep the change inside the irrigation application layer. Add a small weather snapshot store and publish API, derive today's plan display from `PlanStore`, read-only `PlanExecutionTracker` lookups, completed watering records in `RecordStore`, and live `ZoneStatus`, then render the home page with server-generated HTML plus the existing `/api/v1/status` and `/api/v1/flow/history` polling. The home page must never reset or persist plan execution tracker state while rendering; scheduler-owned state transitions stay in the scheduler.

**Tech Stack:** ESP32 Arduino, PlatformIO, Esp32Base Web/AppConfig, C++17-style Arduino code, server-side HTML/SVG streaming via `Esp32BaseWeb`.

---

## File Structure

- Modify `src/web/IrrigationWeb.cpp`: replace the home page table with row cards, add record-backed plan summary rendering, add weather strip rendering, improve flow chart SVG/JS, add weather publish route.
- Modify `src/app/IrrigationApp.cpp`: initialize the weather snapshot store at startup.
- Create `src/storage/WeatherSnapshotStore.h`: public weather snapshot storage API.
- Create `src/storage/WeatherSnapshotStore.cpp`: NVS-backed weather snapshot persistence and validation.
- Modify `platformio.ini`: increase `ESP32BASE_WEB_MAX_ROUTES` by one for the weather publish route.
- Modify `scripts/check-web-structure.mjs`: keep the static web structure checks aligned with the increased route capacity and new overview structure.

## Task 1: Add Weather Snapshot Store

**Files:**
- Create: `src/storage/WeatherSnapshotStore.h`
- Create: `src/storage/WeatherSnapshotStore.cpp`

- [ ] **Step 1: Add public API header**

Create `src/storage/WeatherSnapshotStore.h`:

```cpp
#pragma once

#include <stdint.h>

namespace WeatherSnapshotStore {

static constexpr uint8_t ConditionMaxBytes = 24;
static constexpr uint8_t DayLabelMaxBytes = 12;

struct ForecastDay {
    char label[DayLabelMaxBytes];
    int16_t lowTempC;
    int16_t highTempC;
    uint8_t rainProbabilityPercent;
};

struct Snapshot {
    bool exists;
    uint32_t updatedEpoch;
    int16_t currentTempC;
    char condition[ConditionMaxBytes];
    uint8_t rainProbability24hPercent;
    uint8_t windLevel;
    ForecastDay days[3];
};

void begin();
bool get(Snapshot* out);
bool set(const Snapshot& snapshot);
bool clear();
bool validate(const Snapshot& snapshot);
bool isStale(const Snapshot& snapshot, uint32_t nowEpoch);

}
```

- [ ] **Step 2: Implement storage**

Create `src/storage/WeatherSnapshotStore.cpp`:

```cpp
#include "storage/WeatherSnapshotStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

namespace {

static constexpr const char* kNamespace = "irr_weather";
static constexpr const char* kKeyBlob = "snapshot";
static constexpr uint32_t kMagic = 0x49575448UL;
static constexpr uint16_t kVersion = 1;
static constexpr uint32_t kFreshWindowSec = 6UL * 60UL * 60UL;

struct StoredSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    WeatherSnapshotStore::Snapshot data;
};

WeatherSnapshotStore::Snapshot g_snapshot = {};

bool validUtf8NoControl(const char* text, size_t maxLen, bool allowEmpty) {
    if (!text) return false;
    const size_t len = strnlen(text, maxLen);
    if (len >= maxLen) return false;
    if (!allowEmpty && len == 0) return false;
    size_t i = 0;
    while (i < len) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7F) return false;
        if (c < 0x80) {
            ++i;
            continue;
        }
        uint8_t needed = 0;
        uint32_t codepoint = 0;
        if ((c & 0xE0) == 0xC0) {
            needed = 1;
            codepoint = c & 0x1F;
            if (codepoint == 0) return false;
        } else if ((c & 0xF0) == 0xE0) {
            needed = 2;
            codepoint = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            needed = 3;
            codepoint = c & 0x07;
        } else {
            return false;
        }
        if (i + needed >= len) return false;
        for (uint8_t j = 1; j <= needed; ++j) {
            const unsigned char cc = static_cast<unsigned char>(text[i + j]);
            if ((cc & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (cc & 0x3F);
        }
        if ((needed == 1 && codepoint < 0x80) ||
            (needed == 2 && codepoint < 0x800) ||
            (needed == 3 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
        i += static_cast<size_t>(needed) + 1U;
    }
    return true;
}

StoredSnapshot wrap(const WeatherSnapshotStore::Snapshot& snapshot) {
    StoredSnapshot stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.data = snapshot;
    return stored;
}

bool validStored(const StoredSnapshot& stored) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored) &&
           WeatherSnapshotStore::validate(stored.data);
}

}

namespace WeatherSnapshotStore {

void begin() {
    g_snapshot = {};
    StoredSnapshot stored = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKeyBlob, stored) && validStored(stored)) {
        g_snapshot = stored.data;
    }
}

bool get(Snapshot* out) {
    if (!out) return false;
    *out = g_snapshot;
    return g_snapshot.exists;
}

bool set(const Snapshot& snapshot) {
    if (!validate(snapshot)) return false;
    g_snapshot = snapshot;
    g_snapshot.exists = true;
    return Esp32BaseConfig::setPod(kNamespace, kKeyBlob, wrap(g_snapshot));
}

bool clear() {
    g_snapshot = {};
    return Esp32BaseConfig::clearNamespace(kNamespace);
}

bool validate(const Snapshot& snapshot) {
    if (!snapshot.exists) return true;
    if (!validUtf8NoControl(snapshot.condition, ConditionMaxBytes, false)) return false;
    if (snapshot.currentTempC < -50 || snapshot.currentTempC > 80) return false;
    if (snapshot.rainProbability24hPercent > 100) return false;
    if (snapshot.windLevel > 17) return false;
    for (uint8_t i = 0; i < 3; ++i) {
        const ForecastDay& day = snapshot.days[i];
        if (!validUtf8NoControl(day.label, DayLabelMaxBytes, false)) return false;
        if (day.lowTempC < -50 || day.lowTempC > 80) return false;
        if (day.highTempC < -50 || day.highTempC > 80) return false;
        if (day.lowTempC > day.highTempC) return false;
        if (day.rainProbabilityPercent > 100) return false;
    }
    return true;
}

bool isStale(const Snapshot& snapshot, uint32_t nowEpoch) {
    if (!snapshot.exists || snapshot.updatedEpoch == 0 || nowEpoch == 0) return false;
    return nowEpoch > snapshot.updatedEpoch && (nowEpoch - snapshot.updatedEpoch) > kFreshWindowSec;
}

}
```

- [ ] **Step 3: Build after adding the store**

Run:

```bash
pio run
```

Expected: build succeeds or fails only on route/includes that are not yet wired. If it fails on the new store file, fix includes and type names before continuing.

- [ ] **Step 4: Commit**

```bash
git add src/storage/WeatherSnapshotStore.h src/storage/WeatherSnapshotStore.cpp
git commit -m "Add weather snapshot store"
```

## Task 2: Add Weather Initialization And Publish API

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`
- Modify: `scripts/check-web-structure.mjs`
- Modify: `src/app/IrrigationApp.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Initialize the store**

In `src/app/IrrigationApp.cpp`, add:

```cpp
#include "storage/WeatherSnapshotStore.h"
```

In the application startup sequence near other store `begin()` calls, add:

```cpp
WeatherSnapshotStore::begin();
```

- [ ] **Step 2: Include the store in web code**

In `src/web/IrrigationWeb.cpp`, add with the other storage includes:

```cpp
#include "storage/WeatherSnapshotStore.h"
```

- [ ] **Step 3: Add read helpers**

Near the existing `readU8` and `readU16` helpers, add:

```cpp
bool readI16(const char* name, int16_t* value) {
    char text[16] = "";
    if (!value || !Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    char* end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed < -32768L || parsed > 32767L) {
        return false;
    }
    *value = static_cast<int16_t>(parsed);
    return true;
}

bool readTextParam(const char* name, char* out, size_t outLen) {
    if (!out || outLen == 0 || !Esp32BaseWeb::getParam(name, out, outLen)) {
        return false;
    }
    return out[0] != '\0';
}
```

- [ ] **Step 4: Add weather publish handler**

Add before the plan API handlers:

```cpp
void handleWeatherSnapshotApi() {
    if (!checkBusinessPost("irrigation.weather.snapshot")) {
        return;
    }

    WeatherSnapshotStore::Snapshot snapshot = {};
    snapshot.exists = true;

    if (!readU32("updatedEpoch", &snapshot.updatedEpoch) ||
        !readI16("currentTempC", &snapshot.currentTempC) ||
        !readTextParam("condition", snapshot.condition, sizeof(snapshot.condition)) ||
        !readU8("rainProbability24hPercent", &snapshot.rainProbability24hPercent) ||
        !readU8("windLevel", &snapshot.windLevel)) {
        sendError(400, "invalid_weather_snapshot");
        return;
    }

    static constexpr const char* kLabels[] = {"today", "tomorrow", "dayAfter"};
    for (uint8_t i = 0; i < 3; ++i) {
        char name[40];
        snprintf(name, sizeof(name), "%sLabel", kLabels[i]);
        if (!readTextParam(name, snapshot.days[i].label, sizeof(snapshot.days[i].label))) {
            sendError(400, "invalid_weather_day");
            return;
        }
        snprintf(name, sizeof(name), "%sLowTempC", kLabels[i]);
        if (!readI16(name, &snapshot.days[i].lowTempC)) {
            sendError(400, "invalid_weather_day");
            return;
        }
        snprintf(name, sizeof(name), "%sHighTempC", kLabels[i]);
        if (!readI16(name, &snapshot.days[i].highTempC)) {
            sendError(400, "invalid_weather_day");
            return;
        }
        snprintf(name, sizeof(name), "%sRainProbabilityPercent", kLabels[i]);
        if (!readU8(name, &snapshot.days[i].rainProbabilityPercent)) {
            sendError(400, "invalid_weather_day");
            return;
        }
    }

    if (!WeatherSnapshotStore::set(snapshot)) {
        sendError(400, "invalid_weather_snapshot");
        return;
    }
    sendOk();
}
```

- [ ] **Step 5: Register the route**

In `IrrigationWeb::begin()`, add the route near the other API route registrations:

```cpp
const bool weatherOk = Esp32BaseWeb::addRoute("/api/v1/weather/snapshot", Esp32BaseWeb::METHOD_POST, handleWeatherSnapshotApi);
```

Then include `weatherOk` in both route-health paths:

```cpp
const bool routeResults[] = {
    overviewOk, plansOk, recordsPageOk, eventsPageOk, calibrationPageOk, settingsOk,
    calibrationSamplePageOk, eventDetailPageOk, planEditOk, statusOk, flowHistoryOk, calibrationStatusOk, configOk,
    zoneStartOk, zoneStopOk, allStopOk, zoneConfigOk, clearErrorOk,
    calibrationStartOk, calibrationStopOk, calibrationSampleOk, calibrationSampleUpdateOk,
    calibrationComputeOk, calibrationCandidateSaveOk, calibrationApplyOk, calibrationRestoreOk,
    calibrationClearOk, plansApiOk, planCreateOk, planUpdateOk, planDeleteOk,
    planEnableOk, planDisableOk, skipOk, unskipOk, recordsOk, eventsOk, weatherOk,
};
```

Also add `weather=%s` to the `ESP32BASE_LOG_I("irrigation.web", ...)` format string and pass `weatherOk ? "ok" : "fail"` before `IrrigationVersion::FirmwareVersion`.

- [ ] **Step 6: Increase route limit**

In `platformio.ini`, change:

```ini
-D ESP32BASE_WEB_MAX_ROUTES=42
```

to:

```ini
-D ESP32BASE_WEB_MAX_ROUTES=43
```

Also update `scripts/check-web-structure.mjs` so the route-capacity assertion expects 43:

```js
assert(pio.includes('-D ESP32BASE_WEB_MAX_ROUTES=43'), 'web route capacity should include weather snapshot API');
```

- [ ] **Step 7: Build**

Run:

```bash
pio run
```

Expected: build succeeds.

Run:

```bash
node scripts/check-web-structure.mjs
```

Expected: structure checks pass with route capacity 43.

- [ ] **Step 8: Commit**

```bash
git add src/web/IrrigationWeb.cpp src/app/IrrigationApp.cpp platformio.ini scripts/check-web-structure.mjs
git commit -m "Add weather snapshot publish API"
```

## Task 3: Add Today Plan Display Helpers

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`

- [ ] **Step 1: Add plan display types**

Near the existing `countPlansForDate()` helper, add:

```cpp
struct TodayPlanDisplay {
    bool exists;
    Irrigation::PlanDefinition plan;
    Irrigation::PlanObservationStatus observation;
    bool running;
    bool completed;
    uint32_t completedSec;
    Irrigation::TaskResult lastResult;
};

struct TodayPlanSummary {
    uint8_t total;
    uint8_t completed;
    uint32_t totalSec;
    uint32_t completedSec;
};

struct TodayPlanCompletion {
    uint32_t planId;
    uint32_t completedSec;
    Irrigation::TaskResult result;
};
```

- [ ] **Step 2: Add include dependencies**

Add with the other includes:

```cpp
#include "domain/PlanExecutionTracker.h"
#include "storage/RecordStore.h"
```

- [ ] **Step 3: Add date and duration helpers**

Add near the date/time helpers:

```cpp
uint16_t planMinuteOfDay(const Irrigation::PlanDefinition& plan) {
    return static_cast<uint16_t>(static_cast<uint16_t>(plan.timeHour) * 60U + plan.timeMinute);
}

uint32_t ymdFromEpoch(uint32_t epoch) {
    if (epoch == 0) {
        return 0;
    }
    const time_t value = static_cast<time_t>(epoch);
    tm local = {};
    if (localtime_r(&value, &local) == nullptr) {
        return 0;
    }
    return static_cast<uint32_t>((local.tm_year + 1900) * 10000UL +
                                 (local.tm_mon + 1) * 100UL +
                                 local.tm_mday);
}

uint32_t recordRuntimeSec(const RecordStore::WateringRecord& record) {
    if (record.endedUptimeMs > record.startedUptimeMs) {
        return (record.endedUptimeMs - record.startedUptimeMs + 999UL) / 1000UL;
    }
    if (record.endedEpoch > record.startedEpoch) {
        return record.endedEpoch - record.startedEpoch;
    }
    return 0;
}
```

- [ ] **Step 4: Add observation lookup**

Because the web layer does not own each zone's scheduler instance, use the persisted tracker directly by constructing a temporary tracker. This lookup must stay read-only. Do not call `resetNewDay()` or any method that persists NVS state from the home page render path.

```cpp
Irrigation::PlanObservationStatus planObservationFor(uint8_t zoneId,
                                                     const Irrigation::PlanDefinition& plan,
                                                     uint32_t ymd) {
    PlanExecutionTracker tracker;
    tracker.begin(zoneId);
    Irrigation::PlanObservationStatus status = Irrigation::PlanObservationStatus::NOT_EVALUATED;
    if (tracker.status(plan.planId, ymd, planMinuteOfDay(plan), &status)) {
        return status;
    }
    return Irrigation::PlanObservationStatus::NOT_EVALUATED;
}
```

- [ ] **Step 5: Add record-backed completion collection**

Add a small bounded scan helper near the plan display helpers. `RecordStore` has 256 fixed records, so scanning the latest records once per enabled zone during server rendering is acceptable for the small fixed 4-zone product. This helper must only read records.

```cpp
struct TodayRecordCollectContext {
    uint8_t zoneId;
    uint32_t ymd;
    TodayPlanCompletion* out;
    uint8_t capacity;
    uint8_t count;
};

int8_t findCompletionIndex(const TodayPlanCompletion* completions, uint8_t count, uint32_t planId) {
    if (!completions || planId == Irrigation::NoPlanId) {
        return -1;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (completions[i].planId == planId) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

void collectTodayPlanRecord(const RecordStore::WateringRecord& record, void* user) {
    TodayRecordCollectContext* ctx = static_cast<TodayRecordCollectContext*>(user);
    if (!ctx || !ctx->out || ctx->capacity == 0) {
        return;
    }
    if (record.zoneId != ctx->zoneId ||
        record.planId == Irrigation::NoPlanId ||
        record.taskType != static_cast<uint8_t>(Irrigation::TaskType::PLAN)) {
        return;
    }
    const uint32_t endedYmd = ymdFromEpoch(record.endedEpoch);
    if (endedYmd != ctx->ymd) {
        return;
    }
    const uint32_t runtimeSec = recordRuntimeSec(record);
    const int8_t existing = findCompletionIndex(ctx->out, ctx->count, record.planId);
    if (existing >= 0) {
        TodayPlanCompletion& completion = ctx->out[existing];
        completion.completedSec += runtimeSec;
        completion.result = static_cast<Irrigation::TaskResult>(record.result);
        return;
    }
    if (ctx->count >= ctx->capacity) {
        return;
    }
    TodayPlanCompletion& completion = ctx->out[ctx->count++];
    completion.planId = record.planId;
    completion.completedSec = runtimeSec;
    completion.result = static_cast<Irrigation::TaskResult>(record.result);
}

uint8_t collectTodayPlanCompletions(uint8_t zoneId,
                                    uint32_t ymd,
                                    TodayPlanCompletion* out,
                                    uint8_t capacity) {
    if (!out || capacity == 0 || !PlanStore::validYmd(ymd)) {
        return 0;
    }
    for (uint8_t i = 0; i < capacity; ++i) {
        out[i] = {};
        out[i].result = Irrigation::TaskResult::NONE;
    }
    TodayRecordCollectContext ctx = {};
    ctx.zoneId = zoneId;
    ctx.ymd = ymd;
    ctx.out = out;
    ctx.capacity = capacity;
    (void)RecordStore::readLatest(0, RecordStore::Capacity, collectTodayPlanRecord, &ctx);
    return ctx.count;
}
```

- [ ] **Step 6: Add status label helpers**

Add:

```cpp
const char* todayPlanPrimaryStatus(bool completed, bool running) {
    if (running) return "运行中";
    if (completed) return "完成";
    return "未完成";
}

const char* todayPlanReason(const TodayPlanDisplay& item) {
    if (item.running) return "正在执行";
    if (item.completed) return taskResultLabel(item.lastResult);
    switch (item.observation) {
        case Irrigation::PlanObservationStatus::NOT_EVALUATED: return "未到时间";
        case Irrigation::PlanObservationStatus::SKIPPED_CALENDAR: return "今日跳过";
        case Irrigation::PlanObservationStatus::SKIPPED_DISABLED: return "水路停用";
        case Irrigation::PlanObservationStatus::SKIPPED_BUSY: return "水路忙碌";
        case Irrigation::PlanObservationStatus::SKIPPED_ERROR: return "水路异常";
        case Irrigation::PlanObservationStatus::SKIPPED_LEAK: return "漏水保护";
        case Irrigation::PlanObservationStatus::SKIPPED_RESET: return "恢复出厂";
        case Irrigation::PlanObservationStatus::SKIPPED_CYCLE: return "非执行日";
        case Irrigation::PlanObservationStatus::SKIPPED_CONFIG_INVALID: return "配置无效";
        case Irrigation::PlanObservationStatus::REJECTED: return "启动拒绝";
        case Irrigation::PlanObservationStatus::MISSED: return "已错过";
        case Irrigation::PlanObservationStatus::STARTED: return "已启动，等待记录";
        default: return "";
    }
}
```

- [ ] **Step 7: Add summary collection**

Add:

```cpp
uint8_t collectTodayPlans(uint8_t zoneId,
                          uint32_t ymd,
                          TodayPlanDisplay* out,
                          uint8_t capacity,
                          TodayPlanSummary* summary) {
    if (summary) {
        *summary = {};
    }
    if (!out || capacity == 0 || !PlanStore::validYmd(ymd)) {
        return 0;
    }
    const Irrigation::ZoneStatus zoneStatus = ZoneManager::status(zoneId);
    TodayPlanCompletion completions[Irrigation::MaxPlansPerZone] = {};
    const uint8_t completionCount = collectTodayPlanCompletions(zoneId, ymd, completions, Irrigation::MaxPlansPerZone);
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone && count < capacity; ++slot) {
        const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(zoneId, slot);
        if (!plan.exists || !plan.enabled || !PlanStore::shouldRunOnDate(plan, ymd)) {
            continue;
        }
        TodayPlanDisplay item = {};
        item.exists = true;
        item.plan = plan;
        item.observation = planObservationFor(zoneId, plan, ymd);
        item.lastResult = Irrigation::TaskResult::NONE;
        item.running = zoneStatus.busy &&
                       zoneStatus.taskType == Irrigation::TaskType::PLAN &&
                       zoneStatus.planId == plan.planId;
        if (item.running) {
            item.completedSec = zoneStatus.elapsedSec;
        }
        const int8_t completionIndex = findCompletionIndex(completions, completionCount, plan.planId);
        if (completionIndex >= 0) {
            item.completed = true;
            item.completedSec += completions[completionIndex].completedSec;
            item.lastResult = completions[completionIndex].result;
        }
        out[count++] = item;
        if (summary) {
            ++summary->total;
            summary->totalSec += plan.durationSec;
            summary->completedSec += item.completedSec;
            if (item.completed) {
                ++summary->completed;
            }
        }
    }
    return count;
}
```

Important behavior:

- A plan with tracker status `STARTED` is not automatically complete.
- A stopped-early, fault-stopped, leak-protected, or manually stopped plan counts actual runtime seconds from `RecordStore`, not configured duration.
- A currently running plan contributes live `ZoneStatus.elapsedSec` to completed seconds, but does not increment completed plan count until a record exists.
- A skipped, blocked, missed, rejected, or not-yet-due plan contributes zero completed seconds.

- [ ] **Step 8: Build**

Run:

```bash
pio run
```

Expected: build succeeds.

- [ ] **Step 9: Commit**

```bash
git add src/web/IrrigationWeb.cpp
git commit -m "Add today plan display helpers"
```

## Task 4: Replace Home Table With Zone Rows

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`
- Modify: `scripts/check-web-structure.mjs`

- [ ] **Step 1: Add row layout CSS helper**

Add this helper near the overview rendering helpers:

```cpp
void writeOverviewStyles() {
    Esp32BaseWeb::sendChunk("<style>"
".irr-overview-weather{display:grid;grid-template-columns:minmax(0,1.2fr) repeat(3,minmax(0,.7fr));gap:10px}"
".irr-weather-cell{border:1px solid var(--eb-line-soft);background:var(--eb-soft);border-radius:8px;padding:10px 11px;min-width:0}"
".irr-weather-value{display:block;font-size:20px;line-height:1.1;margin-bottom:6px}"
".irr-weather-cell span{display:block;color:var(--eb-muted);font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-zone-rows{display:grid;gap:12px}"
".irr-zone-row{display:grid;grid-template-columns:minmax(0,1.7fr) minmax(260px,.85fr);gap:12px;align-items:stretch}"
".irr-zone-card,.irr-plan-card{border:1px solid var(--eb-line);background:var(--eb-surface);border-radius:8px;padding:12px;min-width:0}"
".irr-zone-card.running{border-color:#fdba74;background:#fffaf5}"
".irr-zone-card.error{border-color:#fca5a5;background:#fffafa}"
".irr-zone-head{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;margin-bottom:10px}"
".irr-zone-name{font-size:18px;font-weight:750;line-height:1.15;margin-bottom:3px}"
".irr-zone-task{color:var(--eb-muted);font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-zone-metrics{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin-bottom:10px}"
".irr-zone-metric{border:1px solid var(--eb-line-soft);background:var(--eb-soft);border-radius:8px;padding:8px 9px;min-height:58px;min-width:0}"
".irr-zone-metric b{display:block;font-size:18px;line-height:1.05;margin-bottom:6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-zone-metric .label{display:block;color:var(--eb-muted);font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-chart-wrap{border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff;padding:8px 9px;margin-bottom:10px}"
".irr-chart-head{display:flex;justify-content:space-between;gap:8px;margin-bottom:5px;color:var(--eb-muted);font-size:12px}"
".irr-flow-chart{width:100%;height:124px;display:block;border-radius:6px;background:linear-gradient(#fff,#f8fafc)}"
".irr-zone-footer{display:flex;align-items:center;justify-content:space-between;gap:10px;min-height:34px}"
".irr-zone-runtime{color:var(--eb-muted);font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-plan-card h3{font-size:16px;margin:0 0 10px}"
".irr-plan-summary{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;margin-bottom:10px}"
".irr-plan-summary div{border:1px solid var(--eb-line-soft);background:var(--eb-soft);border-radius:7px;padding:6px 7px;min-width:0}"
".irr-plan-summary .value{display:block;font-size:15px;line-height:1.05;margin-bottom:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-plan-summary span{display:block;color:var(--eb-muted);font-size:11px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-plan-list{display:grid;gap:7px}"
".irr-plan-item{display:flex;justify-content:space-between;gap:8px;border-top:1px solid var(--eb-line-soft);padding-top:7px;min-width:0}"
".irr-plan-item:first-child{border-top:0;padding-top:0}"
".irr-plan-time{display:block;font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-plan-note{display:block;color:var(--eb-muted);font-size:12px;font-style:normal;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
".irr-plan-status{display:inline-flex;align-items:center;justify-content:center;min-height:22px;border:1px solid var(--eb-line);border-radius:7px;padding:0 7px;font-size:12px;font-weight:750;white-space:nowrap;align-self:flex-start}"
".irr-plan-status.done{border-color:#bbf7d0;background:#f0fdf4;color:#166534}"
".irr-plan-status.running{border-color:#fed7aa;background:#fff7ed;color:#9a3412}"
".irr-plan-status.pending{border-color:#bfdbfe;background:#eff6ff;color:#1d4ed8}"
"@media(max-width:760px){.irr-overview-weather{grid-template-columns:1fr}.irr-zone-row{grid-template-columns:1fr}.irr-zone-metrics,.irr-plan-summary{grid-template-columns:1fr}.irr-zone-footer{align-items:stretch;flex-direction:column}.irr-flow-chart{height:118px}}"
"</style>");
}
```

- [ ] **Step 2: Add weather strip renderer**

Add this complete renderer. Keep the freshness text inside the current-weather cell before closing that cell.

```cpp
void writeHourMinuteHuman(uint32_t epoch) {
    if (epoch == 0) {
        Esp32BaseWeb::sendChunk("时间未同步");
        return;
    }
    const time_t value = static_cast<time_t>(epoch);
    tm local = {};
    if (localtime_r(&value, &local) == nullptr) {
        Esp32BaseWeb::sendChunk("时间无效");
        return;
    }
    char text[8];
    snprintf(text, sizeof(text), "%02d:%02d", local.tm_hour, local.tm_min);
    Esp32BaseWeb::sendChunk(text);
}

void writeWeatherStrip() {
    WeatherSnapshotStore::Snapshot weather = {};
    Esp32BaseWeb::beginPanel("天气预报");
    if (!WeatherSnapshotStore::get(&weather)) {
        Esp32BaseWeb::sendChunk("<p class='muted'>暂无天气数据</p>");
        Esp32BaseWeb::endPanel();
        return;
    }
    Esp32BaseWeb::sendChunk("<div class='irr-overview-weather'><div class='irr-weather-cell'><span class='irr-weather-value'>");
    writeInt(weather.currentTempC);
    Esp32BaseWeb::sendChunk("°C · ");
    Esp32BaseWeb::writeHtmlEscaped(weather.condition);
    Esp32BaseWeb::sendChunk("</span><span>24 小时降雨 ");
    writeUInt(weather.rainProbability24hPercent);
    Esp32BaseWeb::sendChunk("% · 风力 ");
    writeUInt(weather.windLevel);
    Esp32BaseWeb::sendChunk(" 级</span>");
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot timeSnapshot = Esp32BaseNtp::snapshot();
    if (!timeSnapshot.synced || timeSnapshot.epochSec == 0) {
        Esp32BaseWeb::sendChunk("<span>天气时间未确认</span>");
    } else if (WeatherSnapshotStore::isStale(weather, timeSnapshot.epochSec)) {
        Esp32BaseWeb::sendChunk("<span class='tag warn'>天气数据已过期</span>");
    } else if (weather.updatedEpoch != 0) {
        Esp32BaseWeb::sendChunk("<span>更新于 ");
        writeHourMinuteHuman(weather.updatedEpoch);
        Esp32BaseWeb::sendChunk("</span>");
    }
#else
    Esp32BaseWeb::sendChunk("<span>天气时间未确认</span>");
#endif
    Esp32BaseWeb::sendChunk("</div>");
    for (uint8_t i = 0; i < 3; ++i) {
        Esp32BaseWeb::sendChunk("<div class='irr-weather-cell'><span class='irr-weather-value'>");
        Esp32BaseWeb::writeHtmlEscaped(weather.days[i].label);
        Esp32BaseWeb::sendChunk("</span><span>");
        writeInt(weather.days[i].lowTempC);
        Esp32BaseWeb::sendChunk("-");
        writeInt(weather.days[i].highTempC);
        Esp32BaseWeb::sendChunk("°C</span><span>降雨 ");
        writeUInt(weather.days[i].rainProbabilityPercent);
        Esp32BaseWeb::sendChunk("%</span></div>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();
}
```

- [ ] **Step 3: Add plan card renderer**

Add:

```cpp
void writeTodayPlanCard(uint8_t zoneId, uint32_t ymd) {
    TodayPlanDisplay plans[Irrigation::MaxPlansPerZone] = {};
    TodayPlanSummary summary = {};
    const uint8_t count = collectTodayPlans(zoneId, ymd, plans, Irrigation::MaxPlansPerZone, &summary);
    Esp32BaseWeb::sendChunk("<aside class='irr-plan-card'><h3>今日计划</h3><div class='irr-plan-summary'><div><span class='value'>");
    writeUInt(summary.completed);
    Esp32BaseWeb::sendChunk(" / ");
    writeUInt(summary.total);
    Esp32BaseWeb::sendChunk("</span><span>完成进度</span></div><div><span class='value'>");
    writeUInt(summary.completedSec / 60UL);
    Esp32BaseWeb::sendChunk(" / ");
    writeUInt(summary.totalSec / 60UL);
    Esp32BaseWeb::sendChunk("分钟</span><span>已浇 / 总时长</span></div><div><span class='value'>");
    const uint32_t remainingSec = summary.totalSec > summary.completedSec ? summary.totalSec - summary.completedSec : 0;
    writeUInt(remainingSec / 60UL);
    Esp32BaseWeb::sendChunk("分钟</span><span>剩余时长</span></div></div><div class='irr-plan-list'>");
    if (count == 0) {
        Esp32BaseWeb::sendChunk("<div class='irr-plan-item'><div><span class='irr-plan-time'>无启用计划</span><em class='irr-plan-note'>这一路今天不会自动浇水</em></div></div>");
    }
    for (uint8_t i = 0; i < count; ++i) {
        const TodayPlanDisplay& item = plans[i];
        const char* primary = todayPlanPrimaryStatus(item.completed, item.running);
        const char* reason = todayPlanReason(item);
        Esp32BaseWeb::sendChunk("<div class='irr-plan-item'><div><span class='irr-plan-time'>");
        writeTime(item.plan.timeHour, item.plan.timeMinute);
        Esp32BaseWeb::sendChunk(" · ");
        writeDuration(item.plan.durationSec);
        Esp32BaseWeb::sendChunk("</span><em class='irr-plan-note'>");
        Esp32BaseWeb::writeHtmlEscaped(reason);
        Esp32BaseWeb::sendChunk("</em></div><span class='irr-plan-status ");
        Esp32BaseWeb::sendChunk(strcmp(primary, "完成") == 0 ? "done" : (strcmp(primary, "运行中") == 0 ? "running" : "pending"));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(primary);
        Esp32BaseWeb::sendChunk("</span></div>");
    }
    Esp32BaseWeb::sendChunk("</div></aside>");
}
```

- [ ] **Step 4: Add zone row renderer**

Add:

```cpp
const char* overviewCardStateClass(const Irrigation::ZoneStatus& status) {
    if (status.errorActive) return " error";
    if (status.busy) return " running";
    return "";
}

void writeZoneMetric(uint8_t zoneId, const char* attr, const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<div class='irr-zone-metric'><b");
    if (attr && attr[0]) {
        Esp32BaseWeb::sendChunk(" ");
        Esp32BaseWeb::sendChunk(attr);
        Esp32BaseWeb::sendChunk("='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'");
    }
    Esp32BaseWeb::sendChunk(">");
    Esp32BaseWeb::writeHtmlEscaped(value);
    Esp32BaseWeb::sendChunk("</b><span class='label'>");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk("</span></div>");
}

void writeZoneOverviewRow(uint8_t zoneId, const Irrigation::ZoneStatus& status, uint32_t ymd) {
    const Irrigation::ZoneConfig& config = ZoneManager::config(zoneId);
    char remaining[24];
    char flow[24];
    char volume[24];
    if (status.busy) {
        snprintf(remaining, sizeof(remaining), "%lus", static_cast<unsigned long>(status.remainingSec));
    } else {
        strlcpy(remaining, "-", sizeof(remaining));
    }
    if (status.busy && status.flowRateReady) {
        snprintf(flow, sizeof(flow), "%lu.%03lu", static_cast<unsigned long>(status.flowMlPerMin / 1000UL), static_cast<unsigned long>(status.flowMlPerMin % 1000UL));
    } else {
        strlcpy(flow, "-", sizeof(flow));
    }
    if (status.busy) {
        snprintf(volume, sizeof(volume), "%lu.%03lu", static_cast<unsigned long>(status.estimatedMilliliters / 1000UL), static_cast<unsigned long>(status.estimatedMilliliters % 1000UL));
    } else {
        strlcpy(volume, "-", sizeof(volume));
    }

    Esp32BaseWeb::sendChunk("<section class='irr-zone-row'><article class='irr-zone-card");
    Esp32BaseWeb::sendChunk(overviewCardStateClass(status));
    Esp32BaseWeb::sendChunk("' data-irr-zone-row='");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("' data-state='");
    Esp32BaseWeb::writeHtmlEscaped(Irrigation::zoneStateName(status.state));
    Esp32BaseWeb::sendChunk("' data-busy='");
    Esp32BaseWeb::sendChunk(status.busy ? "1" : "0");
    Esp32BaseWeb::sendChunk("' data-error='");
    Esp32BaseWeb::sendChunk(status.errorActive ? "1" : "0");
    Esp32BaseWeb::sendChunk("' data-zone-name='");
    Esp32BaseWeb::writeHtmlEscaped(config.name);
    Esp32BaseWeb::sendChunk("'><div class='irr-zone-head'><div><div class='irr-zone-name'>");
    Esp32BaseWeb::writeHtmlEscaped(config.name);
    Esp32BaseWeb::sendChunk("</div><div class='irr-zone-task' data-irr-task='");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::writeHtmlEscaped(status.busy ? taskTypeLabel(status.taskType) : (status.errorActive ? zoneErrorLabel(status.errorCode) : "无运行任务"));
    Esp32BaseWeb::sendChunk("</div></div><div data-irr-state='");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("'>");
    if (status.errorActive) {
        Esp32BaseWeb::sendChunk("<button type='button' class='tag danger' style='border:0;cursor:pointer' onclick='irrFaultOpen(\"irrFaultDialog");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("\")'>");
        Esp32BaseWeb::writeHtmlEscaped(zoneStateLabel(status.state));
        Esp32BaseWeb::sendChunk("</button>");
    } else {
        Esp32BaseWeb::sendChunk("<span class='tag");
        Esp32BaseWeb::sendChunk(uiToneForState(status.state));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(zoneStateLabel(status.state));
        Esp32BaseWeb::sendChunk("</span>");
    }
    Esp32BaseWeb::sendChunk("</div></div><div class='irr-zone-metrics'>");
    writeZoneMetric(zoneId, "data-irr-remaining", "剩余时间", remaining);
    writeZoneMetric(zoneId, "data-irr-flow", "L/min 当前流速", flow);
    writeZoneMetric(zoneId, "data-irr-ml", "L 估算水量", volume);
    Esp32BaseWeb::sendChunk("</div><div class='irr-chart-wrap'><div class='irr-chart-head'><span>近期流速</span><span data-irr-flow-chart-label='");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("'>");
    writeUInt(SystemConfigStore::current().flowChartHistoryMin);
    Esp32BaseWeb::sendChunk(" 分钟</span></div><svg class='irr-flow-chart' id='irrFlowChart");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("' viewBox='0 0 360 124' preserveAspectRatio='none' role='img' aria-label='近期流速图表'></svg></div><div class='irr-zone-footer'><div class='irr-zone-runtime' data-irr-runtime='");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("'>已运行 ");
    writeDurationHuman(status.elapsedSec);
    Esp32BaseWeb::sendChunk(" / 目标 ");
    writeDurationHuman(status.targetSec);
    Esp32BaseWeb::sendChunk("</div><div class='fsactions' data-irr-actions='");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("'>");
    if (status.busy) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/stop' onsubmit=\"return confirm('确认停止该水路？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
        Esp32BaseWeb::sendChunk("<input class='fsaction' type='submit' value='停止'></form>");
    }
    if (status.errorActive) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/clear-error' onsubmit=\"return confirm('");
        Esp32BaseWeb::writeHtmlEscaped(zoneErrorClearConfirm(status.errorCode));
        Esp32BaseWeb::sendChunk("')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
        Esp32BaseWeb::sendChunk("<input class='fsaction' type='submit' value='清除异常'></form>");
    }
    if (!status.busy && !status.errorActive) {
        if (ZoneManager::leakAlertActive()) {
            Esp32BaseWeb::sendChunk("<span class='muted'>漏水告警中</span>");
        } else {
            Esp32BaseWeb::sendChunk("<button class='btnlink compact ok' type='button' data-zone-id='");
            writeUInt(zoneId);
            Esp32BaseWeb::sendChunk("' data-zone-name='");
            Esp32BaseWeb::writeHtmlEscaped(config.name);
            Esp32BaseWeb::sendChunk("' onclick='irrManualOpen(this)'>启动</button>");
        }
    }
    Esp32BaseWeb::sendChunk("</div></div></article>");
    writeTodayPlanCard(zoneId, ymd);
    Esp32BaseWeb::sendChunk("</section>");
}
```

- [ ] **Step 5: Replace the table block and preserve existing interactions**

In `handleOverviewPage()`, replace the current water-status table and old sparkline rows with the row layout below. Keep the existing overview script block, `writeManualStartDialog()`, `Esp32BaseWeb::endPanel()`, and `pageFooter()` after this block; Task 5 updates that script for the new DOM.

```cpp
writeWeatherStrip();

Esp32BaseWeb::beginPanel("水路状态");
writeOverviewStyles();
Esp32BaseWeb::sendChunk("<div class='irr-zone-rows'>");
bool wroteStatus = false;
for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
    const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
    if (!status.enabled) continue;
    wroteStatus = true;
    writeZoneOverviewRow(zoneId, status, currentYmd());
}
Esp32BaseWeb::sendChunk("</div>");
if (!wroteStatus) {
    Esp32BaseWeb::sendChunk("<p class='muted'>暂无启用水路</p>");
}
Esp32BaseWeb::sendChunk("<div class='actions'><form method='post' action='/api/v1/zones/stop-all' onsubmit=\"return confirm('确认停止全部水路？')&&once(this)\">");
writeOnePostHidden("source", "web_page");
Esp32BaseWeb::sendChunk("<input id='irrStopAll' class='danger' type='submit' value='全部停止'");
Esp32BaseWeb::sendChunk(runningCount > 0 ? "" : " disabled");
Esp32BaseWeb::sendChunk("></form></div>");
for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
    const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
    if (status.enabled && status.errorActive) {
        writeZoneErrorDialog(zoneId, ZoneManager::config(zoneId), ZoneErrorStore::get(zoneId));
    }
}
```

Do not remove these existing homepage features:

- Stop-all POST form with `confirm()` and `once(this)`.
- Per-zone error detail dialogs.
- Manual start dialog and its asynchronous submit script.
- Overview polling script, which Task 5 rewrites for the new card DOM.

- [ ] **Step 6: Update overview structure checks**

In `scripts/check-web-structure.mjs`, replace the old overview table/sparkline assertions with card-oriented assertions:

```js
assert(web.includes('writeWeatherStrip()'), 'overview should render the weather forecast strip');
assert(web.includes('writeZoneOverviewRow(zoneId, status, currentYmd())'), 'overview should render one row per enabled zone');
assert(web.includes('writeTodayPlanCard(zoneId, ymd)'), 'overview zone rows should include today plan cards');
assert(web.includes('irr-zone-row') && web.includes('irr-zone-card') && web.includes('irr-plan-card'), 'overview should use row/card layout classes');
assert(web.includes('data-irr-runtime') && web.includes('data-irr-remaining') && web.includes('data-irr-flow') && web.includes('data-irr-ml'), 'overview card metrics should expose live-update targets');
assert(web.includes('irrFlowChart') && web.includes('flowHistory'), 'overview should render recent per-zone flow chart data');
assert(web.includes('L/min') && web.includes('无流速'), 'overview flow chart should include axes/unit labels and an idle baseline state');
assert(web.includes('collectTodayPlanCompletions') && web.includes('RecordStore::readLatest'), 'today plan progress should use watering records');
assert(!web.includes('resetNewDay(ymd)'), 'overview rendering must not reset plan execution tracker state');
assert(!web.includes('<table class=\\'part\\'><thead><tr><th>水路</th><th>状态</th><th>任务</th><th>目标时长'), 'overview should not render the old water-status table');
```

Remove or revise old assertions that specifically require the previous overview table, such as:

```js
assert(web.includes('writeLitersFromMilliliters(status.estimatedMilliliters)'), 'overview initial estimated water should use liters');
```

The new overview renders initial estimated water through the card metric formatter and updates it with `irrOverviewLiters()`.

- [ ] **Step 7: Build**

Run:

```bash
pio run
```

Expected: build succeeds.

- [ ] **Step 8: Run structure checks**

Run:

```bash
node scripts/check-web-structure.mjs
```

Expected: structure checks pass with the new overview card assertions.

- [ ] **Step 9: Commit**

```bash
git add src/web/IrrigationWeb.cpp scripts/check-web-structure.mjs
git commit -m "Render home overview as zone rows"
```

## Task 5: Improve Flow Chart Rendering And Live Updates

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`

- [ ] **Step 1: Replace overview live-update helpers**

Replace the old table-oriented `irrOverviewRenderState()`, `irrOverviewRenderActions()`, `irrFlowChart()`, and `irrOverviewApplyStatus()` helpers with card-oriented versions. Keep the existing escaping, duration, flow, liter, refresh, and scheduling helpers.

```js
function irrOverviewRenderState(z){
  var e=document.querySelector('[data-irr-state="'+z.zoneId+'"]');
  if(!e)return;
  if(z.errorActive){
    e.innerHTML='<button type="button" class="tag danger" style="border:0;cursor:pointer" onclick="irrFaultOpen(\\'irrFaultDialog'+z.zoneId+'\\')">'+irrOverviewEscape(irrOverviewStateLabel(z.state))+'</button>';
    return;
  }
  e.innerHTML='<span class="tag'+irrOverviewStateTone(z.state)+'">'+irrOverviewEscape(irrOverviewStateLabel(z.state))+'</span>';
}

function irrOverviewRenderActions(row,z,leak){
  var e=document.querySelector('[data-irr-actions="'+z.zoneId+'"]');
  if(!e)return;
  var name=irrOverviewEscape(row.dataset.zoneName||('水路 '+z.zoneId));
  if(z.busy){
    e.innerHTML='<form method="post" action="/api/v1/zone/stop" onsubmit="return confirm(\\'确认停止该水路？\\')&&once(this)"><input type="hidden" name="source" value="web_page"><input type="hidden" name="zoneId" value="'+z.zoneId+'"><input class="fsaction" type="submit" value="停止"></form>';
    return;
  }
  if(z.errorActive){
    e.innerHTML='<form method="post" action="/api/v1/zone/clear-error" onsubmit="return confirm(\\'确认清除该水路异常？\\')&&once(this)"><input type="hidden" name="source" value="web_page"><input type="hidden" name="zoneId" value="'+z.zoneId+'"><input class="fsaction" type="submit" value="清除异常"></form>';
    return;
  }
  e.innerHTML=leak?'<span class="muted">漏水告警中</span>':'<button class="btnlink compact ok" type="button" data-zone-id="'+z.zoneId+'" data-zone-name="'+name+'" onclick="irrManualOpen(this)">启动</button>';
}

function irrFlowChart(id,busy){
  if(!busy){
    irrFlowChartDraw(id,[],10);
    return;
  }
  fetch('/api/v1/flow/history?zoneId='+id,{headers:{Accept:'application/json'},credentials:'same-origin'}).then(function(r){return r.ok?r.json():null;}).then(function(j){
    if(!j||!j.ok)return;
    irrFlowChartDraw(id,j.flowHistory||[],j.historyMin||10);
    var label=document.querySelector('[data-irr-flow-chart-label="'+id+'"]');
    if(label)label.textContent=String(j.historyMin||'')+' 分钟';
  }).catch(function(){});
}

function irrOverviewApplyStatus(data){
  if(!data||!data.ok||!data.zones)return;
  var enabled=0,running=0,errors=0,structural=false;
  data.zones.forEach(function(z){
    if(!z.enabled)return;
    enabled++;
    if(z.busy)running++;
    if(z.errorActive)errors++;
    var row=document.querySelector('[data-irr-zone-row="'+z.zoneId+'"]');
    if(!row){structural=true;return;}
    var busy=z.busy?'1':'0',err=z.errorActive?'1':'0';
    if(row.dataset.error!==err){structural=true;return;}
    row.dataset.state=z.state;
    row.dataset.busy=busy;
    row.dataset.error=err;
    row.classList.toggle('running',!!z.busy);
    row.classList.toggle('error',!!z.errorActive);
    irrOverviewRenderState(z);
    var e=document.querySelector('[data-irr-task="'+z.zoneId+'"]');
    if(e)e.textContent=z.busy?(z.taskLabel||'浇水中'):(z.errorActive?(z.errorLabel||'水路异常'):'无运行任务');
    e=document.querySelector('[data-irr-remaining="'+z.zoneId+'"]');
    if(e)e.textContent=z.busy?irrOverviewDuration(z.remainingSec):'-';
    e=document.querySelector('[data-irr-flow="'+z.zoneId+'"]');
    if(e)e.textContent=irrOverviewFlow(z.flowMlPerMin,z.flowRateReady,z.busy);
    e=document.querySelector('[data-irr-ml="'+z.zoneId+'"]');
    if(e)e.textContent=z.busy?irrOverviewLiters(z.estimatedMl):'-';
    e=document.querySelector('[data-irr-runtime="'+z.zoneId+'"]');
    if(e)e.textContent='已运行 '+irrOverviewDuration(z.elapsedSec)+' / 目标 '+irrOverviewDuration(z.targetSec);
    irrFlowChart(z.zoneId,z.busy);
    irrOverviewRenderActions(row,z,data.leakAlertActive);
  });
  if(enabled!==document.querySelectorAll('[data-irr-zone-row]').length)structural=true;
  if(structural){location.reload();return;}
  irrOverviewSetMetric('irrMetricRunning',running+' / '+enabled);
  irrOverviewSetMetric('irrMetricErrors',String(errors));
  irrOverviewSetMetric('irrMetricSafety',data.leakAlertActive?'漏水告警':(errors>0?errors+' 路异常':'正常'));
  var stop=document.getElementById('irrStopAll');
  if(stop)stop.disabled=running<=0;
}
```

- [ ] **Step 2: Replace JS chart draw function**

Replace `irrFlowChartDraw()` with an SVG axis renderer:

```js
function irrFlowChartDraw(id,points,historyMin){
  var svg=document.getElementById('irrFlowChart'+id);
  if(!svg)return;
  points=points||[];
  historyMin=parseInt(historyMin||10,10)||10;
  var max=0;
  for(var i=0;i<points.length;i++)max=Math.max(max,parseInt(points[i]||0,10)||0);
  var yMax=Math.max(2000,Math.ceil(max/500)*500);
  var w=360,h=124,left=42,right=12,top=18,bottom=28,plotW=w-left-right,plotH=h-top-bottom;
  var html='';
  for(var gy=0;gy<=4;gy++){
    var value=Math.round((yMax*gy)/4), y=top+plotH-(plotH*gy/4);
    html+='<line x1=\"'+left+'\" y1=\"'+y+'\" x2=\"'+(w-right)+'\" y2=\"'+y+'\" stroke=\"#e2e8f0\" vector-effect=\"non-scaling-stroke\"/>';
    html+='<text x=\"'+(left-7)+'\" y=\"'+(y+4)+'\" text-anchor=\"end\" fill=\"#64748b\" font-size=\"10\">'+(value/1000).toFixed(value%1000?1:0)+'</text>';
  }
  var labels=5;
  for(var tx=0;tx<labels;tx++){
    var x=left+(plotW*tx/(labels-1)), min=Math.round(-historyMin+(historyMin*tx/(labels-1)));
    html+='<line x1=\"'+x+'\" y1=\"'+(top+plotH)+'\" x2=\"'+x+'\" y2=\"'+(top+plotH+4)+'\" stroke=\"#cbd5e1\" vector-effect=\"non-scaling-stroke\"/>';
    html+='<text x=\"'+x+'\" y=\"'+(h-9)+'\" text-anchor=\"middle\" fill=\"#64748b\" font-size=\"10\">'+(tx===labels-1?'现在':String(min))+'</text>';
  }
  html+='<line x1=\"'+left+'\" y1=\"'+top+'\" x2=\"'+left+'\" y2=\"'+(top+plotH)+'\" stroke=\"#94a3b8\" vector-effect=\"non-scaling-stroke\"/>';
  html+='<line x1=\"'+left+'\" y1=\"'+(top+plotH)+'\" x2=\"'+(w-right)+'\" y2=\"'+(top+plotH)+'\" stroke=\"#94a3b8\" vector-effect=\"non-scaling-stroke\"/>';
  html+='<text x=\"6\" y=\"13\" fill=\"#64748b\" font-size=\"10\">L/min</text>';
  if(points.length>=2&&max>0){
    var d='';
    for(var j=0;j<points.length;j++){
      var px=left+(plotW*j/(points.length-1));
      var py=top+plotH-((parseInt(points[j]||0,10)||0)*plotH/yMax);
      d+=(j?'L':'M')+px.toFixed(1)+' '+py.toFixed(1);
    }
    html+='<path d=\"'+d+' L'+(w-right)+' '+(top+plotH)+' L'+left+' '+(top+plotH)+' Z\" fill=\"#0f766e\" opacity=\".08\"/>';
    html+='<path d=\"'+d+'\" fill=\"none\" stroke=\"#0f766e\" stroke-width=\"2.2\" vector-effect=\"non-scaling-stroke\"/>';
  }else{
    html+='<line x1=\"'+left+'\" y1=\"'+(top+plotH)+'\" x2=\"'+(w-right)+'\" y2=\"'+(top+plotH)+'\" stroke=\"#cbd5e1\" stroke-width=\"2\" vector-effect=\"non-scaling-stroke\"/>';
    html+='<text x=\"'+(left+plotW/2)+'\" y=\"'+(top+plotH/2)+'\" text-anchor=\"middle\" fill=\"#94a3b8\" font-size=\"12\">无流速</text>';
  }
  svg.innerHTML=html;
}
```

- [ ] **Step 3: Build and inspect generated HTML manually**

Run:

```bash
pio run
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/web/IrrigationWeb.cpp
git commit -m "Update home overview chart and polling"
```

## Task 6: Final Verification

**Files:**
- No source edits unless verification finds issues.

- [ ] **Step 1: Full build**

Run:

```bash
pio run
```

Expected: build succeeds.

- [ ] **Step 2: Static structure checks**

Run:

```bash
node scripts/check-web-structure.mjs
```

Expected: structure checks pass, including route capacity 43 and the updated home overview markers.

- [ ] **Step 3: Check route capacity**

Run:

```bash
rg -n "ESP32BASE_WEB_MAX_ROUTES|weatherOk|addRoute" platformio.ini src/web/IrrigationWeb.cpp
```

Expected: route limit is at least one higher than before and weather route registration is present.

- [ ] **Step 4: Source safety scan**

Run:

```bash
rg -n "LittleFS|WebServer|server\\.|ESP\\.restart|esp_deep_sleep" src
```

Expected: no new direct base-library bypasses were introduced.

- [ ] **Step 5: Plan-data correctness scan**

Run:

```bash
rg -n "resetNewDay|RecordStore::readLatest|collectTodayPlanCompletions|PlanObservationStatus::STARTED" src/web/IrrigationWeb.cpp
```

Expected:

- `resetNewDay` does not appear in `src/web/IrrigationWeb.cpp`.
- `collectTodayPlanCompletions` and `RecordStore::readLatest` appear in the today-plan helper code.
- `PlanObservationStatus::STARTED` is not used to increment completed plan count or to assign full planned duration.

- [ ] **Step 6: Browser visual verification**

Using the in-app Browser against the local device or dev target, verify:

- 1 enabled zone renders as one full-width row with runtime card and today-plan card; it must not look sparse or broken.
- 2 enabled zones render as two rows with the same structure as the 1-zone case.
- 4 enabled zones render as four rows without horizontal overflow.
- Mobile-width layout stacks runtime card above plan card without overlapping text or controls.
- Idle zones show chart axes and an empty baseline instead of a blank/collapsed chart.
- Running zones update remaining time, flow, estimated volume, and chart without reloading.
- Weather strip shows either forecast data with freshness text or compact `暂无天气数据`.

- [ ] **Step 7: Manual browser/hardware caveat**

Record in final handoff:

```text
PlatformIO build passed. Web layout has not been verified on physical ESP32 hardware unless a device test is explicitly run. Weather publishing API is implemented as display-only and does not alter schedules or valves.
```

- [ ] **Step 8: Commit any verification fixes**

If verification required fixes:

```bash
git add src/web/IrrigationWeb.cpp src/app/IrrigationApp.cpp platformio.ini scripts/check-web-structure.mjs src/storage/WeatherSnapshotStore.h src/storage/WeatherSnapshotStore.cpp
git commit -m "Fix home overview verification issues"
```

If no fixes were required, do not create an empty commit.
