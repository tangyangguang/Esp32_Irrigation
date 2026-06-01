#include "domain/ZoneScheduler.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <time.h>

#include "domain/BusinessEventLog.h"
#include "domain/FlowMeter.h"
#include "domain/Zone.h"
#include "storage/PlanStore.h"
#include "storage/ScheduleSkipStore.h"

namespace {

bool epochToLocal(uint32_t epoch, tm* out) {
    if (!out || epoch == 0) {
        return false;
    }
    const time_t value = static_cast<time_t>(epoch);
    return localtime_r(&value, out) != nullptr;
}

uint32_t makeYmd(const tm& value) {
    return static_cast<uint32_t>(value.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(value.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(value.tm_mday);
}

uint16_t makeMinuteOfDay(const Irrigation::PlanDefinition& plan) {
    return static_cast<uint16_t>(plan.timeHour) * 60U + plan.timeMinute;
}

bool dueEpochForDate(const Irrigation::PlanDefinition& plan, const tm& nowLocal, uint32_t* out) {
    if (!out) {
        return false;
    }
    tm due = nowLocal;
    due.tm_hour = plan.timeHour;
    due.tm_min = plan.timeMinute;
    due.tm_sec = 0;
    due.tm_isdst = -1;
    const time_t value = mktime(&due);
    if (value <= 0) {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

void appendObservation(const Irrigation::PlanDefinition& plan, Irrigation::PlanObservationStatus status, uint32_t dueEpoch) {
    BusinessEventLog::appendScheduleObservation(plan, status, dueEpoch);
}

}

void ZoneScheduler::begin(uint8_t zoneId) {
    m_zoneId = zoneId;
    reset();
}

void ZoneScheduler::reset() {
    m_eligibleFromEpoch = 0;
    m_lastEpoch = 0;
    m_tracker.reset();
}

bool ZoneScheduler::eligible(uint32_t trustedEpoch) const {
    return m_eligibleFromEpoch != 0 && trustedEpoch >= m_eligibleFromEpoch;
}

uint32_t ZoneScheduler::eligibleFromEpoch() const {
    return m_eligibleFromEpoch;
}

const PlanExecutionTracker& ZoneScheduler::tracker() const {
    return m_tracker;
}

void ZoneScheduler::tick(Zone& zone,
                         const Irrigation::SystemConfig& systemConfig,
                         bool leakAlertActive,
                         uint32_t trustedEpoch,
                         uint32_t nowMs) {
    if (!Irrigation::validZoneId(m_zoneId) || trustedEpoch == 0) {
        return;
    }
    if (trustedEpoch == m_lastEpoch) {
        return;
    }
    m_lastEpoch = trustedEpoch;
    if (m_eligibleFromEpoch == 0) {
        m_eligibleFromEpoch = trustedEpoch;
    }

    tm nowLocal = {};
    if (!epochToLocal(trustedEpoch, &nowLocal)) {
        return;
    }
    const uint32_t ymd = makeYmd(nowLocal);
    m_tracker.resetNewDay(ymd);

    for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
        const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(m_zoneId, slot);
        if (!plan.exists) {
            continue;
        }
        const uint16_t minuteOfDay = makeMinuteOfDay(plan);
        uint32_t dueEpoch = 0;
        if (!dueEpochForDate(plan, nowLocal, &dueEpoch) || dueEpoch < m_eligibleFromEpoch) {
            continue;
        }
        if (m_tracker.isHandled(plan.planId, ymd, minuteOfDay)) {
            continue;
        }

        const uint32_t graceEnd = dueEpoch + systemConfig.scheduleGraceSec;
        if (trustedEpoch < dueEpoch) {
            continue;
        }
        if (trustedEpoch > graceEnd) {
            markObserved(plan, ymd, minuteOfDay, Irrigation::PlanObservationStatus::MISSED);
            appendObservation(plan, Irrigation::PlanObservationStatus::MISSED, dueEpoch);
            continue;
        }
        (void)observePlan(zone, plan, systemConfig, leakAlertActive, ymd, minuteOfDay, dueEpoch, trustedEpoch, nowMs);
    }
}

bool ZoneScheduler::observePlan(Zone& zone,
                                const Irrigation::PlanDefinition& plan,
                                const Irrigation::SystemConfig& systemConfig,
                                bool leakAlertActive,
                                uint32_t ymd,
                                uint16_t minuteOfDay,
                                uint32_t dueEpoch,
                                uint32_t trustedEpoch,
                                uint32_t nowMs) {
    Irrigation::PlanObservationStatus status = Irrigation::PlanObservationStatus::NOT_EVALUATED;
    bool started = false;
    if (!plan.enabled) {
        status = Irrigation::PlanObservationStatus::SKIPPED_DISABLED;
    } else if (!PlanStore::shouldRunOnDate(plan, ymd)) {
        status = Irrigation::PlanObservationStatus::SKIPPED_CYCLE;
    } else if (ScheduleSkipStore::isSkipped(plan.planId, ymd)) {
        status = Irrigation::PlanObservationStatus::SKIPPED_CALENDAR;
    } else if (plan.durationSec > systemConfig.maxWateringDurationSec) {
        status = Irrigation::PlanObservationStatus::SKIPPED_CONFIG_INVALID;
    } else if (!zone.config().enabled) {
        status = Irrigation::PlanObservationStatus::SKIPPED_DISABLED;
    } else if (zone.isError()) {
        status = Irrigation::PlanObservationStatus::SKIPPED_ERROR;
    } else if (leakAlertActive) {
        status = Irrigation::PlanObservationStatus::SKIPPED_LEAK;
    } else if (zone.isBusy()) {
        status = Irrigation::PlanObservationStatus::SKIPPED_BUSY;
    } else {
        started = zone.start(Irrigation::TaskType::PLAN,
                             Irrigation::StartSource::SCHEDULER,
                             plan.planId,
                             plan.slotIndex,
                             plan.name,
                             plan.durationSec,
                             systemConfig.maxWateringDurationSec,
                             FlowMeter::pulseCount(plan.zoneId),
                             trustedEpoch,
                             nowMs);
        status = started ? Irrigation::PlanObservationStatus::STARTED : Irrigation::PlanObservationStatus::REJECTED;
    }

    markObserved(plan, ymd, minuteOfDay, status);
    appendObservation(plan, status, dueEpoch);
    return started;
}

void ZoneScheduler::markObserved(const Irrigation::PlanDefinition& plan,
                                 uint32_t ymd,
                                 uint16_t minuteOfDay,
                                 Irrigation::PlanObservationStatus status) {
    m_tracker.mark(plan.planId, ymd, minuteOfDay, status);
}
