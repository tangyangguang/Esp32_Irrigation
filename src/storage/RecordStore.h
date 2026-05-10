#pragma once

#include <stdint.h>

#include "storage/SettingsStore.h"

namespace RecordStore {

static constexpr uint8_t RoadCount = 2;
static constexpr uint16_t Capacity = 256;

enum Source : uint8_t {
    SOURCE_UNKNOWN = 0,
    SOURCE_BUTTON = 1,
    SOURCE_WEB = 2,
    SOURCE_PLAN = 3,
};

struct RoadRecord {
    uint8_t state;
    uint16_t targetSec;
    uint16_t pulsePerLiter;
    uint16_t calibrationX1000;
    uint32_t startedMs;
    uint32_t endedMs;
    uint32_t startedPulseCount;
    uint32_t endedPulseCount;
    uint32_t estimatedMilliliters;
};

struct Record {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t id;
    uint32_t sessionStartedMs;
    uint32_t sessionEndedMs;
    uint8_t source;
    uint8_t mode;
    uint8_t stopReason;
    uint8_t enabledRoads;
    uint8_t flowNoPulseTimeoutSec;
    uint8_t reserved[7];
    RoadRecord roads[RoadCount];
};

static_assert(sizeof(RoadRecord) == 28, "RecordStore::RoadRecord binary layout changed");
static_assert(sizeof(Record) == 88, "RecordStore::Record binary layout changed");

using ReadCallback = void (*)(const Record& record, void* user);

void begin();
bool append(const Record& record);
bool clear();
uint16_t count();
uint16_t capacity();
uint32_t nextId();
bool readLatest(uint16_t offset, uint16_t limit, ReadCallback callback, void* user);

const char* sourceName(Source source);

}
