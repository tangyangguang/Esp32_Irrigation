#include "domain/WateringSession.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"
#include "domain/FlowMeter.h"
#include "domain/ValveController.h"
#include "storage/EventStore.h"
#include "storage/SettingsStore.h"

namespace {

static constexpr uint32_t kStartupFlowGraceMs = 3000UL;
static constexpr uint8_t kNoPlanSlot = 0xFF;

WateringSession::RoadStatus g_roads[IrrigationPins::MaxRoads] = {};

bool roadIndex(uint8_t road, uint8_t* index) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    *index = road - 1;
    return true;
}

EventStore::Source eventSource(RecordStore::TriggerSource source) {
    switch (source) {
        case RecordStore::SOURCE_LOCAL_BUTTON: return EventStore::SOURCE_BUTTON;
        case RecordStore::SOURCE_WEB_PAGE:
        case RecordStore::SOURCE_HTTP_API: return EventStore::SOURCE_WEB;
        case RecordStore::SOURCE_PLAN_SCHEDULER: return EventStore::SOURCE_PLAN;
        case RecordStore::SOURCE_UNKNOWN:
        default: return EventStore::SOURCE_SYSTEM;
    }
}

void resetRoad(uint8_t index) {
    g_roads[index] = {};
    g_roads[index].state = SettingsStore::isRoadEnabled(index + 1)
        ? WateringSession::ROAD_IDLE
        : WateringSession::ROAD_DISABLED;
    g_roads[index].planSlot = kNoPlanSlot;
}

void appendRecord(uint8_t index) {
    const uint8_t road = index + 1;
    RecordStore::Record record = {};
    record.roadId = road;
    record.taskType = static_cast<uint8_t>(g_roads[index].taskType);
    record.startSource = static_cast<uint8_t>(g_roads[index].startSource);
    record.stopSource = static_cast<uint8_t>(g_roads[index].stopSource);
    record.stopScope = static_cast<uint8_t>(g_roads[index].stopScope);
    record.result = static_cast<uint8_t>(g_roads[index].result);
    record.planSlot = g_roads[index].planSlot;
    record.enabledRoads = SettingsStore::enabledRoads();
    record.targetSec = g_roads[index].targetSec;
    record.pulsePerLiter = SettingsStore::current().roads[index].pulsePerLiter;
    record.calibrationX1000 = SettingsStore::current().roads[index].calibrationX1000;
    record.flowNoPulseTimeoutSec = SettingsStore::current().flowNoPulseTimeoutSec;
    record.startedMs = g_roads[index].startedMs;
    record.endedMs = g_roads[index].endedMs;
    record.startedPulseCount = g_roads[index].startedPulseCount;
    record.endedPulseCount = g_roads[index].lastPulseCount;
    const uint32_t pulses = g_roads[index].lastPulseCount >= g_roads[index].startedPulseCount
        ? g_roads[index].lastPulseCount - g_roads[index].startedPulseCount
        : 0;
    record.estimatedMilliliters = SettingsStore::estimateMilliliters(road, pulses);
    if (!RecordStore::append(record)) {
        ESP32BASE_LOG_W("water", "record append failed road=%u", static_cast<unsigned>(road));
    }
}

void finishRoad(uint8_t index, WateringSession::RoadState state, RecordStore::TriggerSource stopSource, RecordStore::StopScope scope, RecordStore::Result result, const char* reason) {
    const uint8_t road = index + 1;
    const uint32_t pulses = FlowMeter::pulseCount(road);
    g_roads[index].lastPulseCount = pulses;
    if (g_roads[index].state == WateringSession::ROAD_RUNNING || g_roads[index].state == WateringSession::ROAD_STARTING) {
        ValveController::off(road, reason);
    }
    g_roads[index].state = state;
    g_roads[index].endedMs = millis();
    g_roads[index].stopSource = stopSource;
    g_roads[index].stopScope = scope;
    g_roads[index].result = result;
    appendRecord(index);
    const uint16_t targetSec = g_roads[index].targetSec;
    const int32_t pulseDelta = g_roads[index].lastPulseCount >= g_roads[index].startedPulseCount
        ? static_cast<int32_t>(g_roads[index].lastPulseCount - g_roads[index].startedPulseCount)
        : 0;
    (void)EventStore::append(result == RecordStore::RESULT_FLOW_ERROR_STOPPED ? EventStore::TYPE_WATER_ERROR : EventStore::TYPE_WATER_STOP,
                             eventSource(stopSource),
                             road,
                             static_cast<uint8_t>(result),
                             targetSec,
                             pulseDelta,
                             reason);
    resetRoad(index);
}

}

namespace WateringSession {

void begin() {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        resetRoad(i);
    }
}

