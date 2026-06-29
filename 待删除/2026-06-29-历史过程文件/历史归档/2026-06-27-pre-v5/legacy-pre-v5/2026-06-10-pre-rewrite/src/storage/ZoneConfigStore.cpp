#include "storage/ZoneConfigStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

#include "domain/BusinessEventLog.h"

namespace {

static constexpr const char* kNamespace = "irr_zone_v1";
static constexpr uint32_t kMagic = 0x495A4F4EUL;
static constexpr uint16_t kVersion = 1;

struct StoredZoneConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Irrigation::ZoneConfig data;
};

static_assert(sizeof(StoredZoneConfig) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN,
              "StoredZoneConfig must fit Esp32BaseConfig blob storage");

Irrigation::ZoneConfig g_configs[Irrigation::MaxZones] = {};
Irrigation::ZoneConfig g_invalid = {};
bool g_schemaResetDetected = false;

static constexpr uint8_t kValvePins[] = {
    IrrigationPins::Valve1,
    IrrigationPins::Valve2,
    IrrigationPins::Valve3,
    IrrigationPins::Valve4,
    IrrigationPins::Valve5,
    IrrigationPins::Valve6,
};

static constexpr const char* kDefaultNames[] = {
    "Zone 1",
    "Zone 2",
    "Zone 3",
    "Zone 4",
    "Zone 5",
    "Zone 6",
};

void key(char* out, size_t len, uint8_t zoneId) {
    snprintf(out, len, "z%u", static_cast<unsigned>(zoneId));
}

