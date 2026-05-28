#pragma once

#include <stdint.h>

#include "storage/SettingsStore.h"

namespace RecordStore {

static constexpr uint16_t Capacity = 256;

enum TaskType : uint8_t {
    TASK_MANUAL = 1,
    TASK_PLAN = 2,
};

enum TriggerSource : uint8_t {
    SOURCE_UNKNOWN = 0,
    SOURCE_WEB_PAGE = 1,
    SOURCE_HTTP_API = 2,
    SOURCE_LOCAL_BUTTON = 3,
    SOURCE_PLAN_SCHEDULER = 4,
    SOURCE_DURATION_REACHED = 5,
    SOURCE_FLOW_ERROR = 6,
    SOURCE_LEAK_MONITOR = 7,
    SOURCE_FACTORY_RESET = 8,
};

enum StopScope : uint8_t {
    SCOPE_NONE = 0,
    SCOPE_ROAD = 1,
    SCOPE_ALL = 2,
};

enum Result : uint8_t {
    RESULT_NONE = 0,
    RESULT_COMPLETED = 1,
    RESULT_USER_STOPPED = 2,
    RESULT_FLOW_ERROR_STOPPED = 3,
    RESULT_LEAK_PROTECTED = 4,
    RESULT_FACTORY_RESET_PROTECTED = 5,
};

struct Record {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t id;
    uint8_t roadId;
    uint8_t taskType;
    uint8_t startSource;
    uint8_t stopSource;
    uint8_t stopScope;
    uint8_t result;
    uint8_t planSlot;
    uint8_t enabledRoads;
    uint16_t targetSec;
    uint16_t pulsePerLiter;
    uint16_t calibrationX1000;
    uint8_t flowNoPulseTimeoutSec;
    uint8_t reserved[3];
    uint32_t startedMs;
    uint32_t endedMs;
    uint32_t startedPulseCount;
    uint32_t endedPulseCount;
    uint32_t estimatedMilliliters;
};

static_assert(sizeof(Record) == 52, "RecordStore::Record binary layout changed");

using ReadCallback = void (*)(const Record& record, void* user);

void begin();
bool append(const Record& record);
bool clear();
uint16_t count();
uint16_t capacity();
uint32_t nextId();
bool readLatest(uint16_t offset, uint16_t limit, ReadCallback callback, void* user);

const char* taskTypeName(TaskType type);
const char* triggerSourceName(TriggerSource source);
const char* stopScopeName(StopScope scope);
const char* resultName(Result result);

}
