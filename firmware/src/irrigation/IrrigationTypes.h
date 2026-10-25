#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BoardPins.h"

constexpr uint32_t kIrrigationConfigSchemaVersion = 5;
constexpr std::size_t kCommandIdTextLength = 36;
constexpr std::size_t kWateringPlanCount = 8;
constexpr std::size_t kPlanStartTimeCount = 4;
// Real-time curve: one 5 s sample, ring of the current zone's last hour.
constexpr std::size_t kFlowHistorySampleCount = 720;
// Whole-task overview persisted for the last watering: one point per 5 s rate
// window on a single task timeline shared by all zones, capped at the first
// six hours (4320 points). The running UI only polls the last 720 (1 h).
constexpr std::size_t kFlowOverviewSampleCount = 4320;
constexpr std::size_t kFlowOverviewTailCount = 720;  // running view: last hour
constexpr uint8_t kFlowOverviewSecondsPerPoint = 5;
constexpr std::size_t kLearningDecisionWindowCount = 5;
constexpr std::size_t kLearningHistoryWindowCount = 10;
constexpr uint16_t kUnusedStartMinute = 0xFFFF;
constexpr std::size_t kObjectNameCapacity = 64;
constexpr uint16_t kDefaultMaximumZoneDurationMinutes = 120;
constexpr uint16_t kMaximumConfigurableZoneDurationMinutes = 720;
constexpr uint16_t kDefaultMaximumSingleOutputLiters = 100;
constexpr uint16_t kMaximumConfigurableSingleOutputLiters = 1000;

enum class FlowAlertAction : uint8_t {
    AlertOnly = 0,
    StopWatering = 1,
};

