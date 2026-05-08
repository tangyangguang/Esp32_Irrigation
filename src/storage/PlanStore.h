#pragma once

#include <stdint.h>

#include "storage/SettingsStore.h"

namespace PlanStore {

static constexpr uint8_t MaxPlans = 8;

enum RepeatMode : uint8_t {
    REPEAT_WEEKLY = 0,
    REPEAT_INTERVAL = 1,
};

struct Plan {
    bool enabled;
    uint16_t minuteOfDay;
    uint16_t roadSec[2];
    SettingsStore::ExecutionMode mode;
    RepeatMode repeatMode;
    uint8_t weekMask;
    uint8_t intervalDays;
    uint32_t skipYmd;
    uint32_t lastRunYmd;
};

void begin();
const Plan& get(uint8_t index);
bool clear();
bool set(uint8_t index, const Plan& plan);
bool setLastRunYmd(uint8_t index, uint32_t ymd);
bool setSkipYmd(uint8_t index, uint32_t ymd);
bool clearSkipYmd(uint8_t index);
bool validate(const Plan& plan);
const char* repeatModeName(RepeatMode mode);
bool parseRepeatMode(const char* text, RepeatMode* mode);

}
