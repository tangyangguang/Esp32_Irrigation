#include "storage/SystemConfigStore.h"

#include <Esp32Base.h>
#include <string.h>

namespace {

static constexpr const char* kNamespace = "irr_sys_v1";
static constexpr const char* kGroupManual = "manual";
static constexpr const char* kGroupSchedule = "schedule";
static constexpr const char* kGroupSafety = "safety";
static constexpr const char* kKeyMaxDurationMin = "max_dur_min";
static constexpr const char* kKeyGrace = "grace";
static constexpr const char* kKeyQueuedPlanMaxDelaySec = "queue_max_s";
static constexpr const char* kKeyManualDefaultMin = "manual_def_min";
static constexpr const char* kKeyLeakWindow = "leak_win";
static constexpr const char* kKeyLeakPulse = "leak_pulse";

Irrigation::SystemConfig g_config = {};

Irrigation::SystemConfig defaults() {
    Irrigation::SystemConfig config = {};
    config.maxWateringDurationSec = 14400UL;
    config.scheduleGraceSec = 5;
    config.queuedPlanMaxDelaySec = 3600;
    config.manualDefaultDurationSec = 300UL;
    config.idleLeakWindowSec = 15;
    config.idleLeakPulseThreshold = 5;
    return config;
}

uint32_t secondsToMinutes(uint32_t seconds) {
    return seconds / 60UL;
}

uint32_t minutesToSeconds(uint32_t minutes) {
    return minutes * 60UL;
}

uint32_t readU32(const char* key, uint32_t def) {
    return static_cast<uint32_t>(Esp32BaseConfig::getInt(kNamespace, key, static_cast<int32_t>(def)));
}

bool writeU32(const char* key, uint32_t value) {
    return Esp32BaseConfig::setInt(kNamespace, key, static_cast<int32_t>(value));
}

Irrigation::SystemConfig readStored() {
    Irrigation::SystemConfig config = defaults();
    config.maxWateringDurationSec = minutesToSeconds(readU32(kKeyMaxDurationMin, secondsToMinutes(config.maxWateringDurationSec)));
    config.scheduleGraceSec = static_cast<uint16_t>(readU32(kKeyGrace, config.scheduleGraceSec));
    config.queuedPlanMaxDelaySec = static_cast<uint16_t>(readU32(kKeyQueuedPlanMaxDelaySec, config.queuedPlanMaxDelaySec));
    config.manualDefaultDurationSec = minutesToSeconds(readU32(kKeyManualDefaultMin, secondsToMinutes(config.manualDefaultDurationSec)));
    config.idleLeakWindowSec = static_cast<uint16_t>(readU32(kKeyLeakWindow, config.idleLeakWindowSec));
    config.idleLeakPulseThreshold = static_cast<uint16_t>(readU32(kKeyLeakPulse, config.idleLeakPulseThreshold));
    return SystemConfigStore::validate(config) ? config : defaults();
}

bool saveStored(const Irrigation::SystemConfig& config) {
    bool ok = true;
    ok = writeU32(kKeyMaxDurationMin, secondsToMinutes(config.maxWateringDurationSec)) && ok;
    ok = writeU32(kKeyGrace, config.scheduleGraceSec) && ok;
    ok = writeU32(kKeyQueuedPlanMaxDelaySec, config.queuedPlanMaxDelaySec) && ok;
    ok = writeU32(kKeyManualDefaultMin, secondsToMinutes(config.manualDefaultDurationSec)) && ok;
    ok = writeU32(kKeyLeakWindow, config.idleLeakWindowSec) && ok;
    ok = writeU32(kKeyLeakPulse, config.idleLeakPulseThreshold) && ok;
    return ok;
}

bool submittedInt(const char* key, uint32_t* out) {
    int32_t raw = 0;
    if (!out || !Esp32BaseAppConfig::submittedInt(kNamespace, key, raw) || raw < 0) {
        return false;
    }
    *out = static_cast<uint32_t>(raw);
    return true;
}

bool submittedMinutesAsSeconds(const char* key, uint32_t* out) {
    uint32_t minutes = 0;
    if (!submittedInt(key, &minutes)) {
        return false;
    }
    *out = minutesToSeconds(minutes);
    return true;
}

bool validateAppConfigPage(char* error, size_t errorLen) {
    Irrigation::SystemConfig config = defaults();
    if (!submittedMinutesAsSeconds(kKeyMaxDurationMin, &config.maxWateringDurationSec) ||
        !submittedMinutesAsSeconds(kKeyManualDefaultMin, &config.manualDefaultDurationSec)) {
        strlcpy(error, "System duration values are invalid.", errorLen);
        return false;
    }
    uint32_t value = 0;
    if (!submittedInt(kKeyGrace, &value)) {
        strlcpy(error, "Schedule grace is invalid.", errorLen);
        return false;
    }
    config.scheduleGraceSec = static_cast<uint16_t>(value);
    if (!submittedInt(kKeyQueuedPlanMaxDelaySec, &value)) {
        strlcpy(error, "Queued plan max delay is invalid.", errorLen);
        return false;
    }
    config.queuedPlanMaxDelaySec = static_cast<uint16_t>(value);
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
        strlcpy(error, "System config values are outside allowed ranges.", errorLen);
        return false;
    }
    return true;
}

void onAppConfigSave(const Esp32BaseAppConfig::SaveSummary& summary) {
    if (summary.savedCount > 0) {
        (void)SystemConfigStore::set(readStored());
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
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyMaxDurationMin, "单次最长分钟", static_cast<int32_t>(secondsToMinutes(def.maxWateringDurationSec)), 1, 1440, 1, "min", "限制手动和计划单次连续出水时长。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kKeyManualDefaultMin, "手动默认分钟", static_cast<int32_t>(secondsToMinutes(def.manualDefaultDurationSec)), 1, 1440, 1, "min", "首页手动浇水默认填入的时长。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSchedule, kNamespace, kKeyGrace, "调度宽限秒", def.scheduleGraceSec, 1, 60, 1, "s", "计划到点后允许补跑的秒数。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSchedule, kNamespace, kKeyQueuedPlanMaxDelaySec, "排队最大延迟秒", def.queuedPlanMaxDelaySec, 0, 86400, 60, "s", "水路因同 Flow 互斥排队时，超过该延迟则跳过。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyLeakWindow, "待机漏水窗口秒", def.idleLeakWindowSec, 1, 300, 1, "s", "待机漏水检测的统计窗口。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyLeakPulse, "待机漏水脉冲", def.idleLeakPulseThreshold, 1, 1000, 1, nullptr, "窗口内达到该脉冲数触发漏水告警。", false, nullptr});
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
    return config.maxWateringDurationSec >= 60UL &&
           config.maxWateringDurationSec <= 86400UL &&
           (config.maxWateringDurationSec % 60UL) == 0 &&
           config.scheduleGraceSec >= 1 &&
           config.scheduleGraceSec <= 60 &&
           config.queuedPlanMaxDelaySec <= 86400 &&
           config.manualDefaultDurationSec >= 60UL &&
           config.manualDefaultDurationSec <= config.maxWateringDurationSec &&
           (config.manualDefaultDurationSec % 60UL) == 0 &&
           config.idleLeakWindowSec >= 1 &&
           config.idleLeakWindowSec <= 300 &&
           config.idleLeakPulseThreshold >= 1 &&
           config.idleLeakPulseThreshold <= 1000;
}

bool set(const Irrigation::SystemConfig& config) {
    if (!validate(config) || !saveStored(config)) {
        return false;
    }
    g_config = config;
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
