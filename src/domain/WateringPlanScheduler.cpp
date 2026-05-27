#include "domain/WateringPlanScheduler.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <time.h>

#include "Pins.h"
#include "domain/WateringSession.h"
#include "storage/EventStore.h"
#include "storage/PlanStore.h"
#include "storage/PlanSkipStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace {

uint16_t g_lastMinuteOfDay = 1440;
uint32_t g_lastYmd = 0;
uint16_t g_lastTriggeredMinute[PlanStore::MaxPlans] = {};
uint32_t g_lastTriggeredYmd[PlanStore::MaxPlans] = {};

uint32_t makeYmd(const tm& value) {
    return static_cast<uint32_t>(value.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(value.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(value.tm_mday);
}

bool readLocalTime(tm* out) {
    if (!out || !Esp32BaseNtp::isTimeSynced()) {
        return false;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    return localtime_r(&now, out) != nullptr;
}

bool shouldTrigger(uint8_t index, const PlanStore::Plan& plan, uint16_t minuteOfDay, uint32_t today) {
    if (!plan.enabled || plan.minuteOfDay != minuteOfDay || plan.lastRunYmd == today || PlanSkipStore::isSkipped(index, today)) {
        return false;
    }
    if (g_lastTriggeredYmd[index] == today && g_lastTriggeredMinute[index] == minuteOfDay) {
        return false;
    }
    return PlanStore::shouldRunOnDate(plan, today);
}

void effectiveRoadSec(const PlanStore::Plan& plan, uint16_t out[2]) {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        out[i] = SettingsStore::isRoadEnabled(i + 1) ? plan.roadSec[i] : 0;
    }
}

bool hasEffectiveRoad(const uint16_t roadSec[2]) {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (roadSec[i] > 0) {
            return true;
        }
    }
    return false;
}

void markPlanHandled(uint8_t index, uint32_t today) {
    if (PlanStore::setLastRunYmd(index, today)) {
        Esp32BaseConfig::flushAll();
    }
}

void triggerPlan(uint8_t index, const PlanStore::Plan& plan, uint32_t today) {
    uint16_t effective[2] = {};
    effectiveRoadSec(plan, effective);
    if (!hasEffectiveRoad(effective)) {
        (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                                 EventStore::SOURCE_PLAN,
                                 0,
                                 WateringSession::REASON_SKIPPED,
                                 0,
                                 0,
                                 "plan no enabled roads");
        markPlanHandled(index, today);
        ESP32BASE_LOG_W("plans", "skip no_enabled_roads index=%u", static_cast<unsigned>(index));
        return;
    }

    if (WateringSession::isActive()) {
        (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                                 EventStore::SOURCE_PLAN,
                                 0,
                                 WateringSession::REASON_SKIPPED,
                                 effective[0],
                                 effective[1],
                                 "plan skipped active");
        markPlanHandled(index, today);
        ESP32BASE_LOG_W("plans", "skip active_session index=%u", static_cast<unsigned>(index));
        return;
    }

    if (!WateringSession::startManual(effective[0], effective[1], plan.mode, RecordStore::SOURCE_PLAN, "plan")) {
        (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                                 EventStore::SOURCE_PLAN,
                                 0,
                                 WateringSession::REASON_SKIPPED,
                                 effective[0],
                                 effective[1],
                                 "plan rejected");
        ESP32BASE_LOG_W("plans", "trigger rejected index=%u", static_cast<unsigned>(index));
        return;
    }
    markPlanHandled(index, today);
    ESP32BASE_LOG_I("plans", "triggered index=%u", static_cast<unsigned>(index));
}

}

namespace WateringPlanScheduler {

void begin() {
    g_lastMinuteOfDay = 1440;
    g_lastYmd = 0;
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        g_lastTriggeredMinute[i] = 1440;
        g_lastTriggeredYmd[i] = 0;
    }
}

void handle() {
    tm now = {};
    if (!readLocalTime(&now)) {
        return;
    }
    const uint16_t minuteOfDay = static_cast<uint16_t>(now.tm_hour * 60 + now.tm_min);
    const uint32_t today = makeYmd(now);
    if (minuteOfDay == g_lastMinuteOfDay && today == g_lastYmd) {
        return;
    }
    g_lastMinuteOfDay = minuteOfDay;
    g_lastYmd = today;

    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        if (shouldTrigger(i, plan, minuteOfDay, today)) {
            g_lastTriggeredMinute[i] = minuteOfDay;
            g_lastTriggeredYmd[i] = today;
            triggerPlan(i, plan, today);
        }
    }
}

}