void handle() {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        RoadStatus& road = g_roads[i];
        if (road.state != ROAD_RUNNING) {
            if (road.state == ROAD_DISABLED && SettingsStore::isRoadEnabled(i + 1)) {
                resetRoad(i);
            } else if (road.state == ROAD_IDLE && !SettingsStore::isRoadEnabled(i + 1)) {
                resetRoad(i);
            }
            continue;
        }
        const uint32_t pulses = FlowMeter::pulseCount(i + 1);
        if (pulses != road.lastPulseCount) {
            road.lastPulseCount = pulses;
            road.lastPulseMs = now;
        }
        const uint32_t elapsedMs = now - road.startedMs;
        const uint32_t noPulseMs = now - road.lastPulseMs;
        const uint32_t timeoutMs = static_cast<uint32_t>(SettingsStore::current().flowNoPulseTimeoutSec) * 1000UL;
        const bool noPulseSinceStart = pulses == road.startedPulseCount;
        const uint32_t effectiveTimeoutMs = noPulseSinceStart && timeoutMs < kStartupFlowGraceMs ? kStartupFlowGraceMs : timeoutMs;
        if (noPulseMs >= effectiveTimeoutMs) {
            finishRoad(i, ROAD_ERROR, RecordStore::SOURCE_FLOW_ERROR, RecordStore::SCOPE_ROAD, RecordStore::RESULT_FLOW_ERROR_STOPPED, "flow no pulse");
            ESP32BASE_LOG_W("water", "flow timeout road=%u timeout=%u", static_cast<unsigned>(i + 1), static_cast<unsigned>(SettingsStore::current().flowNoPulseTimeoutSec));
            continue;
        }
        if (elapsedMs >= static_cast<uint32_t>(road.targetSec) * 1000UL) {
            finishRoad(i, ROAD_DONE, RecordStore::SOURCE_DURATION_REACHED, RecordStore::SCOPE_ROAD, RecordStore::RESULT_COMPLETED, "duration complete");
        }
    }
}

bool startRoadTask(uint8_t road, uint16_t targetSec, RecordStore::TaskType taskType, RecordStore::TriggerSource startSource, uint8_t planSlot, const char* reason) {
    uint8_t index = 0;
    if (!roadIndex(road, &index) || !SettingsStore::isRoadEnabled(road) || targetSec < 1 || targetSec > 14400) {
        ESP32BASE_LOG_W("water", "start rejected road=%u sec=%u", static_cast<unsigned>(road), static_cast<unsigned>(targetSec));
        return false;
    }
    if (g_roads[index].state == ROAD_DISABLED) {
        resetRoad(index);
    }
    if (g_roads[index].state != ROAD_IDLE) {
        ESP32BASE_LOG_W("water", "start rejected road=%u sec=%u", static_cast<unsigned>(road), static_cast<unsigned>(targetSec));
        return false;
    }

    g_roads[index] = {};
    g_roads[index].state = ROAD_RUNNING;
    g_roads[index].taskType = taskType;
    g_roads[index].startSource = startSource;
    g_roads[index].stopSource = RecordStore::SOURCE_UNKNOWN;
    g_roads[index].stopScope = RecordStore::SCOPE_NONE;
    g_roads[index].result = RecordStore::RESULT_NONE;
    g_roads[index].planSlot = planSlot;
    g_roads[index].targetSec = targetSec;
    g_roads[index].startedMs = millis();
    g_roads[index].startedPulseCount = FlowMeter::pulseCount(road);
    g_roads[index].lastPulseCount = g_roads[index].startedPulseCount;
    g_roads[index].lastPulseMs = g_roads[index].startedMs;
    ValveController::setRoad(road, true, reason);
    (void)EventStore::append(EventStore::TYPE_WATER_START,
                             eventSource(startSource),
                             road,
                             static_cast<uint8_t>(taskType),
                             targetSec,
                             planSlot,
                             reason);
    return true;
}

bool stopRoad(uint8_t road, RecordStore::TriggerSource stopSource, const char* reason) {
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        return false;
    }
    if (g_roads[index].state == ROAD_RUNNING || g_roads[index].state == ROAD_STARTING) {
        finishRoad(index, ROAD_STOPPED, stopSource, RecordStore::SCOPE_ROAD, RecordStore::RESULT_USER_STOPPED, reason);
        return true;
    }
    ValveController::off(road, reason);
    return false;
}

void stopAll(RecordStore::TriggerSource stopSource, RecordStore::Result result, const char* reason) {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_roads[i].state == ROAD_RUNNING || g_roads[i].state == ROAD_STARTING) {
            finishRoad(i, ROAD_STOPPED, stopSource, RecordStore::SCOPE_ALL, result, reason);
        } else {
            ValveController::off(i + 1, reason);
        }
    }
}

bool isActive() {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (isRoadActive(i + 1)) {
            return true;
        }
    }
    return false;
}

bool isRoadActive(uint8_t road) {
    uint8_t index = 0;
    return roadIndex(road, &index) && (g_roads[index].state == ROAD_RUNNING || g_roads[index].state == ROAD_STARTING);
}

const RoadStatus& roadStatus(uint8_t road) {
    static RoadStatus invalid = {};
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        invalid.state = ROAD_DISABLED;
        return invalid;
    }
    return g_roads[index];
}

const char* roadStateName(RoadState state) {
    switch (state) {
        case ROAD_DISABLED: return "disabled";
        case ROAD_STARTING: return "starting";
        case ROAD_RUNNING: return "running";
        case ROAD_DONE: return "done";
        case ROAD_STOPPED: return "stopped";
        case ROAD_ERROR: return "error";
        case ROAD_IDLE:
        default: return "idle";
    }
}

}
