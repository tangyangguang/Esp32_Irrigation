#pragma once

#include <stdint.h>

#include "Pins.h"

namespace Irrigation {

static constexpr uint8_t MaxFlowMeters = IrrigationPins::MaxFlowMeters;
static constexpr uint8_t MaxZones = IrrigationPins::MaxZones;
static constexpr uint8_t MaxPlansPerZone = 6;
static constexpr uint8_t TotalPlanSlots = MaxZones * MaxPlansPerZone;
static constexpr uint8_t ScheduleQueueCapacity = 12;
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
    FLOW_LOW_STOPPED = 3,
    FLOW_NO_PULSE_TIMEOUT = 4,
    FLOW_HIGH_STOPPED = 5,
    FACTORY_RESET_PROTECTED = 6,
    CONFIG_INVALID = 7,
    REJECTED = 8,
    IDLE_FLOW_PROTECTED = 9,
};

enum class ZoneErrorCode : uint8_t {
    NONE = 0,
    FLOW_LOW = 1,
    FLOW_NO_PULSE_TIMEOUT = 2,
    FLOW_HIGH = 3,
    CONFIG_INVALID = 4,
    IDLE_FLOW_DETECTED = 5,
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

enum class ParameterSource : uint8_t {
    NONE = 0,
    MANUAL = 1,
    SINGLE_POINT = 2,
    MULTI_POINT = 3,
    LEARNED = 4,
};

enum class FlowFaultAction : uint8_t {
    RECORD_ONLY = 1,
    STOP_ZONE = 2,
};

struct FlowMeterCalibrationProfile {
    ParameterSource source;
    uint8_t reserved0[3];
    int32_t kUlPerMinPerHz;
    int32_t offsetMilliHz;
    uint32_t warningFreqMilliHz;
    uint32_t minValidFreqMilliHz;
    uint32_t maxValidFreqMilliHz;
    uint16_t pressurizeSec;
    uint16_t sampleWindowSec;
    uint32_t updatedAt;
};

struct ZoneFlowBaselineProfile {
    ParameterSource source;
    uint8_t reserved0[3];
    uint32_t learnedFlowMlPerMin;
    uint16_t lowFlowPermille;
    uint16_t highFlowPermille;
    uint16_t flowFaultConfirmSec;
    FlowFaultAction lowFlowAction;
    FlowFaultAction highFlowAction;
    uint16_t noPulseTimeoutSec;
    uint32_t updatedAt;
};

struct FlowMeterConfig {
    uint8_t flowId;
    uint8_t pulsePin;
    bool enabled;
    bool hasPendingCalibration;
    bool hasRollbackCalibration;
    uint8_t reserved0[3];
    FlowMeterCalibrationProfile activeCalibration;
    FlowMeterCalibrationProfile pendingCalibration;
    FlowMeterCalibrationProfile rollbackCalibration;
};

struct ZoneConfig {
    uint8_t zoneId;
    char name[NameMaxBytes];
    uint8_t valvePin;
    uint8_t flowId;
    bool enabled;
    bool hasLearnedBaseline;
    bool hasPendingBaseline;
    bool hasRollbackBaseline;
    uint8_t reserved0[2];
    ZoneFlowBaselineProfile activeBaseline;
    ZoneFlowBaselineProfile pendingBaseline;
    ZoneFlowBaselineProfile rollbackBaseline;
};

struct SystemConfig {
    uint32_t maxWateringDurationSec;
    uint16_t scheduleGraceSec;
    uint16_t queuedPlanMaxDelaySec;
    uint32_t manualDefaultDurationSec;
    uint16_t idleLeakWindowSec;
    uint16_t idleLeakPulseThreshold;
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
    uint8_t flowId;
    uint8_t reserved0[3];
    FlowMeterCalibrationProfile calibration;
    ZoneFlowBaselineProfile baseline;
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
    uint16_t flowSampleWindowSec;
    uint16_t reserved;
};

struct TaskRuntime {
    uint32_t lastPulseCount;
    uint32_t lastPulseMs;
    uint32_t runningStartedMs;
    uint32_t maxFlowMlPerMin;
    uint32_t maxFlowFirstAtSec;
    uint32_t minFlowMlPerMin;
    uint32_t minFlowFirstAtSec;
    bool firstPulseSeen;
    bool flowStatsValid;
    uint8_t reserved[2];
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
    uint32_t flowMlPerMin;
    bool flowRateReady;
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
        case TaskResult::FLOW_LOW_STOPPED: return "flow_low_stopped";
        case TaskResult::FLOW_NO_PULSE_TIMEOUT: return "flow_no_pulse_timeout";
        case TaskResult::FLOW_HIGH_STOPPED: return "flow_high_stopped";
        case TaskResult::FACTORY_RESET_PROTECTED: return "factory_reset_protected";
        case TaskResult::CONFIG_INVALID: return "config_invalid";
        case TaskResult::REJECTED: return "rejected";
        case TaskResult::IDLE_FLOW_PROTECTED: return "idle_flow_protected";
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
