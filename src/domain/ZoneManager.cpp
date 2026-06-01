#include "domain/ZoneManager.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/BusinessEventLog.h"
#include "domain/FlowCalibration.h"
#include "domain/FlowMeter.h"
#include "domain/ValveController.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"
#include "storage/ZoneErrorStore.h"

namespace {

Zone g_zones[Irrigation::MaxZones];
ZoneScheduler g_schedulers[Irrigation::MaxZones];
Zone g_invalidZone;

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

uint32_t pulseCount(uint8_t zoneId) {
    return FlowMeter::pulseCount(zoneId);
}

bool anyBusy() {
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        if (g_zones[indexFor(zoneId)].isBusy()) {
            return true;
        }
    }
    return false;
}

void checkIdleLeaks(uint32_t nowMs) {
    if (anyBusy() || FlowCalibration::active()) {
        for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
            g_zones[indexFor(zoneId)].resetLeakWindow(pulseCount(zoneId), nowMs);
        }
        return;
    }

    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    if (!system.idleLeakDetectionEnabled) {
        for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
            g_zones[indexFor(zoneId)].resetLeakWindow(pulseCount(zoneId), nowMs);
        }
        return;
    }
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        Zone& z = g_zones[indexFor(zoneId)];
        if (!z.config().enabled || z.isError()) {
            z.resetLeakWindow(pulseCount(zoneId), nowMs);
            continue;
        }
        uint32_t observedPulses = 0;
        if (z.checkIdleLeak(pulseCount(zoneId), nowMs, system.idleLeakWindowSec, system.idleLeakPulseThreshold, &observedPulses)) {
            (void)z.markLeak(pulseCount(zoneId), epochNow(), nowMs);
            BusinessEventLog::appendLeakDetected(zoneId, observedPulses, system.idleLeakPulseThreshold, system.idleLeakWindowSec);
        }
    }
}

}

namespace ZoneManager {

void begin() {
    const uint32_t nowMs = millis();
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        g_zones[indexFor(zoneId)].begin(ZoneConfigStore::get(zoneId), ZoneErrorStore::get(zoneId), nowMs);
        g_schedulers[indexFor(zoneId)].begin(zoneId);
    }
}

void handle() {
    const uint32_t nowMs = millis();
    const uint32_t epoch = epochNow();
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    const bool leakActive = ZoneErrorStore::leakAlertActive();
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        Zone& z = g_zones[indexFor(zoneId)];
        z.tick(pulseCount(zoneId), epoch, nowMs);
        g_schedulers[indexFor(zoneId)].tick(z, system, leakActive, epoch, nowMs);
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
    if (!Irrigation::validZoneId(zoneId) || ZoneErrorStore::leakAlertActive()) {
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
                                           pulseCount(zoneId),
                                           epochNow(),
                                           millis());
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
    const uint32_t nowMs = millis();
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

bool leakAlertActive() {
    return ZoneErrorStore::leakAlertActive();
}

Irrigation::ZoneStatus status(uint8_t zoneId) {
    if (!Irrigation::validZoneId(zoneId)) {
        return {};
    }
    return g_zones[indexFor(zoneId)].status(pulseCount(zoneId), FlowMeter::pulseRatePerMinuteX1000(zoneId), millis());
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
