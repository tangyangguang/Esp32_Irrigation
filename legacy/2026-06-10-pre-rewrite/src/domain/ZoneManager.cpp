#include "domain/ZoneManager.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

#include "domain/BusinessEventLog.h"
#include "domain/FlowCalibration.h"
#include "domain/FlowMeter.h"
#include "domain/ValveController.h"
#include "storage/FlowConfigStore.h"
#include "storage/FlowAlertStore.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"
#include "storage/ZoneErrorStore.h"

namespace {

Zone g_zones[Irrigation::MaxZones];
ZoneScheduler g_schedulers[Irrigation::MaxZones];
Zone g_invalidZone;
uint32_t g_flowLeakWindowStartedMs[Irrigation::MaxFlowMeters] = {};
uint32_t g_flowLeakWindowStartPulses[Irrigation::MaxFlowMeters] = {};

uint8_t indexFor(uint8_t zoneId) {
    return static_cast<uint8_t>(zoneId - 1);
}

uint32_t epochNow() {
#if ESP32BASE_ENABLE_NTP
    return Esp32BaseNtp::isTimeSynced() ? static_cast<uint32_t>(Esp32BaseNtp::timestamp()) : 0;
#else
    return 0;
#endif
}

uint8_t flowIdForZone(uint8_t zoneId) {
    return ZoneConfigStore::get(zoneId).flowId;
}

uint32_t pulseCount(uint8_t zoneId) {
    return FlowMeter::pulseCount(flowIdForZone(zoneId));
}

uint32_t pulseCountForFlow(uint8_t flowId) {
    return FlowMeter::pulseCount(flowId);
}

uint32_t flowMillilitersPerMinute(uint8_t zoneId) {
    return FlowMeter::flowMillilitersPerMinute(flowIdForZone(zoneId));
}

uint64_t pulseRatePerMinuteX1000(uint8_t zoneId) {
    return FlowMeter::pulseRatePerMinuteX1000(flowIdForZone(zoneId));
}

bool flowRateReady(uint8_t zoneId) {
    return FlowMeter::flowRateReady(flowIdForZone(zoneId));
}

bool anyBusy() {
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        if (g_zones[indexFor(zoneId)].isBusy()) {
            return true;
        }
    }
    return false;
}

bool flowBusy(uint8_t flowId) {
    if (flowId < 1 || flowId > Irrigation::MaxFlowMeters) {
        return false;
    }
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Zone& zone = g_zones[indexFor(zoneId)];
        if (zone.isBusy() && zone.config().flowId == flowId) {
            return true;
        }
    }
    return false;
}

bool flowHasEnabledZones(uint8_t flowId) {
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& config = ZoneConfigStore::get(zoneId);
        if (config.enabled && config.flowId == flowId) {
            return true;
        }
    }
    return false;
}

bool canStart(uint8_t zoneId) {
    return strcmp(ZoneManager::blockedReason(zoneId), "none") == 0;
}

void resetFlowLeakWindow(uint8_t flowId, uint32_t nowMs) {
    if (flowId < 1 || flowId > Irrigation::MaxFlowMeters) {
        return;
    }
    const uint8_t index = static_cast<uint8_t>(flowId - 1);
    g_flowLeakWindowStartPulses[index] = pulseCountForFlow(flowId);
    g_flowLeakWindowStartedMs[index] = nowMs;
}

void resetAllFlowLeakWindows(uint32_t nowMs) {
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        resetFlowLeakWindow(flowId, nowMs);
    }
}

void checkIdleLeaks(uint32_t nowMs) {
    if (anyBusy() || FlowCalibration::active()) {
        resetAllFlowLeakWindows(nowMs);
        return;
    }

    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        const uint8_t index = static_cast<uint8_t>(flowId - 1);
        const Irrigation::FlowMeterConfig& flow = FlowConfigStore::get(flowId);
        if (!flow.enabled || !flowHasEnabledZones(flowId) || FlowAlertStore::idleLeakActive(flowId)) {
            resetFlowLeakWindow(flowId, nowMs);
            continue;
        }
        const uint32_t windowMs = static_cast<uint32_t>(system.idleLeakWindowSec) * 1000UL;
        if (nowMs - g_flowLeakWindowStartedMs[index] < windowMs) {
            continue;
        }
        const uint32_t pulses = pulseCountForFlow(flowId);
        const uint32_t observedPulses = pulses >= g_flowLeakWindowStartPulses[index]
            ? pulses - g_flowLeakWindowStartPulses[index]
            : 0;
        resetFlowLeakWindow(flowId, nowMs);
        if (observedPulses >= system.idleLeakPulseThreshold) {
            (void)FlowAlertStore::setIdleLeak(flowId, observedPulses, system.idleLeakPulseThreshold, system.idleLeakWindowSec);
            ValveController::allOff("flow idle leak");
            BusinessEventLog::appendFlowIdleLeakDetected(flowId, observedPulses, system.idleLeakPulseThreshold, system.idleLeakWindowSec);
        }
    }
}

}

namespace ZoneManager {

void begin() {
    const uint32_t nowMs = millis();
    resetAllFlowLeakWindows(nowMs);
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        g_zones[indexFor(zoneId)].begin(ZoneConfigStore::get(zoneId), ZoneErrorStore::get(zoneId), nowMs);
        g_schedulers[indexFor(zoneId)].begin(zoneId);
    }
}

