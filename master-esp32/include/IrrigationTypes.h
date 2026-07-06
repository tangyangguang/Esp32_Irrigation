#pragma once

#include <stdint.h>

namespace Irrigation {

constexpr uint8_t kMaxZones = 6;
constexpr uint8_t kDefaultEnabledZones = 1;
constexpr uint8_t kMaxPlans = 8;
constexpr uint8_t kMaxPlanStartTimes = 4;
constexpr uint8_t kMaxRunSteps = kMaxZones;

constexpr uint8_t kZoneNameLength = 24;
constexpr uint8_t kPlanNameLength = 32;
constexpr uint16_t kMinutesPerDay = 1440;
constexpr uint16_t kInvalidMinuteOfDay = 0xFFFF;

enum class ContactType : uint8_t {
    NormallyOpen = 0,
    NormallyClosed = 1,
};

enum class RunSource : uint8_t {
    Manual = 0,
    Plan = 1,
    RunPlanNow = 2,
    Calibration = 3,
};

enum class RunState : uint8_t {
    Idle = 0,
    Precheck,
    OpenValve,
    PumpSignalOn,
    PumpStartDelay,
    FlowGrace,
    Running,
    PumpSignalOff,
    PumpStopDelay,
    CloseValve,
    AdvanceStep,
    Finished,
};

enum class RunResult : uint8_t {
    None = 0,
    Completed,
    UserStopped,
    FaultStopped,
    Skipped,
};

enum class RunReason : uint16_t {
    None = 0,
    ManualRequest,
    PlanStartTime,
    RunPlanNow,
    CalibrationRequest,
    UserStop,
    NoEffectiveStep,
    ControllerBusy,
    PlanDisabled,
    ZoneDisabled,
    InvalidDuration,
    ConfigInvalid,
    TimeInvalid,
    NoFlow,
    LowLevel,
    RebootRecoveredSafe,
};

struct StartTime {
    bool enabled;
    uint16_t minuteOfDay;
};

struct ZoneConfig {
    uint8_t id;
    bool enabled;
    char name[kZoneNameLength];
    uint32_t defaultDurationSec;
    uint32_t standardFlowMlPerMin;
    uint8_t valveIndex;
};

struct WateringPlan {
    uint8_t id;
    bool used;
    bool enabled;
    char name[kPlanNameLength];
    StartTime startTimes[kMaxPlanStartTimes];
    uint32_t zoneDurationSec[kMaxZones];
};

struct WateringStep {
    uint8_t zoneId;
    uint32_t targetDurationSec;
};

struct WateringRun {
    uint32_t id;
    RunSource source;
    uint8_t planId;
    RunState state;
    RunResult result;
    RunReason reason;
    WateringStep steps[kMaxRunSteps];
    uint8_t stepCount;
    uint8_t currentStep;
    uint32_t startedAtEpoch;
    uint32_t finishedAtEpoch;
    uint32_t stateEnteredMs;
};

struct SupplyConfig {
    bool pumpEnabled;
    uint32_t pumpStartDelayMs;
    uint32_t pumpStopDelayMs;
    bool lowLevelEnabled;
    ContactType lowLevelContactType;
    uint32_t lowLevelDebounceMs;
};

struct FlowConfig {
    uint32_t pulsesPerLiter;
    uint32_t startupGraceSec;
    uint32_t noFlowConfirmSec;
    uint32_t leakWindowSec;
    uint32_t leakPulseThreshold;
    uint8_t lowFlowPercent;
    uint16_t highFlowPercent;
    uint32_t lowHighFlowConfirmSec;
};

struct ValveConfig {
    uint32_t pullInMs;
    uint8_t holdPercent;
    uint32_t maxZoneDurationSec;
};

struct IrrigationConfig {
    uint16_t version;
    SupplyConfig supply;
    FlowConfig flow;
    ValveConfig valve;
    ZoneConfig zones[kMaxZones];
    WateringPlan plans[kMaxPlans];
};

struct StatusSnapshot {
    bool configValid;
    bool busy;
    RunState runState;
    RunResult runResult;
    uint8_t activeZoneId;
    uint32_t currentFlowMlPerMin;
    uint32_t currentRunVolumeMl;
    uint32_t todayVolumeMl;
    uint8_t enabledZoneCount;
    uint8_t enabledPlanCount;
    uint32_t nextRunEpoch;
};

struct ZoneSnapshot {
    uint8_t id;
    bool enabled;
    const char* name;
    uint32_t defaultDurationSec;
    uint32_t standardFlowMlPerMin;
    uint32_t lastRunEpoch;
};

struct PlanSnapshot {
    uint8_t id;
    bool used;
    bool enabled;
    const char* name;
    uint16_t startTimes[kMaxPlanStartTimes];
    uint8_t startTimeCount;
    uint32_t zoneDurationSec[kMaxZones];
    uint32_t nextRunEpoch;
};

} // namespace Irrigation
