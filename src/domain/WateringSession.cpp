#include "domain/WateringSession.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"
#include "domain/FlowMeter.h"
#include "domain/ValveController.h"
#include "storage/EventStore.h"

namespace {

static constexpr uint32_t kStartupFlowGraceMs = 3000UL;

struct SessionState {
    bool active;
    SettingsStore::ExecutionMode mode;
    RecordStore::Source source;
    WateringSession::RoadStatus roads[2];
    WateringSession::StopReason lastStopReason;
    uint32_t startedMs;
    uint32_t endedMs;
};

SessionState g_session = {};

bool roadIndex(uint8_t road, uint8_t* index) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    *index = road - 1;
    return true;
}

bool hasRemainingWork() {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_session.roads[i].state == WateringSession::ROAD_PENDING ||
            g_session.roads[i].state == WateringSession::ROAD_RUNNING) {
            return true;
        }
    }
    return false;
}

EventStore::Source eventSource(RecordStore::Source source) {
    switch (source) {
        case RecordStore::SOURCE_BUTTON: return EventStore::SOURCE_BUTTON;
        case RecordStore::SOURCE_WEB: return EventStore::SOURCE_WEB;
        case RecordStore::SOURCE_PLAN: return EventStore::SOURCE_PLAN;
        case RecordStore::SOURCE_UNKNOWN:
        default: return EventStore::SOURCE_SYSTEM;
    }
}

void startRoadByIndex(uint8_t index, uint32_t now, const char* reason) {
    const uint8_t road = index + 1;
    const uint32_t pulses = FlowMeter::pulseCount(road);
    g_session.roads[index].state = WateringSession::ROAD_RUNNING;
    g_session.roads[index].startedPulseCount = pulses;
    g_session.roads[index].lastPulseCount = pulses;
    g_session.roads[index].lastPulseMs = now;
    g_session.roads[index].startedMs = now;
    g_session.roads[index].endedMs = 0;
    ValveController::setRoad(road, true, reason);
}

void finishRoadByIndex(uint8_t index, WateringSession::RoadState state, const char* reason) {
    const uint8_t road = index + 1;
    const uint32_t pulses = FlowMeter::pulseCount(road);
    g_session.roads[index].lastPulseCount = pulses;
    if (g_session.roads[index].state == WateringSession::ROAD_RUNNING) {
        ValveController::off(road, reason);
    }
    g_session.roads[index].state = state;
    g_session.roads[index].endedMs = millis();
}

WateringSession::StopReason finalReason(WateringSession::StopReason fallback) {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_session.roads[i].state == WateringSession::ROAD_ERROR) {
            return WateringSession::REASON_ERROR;
        }
    }
    return fallback;
}

void appendRecord(WateringSession::StopReason reason) {
    RecordStore::Record record = {};
    record.sessionStartedMs = g_session.startedMs;
    record.sessionEndedMs = g_session.endedMs;
    record.source = static_cast<uint8_t>(g_session.source);
    record.mode = static_cast<uint8_t>(g_session.mode);
    record.stopReason = static_cast<uint8_t>(reason);
    record.enabledRoads = SettingsStore::enabledRoads();
    record.flowNoPulseTimeoutSec = SettingsStore::current().flowNoPulseTimeoutSec;
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        record.roads[i].state = static_cast<uint8_t>(g_session.roads[i].state);
        record.roads[i].targetSec = g_session.roads[i].targetSec;
        record.roads[i].pulsePerLiter = SettingsStore::current().roads[i].pulsePerLiter;
        record.roads[i].calibrationX1000 = SettingsStore::current().roads[i].calibrationX1000;
        record.roads[i].startedMs = g_session.roads[i].startedMs;
        record.roads[i].endedMs = g_session.roads[i].endedMs;
        record.roads[i].startedPulseCount = g_session.roads[i].startedPulseCount;
        record.roads[i].endedPulseCount = g_session.roads[i].lastPulseCount;
        const uint32_t pulses = g_session.roads[i].lastPulseCount >= g_session.roads[i].startedPulseCount
            ? g_session.roads[i].lastPulseCount - g_session.roads[i].startedPulseCount
            : 0;
        record.roads[i].estimatedMilliliters = SettingsStore::estimateMilliliters(i + 1, pulses);
    }
    if (!RecordStore::append(record)) {
        ESP32BASE_LOG_W("water", "record append failed");
    }
}

