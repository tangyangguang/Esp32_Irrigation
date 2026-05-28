#pragma once

#include <stdint.h>

#include "storage/RecordStore.h"

namespace WateringSession {

enum RoadState : uint8_t {
    ROAD_DISABLED = 0,
    ROAD_IDLE,
    ROAD_STARTING,
    ROAD_RUNNING,
    ROAD_DONE,
    ROAD_STOPPED,
    ROAD_ERROR,
};

struct RoadStatus {
    RoadState state;
    RecordStore::TaskType taskType;
    RecordStore::TriggerSource startSource;
    RecordStore::TriggerSource stopSource;
    RecordStore::StopScope stopScope;
    RecordStore::Result result;
    uint8_t planSlot;
    uint16_t targetSec;
    uint32_t startedPulseCount;
    uint32_t lastPulseCount;
    uint32_t lastPulseMs;
    uint32_t startedMs;
    uint32_t endedMs;
};

void begin();
void handle();

bool startRoadTask(uint8_t road, uint16_t targetSec, RecordStore::TaskType taskType, RecordStore::TriggerSource startSource, uint8_t planSlot, const char* reason);
bool stopRoad(uint8_t road, RecordStore::TriggerSource stopSource, const char* reason);
void stopAll(RecordStore::TriggerSource stopSource, RecordStore::Result result, const char* reason);

bool isActive();
bool isRoadActive(uint8_t road);
const RoadStatus& roadStatus(uint8_t road);
const char* roadStateName(RoadState state);

}
