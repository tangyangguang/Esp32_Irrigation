# Home Overview Row Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the home page table with a row-based operational dashboard showing global weather, per-zone runtime state, flow charts with axes, and today's per-zone plan progress.

**Architecture:** Keep the change inside the irrigation application layer. Add a small weather snapshot store and publish API, derive today's plan display from existing `PlanStore`, `PlanExecutionTracker`, and `ZoneStatus`, and render the home page with server-generated HTML plus the existing `/api/v1/status` and `/api/v1/flow/history` polling.

**Tech Stack:** ESP32 Arduino, PlatformIO, Esp32Base Web/AppConfig, C++17-style Arduino code, server-side HTML/SVG streaming via `Esp32BaseWeb`.

---

## File Structure

- Modify `src/web/IrrigationWeb.cpp`: replace the home page table with row cards, add plan summary rendering, add weather strip rendering, improve flow chart SVG/JS, add weather publish route.
- Modify `src/app/IrrigationApp.cpp`: initialize the weather snapshot store at startup.
- Create `src/storage/WeatherSnapshotStore.h`: public weather snapshot storage API.
- Create `src/storage/WeatherSnapshotStore.cpp`: NVS-backed weather snapshot persistence and validation.
- Modify `platformio.ini`: increase `ESP32BASE_WEB_MAX_ROUTES` by one for the weather publish route.

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

bool validText(const char* text, size_t maxLen, bool allowEmpty) {
    if (!text) return false;
    const size_t len = strnlen(text, maxLen);
    if (len >= maxLen) return false;
    if (!allowEmpty && len == 0) return false;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7F) return false;
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
    if (!validText(snapshot.condition, ConditionMaxBytes, false)) return false;
    if (snapshot.currentTempC < -50 || snapshot.currentTempC > 80) return false;
    if (snapshot.rainProbability24hPercent > 100) return false;
    if (snapshot.windLevel > 17) return false;
    for (uint8_t i = 0; i < 3; ++i) {
        const ForecastDay& day = snapshot.days[i];
        if (!validText(day.label, DayLabelMaxBytes, false)) return false;
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

- [ ] **Step 2: Add read helpers**

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

- [ ] **Step 3: Add weather publish handler**

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

- [ ] **Step 4: Register the route**

In `IrrigationWeb::begin()`, add:

```cpp
const bool weatherOk = Esp32BaseWeb::addRoute("/api/v1/weather/snapshot", Esp32BaseWeb::METHOD_POST, handleWeatherSnapshotApi);
```

Include `weatherOk` in the route health log message.

- [ ] **Step 5: Increase route limit**

In `platformio.ini`, change:

```ini
-D ESP32BASE_WEB_MAX_ROUTES=42
```

to:

```ini
-D ESP32BASE_WEB_MAX_ROUTES=43
```

- [ ] **Step 6: Build**

Run:

```bash
pio run
```

Expected: build succeeds.

- [ ] **Step 7: Commit**

```bash
git add src/web/IrrigationWeb.cpp platformio.ini
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
    uint32_t completedSec;
};

struct TodayPlanSummary {
    uint8_t total;
    uint8_t completed;
    uint32_t totalSec;
    uint32_t completedSec;
};
```

- [ ] **Step 2: Add minute-of-day helper**

Add near the date/time helpers:

```cpp
uint16_t planMinuteOfDay(const Irrigation::PlanDefinition& plan) {
    return static_cast<uint16_t>(static_cast<uint16_t>(plan.timeHour) * 60U + plan.timeMinute);
}
```

- [ ] **Step 3: Add observation lookup**

Because the web layer does not own each zone's scheduler instance, use the persisted tracker directly by constructing a temporary tracker:

```cpp
Irrigation::PlanObservationStatus planObservationFor(uint8_t zoneId,
                                                     const Irrigation::PlanDefinition& plan,
                                                     uint32_t ymd) {
    PlanExecutionTracker tracker;
    tracker.begin(zoneId);
    (void)tracker.resetNewDay(ymd);
    Irrigation::PlanObservationStatus status = Irrigation::PlanObservationStatus::NOT_EVALUATED;
    if (tracker.status(plan.planId, ymd, planMinuteOfDay(plan), &status)) {
        return status;
    }
    return Irrigation::PlanObservationStatus::NOT_EVALUATED;
}
```

Add include:

```cpp
#include "domain/PlanExecutionTracker.h"
```

- [ ] **Step 4: Add status label helpers**

Add:

```cpp
const char* todayPlanPrimaryStatus(Irrigation::PlanObservationStatus status, bool running, bool dueToday) {
    if (running) return "运行中";
    if (status == Irrigation::PlanObservationStatus::STARTED) return "完成";
    if (!dueToday) return "未完成";
    return "未完成";
}

const char* todayPlanReason(Irrigation::PlanObservationStatus status) {
    switch (status) {
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
        case Irrigation::PlanObservationStatus::STARTED: return "正常完成";
        default: return "";
    }
}
```

- [ ] **Step 5: Add summary collection**

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
        item.running = zoneStatus.busy &&
                       zoneStatus.taskType == Irrigation::TaskType::PLAN &&
                       zoneStatus.planId == plan.planId;
        if (item.running) {
            item.completedSec = zoneStatus.elapsedSec;
        } else if (item.observation == Irrigation::PlanObservationStatus::STARTED) {
            item.completedSec = plan.durationSec;
        } else {
            item.completedSec = 0;
        }
        out[count++] = item;
        if (summary) {
            ++summary->total;
            summary->totalSec += plan.durationSec;
            summary->completedSec += item.completedSec;
            if (item.observation == Irrigation::PlanObservationStatus::STARTED) {
                ++summary->completed;
            }
        }
    }
    return count;
}
```

- [ ] **Step 6: Build**

Run:

```bash
pio run
```

Expected: build succeeds.

- [ ] **Step 7: Commit**

```bash
git add src/web/IrrigationWeb.cpp
git commit -m "Add today plan display helpers"
```

## Task 4: Replace Home Table With Zone Rows

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`

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
".irr-zone-metric span{display:block;color:var(--eb-muted);font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
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

