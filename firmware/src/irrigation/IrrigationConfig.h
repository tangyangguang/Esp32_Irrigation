#pragma once

#include <stdint.h>

#include "IrrigationConstants.h"

namespace irrigation {

enum class AutoMode : uint8_t {
    Disabled = 0,
    Enabled = 1,
    DisabledUntil = 2,
};

enum class FlowFaultAction : uint8_t {
    Warn = 0,
    Stop = 1,
};

struct SystemConfig {
    uint16_t version;
    AutoMode autoMode;
    uint32_t autoResumeAt;
    bool pumpStartEnabled;
    bool lowLevelEnabled;
    uint16_t queuedPlanMaxDelayMin;
};

struct ZoneConfig {
    uint16_t version;
    uint8_t zoneId;
    bool enabled;
    char name[16];
    uint16_t defaultManualDurationMin;
    uint32_t normalFlowMlPerMin;
    uint8_t lowFlowPercent;
    uint8_t highFlowPercent;
    uint16_t flowFaultConfirmSec;
    uint32_t normalFlowMeasuredAt;
};

struct PlanStartTime {
    bool enabled;
    uint16_t minuteOfDay;
};

struct PlanGroupConfig {
    uint16_t version;
    uint8_t planGroupId;
    bool enabled;
    char name[16];
    PlanStartTime startTimes[kMaxStartTimesPerPlan];
    uint8_t cycleLengthDays;
    uint32_t cycleAnchorDate;
    uint32_t activeDayMask;
    uint16_t zoneDurationsMin[kMaxZones];
};

struct FlowValveConfig {
    uint16_t version;
    uint32_t pulsesPerLiter;
    uint32_t calibratedAt;
    uint16_t calibrationSampleCount;
    uint32_t calibrationTotalPulseCount;
    uint32_t calibrationTotalActualMl;
    uint16_t flowSampleWindowSec;
    uint16_t flowUpdateIntervalMs;
    uint16_t firstPulseTimeoutSec;
    uint16_t runningNoPulseTimeoutSec;
    uint16_t flowStabilizeSec;
    uint16_t maintenanceMaxDurationSec;
    uint16_t idleLeakConfirmSec;
    uint16_t valvePullInMs;
    uint8_t valveHoldDutyPercent;
    uint32_t valvePwmFrequencyHz;
};

struct FaultPolicyConfig {
    uint16_t version;
    bool noWaterLockZone;
    FlowFaultAction highFlowAction;
    bool highFlowLockZone;
    FlowFaultAction lowFlowAction;
    bool lowFlowLockZone;
};

struct IrrigationConfigSnapshot {
    SystemConfig system;
    ZoneConfig zones[kMaxZones];
    PlanGroupConfig plans[kMaxPlanGroups];
    FlowValveConfig flowValve;
    FaultPolicyConfig faultPolicy;
};

class IrrigationConfig {
public:
    // Owns irrigation business configuration only. Persistence and migration
    // will be added later through Esp32BaseConfig after schemas are frozen.
    bool begin();
    void handle();

    const IrrigationConfigSnapshot& snapshot() const;

    static IrrigationConfigSnapshot makeDefaultSnapshot();

    bool saveSnapshot(const IrrigationConfigSnapshot& snapshot);

private:
    void loadPersistedSnapshot();

    IrrigationConfigSnapshot _snapshot = {};
    bool _ready = false;
};

}  // namespace irrigation
