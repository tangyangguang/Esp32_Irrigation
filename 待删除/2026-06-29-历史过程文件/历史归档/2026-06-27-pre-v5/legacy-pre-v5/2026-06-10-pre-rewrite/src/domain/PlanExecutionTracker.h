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

    void begin(uint8_t zoneId);
    void reset();
    bool resetNewDay(uint32_t ymd);
    bool isHandled(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay) const;
    bool status(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay, Irrigation::PlanObservationStatus* out) const;
    bool mark(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay, Irrigation::PlanObservationStatus status);
    bool markVolatile(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay, Irrigation::PlanObservationStatus status);
    bool retrySave();
    bool hasPendingSave() const;
    uint8_t count() const;
    const Entry& get(uint8_t index) const;
    static bool clearPersistent();

private:
    bool load();
    bool save() const;
    bool markInternal(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay, Irrigation::PlanObservationStatus status, bool persist);

    uint8_t m_zoneId = 0;
    Entry m_entries[Irrigation::MaxPlansPerZone] = {};
    uint8_t m_count = 0;
    uint32_t m_currentYmd = 0;
    bool m_dirty = false;
};
