#include "domain/WateringPlanScheduler.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <time.h>

#include "domain/LeakMonitor.h"
#include "domain/MaintenanceService.h"
#include "domain/WateringSession.h"
#include "storage/EventStore.h"
#include "storage/PlanResultStore.h"
#include "storage/PlanSkipStore.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace {

uint16_t g_lastMinuteOfDay = 1440;
uint32_t g_lastYmd = 0;
uint16_t g_lastTriggeredMinute[PlanStore::TotalPlans] = {};
uint32_t g_lastTriggeredYmd[PlanStore::TotalPlans] = {};

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

void markPlanHandled(uint8_t index, uint32_t today) {
    if (PlanStore::setLastRunYmd(index, today)) {
        Esp32BaseConfig::flushAll();
    }
}

void appendPlanResult(uint8_t index, const PlanStore::Plan& plan, const char* result, int32_t value = 0) {
    (void)EventStore::append(EventStore::TYPE_WATER_STOP,
                             EventStore::SOURCE_PLAN,
                             plan.roadId,
                             plan.slotIndex,
                             value,
                             plan.durationSec,
                             result);
}

void triggerPlan(uint8_t index, const PlanStore::Plan& plan, uint32_t today) {
    if (MaintenanceService::factoryResetPending()) {
        (void)PlanResultStore::setResult(index, today, PlanResultStore::RESULT_FACTORY_RESET_PENDING);
        appendPlanResult(index, plan, "plan skipped factory reset pending");
        markPlanHandled(index, today);
        return;
    }
    if (LeakMonitor::hasAlert()) {
        (void)PlanResultStore::setResult(index, today, PlanResultStore::RESULT_LEAK_ALERT);
        appendPlanResult(index, plan, "plan skipped leak alert");
        markPlanHandled(index, today);
        return;
    }
    if (!SettingsStore::isRoadEnabled(plan.roadId)) {
        (void)PlanResultStore::setResult(index, today, PlanResultStore::RESULT_SKIPPED_ROAD_DISABLED);
        appendPlanResult(index, plan, "plan skipped road disabled");
        markPlanHandled(index, today);
        return;
    }
    if (WateringSession::isRoadActive(plan.roadId)) {
        (void)PlanResultStore::setResult(index, today, PlanResultStore::RESULT_SKIPPED_ROAD_BUSY);
        appendPlanResult(index, plan, "plan skipped road busy");
        markPlanHandled(index, today);
        return;
    }
    if (!WateringSession::startRoadTask(plan.roadId, plan.durationSec, RecordStore::TASK_PLAN, RecordStore::SOURCE_PLAN_SCHEDULER, plan.slotIndex, "plan")) {
        (void)PlanResultStore::setResult(index, today, PlanResultStore::RESULT_REJECTED);
        appendPlanResult(index, plan, "plan rejected");
        markPlanHandled(index, today);
        return;
    }
    (void)PlanResultStore::setResult(index, today, PlanResultStore::RESULT_STARTED);
    appendPlanResult(index, plan, "plan started", 1);
    markPlanHandled(index, today);
}

}

namespace WateringPlanScheduler {

void begin() {
    g_lastMinuteOfDay = 1440;
    g_lastYmd = 0;
    for (uint8_t i = 0; i < PlanStore::TotalPlans; ++i) {
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

    for (uint8_t i = 0; i < PlanStore::TotalPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        if (shouldTrigger(i, plan, minuteOfDay, today)) {
            g_lastTriggeredMinute[i] = minuteOfDay;
            g_lastTriggeredYmd[i] = today;
            triggerPlan(i, plan, today);
        }
    }
}

}
