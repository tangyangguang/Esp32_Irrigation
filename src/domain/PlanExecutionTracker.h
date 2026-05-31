#pragma once

#include "domain/ZoneTypes.h"

class PlanExecutionTracker {
public:
    struct Entry {
        uint32_t planId;
        uint32_t ymd;
        uint16_t minuteOfDay;
        Irrigation::PlanObservationStatus status;
    };

    void reset();
    void resetNewDay(uint32_t ymd);
    bool isHandled(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay) const;
    void mark(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay, Irrigation::PlanObservationStatus status);
    uint8_t count() const;
    const Entry& get(uint8_t index) const;

private:
    Entry m_entries[Irrigation::MaxPlansPerZone] = {};
    uint8_t m_count = 0;
    uint32_t m_currentYmd = 0;
};
