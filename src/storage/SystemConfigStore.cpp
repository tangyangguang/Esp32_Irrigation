#include "storage/SystemConfigStore.h"

#include <Esp32Base.h>
#include <string.h>

#include "storage/EventStore.h"

namespace {

static constexpr const char* kNamespace = "irr_sys";
static constexpr const char* kKeyConfig = "cfg";
static constexpr uint32_t kMagic = 0x49535953UL;
static constexpr uint16_t kVersion = 1;

struct StoredSystemConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Irrigation::SystemConfig data;
};

Irrigation::SystemConfig g_config = {};

Irrigation::SystemConfig defaults() {
    Irrigation::SystemConfig config = {};
    config.maxWateringDurationSec = 14400UL;
    config.scheduleGraceSec = 5;
    config.manualDefaultDurationSec = 300UL;
    const uint32_t presets[] = {300UL, 600UL, 900UL, 1800UL, 3600UL, 7200UL};
    memcpy(config.durationPresets, presets, sizeof(config.durationPresets));
    config.idleLeakWindowSec = 10;
    config.idleLeakPulseThreshold = 3;
    return config;
}

StoredSystemConfig wrap(const Irrigation::SystemConfig& config) {
    StoredSystemConfig stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.data = config;
    return stored;
}

bool validStored(const StoredSystemConfig& stored) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored) &&
           SystemConfigStore::validate(stored.data);
}

}

namespace SystemConfigStore {

void begin() {
    g_config = defaults();
    StoredSystemConfig stored = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKeyConfig, stored) && validStored(stored)) {
        g_config = stored.data;
        return;
    }
    (void)EventStore::append(Irrigation::EventType::SYSTEM_CONFIG_CHANGED,
                             Irrigation::EventSource::SYSTEM,
                             0,
                             0,
                             0,
                             0,
                             "system config defaults");
}

const Irrigation::SystemConfig& current() {
    return g_config;
}

bool validate(const Irrigation::SystemConfig& config) {
    if (config.maxWateringDurationSec < 60UL || config.maxWateringDurationSec > 86400UL) {
        return false;
    }
    if (config.scheduleGraceSec < 1 || config.scheduleGraceSec > 60) {
        return false;
    }
    if (config.manualDefaultDurationSec < 1 || config.manualDefaultDurationSec > config.maxWateringDurationSec) {
        return false;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        if (config.durationPresets[i] < 1 || config.durationPresets[i] > config.maxWateringDurationSec) {
            return false;
        }
    }
    return config.idleLeakWindowSec >= 1 &&
           config.idleLeakWindowSec <= 300 &&
           config.idleLeakPulseThreshold >= 1 &&
           config.idleLeakPulseThreshold <= 1000;
}

bool set(const Irrigation::SystemConfig& config) {
    if (!validate(config)) {
        return false;
    }
    const StoredSystemConfig stored = wrap(config);
    if (!Esp32BaseConfig::setPod(kNamespace, kKeyConfig, stored)) {
        return false;
    }
    g_config = config;
    (void)EventStore::append(Irrigation::EventType::SYSTEM_CONFIG_CHANGED,
                             Irrigation::EventSource::WEB,
                             0,
                             0,
                             static_cast<int32_t>(config.maxWateringDurationSec),
                             config.scheduleGraceSec,
                             "system config saved");
    return true;
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    g_config = defaults();
    return true;
}

uint32_t maxWateringDurationSec() {
    return g_config.maxWateringDurationSec;
}

uint16_t scheduleGraceSec() {
    return g_config.scheduleGraceSec;
}

}
