#include "storage/PlanStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <time.h>

namespace {

static constexpr const char* kNamespace = "irr_plan";
PlanStore::Plan g_plans[PlanStore::TotalPlans] = {};

void key(char* out, size_t len, uint8_t index, const char* name) {
    snprintf(out, len, "p%u_%s", static_cast<unsigned>(index), name);
}

int32_t getInt(uint8_t index, const char* name, int32_t def) {
    char k[16];
    key(k, sizeof(k), index, name);
    return Esp32BaseConfig::getInt(kNamespace, k, def);
}

bool setInt(uint8_t index, const char* name, int32_t value) {
    char k[16];
    key(k, sizeof(k), index, name);
    return Esp32BaseConfig::setInt(kNamespace, k, value);
}

uint16_t clampDuration(int32_t value) {
    if (value < 0 || value > 14400) {
        return 0;
    }
    return static_cast<uint16_t>(value);
}

PlanStore::Plan defaultPlan(uint8_t road, uint8_t slot) {
    PlanStore::Plan plan = {};
    plan.exists = false;
    plan.roadId = road;
    plan.slotIndex = slot;
    plan.enabled = false;
    plan.minuteOfDay = 7 * 60;
    plan.durationSec = 300;
    plan.cycleDays = 1;
    plan.cycleMask = 0x01;
    plan.cycleStartYmd = PlanStore::DefaultCycleStartYmd;
    return plan;
}

uint8_t clampCycleDays(int32_t value) {
    if (value < 1 || value > 30) {
        return 1;
    }
    return static_cast<uint8_t>(value);
}

uint32_t validCycleMask(uint8_t days) {
    return days >= 32 ? 0xFFFFFFFFUL : ((1UL << days) - 1UL);
}

uint32_t clampCycleMask(int32_t value, uint8_t days) {
    const uint32_t allowed = validCycleMask(days);
    const uint32_t mask = static_cast<uint32_t>(value) & allowed;
    return mask == 0 ? 0x01 : mask;
}

bool ymdToTime(uint32_t ymd, time_t* out) {
    if (!out || ymd < 20000101UL) {
        return false;
    }
    tm value = {};
    value.tm_year = static_cast<int>(ymd / 10000UL) - 1900;
    value.tm_mon = static_cast<int>((ymd / 100UL) % 100UL) - 1;
    value.tm_mday = static_cast<int>(ymd % 100UL);
    *out = mktime(&value);
    if (*out <= 0) {
        return false;
    }
    tm normalized = {};
    if (localtime_r(out, &normalized) == nullptr) {
        return false;
    }
    return normalized.tm_year == static_cast<int>(ymd / 10000UL) - 1900 &&
           normalized.tm_mon == static_cast<int>((ymd / 100UL) % 100UL) - 1 &&
           normalized.tm_mday == static_cast<int>(ymd % 100UL);
}

}

namespace PlanStore {

bool flatIndex(uint8_t road, uint8_t slot, uint8_t* index) {
    if (!index || road < 1 || road > IrrigationPins::MaxRoads || slot >= MaxPlansPerRoad) {
        return false;
    }
    *index = static_cast<uint8_t>((road - 1) * MaxPlansPerRoad + slot);
    return true;
}

void begin() {
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        for (uint8_t slot = 0; slot < MaxPlansPerRoad; ++slot) {
            uint8_t i = 0;
            (void)flatIndex(road, slot, &i);
            Plan plan = defaultPlan(road, slot);
            plan.exists = getInt(i, "exists", 0) == 1;
            plan.enabled = getInt(i, "en", 0) == 1;
            int32_t minute = getInt(i, "min", plan.minuteOfDay);
            plan.minuteOfDay = minute >= 0 && minute < 1440 ? static_cast<uint16_t>(minute) : 7 * 60;
            plan.durationSec = clampDuration(getInt(i, "dur", plan.durationSec));
            plan.cycleDays = clampCycleDays(getInt(i, "cycle_d", plan.cycleDays));
            plan.cycleMask = clampCycleMask(getInt(i, "cycle_m", plan.cycleMask), plan.cycleDays);
            plan.cycleStartYmd = static_cast<uint32_t>(getInt(i, "cycle_s", DefaultCycleStartYmd));
            plan.lastRunYmd = static_cast<uint32_t>(getInt(i, "last", 0));
            g_plans[i] = validate(plan) ? plan : defaultPlan(road, slot);
        }
    }
    ESP32BASE_LOG_I("plans", "loaded roads=%u perRoad=%u total=%u",
                    static_cast<unsigned>(IrrigationPins::MaxRoads),
                    static_cast<unsigned>(MaxPlansPerRoad),
                    static_cast<unsigned>(TotalPlans));
}

const Plan& get(uint8_t index) {
    static Plan invalid = defaultPlan(1, 0);
    if (index >= TotalPlans) {
        return invalid;
    }
    return g_plans[index];
}

