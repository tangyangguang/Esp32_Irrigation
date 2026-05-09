#pragma once

#include <stdint.h>

#include "storage/SettingsStore.h"

namespace PlanStore {

static constexpr uint8_t MaxPlans = 8;

struct Plan {
    bool enabled;
    uint16_t minuteOfDay;
    uint16_t roadSec[2];
    SettingsStore::ExecutionMode mode;
    uint8_t cycleDays;
    uint32_t cycleMask;
    uint32_t cycleStartYmd;
    uint32_t lastRunYmd;
};

void begin();
const Plan& get(uint8_t index);
bool clear();
bool set(uint8_t index, const Plan& plan);
bool setLastRunYmd(uint8_t index, uint32_t ymd);
bool validate(const Plan& plan);
bool shouldRunOnDate(const Plan& plan, uint32_t ymd);

}
