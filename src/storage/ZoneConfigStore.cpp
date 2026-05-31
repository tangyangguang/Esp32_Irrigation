#include "storage/ZoneConfigStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

#include "storage/EventStore.h"

namespace {

static constexpr const char* kNamespace = "irr_zone";
static constexpr uint32_t kMagic = 0x495A4346UL;
static constexpr uint16_t kVersion = 1;

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
    config.pulsePerLiter = 450;
    config.calibrationX1000 = 1000;
    config.startTimeoutSec = 30;
    config.flowNoPulseTimeoutSec = 10;
    config.suppressError = false;
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

bool validate(const Irrigation::ZoneConfig& config) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(config.zoneId, &index)) {
        return false;
    }
    if (config.valvePin != kValvePins[index] || config.flowPin != kFlowPins[index]) {
        return false;
    }
    return validateName(config.name) &&
           config.pulsePerLiter >= 1 &&
           config.pulsePerLiter <= 10000 &&
           config.calibrationX1000 >= 100 &&
           config.calibrationX1000 <= 10000 &&
           config.startTimeoutSec >= 1 &&
           config.startTimeoutSec <= 300 &&
           config.flowNoPulseTimeoutSec >= 1 &&
           config.flowNoPulseTimeoutSec <= 300;
}

bool set(uint8_t zoneId, const Irrigation::ZoneConfig& config) {
    uint8_t index = 0;
    if (!Irrigation::zoneIndex(zoneId, &index) || zoneId != config.zoneId || !validate(config)) {
        return false;
    }
    char k[8];
    key(k, sizeof(k), zoneId);
    const StoredZoneConfig stored = wrap(config);
    if (!Esp32BaseConfig::setPod(kNamespace, k, stored)) {
        return false;
    }
    g_configs[index] = config;
    (void)EventStore::append(Irrigation::EventType::ZONE_CONFIG_CHANGED,
                             Irrigation::EventSource::WEB,
                             zoneId,
                             0,
                             config.enabled ? 1 : 0,
                             config.suppressError ? 1 : 0,
                             "zone config saved");
    return true;
}

uint32_t estimateMilliliters(const Irrigation::ZoneConfigSnapshot& snapshot, uint32_t pulses) {
    if (snapshot.pulsePerLiter == 0) {
        return 0;
    }
    const uint64_t numerator = static_cast<uint64_t>(pulses) * 1000ULL * snapshot.calibrationX1000;
    const uint64_t denominator = static_cast<uint64_t>(snapshot.pulsePerLiter) * 1000ULL;
    return static_cast<uint32_t>(numerator / denominator);
}

}
