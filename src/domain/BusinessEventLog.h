#pragma once

#include <stdint.h>

#include "domain/ZoneTypes.h"

namespace BusinessEventLog {

void appendScheduleObservation(const Irrigation::PlanDefinition& plan,
                               Irrigation::PlanObservationStatus status,
                               uint32_t dueEpoch);
void appendScheduleSkipDecision(uint32_t planId,
                                uint32_t ymd,
                                Irrigation::SkipReason reason,
                                bool skipped,
                                const char* source);
void appendPlanTrackerPersistFailed(uint8_t zoneId,
                                    uint32_t planId,
                                    Irrigation::PlanObservationStatus status);
void appendRecordStoreRecovered(uint16_t count, uint32_t nextId);
void appendRecordMetaSaveFailed(uint32_t recordId, uint16_t slot);
void appendRecordAppendFailed(uint8_t zoneId, uint32_t planId, Irrigation::TaskResult result);
void appendConfigSchemaReset(const char* store, uint16_t invalidCount);
void appendFlowFault(uint8_t zoneId,
                     Irrigation::TaskResult result,
                     uint32_t targetSec,
                     uint32_t pulses,
                     bool locked);
void appendSafetyStop(uint8_t zoneId, Irrigation::TaskResult result, const char* source);
void appendLeakDetected(uint8_t zoneId,
                        uint32_t observedPulses,
                        uint16_t pulseThreshold,
                        uint16_t windowSec);
void appendZoneLocked(uint8_t zoneId, Irrigation::ZoneErrorCode code, Irrigation::TaskResult result);
void appendAlertCleared(uint8_t zoneId, bool allZones, const char* source);
void appendFactoryResetRequested(const char* source);
void appendFactoryResetExecuted(bool ok, const char* source);
void appendFlowCandidateApplied(uint8_t zoneId,
                                const Irrigation::FlowParameters& oldParams,
                                const Irrigation::FlowParameters& newParams,
                                const char* source);
void appendFlowPreviousRestored(uint8_t zoneId,
                                const Irrigation::FlowParameters& oldParams,
                                const Irrigation::FlowParameters& newParams,
                                const char* source);

const char* sourceFromStart(Irrigation::StartSource source);
const char* sourceFromStop(Irrigation::StopSource source);

}