struct ValveDriveConfig {
    uint16_t pullInTimeMs;
    uint16_t switchDelayMs;
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

struct RunLimitsConfig {
    uint16_t maximumZoneDurationMinutes;
    uint16_t maximumSingleOutputLiters;
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

struct IrrigationParameters {
    ValveDriveConfig valveDrive;
    PumpConfig pump;
    FlowMeterConfig flowMeter;
    FlowProtectionConfig flowProtection;
    TimeSafetyConfig timeSafety;
    RunLimitsConfig runLimits;
};

// Runtime configuration combines system parameters with editable plans/zones.
// Parameters are persisted by App Config; JSON only persists the members below.
struct IrrigationConfig : IrrigationParameters {
    uint32_t schemaVersion;
    uint32_t revision;
    std::array<ZoneConfig, BoardPins::kZoneCount> zones;
    std::array<WateringPlan, kWateringPlanCount> plans;
};

// Manual entries carry the real entry point (local web page vs. WeChat
// miniprogram through the platform); automatic plans stay a distinct source.
enum class WateringSource : uint8_t {
    LocalWeb = 0,
    AutomaticPlan = 1,
    WechatMiniprogram = 2,
};

constexpr bool isManualWateringSource(WateringSource source) {
    return source == WateringSource::LocalWeb ||
           source == WateringSource::WechatMiniprogram;
}

// Duration: every step runs for a duration. Volume: a single volume step.
// Mixed: one manual request contains both duration and volume steps.
enum class WateringTargetMode : uint8_t { Duration = 0, Volume = 1, Mixed = 2 };

enum class WateringPurpose : uint8_t {
    Normal = 0,
    ZoneFlowLearning,
};

enum class WateringState : uint8_t {
    Idle = 0,
    StartingZone,
    WaitingForFlow,
    WateringZone,
    StoppingZone,
    SwitchingZone,
};

enum class WateringResult : uint8_t {
    None = 0,
    Completed,
    Stopped,
    Failed,
    Incomplete,
    // The scheduler reached a plan start point but the task was never
    // accepted (busy, not ready or invalid). No physical watering happened;
    // all zones stay not-started and duration/water are zero.
    StartFailed,
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
    TargetVolumeTimeout,
    RebootInterrupted,
    // Schedule start was rejected before any watering began (StartFailed).
    BusyManualWatering,
    BusyAutomaticWatering,
    BusyZoneFlowLearning,
    PreviousResultPending,
    ControllerNotReady,
    InvalidRequest,
};

enum class WateringStartResult : uint8_t {
    Started = 0,
    NotReady,
    Busy,
    PreviousResultPending,
    InvalidRequest,
    ZoneUnavailable,
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
    uint32_t targetWaterMl;
};

struct WateringRequest {
    WateringSource source;
    WateringTargetMode targetMode;
    WateringPurpose purpose;
    uint8_t planId;
    std::array<char, kObjectNameCapacity> planName;
    // Platform command UUID (36 chars, no terminator needed for fixed length);
    // all zeros/empty for local web and automatic plans.
    std::array<char, kCommandIdTextLength> commandId{};
    uint8_t stepCount;
    std::array<WateringStep, BoardPins::kZoneCount> steps;
};

struct ZoneWateringSummary {
    uint32_t startedOffsetSec;
    uint8_t zoneId;
    std::array<char, kObjectNameCapacity> zoneName;
    ZoneWateringResult result;
    uint32_t plannedDurationSec;
    uint32_t targetWaterMl;
    uint32_t actualWateringSec;
    uint32_t pulseCount;
    uint32_t estimatedWaterMl;
    uint32_t averageFlowMlPerMinute;
    uint32_t baselinePulseRateX10000;
    uint32_t baselineFlowMlPerMinute;
    bool waterEstimateCapped;
    bool flowBaselineAvailable;
    bool lowFlowDetected;
    bool highFlowDetected;
    bool lowFlowActive;
    bool highFlowActive;
    bool terminalFlowAvailable;
    bool terminalFlowStable;
    uint32_t suggestedBaselinePulseRateX10000;
};

struct WateringStatus {
    bool active;
    WateringState state;
    WateringSource source;
    WateringTargetMode targetMode;
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
    uint32_t elapsedMs;
    uint32_t currentZoneElapsedSec;
    uint32_t currentZoneElapsedMs;
    uint32_t currentZoneRemainingSec;
    uint32_t currentZoneRemainingMs;
    uint32_t currentZoneTargetWaterMl;
    uint32_t plannedRemainingSec;
    uint32_t plannedRemainingMs;
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

// Real-time curve metadata. Sample values are read separately in small
// segments so the multi-hundred-point ring is never copied onto a task stack.
struct FlowHistoryInfo {
    uint8_t zoneId;
    uint16_t sampleCount;
    uint32_t generation;
    uint32_t latestSerial;
};

// Samples per short critical-section read / HTTP chunk.
constexpr uint16_t kFlowHistoryReadSegment = 32;

// Whole-task overview metadata. Points sit on a single task timeline (zone 0's
// points first, then zone 1, ...); zonePointCount[] gives each segment's size.
struct FlowOverviewInfo {
    uint8_t zoneCount;
    uint16_t pointCount;
    uint32_t startedEpoch;
    // 0 while a task is running; set to the stored record id once bookkeeping
    // completes, so a detail view can prove curve and record are the same task.
    uint32_t recordId;
    std::array<uint16_t, BoardPins::kZoneCount> zonePointCount{};
    // Per-zone baseline flow captured at task start (0 = no baseline).
    std::array<uint32_t, BoardPins::kZoneCount> zoneBaselineFlow{};
};

struct WateringSessionSummary {
    WateringSource source;
    WateringTargetMode targetMode;
    WateringPurpose purpose;
    uint8_t planId;
    std::array<char, kObjectNameCapacity> planName;
    std::array<char, kCommandIdTextLength> commandId{};
    uint8_t zoneCount;
    uint32_t elapsedSec;
    WateringResult result;
    WateringStopReason stopReason;
    bool anyFlowEstablished;
    std::array<ZoneWateringSummary, BoardPins::kZoneCount> zones;
};
