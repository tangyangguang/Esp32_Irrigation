#include "storage/PlanStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static constexpr const char* kNamespace = "irr_plan";

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

PlanStore::RepeatMode clampRepeatMode(int32_t value) {
    return value == PlanStore::REPEAT_INTERVAL
        ? PlanStore::REPEAT_INTERVAL
        : PlanStore::REPEAT_WEEKLY;
}

PlanStore::Plan defaultPlan() {
    PlanStore::Plan plan = {};
    plan.enabled = false;
    plan.minuteOfDay = 7 * 60;
    plan.roadSec[0] = 300;
    plan.roadSec[1] = 0;
    plan.mode = SettingsStore::MODE_SIMULTANEOUS;
    plan.repeatMode = PlanStore::REPEAT_WEEKLY;
    plan.weekMask = 0x7F;
    plan.intervalDays = 1;
    return plan;
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
        plan.repeatMode = clampRepeatMode(getInt(i, "rep", REPEAT_WEEKLY));
        int32_t mask = getInt(i, "week", plan.weekMask);
        plan.weekMask = mask >= 0 && mask <= 0x7F ? static_cast<uint8_t>(mask) : 0x7F;
        int32_t interval = getInt(i, "int", plan.intervalDays);
        plan.intervalDays = interval >= 1 && interval <= 30 ? static_cast<uint8_t>(interval) : 1;
        plan.skipYmd = static_cast<uint32_t>(getInt(i, "skip", 0));
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
                    setInt(index, "rep", static_cast<int32_t>(plan.repeatMode)) &&
                    setInt(index, "week", plan.weekMask) &&
                    setInt(index, "int", plan.intervalDays) &&
                    setInt(index, "skip", static_cast<int32_t>(plan.skipYmd)) &&
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

bool setSkipYmd(uint8_t index, uint32_t ymd) {
    if (index >= MaxPlans) {
        return false;
    }
    if (!setInt(index, "skip", static_cast<int32_t>(ymd))) {
        return false;
    }
    g_plans[index].skipYmd = ymd;
    return true;
}

bool clearSkipYmd(uint8_t index) {
    return setSkipYmd(index, 0);
}

bool validate(const Plan& plan) {
    if (plan.minuteOfDay >= 1440 || plan.intervalDays < 1 || plan.intervalDays > 30 || plan.weekMask > 0x7F) {
        return false;
    }
    if (plan.mode != SettingsStore::MODE_SIMULTANEOUS && plan.mode != SettingsStore::MODE_SEQUENTIAL) {
        return false;
    }
    if (plan.repeatMode != REPEAT_WEEKLY && plan.repeatMode != REPEAT_INTERVAL) {
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

const char* repeatModeName(RepeatMode mode) {
    return mode == REPEAT_INTERVAL ? "interval" : "weekly";
}

bool parseRepeatMode(const char* text, RepeatMode* mode) {
    if (!text || !mode) {
        return false;
    }
    if (strcmp(text, "weekly") == 0 || strcmp(text, "week") == 0) {
        *mode = REPEAT_WEEKLY;
        return true;
    }
    if (strcmp(text, "interval") == 0 || strcmp(text, "every_n_days") == 0) {
        *mode = REPEAT_INTERVAL;
        return true;
    }
    return false;
}

}