const Plan& get(uint8_t road, uint8_t slot) {
    uint8_t index = 0;
    if (!flatIndex(road, slot, &index)) {
        static Plan invalid = defaultPlan(1, 0);
        return invalid;
    }
    return g_plans[index];
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        for (uint8_t slot = 0; slot < MaxPlansPerRoad; ++slot) {
            uint8_t index = 0;
            (void)flatIndex(road, slot, &index);
            g_plans[index] = defaultPlan(road, slot);
        }
    }
    ESP32BASE_LOG_W("plans", "cleared to defaults");
    return true;
}

bool create(uint8_t road, uint8_t* index) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    for (uint8_t slot = 0; slot < MaxPlansPerRoad; ++slot) {
        uint8_t i = 0;
        (void)flatIndex(road, slot, &i);
        if (g_plans[i].exists) {
            continue;
        }
        Plan plan = defaultPlan(road, slot);
        plan.exists = true;
        if (!set(i, plan)) {
            return false;
        }
        if (index) {
            *index = i;
        }
        return true;
    }
    return false;
}

bool remove(uint8_t index) {
    if (index >= TotalPlans) {
        return false;
    }
    const uint8_t road = g_plans[index].roadId;
    const uint8_t slot = g_plans[index].slotIndex;
    Plan plan = defaultPlan(road, slot);
    return set(index, plan);
}

bool set(uint8_t index, const Plan& plan) {
    if (index >= TotalPlans || !validate(plan)) {
        return false;
    }
    const bool ok = setInt(index, "exists", plan.exists ? 1 : 0) &&
                    setInt(index, "en", plan.enabled ? 1 : 0) &&
                    setInt(index, "min", plan.minuteOfDay) &&
                    setInt(index, "dur", plan.durationSec) &&
                    setInt(index, "cycle_d", plan.cycleDays) &&
                    setInt(index, "cycle_m", static_cast<int32_t>(plan.cycleMask)) &&
                    setInt(index, "cycle_s", static_cast<int32_t>(plan.cycleStartYmd)) &&
                    setInt(index, "last", static_cast<int32_t>(plan.lastRunYmd));
    if (ok) {
        g_plans[index] = plan;
    }
    return ok;
}

bool set(uint8_t road, uint8_t slot, const Plan& plan) {
    uint8_t index = 0;
    return flatIndex(road, slot, &index) && set(index, plan);
}

bool setLastRunYmd(uint8_t index, uint32_t ymd) {
    if (index >= TotalPlans || !g_plans[index].exists) {
        return false;
    }
    if (!setInt(index, "last", static_cast<int32_t>(ymd))) {
        return false;
    }
    g_plans[index].lastRunYmd = ymd;
    return true;
}

bool setLastRunYmd(uint8_t road, uint8_t slot, uint32_t ymd) {
    uint8_t index = 0;
    return flatIndex(road, slot, &index) && setLastRunYmd(index, ymd);
}

uint8_t countForRoad(uint8_t road) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return 0;
    }
    uint8_t total = 0;
    for (uint8_t slot = 0; slot < MaxPlansPerRoad; ++slot) {
        uint8_t index = 0;
        (void)flatIndex(road, slot, &index);
        if (g_plans[index].exists) {
            ++total;
        }
    }
    return total;
}

uint8_t count() {
    uint8_t total = 0;
    for (uint8_t i = 0; i < TotalPlans; ++i) {
        if (g_plans[i].exists) {
            ++total;
        }
    }
    return total;
}

bool validate(const Plan& plan) {
    time_t ignored = 0;
    if (!plan.exists && plan.enabled) {
        return false;
    }
    if (plan.roadId < 1 || plan.roadId > IrrigationPins::MaxRoads || plan.slotIndex >= MaxPlansPerRoad ||
        plan.minuteOfDay >= 1440 || plan.cycleDays < 1 || plan.cycleDays > 30 || plan.cycleMask == 0 ||
        (plan.cycleMask & ~validCycleMask(plan.cycleDays)) != 0 || !ymdToTime(plan.cycleStartYmd, &ignored)) {
        return false;
    }
    if (plan.durationSec > 14400) {
        return false;
    }
    return plan.durationSec > 0 || !plan.enabled;
}

bool shouldRunOnDate(const Plan& plan, uint32_t ymd) {
    time_t start = 0;
    time_t target = 0;
    if (!plan.exists || !plan.enabled || !ymdToTime(plan.cycleStartYmd, &start) || !ymdToTime(ymd, &target) || target < start) {
        return false;
    }
    const uint32_t days = static_cast<uint32_t>((target - start) / 86400L);
    const uint8_t dayInCycle = static_cast<uint8_t>(days % plan.cycleDays);
    return (plan.cycleMask & (1UL << dayInCycle)) != 0;
}

}