Add:

```cpp
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
    Esp32BaseWeb::sendChunk(" 级</span></div>");
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

Then add weather freshness text to the current-weather cell:

```cpp
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot timeSnapshot = Esp32BaseNtp::snapshot();
    if (!timeSnapshot.synced || timeSnapshot.epochSec == 0) {
        Esp32BaseWeb::sendChunk("<span>天气时间未确认</span>");
    } else if (WeatherSnapshotStore::isStale(weather, timeSnapshot.epochSec)) {
        Esp32BaseWeb::sendChunk("<span class='tag warn'>天气数据已过期</span>");
    } else if (weather.updatedEpoch != 0) {
        Esp32BaseWeb::sendChunk("<span>更新于 ");
        writeShortDateTimeHuman(weather.updatedEpoch);
        Esp32BaseWeb::sendChunk("</span>");
    }
#else
    Esp32BaseWeb::sendChunk("<span>天气时间未确认</span>");
#endif
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
        Esp32BaseWeb::sendChunk("<div class='irr-plan-item'><div><span class='irr-plan-time'>无启用计划</span><em class='irr-plan-note'>这一路今天不会自动浇水</em></div><span class='irr-plan-status pending'>待机</span></div>");
    }
    for (uint8_t i = 0; i < count; ++i) {
        const TodayPlanDisplay& item = plans[i];
        const char* primary = todayPlanPrimaryStatus(item.observation, item.running, true);
        const char* reason = todayPlanReason(item.observation);
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

void writeZoneMetric(const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<div class='irr-zone-metric'><b>");
    Esp32BaseWeb::writeHtmlEscaped(value);
    Esp32BaseWeb::sendChunk("</b><span>");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk("</span></div>");
}

void writeZoneOverviewRow(uint8_t zoneId, const Irrigation::ZoneStatus& status, uint32_t ymd) {
    const Irrigation::ZoneConfig& config = ZoneManager::config(zoneId);
    char remaining[24];
    char flow[24];
    char volume[24];
    snprintf(remaining, sizeof(remaining), "%lus", static_cast<unsigned long>(status.remainingSec));
    if (status.busy && status.flowRateReady) {
        snprintf(flow, sizeof(flow), "%lu.%03lu", static_cast<unsigned long>(status.flowMlPerMin / 1000UL), static_cast<unsigned long>(status.flowMlPerMin % 1000UL));
    } else {
        strlcpy(flow, "-", sizeof(flow));
    }
    snprintf(volume, sizeof(volume), "%lu.%03lu", static_cast<unsigned long>(status.estimatedMilliliters / 1000UL), static_cast<unsigned long>(status.estimatedMilliliters % 1000UL));

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
    writeZoneMetric("剩余时间", remaining);
    writeZoneMetric("L/min 当前流速", flow);
    writeZoneMetric("L 估算水量", volume);
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

- [ ] **Step 5: Replace the table loop**

In `handleOverviewPage()`, replace the `<table class='part'>` block with:

```cpp
writeWeatherStrip();

Esp32BaseWeb::beginPanel("水路状态");
writeOverviewStyles();
Esp32BaseWeb::sendChunk("<div class='irr-zone-rows'>");
for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
    const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
    if (!status.enabled) continue;
    writeZoneOverviewRow(zoneId, status, currentYmd());
}
Esp32BaseWeb::sendChunk("</div>");
```

- [ ] **Step 6: Build**

Run:

```bash
pio run
```

Expected: build succeeds.

- [ ] **Step 7: Commit**

```bash
git add src/web/IrrigationWeb.cpp
git commit -m "Render home overview as zone rows"
```

## Task 5: Improve Flow Chart Rendering

**Files:**
- Modify: `src/web/IrrigationWeb.cpp`

- [ ] **Step 1: Replace JS chart draw function**

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

- [ ] **Step 2: Pass history length**

In `irrFlowChart()`, change:

```js
irrFlowChartDraw(id,j.flowHistory||[]);
```

to:

```js
irrFlowChartDraw(id,j.flowHistory||[],j.historyMin||10);
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
git commit -m "Add axes to home flow charts"
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

- [ ] **Step 2: Check route capacity**

Run:

```bash
rg -n "ESP32BASE_WEB_MAX_ROUTES|weatherOk|addRoute" platformio.ini src/web/IrrigationWeb.cpp
```

Expected: route limit is at least one higher than before and weather route registration is present.

- [ ] **Step 3: Source safety scan**

Run:

```bash
rg -n "LittleFS|WebServer|server\\.|ESP\\.restart|esp_deep_sleep" src
```

Expected: no new direct base-library bypasses were introduced.

- [ ] **Step 4: Manual browser/hardware caveat**

Record in final handoff:

```text
PlatformIO build passed. Web layout has not been verified on physical ESP32 hardware unless a device test is explicitly run. Weather publishing API is implemented as display-only and does not alter schedules or valves.
```

- [ ] **Step 5: Commit any verification fixes**

If verification required fixes:

```bash
git add src/web/IrrigationWeb.cpp platformio.ini src/storage/WeatherSnapshotStore.h src/storage/WeatherSnapshotStore.cpp
git commit -m "Fix home overview verification issues"
```

If no fixes were required, do not create an empty commit.
