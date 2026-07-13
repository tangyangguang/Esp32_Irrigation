#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "BoardPins.h"

constexpr uint32_t kIrrigationConfigSchemaVersion = 1;
constexpr std::size_t kWateringPlanCount = 8;
constexpr std::size_t kPlanStartTimeCount = 4;
constexpr uint16_t kUnusedStartMinute = 0xFFFF;
constexpr std::size_t kObjectNameCapacity = 32;

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
    uint32_t pulsesPerLiter;
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
    uint32_t learnedFlowMlPerMinute;
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
    FlowProtectionConfig flowProtection;
    TimeSafetyConfig timeSafety;
    std::array<ZoneConfig, BoardPins::kZoneCount> zones;
    std::array<WateringPlan, kWateringPlanCount> plans;
};
