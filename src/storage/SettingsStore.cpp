#include "storage/SettingsStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>

#include "Pins.h"

namespace {

static constexpr const char* kNamespace = "irr_cfg";
static constexpr const char* kKeyRoadEnabledMask = "road_mask";
static constexpr const char* kKeyDefaultMode = "mode";
static constexpr const char* kKeyQuickR1 = "quick_r1";
static constexpr const char* kKeyQuickR2 = "quick_r2";
static constexpr const char* kKeyFlowTimeout = "flow_to";
static constexpr const char* kKeyLeakWindow = "leak_win";
static constexpr const char* kKeyLeakPulses = "leak_pulses";
static constexpr const char* kKeyKeypadLocked = "key_lock";
static constexpr const char* kDefaultRoadNames[] = {"Road 1", "Road 2"};

SettingsStore::Settings g_settings = {
    0x01,
    SettingsStore::MODE_SIMULTANEOUS,
    {300, 300},
    10,
    10,
    3,
    false,
    {
        {"Road 1", 450, 1000},
        {"Road 2", 450, 1000},
    },
};

SettingsStore::Settings defaultSettings() {
    SettingsStore::Settings settings = {
        0x01,
        SettingsStore::MODE_SIMULTANEOUS,
        {300, 300},
        10,
        10,
        3,
        false,
        {
            {"Road 1", 450, 1000},
            {"Road 2", 450, 1000},
        },
    };
    return settings;
}

uint8_t clampRoadMask(int32_t value) {
    const uint8_t mask = static_cast<uint8_t>(value) & 0x03;
    return mask == 0 ? 0x01 : mask;
}

bool validRoadMask(uint8_t value) {
    return (value & 0x03) != 0 && (value & ~0x03) == 0;
}

bool validDuration(uint16_t value) {
    return value >= 1 && value <= 14400;
}

bool validFlowTimeout(uint8_t value) {
    return value >= 1 && value <= 60;
}

bool validLeakWindow(uint8_t value) {
    return value >= 1 && value <= 60;
}

bool validLeakPulses(uint8_t value) {
    return value >= 1 && value <= 100;
}

bool roadIndex(uint8_t road, uint8_t* index) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    *index = road - 1;
    return true;
}

void roadKey(char* out, size_t len, uint8_t road, const char* name) {
    snprintf(out, len, "r%u_%s", static_cast<unsigned>(road), name);
}

uint16_t clampPulsePerLiter(int32_t value) {
    if (value < 1 || value > 10000) {
        return 450;
    }
    return static_cast<uint16_t>(value);
}

uint16_t clampCalibration(int32_t value) {
    if (value < 100 || value > 10000) {
        return 1000;
    }
    return static_cast<uint16_t>(value);
}

SettingsStore::ExecutionMode clampMode(int32_t value) {
    if (value == SettingsStore::MODE_SEQUENTIAL) {
        return SettingsStore::MODE_SEQUENTIAL;
    }
    return SettingsStore::MODE_SIMULTANEOUS;
}

uint16_t clampDuration(int32_t value) {
    if (value < 1 || value > 14400) {
        return 300;
    }
    return static_cast<uint16_t>(value);
}

uint8_t clampFlowTimeout(int32_t value) {
    if (value < 1 || value > 60) {
        return 10;
    }
    return static_cast<uint8_t>(value);
}

uint8_t clampLeakWindow(int32_t value) {
    if (value < 1 || value > 60) {
        return 10;
    }
    return static_cast<uint8_t>(value);
}

uint8_t clampLeakPulses(int32_t value) {
    if (value < 1 || value > 100) {
        return 3;
    }
    return static_cast<uint8_t>(value);
}

}