void maybeFinishSession(WateringSession::StopReason reason) {
    if (!g_session.active || hasRemainingWork()) {
        return;
    }
    reason = finalReason(reason);
    g_session.active = false;
    g_session.endedMs = millis();
    g_session.lastStopReason = reason;
    appendRecord(reason);
    (void)EventStore::append(reason == WateringSession::REASON_ERROR ? EventStore::TYPE_WATER_ERROR : EventStore::TYPE_WATER_STOP,
                             eventSource(g_session.source),
                             0,
                             static_cast<uint8_t>(reason),
                             g_session.roads[0].targetSec,
                             g_session.roads[1].targetSec,
                             WateringSession::stopReasonName(reason));
    ESP32BASE_LOG_I("water", "session ended reason=%s", WateringSession::stopReasonName(reason));
}

void startNextSequential(uint32_t now) {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_session.roads[i].state == WateringSession::ROAD_RUNNING) {
            return;
        }
    }
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_session.roads[i].state == WateringSession::ROAD_PENDING) {
            startRoadByIndex(i, now, "sequential next");
            return;
        }
    }
}

}

namespace WateringSession {

void begin() {
    g_session = {};
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        g_session.roads[i].state = ROAD_IDLE;
    }
}

void handle() {
    if (!g_session.active) {
        return;
    }

    const uint32_t now = millis();
    if (g_session.mode == SettingsStore::MODE_SEQUENTIAL) {
        startNextSequential(now);
    }

    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        RoadStatus& road = g_session.roads[i];
        if (road.state != ROAD_RUNNING) {
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
            finishRoadByIndex(i, ROAD_ERROR, "flow no pulse timeout");
            (void)EventStore::append(EventStore::TYPE_WATER_ERROR,
                                     eventSource(g_session.source),
                                     i + 1,
                                     REASON_ERROR,
                                     static_cast<int32_t>(pulses - road.startedPulseCount),
                                     SettingsStore::current().flowNoPulseTimeoutSec,
                                     "flow no pulse");
            ESP32BASE_LOG_W("water", "flow timeout road=%u timeout=%u pulses=%lu",
                            static_cast<unsigned>(i + 1),
                            static_cast<unsigned>(SettingsStore::current().flowNoPulseTimeoutSec),
                            static_cast<unsigned long>(pulses - road.startedPulseCount));
            continue;
        }
        if (elapsedMs >= static_cast<uint32_t>(road.targetSec) * 1000UL) {
            finishRoadByIndex(i, ROAD_DONE, "duration complete");
        }
    }

    if (g_session.mode == SettingsStore::MODE_SEQUENTIAL) {
        startNextSequential(now);
    }
    maybeFinishSession(REASON_COMPLETED);
}

bool startManual(uint16_t road1Sec, uint16_t road2Sec, SettingsStore::ExecutionMode mode, RecordStore::Source source, const char* reason) {
    const uint16_t requested[2] = {road1Sec, road2Sec};
    bool any = false;

    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (requested[i] == 0) {
            continue;
        }
        if (requested[i] < 1 || requested[i] > 14400) {
            ESP32BASE_LOG_W("water", "invalid duration road=%u sec=%u",
                            static_cast<unsigned>(i + 1),
                            static_cast<unsigned>(requested[i]));
            return false;
        }
        if (!SettingsStore::isRoadEnabled(i + 1)) {
            ESP32BASE_LOG_W("water", "road disabled road=%u", static_cast<unsigned>(i + 1));
            return false;
        }
        any = true;
    }

    if (!any) {
        ESP32BASE_LOG_W("water", "manual start rejected empty request");
        return false;
    }

    if (g_session.active) {
        stopAll(REASON_REPLACED, "manual replaced active session");
    }

    g_session = {};
    g_session.active = true;
    g_session.mode = mode;
    g_session.source = source;
    g_session.startedMs = millis();
    g_session.lastStopReason = REASON_NONE;

    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        g_session.roads[i].targetSec = requested[i];
        g_session.roads[i].startedPulseCount = 0;
        g_session.roads[i].lastPulseCount = 0;
        g_session.roads[i].lastPulseMs = 0;
        g_session.roads[i].startedMs = 0;
        g_session.roads[i].endedMs = 0;
        g_session.roads[i].state = requested[i] > 0 ? ROAD_PENDING : ROAD_IDLE;
    }

    if (g_session.mode == SettingsStore::MODE_SIMULTANEOUS) {
        const uint32_t now = millis();
        for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
            if (g_session.roads[i].state == ROAD_PENDING) {
                startRoadByIndex(i, now, "manual simultaneous");
            }
        }
    } else {
        startNextSequential(millis());
    }

    ESP32BASE_LOG_I("water", "manual session started mode=%s r1=%u r2=%u reason=%s",
                    SettingsStore::executionModeName(g_session.mode),
                    static_cast<unsigned>(road1Sec),
                    static_cast<unsigned>(road2Sec),
                    reason ? reason : "");
    (void)EventStore::append(EventStore::TYPE_WATER_START,
                             eventSource(source),
                             0,
                             static_cast<uint8_t>(mode),
                             road1Sec,
                             road2Sec,
                             reason);
    return true;
}

