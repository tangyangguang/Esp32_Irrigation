#pragma once

#include <stdint.h>

#include "Pins.h"

namespace Irrigation {

static constexpr uint8_t MaxZones = IrrigationPins::MaxRoads;
static constexpr uint8_t MaxPlansPerZone = 6;
static constexpr uint8_t TotalPlanSlots = MaxZones * MaxPlansPerZone;
static constexpr uint8_t NameMaxBytes = 32;
static constexpr uint32_t NoPlanId = 0;
static constexpr uint8_t NoPlanSlot = 0xFF;

#ifdef DISABLED
#undef DISABLED
#endif

enum class ZoneState : uint8_t {
    DISABLED = 0,
    IDLE,
    STARTING,
    RUNNING,
    ERROR,
};

enum class TaskType : uint8_t {
    MANUAL = 1,
    PLAN = 2,
};

enum class StartSource : uint8_t {
    UNKNOWN = 0,
    WEB_PAGE = 1,
    HTTP_API = 2,
    LOCAL_BUTTON = 3,
    SCHEDULER = 4,
};

enum class StopSource : uint8_t {
    UNKNOWN = 0,
    WEB_PAGE = 1,
    HTTP_API = 2,
    LOCAL_BUTTON = 3,
    SCHEDULER = 4,
    DURATION_REACHED = 5,
    FLOW_MONITOR = 6,
    LEAK_MONITOR = 7,
    CONFIG_CHANGE = 8,
    FACTORY_RESET = 9,
};

enum class StopScope : uint8_t {
    NONE = 0,
    ZONE = 1,
    ALL = 2,
};

enum class TaskResult : uint8_t {
    NONE = 0,
    COMPLETED = 1,
    USER_STOPPED = 2,
    FLOW_START_TIMEOUT = 3,
    FLOW_NO_PULSE_TIMEOUT = 4,
    LEAK_PROTECTED = 5,
    FACTORY_RESET_PROTECTED = 6,
    CONFIG_INVALID = 7,
    REJECTED = 8,
};

enum class ZoneErrorCode : uint8_t {
    NONE = 0,
    FLOW_START_TIMEOUT = 1,
    FLOW_NO_PULSE_TIMEOUT = 2,
    LEAK_DETECTED = 3,
    CONFIG_INVALID = 4,
};

enum class SkipReason : uint8_t {
    OTHER = 0,
    MANUAL = 1,
    WEATHER = 2,
};

enum class PlanObservationStatus : uint8_t {
    NOT_EVALUATED = 0,
    STARTED = 1,
    SKIPPED_CALENDAR = 2,
    SKIPPED_DISABLED = 3,
    SKIPPED_BUSY = 4,
    SKIPPED_ERROR = 5,
    SKIPPED_LEAK = 6,
    SKIPPED_RESET = 7,
    SKIPPED_CYCLE = 8,
    SKIPPED_CONFIG_INVALID = 9,
    REJECTED = 10,
    MISSED = 11,
};

struct FlowParameters {
    uint16_t startupPulseLimit;
    uint16_t startupEstimatedMl;
    uint16_t stablePulsePerLiter;
};

struct FlowCandidateSlot {
    bool exists;
    uint8_t reserved[3];
    FlowParameters params;
};

struct ZoneConfig {
    uint8_t zoneId;
    char name[NameMaxBytes];
    uint8_t valvePin;
    uint8_t flowPin;
    bool enabled;
    FlowParameters flow;
    FlowCandidateSlot candidateFlow;
    bool previousFlowExists;
    uint8_t reservedFlow[3];
    FlowParameters previousFlow;
    uint16_t startTimeoutSec;
    uint16_t flowNoPulseTimeoutSec;
    bool suppressError;
    uint8_t reserved[3];
};

struct SystemConfig {
    uint32_t maxWateringDurationSec;
    uint16_t scheduleGraceSec;
    uint32_t manualDefaultDurationSec;
    uint32_t durationPresets[6];
    bool idleLeakDetectionEnabled;
    uint8_t calibrationSampleTarget;
    uint16_t idleLeakWindowSec;
    uint16_t idleLeakPulseThreshold;
    uint16_t calibrationMaxCaptureMin;
    uint16_t calibrationDetailCaptureSec;
    uint16_t calibrationDetailPulseLimit;
};

struct PlanDefinition {
    bool exists;
    uint32_t planId;
    uint8_t zoneId;
    uint8_t slotIndex;
    char name[NameMaxBytes];
    bool enabled;
    uint8_t timeHour;
    uint8_t timeMinute;
    uint32_t durationSec;
    uint8_t cycleDays;
    uint32_t cycleMask;
    uint32_t cycleStartYmd;
    uint32_t createdAt;
    uint8_t reserved[2];
};

struct ZoneConfigSnapshot {
    FlowParameters flow;
    uint16_t startTimeoutSec;
    uint16_t flowNoPulseTimeoutSec;
    bool suppressError;
    uint8_t reserved[1];
};

struct ActiveTask {
    bool active;
    TaskType type;
    StartSource startSource;
    uint32_t planId;
    uint8_t planSlot;
    char planNameSnapshot[NameMaxBytes];
    uint32_t targetSec;
    uint32_t startedEpoch;
    uint32_t startedUptimeMs;
    uint32_t startedPulseCount;
    ZoneConfigSnapshot configSnapshot;
};

struct TaskRuntime {
    uint32_t lastPulseCount;
    uint32_t lastPulseMs;
    bool firstPulseSeen;
};

struct FinishedTask {
    bool valid;
    TaskResult result;
    StopSource stopSource;
    StopScope stopScope;
    uint32_t endedEpoch;
    uint32_t endedUptimeMs;
    uint32_t endedPulseCount;
};

struct ZoneStatus {
    uint8_t zoneId;
    ZoneState state;
    bool enabled;
    bool busy;
    bool errorActive;
    ZoneErrorCode errorCode;
    bool leakAlert;
    uint32_t targetSec;
    uint32_t elapsedSec;
    uint32_t remainingSec;
    uint32_t pulses;
    uint32_t estimatedMilliliters;
    uint32_t flowRatePerMinuteX1000;
    TaskType taskType;
    uint32_t planId;
};

inline const char* zoneStateName(ZoneState state) {
    switch (state) {
        case ZoneState::DISABLED: return "disabled";
        case ZoneState::IDLE: return "idle";
        case ZoneState::STARTING: return "starting";
        case ZoneState::RUNNING: return "running";
        case ZoneState::ERROR: return "error";
        default: return "unknown";
    }
}

inline const char* taskTypeName(TaskType type) {
    return type == TaskType::PLAN ? "plan" : "manual";
}

inline const char* taskResultName(TaskResult result) {
    switch (result) {
        case TaskResult::COMPLETED: return "completed";
        case TaskResult::USER_STOPPED: return "user_stopped";
        case TaskResult::FLOW_START_TIMEOUT: return "flow_start_timeout";
        case TaskResult::FLOW_NO_PULSE_TIMEOUT: return "flow_no_pulse_timeout";
        case TaskResult::LEAK_PROTECTED: return "leak_protected";
        case TaskResult::FACTORY_RESET_PROTECTED: return "factory_reset_protected";
        case TaskResult::CONFIG_INVALID: return "config_invalid";
        case TaskResult::REJECTED: return "rejected";
        case TaskResult::NONE:
        default: return "none";
    }
}

inline const char* observationStatusName(PlanObservationStatus status) {
    switch (status) {
        case PlanObservationStatus::STARTED: return "started";
        case PlanObservationStatus::SKIPPED_CALENDAR: return "skipped_calendar";
        case PlanObservationStatus::SKIPPED_DISABLED: return "skipped_disabled";
        case PlanObservationStatus::SKIPPED_BUSY: return "skipped_busy";
        case PlanObservationStatus::SKIPPED_ERROR: return "skipped_error";
        case PlanObservationStatus::SKIPPED_LEAK: return "skipped_leak";
        case PlanObservationStatus::SKIPPED_RESET: return "skipped_reset";
        case PlanObservationStatus::SKIPPED_CYCLE: return "skipped_cycle";
        case PlanObservationStatus::SKIPPED_CONFIG_INVALID: return "skipped_config_invalid";
        case PlanObservationStatus::REJECTED: return "rejected";
        case PlanObservationStatus::MISSED: return "missed";
        case PlanObservationStatus::NOT_EVALUATED:
        default: return "not_evaluated";
    }
}

inline bool validZoneId(uint8_t zoneId) {
    return zoneId >= 1 && zoneId <= MaxZones;
}

inline bool zoneIndex(uint8_t zoneId, uint8_t* index) {
    if (!index || !validZoneId(zoneId)) {
        return false;
    }
    *index = static_cast<uint8_t>(zoneId - 1);
    return true;
}

}
