#include "storage/SystemConfigStore.h"

#include <Esp32Base.h>
#include <string.h>

#include "storage/EventStore.h"

namespace {

static constexpr const char* kNamespace = "irr_sys";
static constexpr const char* kGroupManual = "manual";
static constexpr const char* kGroupSchedule = "schedule";
static constexpr const char* kGroupSafety = "safety";
static constexpr const char* kKeyMaxDuration = "max_dur";
static constexpr const char* kKeyGrace = "grace";
static constexpr const char* kKeyManualDefault = "manual_def";
static constexpr const char* kKeyLeakWindow = "leak_win";
static constexpr const char* kKeyLeakPulse = "leak_pulse";
static constexpr const char* kPresetKeys[] = {
    "preset0",
    "preset1",
    "preset2",
    "preset3",
    "preset4",
    "preset5",
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

uint32_t readU32(const char* key, uint32_t def) {
    return static_cast<uint32_t>(Esp32BaseConfig::getInt(kNamespace, key, static_cast<int32_t>(def)));
}

bool writeU32(const char* key, uint32_t value) {
    return Esp32BaseConfig::setInt(kNamespace, key, static_cast<int32_t>(value));
}

Irrigation::SystemConfig readStored() {
    Irrigation::SystemConfig config = defaults();
    config.maxWateringDurationSec = readU32(kKeyMaxDuration, config.maxWateringDurationSec);
    config.scheduleGraceSec = static_cast<uint16_t>(readU32(kKeyGrace, config.scheduleGraceSec));
    config.manualDefaultDurationSec = readU32(kKeyManualDefault, config.manualDefaultDurationSec);
    for (uint8_t i = 0; i < 6; ++i) {
        config.durationPresets[i] = readU32(kPresetKeys[i], config.durationPresets[i]);
    }
    config.idleLeakWindowSec = static_cast<uint16_t>(readU32(kKeyLeakWindow, config.idleLeakWindowSec));
    config.idleLeakPulseThreshold = static_cast<uint16_t>(readU32(kKeyLeakPulse, config.idleLeakPulseThreshold));
    return SystemConfigStore::validate(config) ? config : defaults();
}

bool submittedInt(const char* key, uint32_t* out) {
    int32_t raw = 0;
    if (!out || !Esp32BaseAppConfig::submittedInt(kNamespace, key, raw) || raw < 0) {
        return false;
    }
    *out = static_cast<uint32_t>(raw);
    return true;
}

bool validateAppConfigPage(char* error, size_t errorLen) {
    Irrigation::SystemConfig config = {};
    if (!submittedInt(kKeyMaxDuration, &config.maxWateringDurationSec) ||
        !submittedInt(kKeyManualDefault, &config.manualDefaultDurationSec)) {
        strlcpy(error, "System duration values are invalid.", errorLen);
        return false;
    }
    uint32_t value = 0;
    if (!submittedInt(kKeyGrace, &value)) {
        strlcpy(error, "Schedule grace is invalid.", errorLen);
        return false;
    }
    config.scheduleGraceSec = static_cast<uint16_t>(value);
    for (uint8_t i = 0; i < 6; ++i) {
        if (!submittedInt(kPresetKeys[i], &config.durationPresets[i])) {
            strlcpy(error, "Duration presets are invalid.", errorLen);
            return false;
        }
    }
    if (!submittedInt(kKeyLeakWindow, &value)) {
        strlcpy(error, "Leak window is invalid.", errorLen);
        return false;
    }
    config.idleLeakWindowSec = static_cast<uint16_t>(value);
    if (!submittedInt(kKeyLeakPulse, &value)) {
        strlcpy(error, "Leak pulse threshold is invalid.", errorLen);
        return false;
    }
    config.idleLeakPulseThreshold = static_cast<uint16_t>(value);
    if (!SystemConfigStore::validate(config)) {
        strlcpy(error, "Manual duration and presets must be within the max watering duration.", errorLen);
        return false;
    }
    return true;
}

void onAppConfigSave(const Esp32BaseAppConfig::SaveSummary& summary) {
    if (summary.savedCount > 0) {
        SystemConfigStore::reload();
        (void)EventStore::append(Irrigation::EventType::SYSTEM_CONFIG_CHANGED,
                                 Irrigation::EventSource::WEB,
                                 0,
                                 0,
                                 static_cast<int32_t>(SystemConfigStore::current().maxWateringDurationSec),
                                 SystemConfigStore::current().scheduleGraceSec,
                                 "system app config saved");
    }
}

}

namespace SystemConfigStore {

void registerAppConfig() {
#if ESP32BASE_ENABLE_APP_CONFIG
    Esp32BaseAppConfig::setTitle("系统配置");
    Esp32BaseAppConfig::setPageValidateCallback(validateAppConfigPage);
    Esp32BaseAppConfig::setSaveCallback(onAppConfigSave);
    (void)Esp32BaseAppConfig::addGroup({kGroupManual, "手动浇水"});
    (void)Esp32BaseAppConfig::addGroup({kGroupSchedule, "计划调度"});
    (void)Esp32BaseAppConfig::addGroup({kGroupSafety, "安全保护"});
    const Irrigation::SystemConfig def = defaults();
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyMaxDuration, "单次最长秒", static_cast<int32_t>(def.maxWateringDurationSec), 60, 86400, 1, "s", "限制手动和计划单次连续出水时长。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSchedule, kNamespace, kKeyGrace, "调度宽限秒", def.scheduleGraceSec, 1, 60, 1, "s", "计划到点后允许补跑的秒数。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kKeyManualDefault, "手动默认秒", static_cast<int32_t>(def.manualDefaultDurationSec), 1, 86400, 1, "s", "首页手动浇水默认填入的时长。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetKeys[0], "预设 1 秒", static_cast<int32_t>(def.durationPresets[0]), 1, 86400, 1, "s", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetKeys[1], "预设 2 秒", static_cast<int32_t>(def.durationPresets[1]), 1, 86400, 1, "s", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetKeys[2], "预设 3 秒", static_cast<int32_t>(def.durationPresets[2]), 1, 86400, 1, "s", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetKeys[3], "预设 4 秒", static_cast<int32_t>(def.durationPresets[3]), 1, 86400, 1, "s", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetKeys[4], "预设 5 秒", static_cast<int32_t>(def.durationPresets[4]), 1, 86400, 1, "s", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetKeys[5], "预设 6 秒", static_cast<int32_t>(def.durationPresets[5]), 1, 86400, 1, "s", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyLeakWindow, "漏水窗口秒", def.idleLeakWindowSec, 1, 300, 1, "s", "待机漏水检测的统计窗口。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyLeakPulse, "漏水脉冲", def.idleLeakPulseThreshold, 1, 1000, 1, nullptr, "窗口内达到该脉冲数触发漏水告警。", false, nullptr});
#endif
}

void begin() {
    reload();
}

void reload() {
    g_config = readStored();
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
    bool ok = true;
    ok = writeU32(kKeyMaxDuration, config.maxWateringDurationSec) && ok;
    ok = writeU32(kKeyGrace, config.scheduleGraceSec) && ok;
    ok = writeU32(kKeyManualDefault, config.manualDefaultDurationSec) && ok;
    for (uint8_t i = 0; i < 6; ++i) {
        ok = writeU32(kPresetKeys[i], config.durationPresets[i]) && ok;
    }
    ok = writeU32(kKeyLeakWindow, config.idleLeakWindowSec) && ok;
    ok = writeU32(kKeyLeakPulse, config.idleLeakPulseThreshold) && ok;
    if (!ok) {
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