bool validUtf8NoControl(const char* text, size_t maxLen) {
    if (!text || text[0] == '\0') {
        return false;
    }
    const uint8_t* s = reinterpret_cast<const uint8_t*>(text);
    size_t i = 0;
    while (i < maxLen && s[i] != 0) {
        const uint8_t c = s[i];
        const size_t remaining = maxLen - i;
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
        if (c < 0x80) {
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            if (remaining < 2) return false;
            if ((s[i + 1] & 0xC0) != 0x80 || c < 0xC2) return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (remaining < 3) return false;
            if ((s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (remaining < 4) return false;
            if ((s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
        if (i >= maxLen) {
            return false;
        }
    }
    return i > 0 && i < maxLen && s[i] == 0;
}

Irrigation::ZoneFlowBaselineProfile normalizedBaseline(Irrigation::ZoneFlowBaselineProfile profile) {
    if (profile.source == Irrigation::ParameterSource::NONE) {
        profile.source = Irrigation::ParameterSource::MANUAL;
    }
    if (profile.lowFlowAction != Irrigation::FlowFaultAction::RECORD_ONLY &&
        profile.lowFlowAction != Irrigation::FlowFaultAction::STOP_ZONE) {
        profile.lowFlowAction = Irrigation::FlowFaultAction::STOP_ZONE;
    }
    if (profile.highFlowAction != Irrigation::FlowFaultAction::RECORD_ONLY &&
        profile.highFlowAction != Irrigation::FlowFaultAction::STOP_ZONE) {
        profile.highFlowAction = Irrigation::FlowFaultAction::STOP_ZONE;
    }
    return profile;
}

Irrigation::ZoneConfig defaultConfig(uint8_t zoneId) {
    Irrigation::ZoneConfig config = {};
    config.zoneId = zoneId;
    strlcpy(config.name, kDefaultNames[zoneId - 1], sizeof(config.name));
    config.valvePin = kValvePins[zoneId - 1];
    config.flowId = 1;
    config.enabled = zoneId <= 2;
    config.activeBaseline = ZoneConfigStore::defaultBaseline();
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

Irrigation::ZoneFlowBaselineProfile defaultBaseline() {
    Irrigation::ZoneFlowBaselineProfile profile = {};
    profile.source = Irrigation::ParameterSource::MANUAL;
    profile.learnedFlowMlPerMin = 0;
    profile.lowFlowPermille = 100;
    profile.highFlowPermille = 3000;
    profile.flowFaultConfirmSec = 15;
    profile.lowFlowAction = Irrigation::FlowFaultAction::STOP_ZONE;
    profile.highFlowAction = Irrigation::FlowFaultAction::STOP_ZONE;
    profile.noPulseTimeoutSec = 10;
    return profile;
}

void begin() {
    uint16_t invalidCount = 0;
    g_schemaResetDetected = false;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        g_configs[zoneId - 1] = defaultConfig(zoneId);
        char k[8];
        key(k, sizeof(k), zoneId);
        StoredZoneConfig stored = {};
        if (Esp32BaseConfig::getPod(kNamespace, k, stored)) {
            if (validStored(stored)) {
                g_configs[zoneId - 1] = stored.data;
            } else {
                ++invalidCount;
            }
        }
    }
    if (invalidCount > 0) {
        g_schemaResetDetected = true;
        BusinessEventLog::appendConfigSchemaReset("zones", invalidCount);
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
    return validUtf8NoControl(name, Irrigation::NameMaxBytes);
}

bool validateBaseline(const Irrigation::ZoneFlowBaselineProfile& raw) {
    const Irrigation::ZoneFlowBaselineProfile profile = normalizedBaseline(raw);
    const uint8_t source = static_cast<uint8_t>(profile.source);
    return source <= static_cast<uint8_t>(Irrigation::ParameterSource::LEARNED) &&
           profile.learnedFlowMlPerMin <= 1000000UL &&
           profile.lowFlowPermille <= profile.highFlowPermille &&
           profile.highFlowPermille <= 10000 &&
           profile.flowFaultConfirmSec >= 1 &&
           profile.flowFaultConfirmSec <= 300 &&
           profile.noPulseTimeoutSec >= 1 &&
           profile.noPulseTimeoutSec <= 300;
}

bool validate(const Irrigation::ZoneConfig& config) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(config.zoneId, &index)) {
        return false;
    }
    if (config.valvePin != kValvePins[index]) {
        return false;
    }
    return validateName(config.name) &&
           config.flowId >= 1 &&
           config.flowId <= Irrigation::MaxFlowMeters &&
           validateBaseline(config.activeBaseline) &&
           (!config.hasPendingBaseline || validateBaseline(config.pendingBaseline)) &&
           (!config.hasRollbackBaseline || validateBaseline(config.rollbackBaseline));
}

bool set(uint8_t zoneId, const Irrigation::ZoneConfig& config) {
    uint8_t index = 0;
    Irrigation::ZoneConfig normalized = config;
    normalized.activeBaseline = normalizedBaseline(normalized.activeBaseline);
    if (normalized.hasPendingBaseline) {
        normalized.pendingBaseline = normalizedBaseline(normalized.pendingBaseline);
    } else {
        normalized.pendingBaseline = {};
    }
    if (normalized.hasRollbackBaseline) {
        normalized.rollbackBaseline = normalizedBaseline(normalized.rollbackBaseline);
    } else {
        normalized.rollbackBaseline = {};
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

bool savePendingBaseline(uint8_t zoneId, const Irrigation::ZoneFlowBaselineProfile& profile) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index)) {
        return false;
    }
    Irrigation::ZoneConfig config = g_configs[index];
    config.pendingBaseline = normalizedBaseline(profile);
    config.hasPendingBaseline = true;
    return set(zoneId, config);
}

bool applyPendingBaseline(uint8_t zoneId,
                          Irrigation::ZoneFlowBaselineProfile* oldProfile,
                          Irrigation::ZoneFlowBaselineProfile* newProfile) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index)) {
        return false;
    }
    Irrigation::ZoneConfig config = g_configs[index];
    if (!config.hasPendingBaseline || !validateBaseline(config.pendingBaseline)) {
        return false;
    }
    const Irrigation::ZoneFlowBaselineProfile oldValue = config.activeBaseline;
    const Irrigation::ZoneFlowBaselineProfile newValue = normalizedBaseline(config.pendingBaseline);
    config.rollbackBaseline = oldValue;
    config.hasRollbackBaseline = true;
    config.activeBaseline = newValue;
    config.hasLearnedBaseline = newValue.learnedFlowMlPerMin > 0;
    config.pendingBaseline = {};
    config.hasPendingBaseline = false;
    if (!set(zoneId, config)) {
        return false;
    }
    if (oldProfile) {
        *oldProfile = oldValue;
    }
    if (newProfile) {
        *newProfile = newValue;
    }
    return true;
}

bool restoreRollbackBaseline(uint8_t zoneId,
                             Irrigation::ZoneFlowBaselineProfile* oldProfile,
                             Irrigation::ZoneFlowBaselineProfile* restoredProfile) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index)) {
        return false;
    }
    Irrigation::ZoneConfig config = g_configs[index];
    if (!config.hasRollbackBaseline || !validateBaseline(config.rollbackBaseline)) {
        return false;
    }
    const Irrigation::ZoneFlowBaselineProfile oldValue = config.activeBaseline;
    const Irrigation::ZoneFlowBaselineProfile restored = normalizedBaseline(config.rollbackBaseline);
    config.activeBaseline = restored;
    config.hasLearnedBaseline = restored.learnedFlowMlPerMin > 0;
    config.rollbackBaseline = oldValue;
    config.hasRollbackBaseline = true;
    if (!set(zoneId, config)) {
        return false;
    }
    if (oldProfile) {
        *oldProfile = oldValue;
    }
    if (restoredProfile) {
        *restoredProfile = restored;
    }
    return true;
}

bool schemaResetDetected() {
    return g_schemaResetDetected;
}

}
