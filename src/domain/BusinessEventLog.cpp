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
        case Irrigation::ZoneErrorCode::FLOW_LOW: return "flow_low";
        case Irrigation::ZoneErrorCode::FLOW_NO_PULSE_TIMEOUT: return "flow_no_pulse_timeout";
        case Irrigation::ZoneErrorCode::FLOW_HIGH: return "flow_high";
        case Irrigation::ZoneErrorCode::CONFIG_INVALID: return "config_invalid";
        case Irrigation::ZoneErrorCode::IDLE_FLOW_DETECTED: return "idle_flow_detected";
        case Irrigation::ZoneErrorCode::NONE:
        default: return "none";
    }
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

void appendPlanTrackerPersistFailed(uint8_t zoneId,
                                    uint32_t planId,
                                    Irrigation::PlanObservationStatus status) {
    char object[24];
    planObject(planId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_ERROR,
                      "schedule",
                      "schedule_tracker_fault",
                      "persist_failed",
                      object,
                      static_cast<uint16_t>(status),
                      static_cast<int32_t>(zoneId),
                      static_cast<int32_t>(planId),
                      0,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2,
                      "plan execution tracker persistence failed");
}

void appendRecordStoreRecovered(uint16_t count, uint32_t nextId) {
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_WARN,
                      "storage",
                      "record_store_recovered",
                      "meta_rebuilt",
                      kObjectSystem,
                      count,
                      count,
                      static_cast<int32_t>(nextId),
                      0,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2,
                      "watering record metadata rebuilt from file");
}

void appendRecordMetaSaveFailed(uint32_t recordId, uint16_t slot) {
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_ERROR,
                      "storage",
                      "record_store_fault",
                      "meta_save_failed",
                      kObjectSystem,
                      slot,
                      static_cast<int32_t>(recordId),
                      slot,
                      0,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2,
                      "watering record metadata save failed");
}

void appendConfigSchemaReset(const char* store, uint16_t invalidCount) {
    char object[24];
    snprintf(object, sizeof(object), "config:%s", store && store[0] ? store : "unknown");
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_WARN,
                      "storage",
                      "config_schema_reset",
                      "format_changed",
                      object,
                      invalidCount,
                      invalidCount,
                      0,
                      0,
                      Esp32BaseAppEventLog::VALUE1,
                      "stored configuration format changed");
}

void appendRecordAppendFailed(uint8_t zoneId, uint32_t planId, Irrigation::TaskResult result) {
    char object[16];
    zoneObject(zoneId, object, sizeof(object));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_ERROR,
                      "storage",
                      "record_store_fault",
                      "append_failed",
                      object,
                      static_cast<uint16_t>(result),
                      static_cast<int32_t>(zoneId),
                      static_cast<int32_t>(planId),
                      0,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2,
                      "watering record append failed");
}

void appendWebRouteRegistrationFailed(uint8_t failedCount) {
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_ERROR,
                      "web",
                      "web_route_fault",
                      "registration_failed",
                      kObjectSystem,
                      failedCount,
                      failedCount,
                      0,
                      0,
                      Esp32BaseAppEventLog::VALUE1,
                      "business web route registration failed");
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

void appendFactoryResetRequested(const char* source) {
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_WARN,
                      source && source[0] ? source : "runtime",
                      "factory_reset",
                      "requested",
                      kObjectSystem,
                      0,
                      0,
                      0,
                      0,
                      0,
                      "factory reset requested");
}

void appendFactoryResetExecuted(bool ok, const char* source) {
    (void)appendEvent(ok ? Esp32BaseAppEventLog::LEVEL_INFO : Esp32BaseAppEventLog::LEVEL_ERROR,
                      source && source[0] ? source : "runtime",
                      "factory_reset",
                      ok ? "executed" : "failed",
                      kObjectSystem,
                      ok ? 1 : 0,
                      ok ? 1 : 0,
                      0,
                      0,
                      Esp32BaseAppEventLog::VALUE1,
                      "factory reset executed");
}

void appendFlowCalibrationApplied(uint8_t flowId,
                                  const Irrigation::FlowMeterCalibrationProfile& oldProfile,
                                  const Irrigation::FlowMeterCalibrationProfile& newProfile,
                                  const char* source) {
    char object[16];
    snprintf(object, sizeof(object), "flow:%u", static_cast<unsigned>(flowId));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                      source && source[0] ? source : "web",
                      "flow_calibration_applied",
                      "pending",
                      object,
                      0,
                      oldProfile.kUlPerMinPerHz,
                      newProfile.kUlPerMinPerHz,
                      newProfile.offsetMilliHz,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                      "flow calibration applied");
}

void appendFlowCalibrationRestored(uint8_t flowId,
                                   const Irrigation::FlowMeterCalibrationProfile& oldProfile,
                                   const Irrigation::FlowMeterCalibrationProfile& newProfile,
                                   const char* source) {
    char object[16];
    snprintf(object, sizeof(object), "flow:%u", static_cast<unsigned>(flowId));
    (void)appendEvent(Esp32BaseAppEventLog::LEVEL_INFO,
                      source && source[0] ? source : "web",
                      "flow_calibration_restored",
                      "rollback",
                      object,
                      0,
                      oldProfile.kUlPerMinPerHz,
                      newProfile.kUlPerMinPerHz,
                      newProfile.offsetMilliHz,
                      Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3,
                      "flow calibration restored");
}

}
