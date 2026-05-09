#include "storage/PlanStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

static constexpr const char* kNamespace = "irr_plan";
static constexpr uint32_t kDefaultCycleStartYmd = 20260101UL;

PlanStore::Plan g_plans[PlanStore::MaxPlans] = {};

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

SettingsStore::ExecutionMode clampMode(int32_t value) {
    return value == SettingsStore::MODE_SEQUENTIAL
        ? SettingsStore::MODE_SEQUENTIAL
        : SettingsStore::MODE_SIMULTANEOUS;
}

PlanStore::Plan defaultPlan() {
    PlanStore::Plan plan = {};
    plan.enabled = false;
    plan.minuteOfDay = 7 * 60;
    plan.roadSec[0] = 300;
    plan.roadSec[1] = 0;
    plan.mode = SettingsStore::MODE_SIMULTANEOUS;
    plan.cycleDays = 1;
    plan.cycleMask = 0x01;
    plan.cycleStartYmd = kDefaultCycleStartYmd;
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

void begin() {
    for (uint8_t i = 0; i < MaxPlans; ++i) {
        Plan plan = defaultPlan();
        plan.enabled = getInt(i, "en", 0) == 1;
        int32_t minute = getInt(i, "min", plan.minuteOfDay);
        plan.minuteOfDay = minute >= 0 && minute < 1440 ? static_cast<uint16_t>(minute) : 7 * 60;
        plan.roadSec[0] = clampDuration(getInt(i, "r1", plan.roadSec[0]));
        plan.roadSec[1] = clampDuration(getInt(i, "r2", plan.roadSec[1]));
        plan.mode = clampMode(getInt(i, "mode", SettingsStore::MODE_SIMULTANEOUS));
        plan.cycleDays = clampCycleDays(getInt(i, "cycle_d", plan.cycleDays));
        plan.cycleMask = clampCycleMask(getInt(i, "cycle_m", plan.cycleMask), plan.cycleDays);
        plan.cycleStartYmd = static_cast<uint32_t>(getInt(i, "cycle_s", kDefaultCycleStartYmd));
        plan.lastRunYmd = static_cast<uint32_t>(getInt(i, "last", 0));
        g_plans[i] = validate(plan) ? plan : defaultPlan();
    }
    ESP32BASE_LOG_I("plans", "loaded max=%u", static_cast<unsigned>(MaxPlans));
}

const Plan& get(uint8_t index) {
    static Plan invalid = defaultPlan();
    if (index >= MaxPlans) {
        return invalid;
    }
    return g_plans[index];
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    for (uint8_t i = 0; i < MaxPlans; ++i) {
        g_plans[i] = defaultPlan();
    }
    ESP32BASE_LOG_W("plans", "cleared to defaults");
    return true;
}

bool set(uint8_t index, const Plan& plan) {
    if (index >= MaxPlans || !validate(plan)) {
        return false;
    }
    const bool ok = setInt(index, "en", plan.enabled ? 1 : 0) &&
                    setInt(index, "min", plan.minuteOfDay) &&
                    setInt(index, "r1", plan.roadSec[0]) &&
                    setInt(index, "r2", plan.roadSec[1]) &&
                    setInt(index, "mode", static_cast<int32_t>(plan.mode)) &&
                    setInt(index, "cycle_d", plan.cycleDays) &&
                    setInt(index, "cycle_m", static_cast<int32_t>(plan.cycleMask)) &&
                    setInt(index, "cycle_s", static_cast<int32_t>(plan.cycleStartYmd)) &&
                    setInt(index, "last", static_cast<int32_t>(plan.lastRunYmd));
    if (ok) {
        g_plans[index] = plan;
    }
    return ok;
}

bool setLastRunYmd(uint8_t index, uint32_t ymd) {
    if (index >= MaxPlans) {
        return false;
    }
    if (!setInt(index, "last", static_cast<int32_t>(ymd))) {
        return false;
    }
    g_plans[index].lastRunYmd = ymd;
    return true;
}

bool validate(const Plan& plan) {
    time_t ignored = 0;
    if (plan.minuteOfDay >= 1440 || plan.cycleDays < 1 || plan.cycleDays > 30 || plan.cycleMask == 0 ||
        (plan.cycleMask & ~validCycleMask(plan.cycleDays)) != 0 || !ymdToTime(plan.cycleStartYmd, &ignored)) {
        return false;
    }
    if (plan.mode != SettingsStore::MODE_SIMULTANEOUS && plan.mode != SettingsStore::MODE_SEQUENTIAL) {
        return false;
    }
    bool any = false;
    for (uint8_t i = 0; i < 2; ++i) {
        if (plan.roadSec[i] > 14400) {
            return false;
        }
        if (plan.roadSec[i] > 0) {
            any = true;
        }
    }
    return any || !plan.enabled;
}

bool shouldRunOnDate(const Plan& plan, uint32_t ymd) {
    time_t start = 0;
    time_t target = 0;
    if (!plan.enabled || !ymdToTime(plan.cycleStartYmd, &start) || !ymdToTime(ymd, &target) || target < start) {
        return false;
    }
    const uint32_t days = static_cast<uint32_t>((target - start) / 86400L);
    const uint8_t dayInCycle = static_cast<uint8_t>(days % plan.cycleDays);
    return (plan.cycleMask & (1UL << dayInCycle)) != 0;
}

}