namespace SettingsStore {

void begin() {
    g_settings.roadEnabledMask = clampRoadMask(Esp32BaseConfig::getInt(kNamespace, kKeyRoadEnabledMask, IrrigationPins::DefaultRoadEnabledMask));
    g_settings.defaultMode = clampMode(Esp32BaseConfig::getInt(kNamespace, kKeyDefaultMode, MODE_SIMULTANEOUS));
    g_settings.quickDurationSec[0] = clampDuration(Esp32BaseConfig::getInt(kNamespace, kKeyQuickR1, 300));
    g_settings.quickDurationSec[1] = clampDuration(Esp32BaseConfig::getInt(kNamespace, kKeyQuickR2, 300));
    g_settings.flowNoPulseTimeoutSec = clampFlowTimeout(Esp32BaseConfig::getInt(kNamespace, kKeyFlowTimeout, 10));
    g_settings.idleLeakWindowSec = clampLeakWindow(Esp32BaseConfig::getInt(kNamespace, kKeyLeakWindow, 10));
    g_settings.idleLeakPulseThreshold = clampLeakPulses(Esp32BaseConfig::getInt(kNamespace, kKeyLeakPulses, 3));
    g_settings.keypadLocked = Esp32BaseConfig::getBool(kNamespace, kKeyKeypadLocked, false);
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        const uint8_t road = i + 1;
        char key[16];
        roadKey(key, sizeof(key), road, "name");
        Esp32BaseConfig::getStr(kNamespace, key, g_settings.roads[i].name, sizeof(g_settings.roads[i].name), kDefaultRoadNames[i]);
        roadKey(key, sizeof(key), road, "ppl");
        g_settings.roads[i].pulsePerLiter = clampPulsePerLiter(Esp32BaseConfig::getInt(kNamespace, key, 450));
        roadKey(key, sizeof(key), road, "cal");
        g_settings.roads[i].calibrationX1000 = clampCalibration(Esp32BaseConfig::getInt(kNamespace, key, 1000));
    }
    ESP32BASE_LOG_I("settings", "loaded roadMask=0x%02x mode=%s quick=%u/%u flowTimeout=%u keypadLocked=%s",
                    static_cast<unsigned>(g_settings.roadEnabledMask),
                    executionModeName(g_settings.defaultMode),
                    static_cast<unsigned>(g_settings.quickDurationSec[0]),
                    static_cast<unsigned>(g_settings.quickDurationSec[1]),
                    static_cast<unsigned>(g_settings.flowNoPulseTimeoutSec),
                    g_settings.keypadLocked ? "yes" : "no");
}

const Settings& current() {
    return g_settings;
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    g_settings = defaultSettings();
    ESP32BASE_LOG_W("settings", "cleared to defaults");
    return true;
}

uint8_t enabledRoads() {
    uint8_t count = 0;
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        if (isRoadEnabled(road)) {
            ++count;
        }
    }
    return count;
}

uint8_t roadEnabledMask() {
    return g_settings.roadEnabledMask;
}

bool isRoadEnabled(uint8_t road) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    return (g_settings.roadEnabledMask & (1U << (road - 1))) != 0;
}

ExecutionMode defaultExecutionMode() {
    return g_settings.defaultMode;
}

const char* executionModeName(ExecutionMode mode) {
    return mode == MODE_SEQUENTIAL ? "sequential" : "simultaneous";
}

bool parseExecutionMode(const char* text, ExecutionMode* mode) {
    if (!text || !mode) {
        return false;
    }
    if (strcmp(text, "sequential") == 0 || strcmp(text, "seq") == 0) {
        *mode = MODE_SEQUENTIAL;
        return true;
    }
    if (strcmp(text, "simultaneous") == 0 || strcmp(text, "sim") == 0 || strcmp(text, "parallel") == 0) {
        *mode = MODE_SIMULTANEOUS;
        return true;
    }
    return false;
}

bool setEnabledRoads(uint8_t roads) {
    if (roads < 1 || roads > IrrigationPins::MaxRoads) {
        return false;
    }
    return setRoadEnabledMask(roads >= 2 ? 0x03 : 0x01);
}

bool setRoadEnabled(uint8_t road, bool enabled) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    uint8_t mask = g_settings.roadEnabledMask;
    const uint8_t bit = static_cast<uint8_t>(1U << (road - 1));
    if (enabled) {
        mask |= bit;
    } else {
        mask &= static_cast<uint8_t>(~bit);
    }
    return setRoadEnabledMask(mask);
}

bool setRoadEnabledMask(uint8_t mask) {
    mask &= 0x03;
    if (!validRoadMask(mask)) {
        return false;
    }
    if (g_settings.roadEnabledMask == mask) {
        return true;
    }
    if (!Esp32BaseConfig::setInt(kNamespace, kKeyRoadEnabledMask, mask)) {
        return false;
    }
    g_settings.roadEnabledMask = mask;
    return true;
}

bool setDefaultExecutionMode(ExecutionMode mode) {
    if (mode != MODE_SIMULTANEOUS && mode != MODE_SEQUENTIAL) {
        return false;
    }
    if (g_settings.defaultMode == mode) {
        return true;
    }
    if (!Esp32BaseConfig::setInt(kNamespace, kKeyDefaultMode, static_cast<int32_t>(mode))) {
        return false;
    }
    g_settings.defaultMode = mode;
    return true;
}

