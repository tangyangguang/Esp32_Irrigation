#include "storage/ZoneErrorStore.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "storage/EventStore.h"

namespace {

static constexpr const char* kNamespace = "irr_zerr";
static constexpr const char* kKey = "state";
static constexpr uint32_t kMagic = 0x495A4552UL;
static constexpr uint16_t kVersion = 1;

struct StoredZoneErrors {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    bool leakAlertActive;
    uint8_t reserved[3];
    ZoneErrorStore::ZoneError zones[Irrigation::MaxZones];
    uint32_t updatedEpoch;
};

StoredZoneErrors g_state = {};
ZoneErrorStore::ZoneError g_invalid = {};

uint32_t currentEpoch() {
#if ESP32BASE_ENABLE_NTP
    return Esp32BaseNtp::isTimeSynced() ? static_cast<uint32_t>(Esp32BaseNtp::timestamp()) : 0;
#else
    return 0;
#endif
}

void resetState() {
    g_state = {};
    g_state.magic = kMagic;
    g_state.version = kVersion;
    g_state.size = sizeof(g_state);
}

bool validStored(const StoredZoneErrors& stored) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored);
}

bool persist() {
    g_state.updatedEpoch = currentEpoch();
    return Esp32BaseConfig::setPod(kNamespace, kKey, g_state);
}

bool hasLeakErrors() {
    for (uint8_t i = 0; i < Irrigation::MaxZones; ++i) {
        if (g_state.zones[i].active && g_state.zones[i].errorCode == Irrigation::ZoneErrorCode::LEAK_DETECTED) {
            return true;
        }
    }
    return false;
}

}

namespace ZoneErrorStore {

void begin() {
    resetState();
    StoredZoneErrors stored = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKey, stored) && validStored(stored)) {
        g_state = stored;
    }
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    resetState();
    return true;
}

bool leakAlertActive() {
    return g_state.leakAlertActive;
}

const ZoneError& get(uint8_t zoneId) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index)) {
        return g_invalid;
    }
    return g_state.zones[index];
}

bool setError(uint8_t zoneId, Irrigation::ZoneErrorCode code, Irrigation::StopSource source, Irrigation::TaskResult result) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index) || code == Irrigation::ZoneErrorCode::NONE) {
        return false;
    }
    ZoneError& error = g_state.zones[index];
    error = {};
    error.active = true;
    error.errorCode = code;
    error.occurredEpoch = currentEpoch();
    error.occurredUptimeMs = millis();
    error.source = source;
    error.result = result;
    if (code == Irrigation::ZoneErrorCode::LEAK_DETECTED) {
        g_state.leakAlertActive = true;
    }
    return persist();
}

bool clearError(uint8_t zoneId) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index)) {
        return false;
    }
    g_state.zones[index] = {};
    if (!hasLeakErrors()) {
        g_state.leakAlertActive = false;
    }
    const bool ok = persist();
    if (ok) {
        (void)EventStore::append(Irrigation::EventType::ALERT_CLEARED,
                                 Irrigation::EventSource::WEB,
                                 zoneId,
                                 0,
                                 0,
                                 0,
                                 "zone error cleared");
    }
    return ok;
}

bool clearAllErrors() {
    for (uint8_t i = 0; i < Irrigation::MaxZones; ++i) {
        g_state.zones[i] = {};
    }
    g_state.leakAlertActive = false;
    const bool ok = persist();
    if (ok) {
        (void)EventStore::append(Irrigation::EventType::ALERT_CLEARED,
                                 Irrigation::EventSource::WEB,
                                 0,
                                 0,
                                 0,
                                 0,
                                 "all errors cleared");
    }
    return ok;
}

}
