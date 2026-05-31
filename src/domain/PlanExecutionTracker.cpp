#include "domain/PlanExecutionTracker.h"

void PlanExecutionTracker::reset() {
    for (uint8_t i = 0; i < Irrigation::MaxPlansPerZone; ++i) {
        m_entries[i] = {};
    }
    m_count = 0;
    m_currentYmd = 0;
}

void PlanExecutionTracker::resetNewDay(uint32_t ymd) {
    if (m_currentYmd == ymd) {
        return;
    }
    reset();
    m_currentYmd = ymd;
}

bool PlanExecutionTracker::isHandled(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay) const {
    for (uint8_t i = 0; i < m_count; ++i) {
        if (m_entries[i].planId == planId && m_entries[i].ymd == ymd && m_entries[i].minuteOfDay == minuteOfDay) {
            return true;
        }
    }
    return false;
}

void PlanExecutionTracker::mark(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay, Irrigation::PlanObservationStatus status) {
    for (uint8_t i = 0; i < m_count; ++i) {
        if (m_entries[i].planId == planId && m_entries[i].ymd == ymd && m_entries[i].minuteOfDay == minuteOfDay) {
            m_entries[i].status = status;
            return;
        }
    }
    if (m_count >= Irrigation::MaxPlansPerZone) {
        return;
    }
    m_entries[m_count++] = {planId, ymd, minuteOfDay, status};
}

uint8_t PlanExecutionTracker::count() const {
    return m_count;
}

const PlanExecutionTracker::Entry& PlanExecutionTracker::get(uint8_t index) const {
    static Entry invalid = {};
    return index < m_count ? m_entries[index] : invalid;
}