bool setQuickDurationSec(uint8_t road, uint16_t seconds) {
    if ((road != 1 && road != 2) || !validDuration(seconds)) {
        return false;
    }
    if (g_settings.quickDurationSec[road - 1] == seconds) {
        return true;
    }
    const char* key = road == 1 ? kKeyQuickR1 : kKeyQuickR2;
    if (!Esp32BaseConfig::setInt(kNamespace, key, seconds)) {
        return false;
    }
    g_settings.quickDurationSec[road - 1] = seconds;
    return true;
}

bool setFlowNoPulseTimeoutSec(uint8_t seconds) {
    if (!validFlowTimeout(seconds)) {
        return false;
    }
    if (g_settings.flowNoPulseTimeoutSec == seconds) {
        return true;
    }
    if (!Esp32BaseConfig::setInt(kNamespace, kKeyFlowTimeout, seconds)) {
        return false;
    }
    g_settings.flowNoPulseTimeoutSec = seconds;
    return true;
}

bool setIdleLeakWindowSec(uint8_t seconds) {
    if (!validLeakWindow(seconds)) {
        return false;
    }
    if (g_settings.idleLeakWindowSec == seconds) {
        return true;
    }
    if (!Esp32BaseConfig::setInt(kNamespace, kKeyLeakWindow, seconds)) {
        return false;
    }
    g_settings.idleLeakWindowSec = seconds;
    return true;
}

bool setIdleLeakPulseThreshold(uint8_t pulses) {
    if (!validLeakPulses(pulses)) {
        return false;
    }
    if (g_settings.idleLeakPulseThreshold == pulses) {
        return true;
    }
    if (!Esp32BaseConfig::setInt(kNamespace, kKeyLeakPulses, pulses)) {
        return false;
    }
    g_settings.idleLeakPulseThreshold = pulses;
    return true;
}

bool setKeypadLocked(bool locked) {
    if (g_settings.keypadLocked == locked) {
        return true;
    }
    if (!Esp32BaseConfig::setBool(kNamespace, kKeyKeypadLocked, locked)) {
        return false;
    }
    g_settings.keypadLocked = locked;
    return true;
}

bool setRoadName(uint8_t road, const char* name) {
    uint8_t index = 0;
    if (!roadIndex(road, &index) || !name || name[0] == '\0' || strlen(name) >= sizeof(g_settings.roads[index].name)) {
        return false;
    }
    if (strcmp(g_settings.roads[index].name, name) == 0) {
        return true;
    }
    char key[16];
    roadKey(key, sizeof(key), road, "name");
    if (!Esp32BaseConfig::setStr(kNamespace, key, name)) {
        return false;
    }
    strlcpy(g_settings.roads[index].name, name, sizeof(g_settings.roads[index].name));
    return true;
}

bool setRoadPulsePerLiter(uint8_t road, uint16_t pulsePerLiter) {
    uint8_t index = 0;
    if (!roadIndex(road, &index) || pulsePerLiter < 1 || pulsePerLiter > 10000) {
        return false;
    }
    if (g_settings.roads[index].pulsePerLiter == pulsePerLiter) {
        return true;
    }
    char key[16];
    roadKey(key, sizeof(key), road, "ppl");
    if (!Esp32BaseConfig::setInt(kNamespace, key, pulsePerLiter)) {
        return false;
    }
    g_settings.roads[index].pulsePerLiter = pulsePerLiter;
    return true;
}

bool setRoadCalibrationX1000(uint8_t road, uint16_t calibrationX1000) {
    uint8_t index = 0;
    if (!roadIndex(road, &index) || calibrationX1000 < 100 || calibrationX1000 > 10000) {
        return false;
    }
    if (g_settings.roads[index].calibrationX1000 == calibrationX1000) {
        return true;
    }
    char key[16];
    roadKey(key, sizeof(key), road, "cal");
    if (!Esp32BaseConfig::setInt(kNamespace, key, calibrationX1000)) {
        return false;
    }
    g_settings.roads[index].calibrationX1000 = calibrationX1000;
    return true;
}

uint32_t estimateMilliliters(uint8_t road, uint32_t pulses) {
    uint8_t index = 0;
    if (!roadIndex(road, &index) || g_settings.roads[index].pulsePerLiter == 0) {
        return 0;
    }
    const uint64_t numerator = static_cast<uint64_t>(pulses) * 1000ULL * g_settings.roads[index].calibrationX1000;
    const uint64_t denominator = static_cast<uint64_t>(g_settings.roads[index].pulsePerLiter) * 1000ULL;
    return static_cast<uint32_t>(numerator / denominator);
}

}
