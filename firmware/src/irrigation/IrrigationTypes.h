#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BoardPins.h"

constexpr uint32_t kIrrigationConfigSchemaVersion = 3;
constexpr std::size_t kWateringPlanCount = 8;
constexpr std::size_t kPlanStartTimeCount = 4;
constexpr std::size_t kFlowHistorySampleCount = 120;
constexpr std::size_t kLearningDecisionWindowCount = 5;
constexpr std::size_t kLearningHistoryWindowCount = 10;
constexpr uint16_t kUnusedStartMinute = 0xFFFF;
constexpr std::size_t kObjectNameCapacity = 64;

enum class FlowAlertAction : uint8_t {
    AlertOnly = 0,
    StopWatering = 1,
};

struct ValveDriveConfig {
    uint16_t pullInTimeMs;
    uint32_t pwmFrequencyHz;
    uint8_t holdDutyPercent;
};

struct PumpConfig {
    bool enabled;
    uint16_t startDelayMs;
    uint16_t stopToValveCloseDelayMs;
};

struct FlowMeterConfig {
    uint32_t pulsesPerLiterX100;
    uint32_t calibrationStartupPulseCount;
    uint32_t calibrationStartupWaterMl;
};

struct CalibrationStabilityConfig {
    uint8_t windowSec;
    uint8_t requiredWindows;
    uint8_t allowedVariationPercent;
};

struct FlowProtectionConfig {
    uint16_t flowStartTimeoutSec;
    uint16_t noFlowTimeoutSec;
    uint16_t unexpectedFlowDelaySec;
    uint16_t unexpectedFlowWindowSec;
    uint16_t unexpectedFlowPulseCount;
    uint16_t flowDeviationConfirmSec;
    uint16_t lowFlowPercent;
    uint16_t highFlowPercent;
    FlowAlertAction lowFlowAction;
    FlowAlertAction highFlowAction;
};

struct TimeSafetyConfig {
    uint8_t rtcRollbackThresholdMinutes;
    uint8_t aliveCheckpointHours;
};

struct ZoneConfig {
    uint8_t id;
    bool enabled;
    std::array<char, kObjectNameCapacity> name;
    uint32_t baselinePulseRateX10000;
};

struct WateringPlan {
    uint8_t id;
    bool configured;
    bool scheduleEnabled;
    std::array<char, kObjectNameCapacity> name;
    std::array<uint16_t, kPlanStartTimeCount> startMinutes;
    std::array<uint16_t, BoardPins::kZoneCount> zoneDurationMinutes;
};

struct IrrigationConfig {
    uint32_t schemaVersion;
    uint32_t revision;
    ValveDriveConfig valveDrive;
    PumpConfig pump;
    FlowMeterConfig flowMeter;
    CalibrationStabilityConfig calibrationStability;
    FlowProtectionConfig flowProtection;
    TimeSafetyConfig timeSafety;
    std::array<ZoneConfig, BoardPins::kZoneCount> zones;
    std::array<WateringPlan, kWateringPlanCount> plans;
};

enum class WateringSource : uint8_t {
    ManualZones = 0,
    AutomaticPlan = 2,
};

enum class WateringPurpose : uint8_t {
    Normal = 0,
    FlowCalibration,
    ZoneFlowLearning,
};

enum class WateringState : uint8_t {
    Idle = 0,
    StartingZone,
    WaitingForFlow,
    WateringZone,
    StoppingZone,
};

enum class WateringResult : uint8_t {
    None = 0,
    Completed,
    Stopped,
    Failed,
};

enum class WateringStopReason : uint8_t {
    None = 0,
    Completed,
    UserStopped,
    FlowStartTimeout,
    NoFlowTimeout,
    LowFlow,
    HighFlow,
    LearningTimeout,
    HardwareFailure,
    MaintenanceInterrupted,
};

enum class WateringStartResult : uint8_t {
    Started = 0,
    NotReady,
    Busy,
    PreviousResultPending,
    InvalidRequest,
    HardwareFailure,
};

enum class AutomaticWateringMode : uint8_t {
    Enabled = 0,
    PausedIndefinitely,
    PausedUntil,
};

struct AutomaticWateringState {
    AutomaticWateringMode mode;
    uint32_t resumeAtEpoch;
};

enum class ZoneWateringResult : uint8_t {
    NotStarted = 0,
    Completed,
    Stopped,
    Failed,
};

