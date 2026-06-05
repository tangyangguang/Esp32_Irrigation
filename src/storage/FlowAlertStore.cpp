#include "storage/FlowAlertStore.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/ZoneTypes.h"

namespace {

static constexpr const char* kNamespace = "irr_falert_v1";
static constexpr const char* kKey = "state";
static constexpr uint32_t kMagic = 0x4946414CUL;
static constexpr uint16_t kVersion = 1;

struct StoredFlowAlerts {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    FlowAlertStore::FlowAlert flows[Irrigation::MaxFlowMeters];
    uint32_t updatedEpoch;
};

StoredFlowAlerts g_state = {};
FlowAlertStore::FlowAlert g_invalid = {};

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

bool validFlowId(uint8_t flowId, uint8_t* index) {
    if (flowId < 1 || flowId > Irrigation::MaxFlowMeters) {
        return false;
    }
    if (index) {
        *index = static_cast<uint8_t>(flowId - 1);
    }
    return true;
}

bool validStored(const StoredFlowAlerts& stored) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored);
}

bool persist() {
    g_state.updatedEpoch = currentEpoch();
    return Esp32BaseConfig::setPod(kNamespace, kKey, g_state);
}

}

namespace FlowAlertStore {

void begin() {
    resetState();
    StoredFlowAlerts stored = {};
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

bool anyIdleLeakActive() {
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        if (idleLeakActive(flowId)) {
            return true;
        }
    }
    return false;
}

bool idleLeakActive(uint8_t flowId) {
    uint8_t index = 0;
    return validFlowId(flowId, &index) && g_state.flows[index].idleLeakActive;
}

const FlowAlert& get(uint8_t flowId) {
    uint8_t index = 0;
    if (!validFlowId(flowId, &index)) {
        return g_invalid;
    }
    return g_state.flows[index];
}

bool setIdleLeak(uint8_t flowId, uint32_t observedPulses, uint16_t pulseThreshold, uint16_t windowSec) {
    uint8_t index = 0;
    if (!validFlowId(flowId, &index)) {
        return false;
    }
    FlowAlert& alert = g_state.flows[index];
    alert = {};
    alert.idleLeakActive = true;
    alert.observedPulses = observedPulses;
    alert.pulseThreshold = pulseThreshold;
    alert.windowSec = windowSec;
    alert.occurredEpoch = currentEpoch();
    alert.occurredUptimeMs = millis();
    return persist();
}

bool clearFlow(uint8_t flowId) {
    uint8_t index = 0;
    if (!validFlowId(flowId, &index)) {
        return false;
    }
    g_state.flows[index] = {};
    return persist();
}

bool clearAll() {
    for (uint8_t i = 0; i < Irrigation::MaxFlowMeters; ++i) {
        g_state.flows[i] = {};
    }
    return persist();
}

}
