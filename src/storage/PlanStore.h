#pragma once

#include <stdint.h>

#include "Pins.h"
#include "storage/SettingsStore.h"

namespace PlanStore {

static constexpr uint8_t MaxPlansPerRoad = 6;
static constexpr uint8_t TotalPlans = IrrigationPins::MaxRoads * MaxPlansPerRoad;
static constexpr uint8_t MaxPlans = TotalPlans;
static constexpr uint32_t DefaultCycleStartYmd = 20260101UL;

struct Plan {
    bool exists;
    uint8_t roadId;
    uint8_t slotIndex;
    bool enabled;
    uint16_t minuteOfDay;
    uint16_t durationSec;
    uint8_t cycleDays;
    uint32_t cycleMask;
    uint32_t cycleStartYmd;
    uint32_t lastRunYmd;
};

void begin();
const Plan& get(uint8_t index);
const Plan& get(uint8_t road, uint8_t slot);
bool clear();
bool create(uint8_t road, uint8_t* index);
bool remove(uint8_t index);
bool set(uint8_t index, const Plan& plan);
bool set(uint8_t road, uint8_t slot, const Plan& plan);
bool setLastRunYmd(uint8_t index, uint32_t ymd);
bool setLastRunYmd(uint8_t road, uint8_t slot, uint32_t ymd);
bool flatIndex(uint8_t road, uint8_t slot, uint8_t* index);
uint8_t countForRoad(uint8_t road);
uint8_t count();
bool validate(const Plan& plan);
bool shouldRunOnDate(const Plan& plan, uint32_t ymd);

}