void stopAll(StopReason reason, const char* textReason) {
    ValveController::allOff(textReason);
    if (!g_session.active) {
        g_session.lastStopReason = reason;
        return;
    }
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_session.roads[i].state == ROAD_RUNNING) {
            const uint32_t pulses = FlowMeter::pulseCount(i + 1);
            g_session.roads[i].lastPulseCount = pulses;
        }
        if (g_session.roads[i].state == ROAD_RUNNING || g_session.roads[i].state == ROAD_PENDING) {
            g_session.roads[i].state = ROAD_STOPPED;
            g_session.roads[i].endedMs = millis();
        }
    }
    g_session.active = false;
    g_session.endedMs = millis();
    g_session.lastStopReason = reason;
    appendRecord(reason);
    ESP32BASE_LOG_I("water", "session stopped reason=%s detail=%s",
                    stopReasonName(reason),
                    textReason ? textReason : "");
    (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                             eventSource(g_session.source),
                             0,
                             static_cast<uint8_t>(reason),
                             g_session.roads[0].targetSec,
                             g_session.roads[1].targetSec,
                             textReason);
}

bool stopRoad(uint8_t road, StopReason reason, const char* textReason) {
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        return false;
    }
    if (!g_session.active) {
        const bool ok = ValveController::off(road, textReason);
        if (ok) {
            (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                                     EventStore::SOURCE_SYSTEM,
                                     road,
                                     static_cast<uint8_t>(reason),
                                     0,
                                     0,
                                     textReason);
        }
        return ok;
    }
    RoadStatus& status = g_session.roads[index];
    if (status.state == ROAD_RUNNING || status.state == ROAD_PENDING) {
        finishRoadByIndex(index, ROAD_STOPPED, textReason);
        ESP32BASE_LOG_I("water", "road stopped road=%u reason=%s",
                        static_cast<unsigned>(road),
                        textReason ? textReason : "");
        (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                                 eventSource(g_session.source),
                                 road,
                                 static_cast<uint8_t>(reason),
                                 road == 1 ? status.targetSec : 0,
                                 road == 2 ? status.targetSec : 0,
                                 textReason);
    } else {
        ValveController::off(road, textReason);
    }
    maybeFinishSession(reason);
    return true;
}

bool isActive() {
    return g_session.active;
}

RecordStore::Source source() {
    return g_session.source;
}

SettingsStore::ExecutionMode mode() {
    return g_session.mode;
}

const RoadStatus& roadStatus(uint8_t road) {
    static RoadStatus invalid = {ROAD_IDLE, 0, 0, 0, 0, 0, 0};
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        return invalid;
    }
    return g_session.roads[index];
}

const char* roadStateName(RoadState state) {
    switch (state) {
        case ROAD_PENDING: return "pending";
        case ROAD_RUNNING: return "running";
        case ROAD_DONE: return "done";
        case ROAD_STOPPED: return "stopped";
        case ROAD_ERROR: return "error";
        case ROAD_IDLE:
        default: return "idle";
    }
}

const char* stopReasonName(StopReason reason) {
    switch (reason) {
        case REASON_COMPLETED: return "completed";
        case REASON_MANUAL_STOP: return "manual_stop";
        case REASON_EMERGENCY_STOP: return "emergency_stop";
        case REASON_REPLACED: return "replaced";
        case REASON_ERROR: return "error";
        case REASON_SKIPPED: return "skipped";
        case REASON_NONE:
        default: return "none";
    }
}

StopReason lastStopReason() {
    return g_session.lastStopReason;
}

}
