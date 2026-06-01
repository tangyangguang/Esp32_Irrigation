#include "storage/ZoneConfigStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

namespace {

static constexpr const char* kNamespace = "irr_zone";
static constexpr uint32_t kMagic = 0x495A4346UL;
static constexpr uint16_t kVersion = 3;

struct StoredZoneConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Irrigation::ZoneConfig data;
};

Irrigation::ZoneConfig g_configs[Irrigation::MaxZones] = {};
Irrigation::ZoneConfig g_invalid = {};

static constexpr uint8_t kValvePins[] = {
    IrrigationPins::Valve1,
    IrrigationPins::Valve2,
    IrrigationPins::Valve3,
    IrrigationPins::Valve4,
};

static constexpr uint8_t kFlowPins[] = {
    IrrigationPins::Flow1,
    IrrigationPins::Flow2,
    IrrigationPins::Flow3,
    IrrigationPins::Flow4,
};

static constexpr const char* kDefaultNames[] = {
    "Zone 1",
    "Zone 2",
    "Zone 3",
    "Zone 4",
};

void key(char* out, size_t len, uint8_t zoneId) {
    snprintf(out, len, "z%u", static_cast<unsigned>(zoneId));
}

bool validUtf8NoControl(const char* text) {
    if (!text || text[0] == '\0') {
        return false;
    }
    const uint8_t* s = reinterpret_cast<const uint8_t*>(text);
    size_t i = 0;
    while (s[i] != 0) {
        const uint8_t c = s[i];
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
        if (c < 0x80) {
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            if ((s[i + 1] & 0xC0) != 0x80 || c < 0xC2) return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if ((s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if ((s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
        if (i >= Irrigation::NameMaxBytes) {
            return false;
        }
    }
    return i > 0 && i < Irrigation::NameMaxBytes;
}

Irrigation::ZoneConfig defaultConfig(uint8_t zoneId) {
    Irrigation::ZoneConfig config = {};
    config.zoneId = zoneId;
    strlcpy(config.name, kDefaultNames[zoneId - 1], sizeof(config.name));
    config.valvePin = kValvePins[zoneId - 1];
    config.flowPin = kFlowPins[zoneId - 1];
    config.enabled = zoneId <= 2;
    config.flow.startupPulseLimit = 0;
    config.flow.startupEstimatedMl = 0;
    config.flow.stablePulsePerLiter = 450;
    config.startTimeoutSec = 30;
    config.flowNoPulseTimeoutSec = 10;
    config.suppressError = true;
    return config;
}

StoredZoneConfig wrap(const Irrigation::ZoneConfig& config) {
    StoredZoneConfig stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.data = config;
    return stored;
}

bool validStored(const StoredZoneConfig& stored) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored) &&
           ZoneConfigStore::validate(stored.data);
}

}

namespace ZoneConfigStore {

void begin() {
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        g_configs[zoneId - 1] = defaultConfig(zoneId);
        char k[8];
        key(k, sizeof(k), zoneId);
        StoredZoneConfig stored = {};
        if (Esp32BaseConfig::getPod(kNamespace, k, stored) && validStored(stored)) {
            g_configs[zoneId - 1] = stored.data;
        }
    }
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        g_configs[zoneId - 1] = defaultConfig(zoneId);
    }
    return true;
}

const Irrigation::ZoneConfig& get(uint8_t zoneId) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index)) {
        return g_invalid;
    }
    return g_configs[index];
}

bool validateName(const char* name) {
    return validUtf8NoControl(name);
}

Irrigation::FlowParameters normalizeFlowParameters(Irrigation::FlowParameters params) {
    if (params.startupPulseLimit == 0) {
        params.startupEstimatedMl = 0;
    }
    return params;
}

bool validateFlowParameters(const Irrigation::FlowParameters& raw) {
    const Irrigation::FlowParameters params = normalizeFlowParameters(raw);
    return params.startupPulseLimit <= 10000 &&
           params.startupEstimatedMl <= 10000 &&
           params.stablePulsePerLiter >= 1 &&
           params.stablePulsePerLiter <= 10000;
}

bool flowParametersEqual(const Irrigation::FlowParameters& a, const Irrigation::FlowParameters& b) {
    const Irrigation::FlowParameters left = normalizeFlowParameters(a);
    const Irrigation::FlowParameters right = normalizeFlowParameters(b);
    return left.startupPulseLimit == right.startupPulseLimit &&
           left.startupEstimatedMl == right.startupEstimatedMl &&
           left.stablePulsePerLiter == right.stablePulsePerLiter;
}

bool validateCandidate(uint8_t zoneId, const Irrigation::FlowCandidateSlot& slot) {
    (void)zoneId;
    if (!slot.exists) {
        return true;
    }
    return validateFlowParameters(slot.params);
}

