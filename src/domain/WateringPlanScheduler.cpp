#include "domain/WateringPlanScheduler.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <time.h>

#include "domain/WateringSession.h"
#include "storage/EventStore.h"
#include "storage/PlanStore.h"
#include "storage/PlanSkipStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace {

uint16_t g_lastMinuteOfDay = 1440;
uint32_t g_lastYmd = 0;

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
    return PlanStore::shouldRunOnDate(plan, today);
}

void triggerPlan(uint8_t index, const PlanStore::Plan& plan, uint32_t today) {
    if (WateringSession::isActive()) {
        RecordStore::Record record = {};
        record.sessionStartedMs = millis();
        record.sessionEndedMs = record.sessionStartedMs;
        record.source = static_cast<uint8_t>(RecordStore::SOURCE_PLAN);
        record.mode = static_cast<uint8_t>(plan.mode);
        record.stopReason = static_cast<uint8_t>(WateringSession::REASON_SKIPPED);
        record.enabledRoads = SettingsStore::enabledRoads();
        record.flowNoPulseTimeoutSec = SettingsStore::current().flowNoPulseTimeoutSec;
        for (uint8_t i = 0; i < 2; ++i) {
            record.roads[i].state = static_cast<uint8_t>(WateringSession::ROAD_STOPPED);
            record.roads[i].targetSec = plan.roadSec[i];
            record.roads[i].pulsePerLiter = SettingsStore::current().roads[i].pulsePerLiter;
            record.roads[i].calibrationX1000 = SettingsStore::current().roads[i].calibrationX1000;
        }
        (void)RecordStore::append(record);
        (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                                 EventStore::SOURCE_PLAN,
                                 0,
                                 WateringSession::REASON_SKIPPED,
                                 plan.roadSec[0],
                                 plan.roadSec[1],
                                 "plan skipped active");
        (void)PlanStore::setLastRunYmd(index, today);
        ESP32BASE_LOG_W("plans", "skip active_session index=%u", static_cast<unsigned>(index));
        return;
    }

    if (!WateringSession::startManual(plan.roadSec[0], plan.roadSec[1], plan.mode, RecordStore::SOURCE_PLAN, "plan")) {
        (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                                 EventStore::SOURCE_PLAN,
                                 0,
                                 WateringSession::REASON_SKIPPED,
                                 plan.roadSec[0],
                                 plan.roadSec[1],
                                 "plan rejected");
        ESP32BASE_LOG_W("plans", "trigger rejected index=%u", static_cast<unsigned>(index));
        return;
    }
    (void)PlanStore::setLastRunYmd(index, today);
    ESP32BASE_LOG_I("plans", "triggered index=%u", static_cast<unsigned>(index));
}

}

namespace WateringPlanScheduler {

void begin() {
    g_lastMinuteOfDay = 1440;
    g_lastYmd = 0;
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
            triggerPlan(i, plan, today);
        }
    }
}

}
