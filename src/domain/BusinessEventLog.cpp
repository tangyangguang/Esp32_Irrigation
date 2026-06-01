#include "domain/BusinessEventLog.h"

#include <Esp32Base.h>
#include <stdio.h>

namespace {

const char* kObjectSystem = "system:irrigation";

bool appendEvent(Esp32BaseAppEventLog::Level level,
                 const char* source,
                 const char* type,
                 const char* reason,
                 const char* object,
                 uint16_t code,
                 int32_t value1,
                 int32_t value2,
                 int32_t value3,
                 uint8_t valueMask,
                 const char* text) {
    Esp32BaseAppEventLog::Event event;
    event.level = level;
    event.source = source;
    event.type = type;
    event.reason = reason;
    event.object = object;
    event.code = code;
    event.value1 = value1;
    event.value2 = value2;
    event.value3 = value3;
    event.valueMask = valueMask;
    event.text = text;
    const bool ok = Esp32BaseAppEventLog::append(event);
    if (!ok) {
        ESP32BASE_LOG_W("business_events", "append_failed type=%s reason=%s error=%s",
                        type ? type : "",
                        reason ? reason : "",
                        Esp32BaseAppEventLog::lastError());
    }
    return ok;
}

void zoneObject(uint8_t zoneId, char* out, size_t len) {
    snprintf(out, len, "zone:%u", static_cast<unsigned>(zoneId));
}

void planObject(uint32_t planId, char* out, size_t len) {
    snprintf(out, len, "plan:%lu", static_cast<unsigned long>(planId));
}

Esp32BaseAppEventLog::Level levelForObservation(Irrigation::PlanObservationStatus status) {
    switch (status) {
        case Irrigation::PlanObservationStatus::SKIPPED_CALENDAR:
        case Irrigation::PlanObservationStatus::SKIPPED_CYCLE:
            return Esp32BaseAppEventLog::LEVEL_INFO;
        case Irrigation::PlanObservationStatus::SKIPPED_DISABLED:
        case Irrigation::PlanObservationStatus::SKIPPED_BUSY:
        case Irrigation::PlanObservationStatus::SKIPPED_ERROR:
        case Irrigation::PlanObservationStatus::SKIPPED_LEAK:
        case Irrigation::PlanObservationStatus::SKIPPED_RESET:
        case Irrigation::PlanObservationStatus::SKIPPED_CONFIG_INVALID:
        case Irrigation::PlanObservationStatus::REJECTED:
        case Irrigation::PlanObservationStatus::MISSED:
            return Esp32BaseAppEventLog::LEVEL_WARN;
        case Irrigation::PlanObservationStatus::STARTED:
        case Irrigation::PlanObservationStatus::NOT_EVALUATED:
        default:
            return Esp32BaseAppEventLog::LEVEL_INFO;
    }
}

const char* reasonForSkip(Irrigation::SkipReason reason) {
    switch (reason) {
        case Irrigation::SkipReason::MANUAL: return "manual";
        case Irrigation::SkipReason::WEATHER: return "weather";
        case Irrigation::SkipReason::OTHER:
        default: return "other";
    }
}

const char* reasonForZoneError(Irrigation::ZoneErrorCode code) {
    switch (code) {
        case Irrigation::ZoneErrorCode::FLOW_START_TIMEOUT: return "flow_start_timeout";
        case Irrigation::ZoneErrorCode::FLOW_NO_PULSE_TIMEOUT: return "flow_no_pulse_timeout";
        case Irrigation::ZoneErrorCode::LEAK_DETECTED: return "leak_detected";
        case Irrigation::ZoneErrorCode::CONFIG_INVALID: return "config_invalid";
        case Irrigation::ZoneErrorCode::NONE:
        default: return "none";
    }
}

int32_t packU16(uint16_t high, uint16_t low) {
    return static_cast<int32_t>((static_cast<uint32_t>(high) << 16) | static_cast<uint32_t>(low));
}

}

