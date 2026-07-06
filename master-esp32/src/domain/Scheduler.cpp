#include "Scheduler.h"

#include <Esp32Base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ConfigStore.h"
#include "EventService.h"
#include "IrrigationConfig.h"
#include "RunController.h"

namespace Irrigation {

namespace {
uint32_t g_lastTriggeredDay[kMaxPlans][kMaxPlanStartTimes];
uint32_t g_lastCheckedEpoch = 0;

bool currentLocalMinute(uint16_t& minuteOfDay, uint32_t& dayKey) {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    if (!time.synced || time.epochSec == 0) {
        return false;
    }

    if (time.epochSec == g_lastCheckedEpoch) {
        return false;
    }
    g_lastCheckedEpoch = time.epochSec;

    char clockText[6];
    if (!Esp32BaseTime::formatTime(clockText, sizeof(clockText), "%H:%M")) {
        return false;
    }
    const uint8_t hour = static_cast<uint8_t>((clockText[0] - '0') * 10 + (clockText[1] - '0'));
    const uint8_t minute = static_cast<uint8_t>((clockText[3] - '0') * 10 + (clockText[4] - '0'));
    if (hour > 23 || minute > 59) {
        return false;
    }
    minuteOfDay = static_cast<uint16_t>(hour) * 60U + minute;

    char dayText[9];
    if (!Esp32BaseTime::formatTime(dayText, sizeof(dayText), "%Y%m%d")) {
        return false;
    }
    dayKey = static_cast<uint32_t>(strtoul(dayText, nullptr, 10));
    return dayKey != 0;
}

bool currentLocalClock(uint16_t& minuteOfDay, uint8_t& second, uint32_t& epoch) {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    if (!time.synced || time.epochSec == 0) {
        return false;
    }

    char clockText[9];
    if (!Esp32BaseTime::formatTime(clockText, sizeof(clockText), "%H:%M:%S")) {
        return false;
    }
    const uint8_t hour = static_cast<uint8_t>((clockText[0] - '0') * 10 + (clockText[1] - '0'));
    const uint8_t minute = static_cast<uint8_t>((clockText[3] - '0') * 10 + (clockText[4] - '0'));
    second = static_cast<uint8_t>((clockText[6] - '0') * 10 + (clockText[7] - '0'));
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }

    minuteOfDay = static_cast<uint16_t>(hour) * 60U + minute;
    epoch = time.epochSec;
    return true;
}

uint16_t minutesUntilNextStart(uint16_t nowMinute, uint16_t startMinute) {
    if (startMinute > nowMinute) {
        return static_cast<uint16_t>(startMinute - nowMinute);
    }
    return static_cast<uint16_t>(kMinutesPerDay - nowMinute + startMinute);
}

void tryTriggerPlan(uint8_t planIndex, uint8_t startIndex, uint32_t dayKey) {
    const WateringPlan& plan = ConfigStore::config().plans[planIndex];
    if (Scheduler::alreadyTriggered(planIndex, startIndex, dayKey)) {
        return;
    }

    Scheduler::markTriggered(planIndex, startIndex, dayKey);

    if (RunController::busy()) {
        EventService::planSkipped(plan.id, startIndex, "controller_busy");
        ESP32BASE_LOG_W("scheduler", "plan_skipped_busy plan=%u start=%u", plan.id, startIndex);
        return;
    }

    RunReason reason = RunReason::None;
    if (RunController::startPlan(plan.id, reason)) {
        EventService::planTriggered(plan.id, startIndex);
        ESP32BASE_LOG_I("scheduler", "plan_triggered plan=%u start=%u", plan.id, startIndex);
        return;
    }

    EventService::planSkipped(plan.id, startIndex, runReasonToString(reason));
    ESP32BASE_LOG_W("scheduler", "plan_skipped plan=%u start=%u reason=%s",
                    plan.id,
                    startIndex,
                    runReasonToString(reason));
}

} // namespace

void Scheduler::begin() {
    memset(g_lastTriggeredDay, 0, sizeof(g_lastTriggeredDay));
    g_lastCheckedEpoch = 0;
}

void Scheduler::handle() {
    uint16_t minuteOfDay = kInvalidMinuteOfDay;
    uint32_t dayKey = 0;
    if (!currentLocalMinute(minuteOfDay, dayKey)) {
        return;
    }

    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t planIndex = 0; planIndex < kMaxPlans; ++planIndex) {
        const WateringPlan& plan = config.plans[planIndex];
        if (!plan.used || !plan.enabled) {
            continue;
        }

        for (uint8_t startIndex = 0; startIndex < kMaxPlanStartTimes; ++startIndex) {
            const StartTime& start = plan.startTimes[startIndex];
            if (!start.enabled || start.minuteOfDay != minuteOfDay) {
                continue;
            }
            tryTriggerPlan(planIndex, startIndex, dayKey);
        }
    }
}

uint32_t Scheduler::nextRunEpoch() {
    uint16_t nowMinute = kInvalidMinuteOfDay;
    uint8_t second = 0;
    uint32_t epoch = 0;
    if (!currentLocalClock(nowMinute, second, epoch)) {
        return 0;
    }

    uint16_t bestDeltaMinutes = kMinutesPerDay;
    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t planIndex = 0; planIndex < kMaxPlans; ++planIndex) {
        const WateringPlan& plan = config.plans[planIndex];
        if (!plan.used || !plan.enabled || !planHasEffectiveStep(config, plan)) {
            continue;
        }

        for (uint8_t startIndex = 0; startIndex < kMaxPlanStartTimes; ++startIndex) {
            const StartTime& start = plan.startTimes[startIndex];
            if (!start.enabled || !isValidMinuteOfDay(start.minuteOfDay)) {
                continue;
            }
            const uint16_t delta = minutesUntilNextStart(nowMinute, start.minuteOfDay);
            if (delta < bestDeltaMinutes) {
                bestDeltaMinutes = delta;
            }
        }
    }

    if (bestDeltaMinutes >= kMinutesPerDay) {
        return 0;
    }

    const uint32_t deltaSec = static_cast<uint32_t>(bestDeltaMinutes) * 60UL;
    return epoch + deltaSec - second;
}

void Scheduler::markTriggered(uint8_t planIndex, uint8_t startIndex, uint32_t dayKey) {
    if (planIndex < kMaxPlans && startIndex < kMaxPlanStartTimes) {
        g_lastTriggeredDay[planIndex][startIndex] = dayKey;
    }
}

bool Scheduler::alreadyTriggered(uint8_t planIndex, uint8_t startIndex, uint32_t dayKey) {
    if (planIndex >= kMaxPlans || startIndex >= kMaxPlanStartTimes) {
        return true;
    }
    return g_lastTriggeredDay[planIndex][startIndex] == dayKey;
}

} // namespace Irrigation
