#include "IrrigationConfig.h"

#include <Esp32Base.h>
#include <stdio.h>

namespace irrigation {

namespace {

constexpr uint16_t kConfigVersion = 1;
constexpr const char* kConfigKey = "data";

struct ZoneConfigStore {
    uint16_t version;
    ZoneConfig zones[kMaxZones];
};

struct PlanGroupConfigStore {
    uint16_t version;
    PlanGroupConfig plans[kMaxPlanGroups];
};

static_assert(sizeof(SystemConfig) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN, "SystemConfig is too large for Esp32BaseConfig");
static_assert(sizeof(ZoneConfigStore) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN, "ZoneConfigStore is too large for Esp32BaseConfig");
static_assert(sizeof(PlanGroupConfigStore) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN, "PlanGroupConfigStore is too large for Esp32BaseConfig");
static_assert(sizeof(FlowValveConfig) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN, "FlowValveConfig is too large for Esp32BaseConfig");
static_assert(sizeof(FaultPolicyConfig) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN, "FaultPolicyConfig is too large for Esp32BaseConfig");

void setName(char* out, size_t len, const char* prefix, uint8_t id) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%s%u", prefix, static_cast<unsigned>(id));
    out[len - 1] = '\0';
}

template <typename T>
bool loadVersionedPod(const char* ns, const char* key, T& out, const T& fallback) {
    T loaded = fallback;
    if (!Esp32BaseConfig::getPod(ns, key, loaded, fallback) || loaded.version != fallback.version) {
        out = fallback;
        return false;
    }
    out = loaded;
    return true;
}

ZoneConfigStore makeZoneStore(const IrrigationConfigSnapshot& snapshot) {
    ZoneConfigStore store = {};
    store.version = kConfigVersion;
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        store.zones[i] = snapshot.zones[i];
    }
    return store;
}

PlanGroupConfigStore makePlanStore(const IrrigationConfigSnapshot& snapshot) {
    PlanGroupConfigStore store = {};
    store.version = kConfigVersion;
    for (uint8_t i = 0; i < kMaxPlanGroups; ++i) {
        store.plans[i] = snapshot.plans[i];
    }
    return store;
}

void applyZoneStore(IrrigationConfigSnapshot& snapshot, const ZoneConfigStore& store) {
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        snapshot.zones[i] = store.zones[i];
    }
}

void applyPlanStore(IrrigationConfigSnapshot& snapshot, const PlanGroupConfigStore& store) {
    for (uint8_t i = 0; i < kMaxPlanGroups; ++i) {
        snapshot.plans[i] = store.plans[i];
    }
}

}  // namespace

bool IrrigationConfig::begin() {
    _snapshot = makeDefaultSnapshot();
    loadPersistedSnapshot();
    _ready = true;
    return _ready;
}

void IrrigationConfig::handle() {
}

const IrrigationConfigSnapshot& IrrigationConfig::snapshot() const {
    return _snapshot;
}

bool IrrigationConfig::saveSnapshot(const IrrigationConfigSnapshot& snapshot) {
    const ZoneConfigStore zoneStore = makeZoneStore(snapshot);
    const PlanGroupConfigStore planStore = makePlanStore(snapshot);

    bool ok = true;
    ok = Esp32BaseConfig::setPod(kNamespaceSystem, kConfigKey, snapshot.system) && ok;
    ok = Esp32BaseConfig::setPod(kNamespaceZone, kConfigKey, zoneStore) && ok;
    ok = Esp32BaseConfig::setPod(kNamespacePlan, kConfigKey, planStore) && ok;
    ok = Esp32BaseConfig::setPod(kNamespaceFlow, kConfigKey, snapshot.flowValve) && ok;
    ok = Esp32BaseConfig::setPod(kNamespaceFault, kConfigKey, snapshot.faultPolicy) && ok;

    if (ok) {
        _snapshot = snapshot;
    }
    return ok;
}

void IrrigationConfig::loadPersistedSnapshot() {
    const IrrigationConfigSnapshot defaults = makeDefaultSnapshot();

    loadVersionedPod(kNamespaceSystem, kConfigKey, _snapshot.system, defaults.system);
    loadVersionedPod(kNamespaceFlow, kConfigKey, _snapshot.flowValve, defaults.flowValve);
    loadVersionedPod(kNamespaceFault, kConfigKey, _snapshot.faultPolicy, defaults.faultPolicy);

    ZoneConfigStore zoneStore = makeZoneStore(defaults);
    if (loadVersionedPod(kNamespaceZone, kConfigKey, zoneStore, makeZoneStore(defaults))) {
        applyZoneStore(_snapshot, zoneStore);
    } else {
        applyZoneStore(_snapshot, makeZoneStore(defaults));
    }

    PlanGroupConfigStore planStore = makePlanStore(defaults);
    if (loadVersionedPod(kNamespacePlan, kConfigKey, planStore, makePlanStore(defaults))) {
        applyPlanStore(_snapshot, planStore);
    } else {
        applyPlanStore(_snapshot, makePlanStore(defaults));
    }
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