namespace BusinessEventLog {

const char* sourceFromStart(Irrigation::StartSource source) {
    switch (source) {
        case Irrigation::StartSource::WEB_PAGE: return "web";
        case Irrigation::StartSource::HTTP_API: return "api";
        case Irrigation::StartSource::LOCAL_BUTTON: return "button";
        case Irrigation::StartSource::SCHEDULER: return "schedule";
        case Irrigation::StartSource::UNKNOWN:
        default: return "runtime";
    }
}

const char* sourceFromStop(Irrigation::StopSource source) {
    switch (source) {
        case Irrigation::StopSource::WEB_PAGE: return "web";
        case Irrigation::StopSource::HTTP_API: return "api";
        case Irrigation::StopSource::LOCAL_BUTTON: return "button";
        case Irrigation::StopSource::SCHEDULER: return "schedule";
        case Irrigation::StopSource::DURATION_REACHED: return "runtime";
        case Irrigation::StopSource::FLOW_MONITOR: return "monitor";
        case Irrigation::StopSource::LEAK_MONITOR: return "monitor";
        case Irrigation::StopSource::CONFIG_CHANGE: return "web";
        case Irrigation::StopSource::FACTORY_RESET: return "runtime";
        case Irrigation::StopSource::UNKNOWN:
        default: return "runtime";
    }
}

void appendScheduleObservation(const Irrigation::PlanDefinition& plan,
                               Irrigation::PlanObservationStatus status,
                               uint32_t dueEpoch) {
    if (status == Irrigation::PlanObservationStatus::STARTED ||
        status == Irrigation::PlanObservationStatus::NOT_EVALUATED) {
        return;
    }
    char object[24];
    planObject(plan.planId, object, sizeof(object));
    (void)appendEvent(levelForObservation(status),
                      "schedule",
                      "schedule_skipped",
                      Irrigation::observationStatusName(status),
                      object,
                      static_cast<uint16_t>(status),
                      static_cast<int32_t>(plan.zoneId),
                      static_cast<int32_t>(plan.durationSec),
                      static_cast<int32_t>(dueEpoch),
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                      "schedule not started");
}

void appendScheduleSkipDecision(uint32_t planId,
                                uint32_t ymd,
                                Irrigation::SkipReason reason,
                                bool skipped,
                                const char* source) {
    char object[24];
    planObject(planId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                      source && source[0] ? source : "api",
                      skipped ? "schedule_skipped" : "schedule_unskipped",
                      reasonForSkip(reason),
                      object,
                      static_cast<uint16_t>(reason),
                      static_cast<int32_t>(ymd),
                      0,
                      0,
                      Esp32BaseAppEventLog::VALUE1,
                      skipped ? "schedule skip saved" : "schedule skip removed");
}

void appendFlowFault(uint8_t zoneId,
                     Irrigation::TaskResult result,
                     uint32_t targetSec,
                     uint32_t pulses,
                     bool locked) {
    char object[16];
    zoneObject(zoneId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_ERROR,
                      "monitor",
                      "flow_fault",
                      Irrigation::taskResultName(result),
                      object,
                      static_cast<uint16_t>(result),
                      static_cast<int32_t>(targetSec),
                      static_cast<int32_t>(pulses),
                      locked ? 1 : 0,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                      locked ? "flow fault locked zone" : "flow fault stopped task");
}

void appendSafetyStop(uint8_t zoneId, Irrigation::TaskResult result, const char* source) {
    char object[16];
    zoneObject(zoneId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_WARN,
                      source && source[0] ? source : "runtime",
                      "safety_stop",
                      Irrigation::taskResultName(result),
                      object,
                      static_cast<uint16_t>(result),
                      0,
                      0,
                      0,
                      0,
                      "safety stopped watering");
}

void appendLeakDetected(uint8_t zoneId,
                        uint32_t observedPulses,
                        uint16_t pulseThreshold,
                        uint16_t windowSec) {
    char object[16];
    zoneObject(zoneId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_ERROR,
                      "monitor",
                      "leak_detected",
                      "idle_flow",
                      object,
                      0,
                      static_cast<int32_t>(observedPulses),
                      pulseThreshold,
                      windowSec,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                      "idle flow detected");
}

void appendZoneLocked(uint8_t zoneId, Irrigation::ZoneErrorCode code, Irrigation::TaskResult result) {
    char object[16];
    zoneObject(zoneId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_ERROR,
                      "monitor",
                      "zone_locked",
                      reasonForZoneError(code),
                      object,
                      static_cast<uint16_t>(code),
                      static_cast<int32_t>(result),
                      0,
                      0,
                      Esp32BaseAppEventLog::VALUE1,
                      "zone requires manual clear");
}

void appendAlertCleared(uint8_t zoneId, bool allZones, const char* source) {
    char object[16];
    if (allZones) {
        snprintf(object, sizeof(object), "zone:all");
    } else {
        zoneObject(zoneId, object, sizeof(object));
    }
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                      source && source[0] ? source : "web",
                      "alert_cleared",
                      allZones ? "all_zones" : "zone",
                      object,
                      0,
                      allZones ? 0 : zoneId,
                      0,
                      0,
                      allZones ? 0 : Esp32BaseAppEventLog::VALUE1,
                      allZones ? "all alerts cleared" : "zone alert cleared");
}

void appendFactoryResetRequested(bool clearRecords, const char* source) {
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_WARN,
                      source && source[0] ? source : "runtime",
                      "factory_reset",
                      "requested",
                      kObjectSystem,
                      clearRecords ? 1 : 0,
                      clearRecords ? 1 : 0,
                      0,
                      0,
                      Esp32BaseAppEventLog::VALUE1,
                      "factory reset requested");
}

void appendFactoryResetExecuted(bool ok, bool clearRecords, const char* source) {
    (void)appendEvent(ok ? Esp32BaseAppEventLog::LEVEL_INFO : Esp32BaseAppEventLog::LEVEL_ERROR,
                      source && source[0] ? source : "runtime",
                      "factory_reset",
                      ok ? "executed" : "failed",
                      kObjectSystem,
                      ok ? 1 : 0,
                      clearRecords ? 1 : 0,
                      0,
                      0,
                      Esp32BaseAppEventLog::VALUE1,
                      "factory reset executed");
}

void appendFlowCandidateApplied(uint8_t zoneId,
                                const Irrigation::FlowParameters& oldParams,
                                const Irrigation::FlowParameters& newParams,
                                const char* source) {
    char object[16];
    zoneObject(zoneId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                      source && source[0] ? source : "web",
                      "flow_params_applied",
                      "candidate",
                      object,
                      0,
                      packU16(oldParams.startupPulseLimit, oldParams.startupEstimatedMl),
                      packU16(newParams.startupPulseLimit, newParams.startupEstimatedMl),
                      packU16(oldParams.stablePulsePerLiter, newParams.stablePulsePerLiter),
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                      "candidate applied");
}

void appendFlowPreviousRestored(uint8_t zoneId,
                                const Irrigation::FlowParameters& oldParams,
                                const Irrigation::FlowParameters& newParams,
                                const char* source) {
    char object[16];
    zoneObject(zoneId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                      source && source[0] ? source : "web",
                      "flow_params_restored",
                      "previous",
                      object,
                      0,
                      packU16(oldParams.startupPulseLimit, oldParams.startupEstimatedMl),
                      packU16(newParams.startupPulseLimit, newParams.startupEstimatedMl),
                      packU16(oldParams.stablePulsePerLiter, newParams.stablePulsePerLiter),
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                      "previous restored");
}

}