bool validate(const Irrigation::ZoneConfig& config) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(config.zoneId, &index)) {
        return false;
    }
    if (config.valvePin != kValvePins[index] || config.flowPin != kFlowPins[index]) {
        return false;
    }
    return validateName(config.name) &&
           validateFlowParameters(config.flow) &&
           validateCandidate(config.zoneId, config.candidateFlow) &&
           (!config.previousFlowExists || validateFlowParameters(config.previousFlow)) &&
           config.startTimeoutSec >= 1 &&
           config.startTimeoutSec <= 300 &&
           config.flowNoPulseTimeoutSec >= 1 &&
           config.flowNoPulseTimeoutSec <= 300;
}

bool set(uint8_t zoneId, const Irrigation::ZoneConfig& config) {
    uint8_t index = 0;
    Irrigation::ZoneConfig normalized = config;
    normalized.flow = normalizeFlowParameters(normalized.flow);
    if (normalized.candidateFlow.exists) {
        normalized.candidateFlow.params = normalizeFlowParameters(normalized.candidateFlow.params);
        memset(normalized.candidateFlow.reserved, 0, sizeof(normalized.candidateFlow.reserved));
    } else {
        normalized.candidateFlow = {};
    }
    if (normalized.previousFlowExists) {
        normalized.previousFlow = normalizeFlowParameters(normalized.previousFlow);
    } else {
        normalized.previousFlow = {};
    }
    if (!Irrigation::zoneIndex(zoneId, &index) || zoneId != normalized.zoneId || !validate(normalized)) {
        return false;
    }
    char k[8];
    key(k, sizeof(k), zoneId);
    const StoredZoneConfig stored = wrap(normalized);
    if (!Esp32BaseConfig::setPod(kNamespace, k, stored)) {
        return false;
    }
    g_configs[index] = normalized;
    return true;
}

bool saveCandidate(uint8_t zoneId, Irrigation::FlowParameters params) {
    if (!Irrigation::validZoneId(zoneId)) {
        return false;
    }
    Irrigation::ZoneConfig config = get(zoneId);
    config.candidateFlow.exists = true;
    memset(config.candidateFlow.reserved, 0, sizeof(config.candidateFlow.reserved));
    config.candidateFlow.params = normalizeFlowParameters(params);
    return set(zoneId, config);
}

bool applyCandidate(uint8_t zoneId, Irrigation::FlowParameters* oldParams, Irrigation::FlowParameters* newParams) {
    if (!Irrigation::validZoneId(zoneId)) {
        return false;
    }
    Irrigation::ZoneConfig config = get(zoneId);
    if (!config.candidateFlow.exists || flowParametersEqual(config.flow, config.candidateFlow.params)) {
        return false;
    }
    const Irrigation::FlowParameters oldFlow = normalizeFlowParameters(config.flow);
    const Irrigation::FlowParameters newFlow = normalizeFlowParameters(config.candidateFlow.params);
    config.previousFlowExists = true;
    config.previousFlow = oldFlow;
    config.flow = newFlow;
    if (!set(zoneId, config)) {
        return false;
    }
    if (oldParams) {
        *oldParams = oldFlow;
    }
    if (newParams) {
        *newParams = newFlow;
    }
    return true;
}

bool restorePrevious(uint8_t zoneId, Irrigation::FlowParameters* oldParams, Irrigation::FlowParameters* newParams) {
    if (!Irrigation::validZoneId(zoneId)) {
        return false;
    }
    Irrigation::ZoneConfig config = get(zoneId);
    if (!config.previousFlowExists || flowParametersEqual(config.flow, config.previousFlow)) {
        return false;
    }
    const Irrigation::FlowParameters oldFlow = normalizeFlowParameters(config.flow);
    const Irrigation::FlowParameters restored = normalizeFlowParameters(config.previousFlow);
    config.flow = restored;
    config.previousFlow = oldFlow;
    if (!set(zoneId, config)) {
        return false;
    }
    if (oldParams) {
        *oldParams = oldFlow;
    }
    if (newParams) {
        *newParams = restored;
    }
    return true;
}

uint32_t estimateMilliliters(const Irrigation::FlowParameters& raw, uint32_t pulses) {
    const Irrigation::FlowParameters params = normalizeFlowParameters(raw);
    if (params.stablePulsePerLiter == 0) {
        return 0;
    }
    if (params.startupPulseLimit == 0) {
        return static_cast<uint32_t>((static_cast<uint64_t>(pulses) * 1000ULL) / params.stablePulsePerLiter);
    }
    const uint32_t startupPulses = pulses < params.startupPulseLimit ? pulses : params.startupPulseLimit;
    const uint32_t stablePulses = pulses > params.startupPulseLimit ? pulses - params.startupPulseLimit : 0;
    const uint64_t startupMl = (static_cast<uint64_t>(startupPulses) * params.startupEstimatedMl) / params.startupPulseLimit;
    const uint64_t stableMl = (static_cast<uint64_t>(stablePulses) * 1000ULL) / params.stablePulsePerLiter;
    return static_cast<uint32_t>(startupMl + stableMl);
}

uint32_t estimateMilliliters(const Irrigation::ZoneConfigSnapshot& snapshot, uint32_t pulses) {
    return estimateMilliliters(snapshot.flow, pulses);
}

}
