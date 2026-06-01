#pragma once

#include <stdint.h>

#include "domain/ZoneTypes.h"

namespace RecordStore {

static constexpr uint16_t Capacity = 256;

struct WateringRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t recordId;
    uint8_t zoneId;
    uint8_t taskType;
    uint8_t startSource;
    uint8_t stopSource;
    uint8_t stopScope;
    uint8_t result;
    uint16_t reserved0;
    uint32_t planId;
    char planNameSnapshot[Irrigation::NameMaxBytes];
    uint32_t targetSec;
    uint32_t startedEpoch;
    uint32_t endedEpoch;
    uint32_t startedUptimeMs;
    uint32_t endedUptimeMs;
    uint32_t startedPulseCount;
    uint32_t endedPulseCount;
    uint32_t estimatedMilliliters;
    uint16_t flowRateWindowSec;
    bool flowStatsValid;
    uint8_t reserved1;
    uint32_t maxFlowMlPerMin;
    uint32_t maxFlowFirstAtSec;
    uint32_t minFlowMlPerMin;
    uint32_t minFlowFirstAtSec;
    Irrigation::ZoneConfigSnapshot configSnapshot; // includes startTimeoutSec and flowNoPulseTimeoutSec
    uint32_t commitMagic;
    uint32_t crc32;
};

static_assert(sizeof(WateringRecord) == 128, "RecordStore::WateringRecord binary layout changed");

using ReadCallback = void (*)(const WateringRecord& record, void* user);

void begin();
bool append(const WateringRecord& record);
bool clear();
uint16_t count();
uint16_t capacity();
uint32_t nextId();
bool readLatest(uint16_t offset, uint16_t limit, ReadCallback callback, void* user);

}
