#include "storage/FlowConfigStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

#include "domain/BusinessEventLog.h"

namespace {

static constexpr const char* kNamespace = "irr_flow_v1";
static constexpr uint32_t kMagic = 0x49464C57UL;
static constexpr uint16_t kVersion = 1;

struct StoredFlowConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Irrigation::FlowMeterConfig data;
};

static_assert(sizeof(StoredFlowConfig) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN,
              "StoredFlowConfig must fit Esp32BaseConfig blob storage");

Irrigation::FlowMeterConfig g_configs[Irrigation::MaxFlowMeters] = {};
Irrigation::FlowMeterConfig g_invalid = {};
bool g_schemaResetDetected = false;

static constexpr uint8_t kPulsePins[] = {
    IrrigationPins::Flow1,
    IrrigationPins::Flow2,
};

void key(char* out, size_t len, uint8_t flowId) {
    snprintf(out, len, "f%u", static_cast<unsigned>(flowId));
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

Irrigation::FlowMeterCalibrationProfile normalizedCalibration(Irrigation::FlowMeterCalibrationProfile profile) {
    if (profile.source == Irrigation::ParameterSource::NONE) {
        profile.source = Irrigation::ParameterSource::MANUAL;
    }
    return profile;
}

Irrigation::FlowMeterConfig defaultConfig(uint8_t flowId) {
    Irrigation::FlowMeterConfig config = {};
    config.flowId = flowId;
    config.pulsePin = kPulsePins[flowId - 1];
    config.enabled = flowId == 1;
    config.activeCalibration = FlowConfigStore::defaultCalibration();
    return config;
}

StoredFlowConfig wrap(const Irrigation::FlowMeterConfig& config) {
    StoredFlowConfig stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.data = config;
    return stored;
}

bool validStored(const StoredFlowConfig& stored) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored) &&
           FlowConfigStore::validate(stored.data);
}

}

namespace FlowConfigStore {

Irrigation::FlowMeterCalibrationProfile defaultCalibration() {
    Irrigation::FlowMeterCalibrationProfile profile = {};
    profile.source = Irrigation::ParameterSource::MANUAL;
    profile.kUlPerMinPerHz = 244897;
    profile.offsetMilliHz = 0;
    profile.warningFreqMilliHz = 4000;
    profile.minValidFreqMilliHz = 500;
    profile.maxValidFreqMilliHz = 0;
    profile.pressurizeSec = 5;
    profile.sampleWindowSec = 2;
    return profile;
}

void begin() {
    uint16_t invalidCount = 0;
    g_schemaResetDetected = false;
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        g_configs[flowId - 1] = defaultConfig(flowId);
        char k[8];
        key(k, sizeof(k), flowId);
        StoredFlowConfig stored = {};
        if (Esp32BaseConfig::getPod(kNamespace, k, stored)) {
            if (validStored(stored)) {
                g_configs[flowId - 1] = stored.data;
            } else {
                ++invalidCount;
            }
        }
    }
    if (invalidCount > 0) {
        g_schemaResetDetected = true;
        BusinessEventLog::appendConfigSchemaReset("flows", invalidCount);
    }
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        g_configs[flowId - 1] = defaultConfig(flowId);
    }
    return true;
}

const Irrigation::FlowMeterConfig& get(uint8_t flowId) {
    uint8_t index = 0;
    if (!validFlowId(flowId, &index)) {
        return g_invalid;
    }
    return g_configs[index];
}

bool validateCalibration(const Irrigation::FlowMeterCalibrationProfile& raw) {
    const Irrigation::FlowMeterCalibrationProfile profile = normalizedCalibration(raw);
    const uint8_t source = static_cast<uint8_t>(profile.source);
    return source <= static_cast<uint8_t>(Irrigation::ParameterSource::LEARNED) &&
           profile.kUlPerMinPerHz > 0 &&
           profile.kUlPerMinPerHz <= 10000000L &&
           profile.offsetMilliHz >= -1000000L &&
           profile.offsetMilliHz <= 1000000L &&
           profile.warningFreqMilliHz <= 1000000UL &&
           profile.minValidFreqMilliHz <= 1000000UL &&
           profile.maxValidFreqMilliHz <= 1000000UL &&
           (profile.maxValidFreqMilliHz == 0 || profile.maxValidFreqMilliHz >= profile.minValidFreqMilliHz) &&
           profile.pressurizeSec <= 300 &&
           profile.sampleWindowSec >= 1 &&
           profile.sampleWindowSec <= 300;
}

bool validate(const Irrigation::FlowMeterConfig& config) {
    uint8_t index = 0;
    if (!validFlowId(config.flowId, &index)) {
        return false;
    }
    if (config.pulsePin != kPulsePins[index]) {
        return false;
    }
    return validateCalibration(config.activeCalibration) &&
           (!config.hasPendingCalibration || validateCalibration(config.pendingCalibration)) &&
           (!config.hasRollbackCalibration || validateCalibration(config.rollbackCalibration));
}

bool set(uint8_t flowId, const Irrigation::FlowMeterConfig& config) {
    uint8_t index = 0;
    Irrigation::FlowMeterConfig normalized = config;
    normalized.activeCalibration = normalizedCalibration(normalized.activeCalibration);
    if (normalized.hasPendingCalibration) {
        normalized.pendingCalibration = normalizedCalibration(normalized.pendingCalibration);
    } else {
        normalized.pendingCalibration = {};
    }
    if (normalized.hasRollbackCalibration) {
        normalized.rollbackCalibration = normalizedCalibration(normalized.rollbackCalibration);
    } else {
        normalized.rollbackCalibration = {};
    }
    if (!validFlowId(flowId, &index) || normalized.flowId != flowId || !validate(normalized)) {
        return false;
    }
    char k[8];
    key(k, sizeof(k), flowId);
    const StoredFlowConfig stored = wrap(normalized);
    if (!Esp32BaseConfig::setPod(kNamespace, k, stored)) {
        return false;
    }
    g_configs[index] = normalized;
    return true;
}

uint32_t estimateMilliliters(const Irrigation::FlowMeterCalibrationProfile& raw,
                             uint32_t pulses,
                             uint32_t durationMs) {
    if (pulses == 0 || durationMs == 0) {
        return 0;
    }
    const Irrigation::FlowMeterCalibrationProfile profile = normalizedCalibration(raw);
    if (!validateCalibration(profile)) {
        return 0;
    }
    const int64_t measuredMilliHz = (static_cast<int64_t>(pulses) * 1000000LL) / durationMs;
    const int64_t correctedMilliHz = measuredMilliHz + profile.offsetMilliHz;
    if (correctedMilliHz <= 0) {
        return 0;
    }
    const uint64_t flowUlPerMin = (static_cast<uint64_t>(correctedMilliHz) *
                                   static_cast<uint64_t>(profile.kUlPerMinPerHz)) /
                                  1000ULL;
    const uint64_t totalUl = (flowUlPerMin * durationMs) / 60000ULL;
    return static_cast<uint32_t>(totalUl / 1000ULL);
}

bool schemaResetDetected() {
    return g_schemaResetDetected;
}

}