void handle() {
    const uint32_t nowMs = millis();
    const uint32_t epoch = epochNow();
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        const Irrigation::FlowMeterConfig& flow = FlowConfigStore::get(flowId);
        FlowMeter::configureCalibration(flowId, flow.activeCalibration.kUlPerMinPerHz, flow.activeCalibration.offsetMilliHz);
    }
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        Zone& z = g_zones[indexFor(zoneId)];
        z.tick(pulseCount(zoneId), flowMillilitersPerMinute(zoneId), flowRateReady(zoneId), epoch, nowMs);
        g_schedulers[indexFor(zoneId)].tick(z, system, FlowAlertStore::idleLeakActive(z.config().flowId), epoch, nowMs);
    }
    checkIdleLeaks(nowMs);
}

bool reloadZone(uint8_t zoneId) {
    if (!Irrigation::validZoneId(zoneId)) {
        return false;
    }
    g_zones[indexFor(zoneId)].applyConfig(ZoneConfigStore::get(zoneId), epochNow(), millis());
    return true;
}

bool startManual(uint8_t zoneId, uint32_t durationSec, Irrigation::StartSource source) {
    if (!canStart(zoneId)) {
        return false;
    }
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    return g_zones[indexFor(zoneId)].start(Irrigation::TaskType::MANUAL,
                                           source,
                                           Irrigation::NoPlanId,
                                           Irrigation::NoPlanSlot,
                                           "",
                                           durationSec,
                                           system.maxWateringDurationSec,
                                           5,
                                           pulseCount(zoneId),
                                           epochNow(),
                                           millis());
}

bool startPlan(uint8_t zoneId,
               uint32_t planId,
               uint8_t planSlot,
               const char* planName,
               uint32_t durationSec,
               uint32_t trustedEpoch,
               uint32_t nowMs) {
    if (!canStart(zoneId)) {
        return false;
    }
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    return g_zones[indexFor(zoneId)].start(Irrigation::TaskType::PLAN,
                                           Irrigation::StartSource::SCHEDULER,
                                           planId,
                                           planSlot,
                                           planName,
                                           durationSec,
                                           system.maxWateringDurationSec,
                                           5,
                                           pulseCount(zoneId),
                                           trustedEpoch,
                                           nowMs);
}

bool stopZone(uint8_t zoneId, Irrigation::StopSource source, Irrigation::TaskResult result) {
    if (!Irrigation::validZoneId(zoneId)) {
        return false;
    }
    return g_zones[indexFor(zoneId)].stop(source,
                                          Irrigation::StopScope::ZONE,
                                          result,
                                          pulseCount(zoneId),
                                          epochNow(),
                                          millis());
}

uint8_t stopAll(Irrigation::StopSource source, Irrigation::TaskResult result) {
    uint8_t stopped = 0;
    const uint32_t nowMs = millis();
    const uint32_t epoch = epochNow();
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        if (g_zones[indexFor(zoneId)].stop(source, Irrigation::StopScope::ALL, result, pulseCount(zoneId), epoch, nowMs)) {
            ++stopped;
        }
    }
    (void)FlowCalibration::abort("stop_all");
    ValveController::allOff("zone manager stop all");
    return stopped;
}

bool clearError(uint8_t zoneId) {
    if (!Irrigation::validZoneId(zoneId)) {
        return false;
    }
    return g_zones[indexFor(zoneId)].clearError(millis());
}

bool clearAllErrors() {
    if (!ZoneErrorStore::clearAllErrors()) {
        return false;
    }
    if (!FlowAlertStore::clearAll()) {
        return false;
    }
    const uint32_t nowMs = millis();
    resetAllFlowLeakWindows(nowMs);
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        g_zones[indexFor(zoneId)].begin(ZoneConfigStore::get(zoneId), ZoneErrorStore::get(zoneId), nowMs);
    }
    return true;
}

bool isBusy() {
    return anyBusy();
}

bool isZoneBusy(uint8_t zoneId) {
    return Irrigation::validZoneId(zoneId) && g_zones[indexFor(zoneId)].isBusy();
}

bool isFlowBusy(uint8_t flowId) {
    return flowBusy(flowId);
}

bool canStartZoneNow(uint8_t zoneId) {
    return canStart(zoneId);
}

const char* blockedReason(uint8_t zoneId) {
    if (!Irrigation::validZoneId(zoneId)) {
        return "invalid_zone";
    }
    if (FlowCalibration::active()) {
        return "calibration_active";
    }
    const Zone& zone = g_zones[indexFor(zoneId)];
    const Irrigation::ZoneConfig& config = zone.config();
    if (!config.enabled) {
        return "zone_disabled";
    }
    if (zone.isError()) {
        return "zone_fault";
    }
    if (FlowAlertStore::idleLeakActive(config.flowId)) {
        return "flow_leak_protected";
    }
    if (zone.isBusy()) {
        return "zone_busy";
    }
    const Irrigation::FlowMeterConfig& flow = FlowConfigStore::get(config.flowId);
    if (!flow.enabled) {
        return "flow_disabled";
    }
    if (flowBusy(config.flowId)) {
        return "flow_busy";
    }
    return "none";
}

bool leakAlertActive() {
    return FlowAlertStore::anyIdleLeakActive();
}

Irrigation::ZoneStatus status(uint8_t zoneId) {
    if (!Irrigation::validZoneId(zoneId)) {
        return {};
    }
    return g_zones[indexFor(zoneId)].status(pulseCount(zoneId),
                                            pulseRatePerMinuteX1000(zoneId),
                                            flowMillilitersPerMinute(zoneId),
                                            flowRateReady(zoneId),
                                            millis());
}

const Irrigation::ZoneConfig& config(uint8_t zoneId) {
    return ZoneConfigStore::get(zoneId);
}

uint32_t trustedEpoch() {
    return epochNow();
}

Zone& zone(uint8_t zoneId) {
    if (!Irrigation::validZoneId(zoneId)) {
        return g_invalidZone;
    }
    return g_zones[indexFor(zoneId)];
}

}
