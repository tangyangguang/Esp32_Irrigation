#include "IrrigationConfig.h"

#include <stdio.h>

namespace irrigation {

namespace {

constexpr uint16_t kConfigVersion = 1;

void setName(char* out, size_t len, const char* prefix, uint8_t id) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%s%u", prefix, static_cast<unsigned>(id));
    out[len - 1] = '\0';
}

}  // namespace

bool IrrigationConfig::begin() {
    _snapshot = makeDefaultSnapshot();
    _ready = true;
    return _ready;
}

void IrrigationConfig::handle() {
}

const IrrigationConfigSnapshot& IrrigationConfig::snapshot() const {
    return _snapshot;
}

IrrigationConfigSnapshot IrrigationConfig::makeDefaultSnapshot() {
    IrrigationConfigSnapshot snapshot = {};

    snapshot.system.version = kConfigVersion;
    snapshot.system.autoMode = AutoMode::Disabled;
    snapshot.system.autoResumeAt = 0;
    snapshot.system.pumpStartEnabled = false;
    snapshot.system.lowLevelEnabled = false;
    snapshot.system.queuedPlanMaxDelayMin = kDefaultQueuedPlanMaxDelayMin;

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        ZoneConfig& zone = snapshot.zones[i];
        zone.version = kConfigVersion;
        zone.zoneId = static_cast<uint8_t>(i + 1);
        zone.enabled = (i == 0);
        setName(zone.name, sizeof(zone.name), "Zone ", zone.zoneId);
        zone.defaultManualDurationMin = kDefaultManualDurationMin;
        zone.normalFlowMlPerMin = 0;
        zone.lowFlowPercent = kDefaultLowFlowPercent;
        zone.highFlowPercent = kDefaultHighFlowPercent;
        zone.flowFaultConfirmSec = kDefaultFlowFaultConfirmSec;
        zone.normalFlowMeasuredAt = 0;
    }

    for (uint8_t i = 0; i < kMaxPlanGroups; ++i) {
        PlanGroupConfig& plan = snapshot.plans[i];
        plan.version = kConfigVersion;
        plan.planGroupId = static_cast<uint8_t>(i + 1);
        plan.enabled = false;
        setName(plan.name, sizeof(plan.name), "Plan ", plan.planGroupId);
        for (uint8_t startIndex = 0; startIndex < kMaxStartTimesPerPlan; ++startIndex) {
            plan.startTimes[startIndex].enabled = false;
            plan.startTimes[startIndex].minuteOfDay = 0;
        }
        plan.cycleLengthDays = 1;
        plan.cycleAnchorDate = 0;
        plan.activeDayMask = 0x01;
        for (uint8_t zoneIndex = 0; zoneIndex < kMaxZones; ++zoneIndex) {
            plan.zoneDurationsMin[zoneIndex] = 0;
        }
    }

    snapshot.flowValve.version = kConfigVersion;
    snapshot.flowValve.pulsesPerLiter = 0;
    snapshot.flowValve.calibratedAt = 0;
    snapshot.flowValve.calibrationSampleCount = 0;
    snapshot.flowValve.calibrationTotalPulseCount = 0;
    snapshot.flowValve.calibrationTotalActualMl = 0;
    snapshot.flowValve.flowSampleWindowSec = kDefaultFlowSampleWindowSec;
    snapshot.flowValve.flowUpdateIntervalMs = kDefaultFlowUpdateIntervalMs;
    snapshot.flowValve.firstPulseTimeoutSec = kDefaultFirstPulseTimeoutSec;
    snapshot.flowValve.runningNoPulseTimeoutSec = kDefaultRunningNoPulseTimeoutSec;
    snapshot.flowValve.flowStabilizeSec = kDefaultFlowStabilizeSec;
    snapshot.flowValve.maintenanceMaxDurationSec = kDefaultMaintenanceMaxDurationSec;
    snapshot.flowValve.idleLeakConfirmSec = kDefaultIdleLeakConfirmSec;
    snapshot.flowValve.valvePullInMs = kDefaultValvePullInMs;
    snapshot.flowValve.valveHoldDutyPercent = kDefaultValveHoldDutyPercent;
    snapshot.flowValve.valvePwmFrequencyHz = kDefaultValvePwmFrequencyHz;

    snapshot.faultPolicy.version = kConfigVersion;
    snapshot.faultPolicy.noWaterLockZone = true;
    snapshot.faultPolicy.highFlowAction = FlowFaultAction::Stop;
    snapshot.faultPolicy.highFlowLockZone = true;
    snapshot.faultPolicy.lowFlowAction = FlowFaultAction::Warn;
    snapshot.faultPolicy.lowFlowLockZone = false;

    return snapshot;
}

}  // namespace irrigation