struct WateringStep {
    uint8_t zoneId;
    uint32_t targetDurationSec;
};

struct WateringRequest {
    WateringSource source;
    WateringPurpose purpose;
    uint8_t planId;
    uint8_t stepCount;
    std::array<WateringStep, BoardPins::kZoneCount> steps;
};

struct ZoneWateringSummary {
    uint8_t zoneId;
    ZoneWateringResult result;
    uint32_t plannedDurationSec;
    uint32_t actualWateringSec;
    uint32_t pulseCount;
    uint32_t estimatedWaterMl;
    uint32_t averageFlowMlPerMinute;
    uint32_t baselineFlowMlPerMinute;
    uint32_t terminalFlowMlPerMinute;
    uint32_t terminalMinimumFlowMlPerMinute;
    uint32_t terminalMaximumFlowMlPerMinute;
    uint32_t lowFlowDetectedMlPerMinute;
    uint32_t highFlowDetectedMlPerMinute;
    bool waterEstimateCapped;
    bool flowBaselineAvailable;
    bool lowFlowDetected;
    bool highFlowDetected;
    bool lowFlowActive;
    bool highFlowActive;
    bool terminalFlowAvailable;
    bool terminalFlowStable;
    uint32_t suggestedBaselinePulseRateX10000;
    uint32_t calibrationFlowEstablishedMs;
    uint32_t calibrationSteadyStartedMs;
    uint32_t calibrationStartupPulses;
    uint32_t calibrationSteadyDurationMs;
    uint32_t calibrationSteadyPulses;
    uint32_t calibrationStopDurationMs;
    uint32_t calibrationStopPulses;
    uint32_t calibrationPulseRateX100;
    uint32_t calibrationLatestPulseRateX100;
    uint8_t calibrationWindowSec;
    uint8_t calibrationRequiredWindows;
    uint8_t calibrationAllowedVariationPercent;
    uint8_t calibrationCollectedWindows;
    bool calibrationSteadyDetected;
    bool calibrationSteadyLaterUnstable;
};

struct WateringStatus {
    bool active;
    WateringState state;
    WateringSource source;
    uint8_t planId;
    uint8_t stepCount;
    uint8_t activeZoneId;
    uint8_t lastZoneId;
    uint8_t currentStepIndex;
    bool flowEstablished;
    WateringResult lastResult;
    WateringStopReason lastStopReason;
    WateringPurpose purpose;
    uint32_t elapsedSec;
    uint32_t currentZoneElapsedSec;
    uint32_t currentZoneRemainingSec;
    uint32_t plannedRemainingSec;
    uint32_t pulseCount;
    uint32_t currentFlowMlPerMinute;
    uint32_t expectedFlowMlPerMinute;
    uint64_t totalEstimatedWaterMl;
    uint32_t learningAverageMlPerMinute;
    uint32_t learningMinimumMlPerMinute;
    uint32_t learningMaximumMlPerMinute;
    uint32_t learningAveragePulseRateX100;
    uint32_t learningMinimumPulseRateX100;
    uint32_t learningMaximumPulseRateX100;
    uint32_t learningAllowedPulseRateSpreadX100;
    uint8_t learningWindowCount;
    uint32_t learningTotalWindowCount;
    struct LearningWindowSample {
        uint32_t sequence;
        uint32_t pulseCount;
        uint32_t windowMs;
        uint32_t pulseRateX100;
        uint32_t flowMlPerMinute;
    };
    std::array<LearningWindowSample, kLearningHistoryWindowCount> learningWindows;
    uint32_t flowHistoryGeneration;
    uint32_t flowSampleSerial;
    std::array<ZoneWateringSummary, BoardPins::kZoneCount> zones;
};

struct FlowHistorySnapshot {
    uint8_t zoneId;
    uint16_t sampleCount;
    uint32_t generation;
    uint32_t latestSerial;
    std::array<uint32_t, kFlowHistorySampleCount> samples;
};

struct WateringSessionSummary {
    WateringSource source;
    WateringPurpose purpose;
    uint8_t planId;
    uint8_t zoneCount;
    uint32_t elapsedSec;
    WateringResult result;
    WateringStopReason stopReason;
    bool anyFlowEstablished;
    std::array<ZoneWateringSummary, BoardPins::kZoneCount> zones;
};
