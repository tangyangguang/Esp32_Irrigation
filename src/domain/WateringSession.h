#pragma once

#include <stdint.h>

#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace WateringSession {

enum RoadState : uint8_t {
    ROAD_IDLE = 0,
    ROAD_PENDING,
    ROAD_RUNNING,
    ROAD_DONE,
    ROAD_STOPPED,
    ROAD_ERROR,
};

enum StopReason : uint8_t {
    REASON_NONE = 0,
    REASON_COMPLETED,
    REASON_MANUAL_STOP,
    REASON_EMERGENCY_STOP,
    REASON_REPLACED,
    REASON_ERROR,
    REASON_SKIPPED,
};

struct RoadStatus {
    RoadState state;
    uint16_t targetSec;
    uint32_t startedPulseCount;
    uint32_t lastPulseCount;
    uint32_t lastPulseMs;
    uint32_t startedMs;
    uint32_t endedMs;
};

void begin();
void handle();

bool startManual(uint16_t road1Sec, uint16_t road2Sec, SettingsStore::ExecutionMode mode, RecordStore::Source source, const char* reason);
void stopAll(StopReason reason, const char* textReason);
bool stopRoad(uint8_t road, StopReason reason, const char* textReason);

bool isActive();
RecordStore::Source source();
SettingsStore::ExecutionMode mode();
const RoadStatus& roadStatus(uint8_t road);
const char* roadStateName(RoadState state);
const char* stopReasonName(StopReason reason);
StopReason lastStopReason();

}
