#pragma once

#include <stdint.h>

#include "domain/PlanExecutionTracker.h"
#include "domain/ZoneTypes.h"

class Zone;

class ZoneScheduler {
public:
    void begin(uint8_t zoneId);
    void reset();
    void tick(Zone& zone,
              const Irrigation::SystemConfig& systemConfig,
              bool leakAlertActive,
              uint32_t trustedEpoch,
              uint32_t nowMs);
    bool eligible(uint32_t trustedEpoch) const;
    uint32_t eligibleFromEpoch() const;
    const PlanExecutionTracker& tracker() const;

private:
    bool observePlan(Zone& zone,
                     const Irrigation::PlanDefinition& plan,
                     const Irrigation::SystemConfig& systemConfig,
                     bool leakAlertActive,
                     uint32_t ymd,
                     uint16_t minuteOfDay,
                     uint32_t dueEpoch,
                     uint32_t trustedEpoch,
                     uint32_t nowMs);
    void markObserved(const Irrigation::PlanDefinition& plan,
                      uint32_t ymd,
                      uint16_t minuteOfDay,
                      Irrigation::PlanObservationStatus status);

    uint8_t m_zoneId = 0;
    uint32_t m_eligibleFromEpoch = 0;
    uint32_t m_lastEpoch = 0;
    PlanExecutionTracker m_tracker;
};
