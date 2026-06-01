#include "storage/SystemConfigStore.h"

#include <Esp32Base.h>
#include <string.h>

namespace {

static constexpr const char* kNamespace = "irr_sys";
static constexpr const char* kGroupManual = "manual";
static constexpr const char* kGroupSchedule = "schedule";
static constexpr const char* kGroupSafety = "safety";
static constexpr const char* kGroupCalibration = "calibration";
static constexpr const char* kKeyMaxDurationMin = "max_dur_min";
static constexpr const char* kKeyGrace = "grace";
static constexpr const char* kKeyManualDefaultMin = "manual_def_min";
static constexpr const char* kKeyLeakEnabled = "leak_enabled";
static constexpr const char* kKeyLeakWindow = "leak_win";
static constexpr const char* kKeyLeakPulse = "leak_pulse";
static constexpr const char* kKeyCalibrationSampleTarget = "cal_samp";
static constexpr const char* kKeyCalibrationMaxCaptureMin = "cal_max_min";
static constexpr const char* kKeyCalibrationDetailCaptureSec = "cal_detail_s";
static constexpr const char* kKeyCalibrationDetailPulseLimit = "cal_detail_p";
static constexpr const char* kPresetMinuteKeys[] = {
    "preset_min0",
    "preset_min1",
    "preset_min2",
    "preset_min3",
    "preset_min4",
    "preset_min5",
};

Irrigation::SystemConfig g_config = {};

Irrigation::SystemConfig defaults() {
    Irrigation::SystemConfig config = {};
    config.maxWateringDurationSec = 14400UL;
    config.scheduleGraceSec = 5;
    config.manualDefaultDurationSec = 300UL;
    const uint32_t presets[] = {300UL, 600UL, 900UL, 1800UL, 3600UL, 7200UL};
    memcpy(config.durationPresets, presets, sizeof(config.durationPresets));
    config.idleLeakDetectionEnabled = true;
    config.idleLeakWindowSec = 10;
    config.idleLeakPulseThreshold = 3;
    config.calibrationSampleTarget = 5;
    config.calibrationMaxCaptureMin = 10;
    config.calibrationDetailCaptureSec = 20;
    config.calibrationDetailPulseLimit = 2000;
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

bool readBoolStored(const char* key, bool def) {
    return Esp32BaseConfig::getBool(kNamespace, key, def);
}

bool writeBoolStored(const char* key, bool value) {
    return Esp32BaseConfig::setBool(kNamespace, key, value);
}

Irrigation::SystemConfig readStored() {
    Irrigation::SystemConfig config = defaults();
    config.maxWateringDurationSec = minutesToSeconds(readU32(kKeyMaxDurationMin, secondsToMinutes(config.maxWateringDurationSec)));
    config.scheduleGraceSec = static_cast<uint16_t>(readU32(kKeyGrace, config.scheduleGraceSec));
    config.manualDefaultDurationSec = minutesToSeconds(readU32(kKeyManualDefaultMin, secondsToMinutes(config.manualDefaultDurationSec)));
    for (uint8_t i = 0; i < 6; ++i) {
        config.durationPresets[i] = minutesToSeconds(readU32(kPresetMinuteKeys[i], secondsToMinutes(config.durationPresets[i])));
    }
    config.idleLeakDetectionEnabled = readBoolStored(kKeyLeakEnabled, config.idleLeakDetectionEnabled);
    config.idleLeakWindowSec = static_cast<uint16_t>(readU32(kKeyLeakWindow, config.idleLeakWindowSec));
    config.idleLeakPulseThreshold = static_cast<uint16_t>(readU32(kKeyLeakPulse, config.idleLeakPulseThreshold));
    config.calibrationSampleTarget = static_cast<uint8_t>(readU32(kKeyCalibrationSampleTarget, config.calibrationSampleTarget));
    config.calibrationMaxCaptureMin = static_cast<uint16_t>(readU32(kKeyCalibrationMaxCaptureMin, config.calibrationMaxCaptureMin));
    config.calibrationDetailCaptureSec = static_cast<uint16_t>(readU32(kKeyCalibrationDetailCaptureSec, config.calibrationDetailCaptureSec));
    config.calibrationDetailPulseLimit = static_cast<uint16_t>(readU32(kKeyCalibrationDetailPulseLimit, config.calibrationDetailPulseLimit));
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

bool submittedBool(const char* key, bool* out) {
    if (!out) {
        return false;
    }
    bool raw = false;
    if (!Esp32BaseAppConfig::submittedBool(kNamespace, key, raw)) {
        return false;
    }
    *out = raw;
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
    Irrigation::SystemConfig config = {};
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
    for (uint8_t i = 0; i < 6; ++i) {
        if (!submittedMinutesAsSeconds(kPresetMinuteKeys[i], &config.durationPresets[i])) {
            strlcpy(error, "Duration presets are invalid.", errorLen);
            return false;
        }
    }
    if (!submittedBool(kKeyLeakEnabled, &config.idleLeakDetectionEnabled)) {
        strlcpy(error, "Leak detection switch is invalid.", errorLen);
        return false;
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
    if (!submittedInt(kKeyCalibrationSampleTarget, &value)) {
        strlcpy(error, "Calibration sample capacity is invalid.", errorLen);
        return false;
    }
    config.calibrationSampleTarget = static_cast<uint8_t>(value);
    if (!submittedInt(kKeyCalibrationMaxCaptureMin, &value)) {
        strlcpy(error, "Calibration max capture minutes is invalid.", errorLen);
        return false;
    }
    config.calibrationMaxCaptureMin = static_cast<uint16_t>(value);
    if (!submittedInt(kKeyCalibrationDetailCaptureSec, &value)) {
        strlcpy(error, "Calibration detail capture seconds is invalid.", errorLen);
        return false;
    }
    config.calibrationDetailCaptureSec = static_cast<uint16_t>(value);
    if (!submittedInt(kKeyCalibrationDetailPulseLimit, &value)) {
        strlcpy(error, "Calibration detail pulse limit is invalid.", errorLen);
        return false;
    }
    config.calibrationDetailPulseLimit = static_cast<uint16_t>(value);
    if (!SystemConfigStore::validate(config)) {
        strlcpy(error, "Manual duration and presets must be within the max watering duration.", errorLen);
        return false;
    }
    return true;
}

void onAppConfigSave(const Esp32BaseAppConfig::SaveSummary& summary) {
    if (summary.savedCount > 0) {
        SystemConfigStore::reload();
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
    (void)Esp32BaseAppConfig::addGroup({kGroupCalibration, "流量校准"});
    const Irrigation::SystemConfig def = defaults();
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyMaxDurationMin, "单次最长分钟", static_cast<int32_t>(secondsToMinutes(def.maxWateringDurationSec)), 1, 1440, 1, "min", "限制手动和计划单次连续出水时长。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSchedule, kNamespace, kKeyGrace, "调度宽限秒", def.scheduleGraceSec, 1, 60, 1, "s", "计划到点后允许补跑的秒数。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kKeyManualDefaultMin, "手动默认分钟", static_cast<int32_t>(secondsToMinutes(def.manualDefaultDurationSec)), 1, 1440, 1, "min", "首页手动浇水默认填入的时长。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetMinuteKeys[0], "预设 1 分钟", static_cast<int32_t>(secondsToMinutes(def.durationPresets[0])), 1, 1440, 1, "min", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetMinuteKeys[1], "预设 2 分钟", static_cast<int32_t>(secondsToMinutes(def.durationPresets[1])), 1, 1440, 1, "min", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetMinuteKeys[2], "预设 3 分钟", static_cast<int32_t>(secondsToMinutes(def.durationPresets[2])), 1, 1440, 1, "min", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetMinuteKeys[3], "预设 4 分钟", static_cast<int32_t>(secondsToMinutes(def.durationPresets[3])), 1, 1440, 1, "min", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetMinuteKeys[4], "预设 5 分钟", static_cast<int32_t>(secondsToMinutes(def.durationPresets[4])), 1, 1440, 1, "min", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupManual, kNamespace, kPresetMinuteKeys[5], "预设 6 分钟", static_cast<int32_t>(secondsToMinutes(def.durationPresets[5])), 1, 1440, 1, "min", "手动浇水快捷时长，可在首页选择。", false, nullptr});
    (void)Esp32BaseAppConfig::addBool({kGroupSafety, kNamespace, kKeyLeakEnabled, "启用漏水检测", def.idleLeakDetectionEnabled, "关闭后待机状态不会根据流量脉冲触发漏水告警。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyLeakWindow, "漏水窗口秒", def.idleLeakWindowSec, 1, 300, 1, "s", "待机漏水检测的统计窗口。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupSafety, kNamespace, kKeyLeakPulse, "漏水脉冲", def.idleLeakPulseThreshold, 1, 1000, 1, nullptr, "窗口内达到该脉冲数触发漏水告警。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupCalibration, kNamespace, kKeyCalibrationSampleTarget, "校准样本容量", def.calibrationSampleTarget, 2, 5, 1, nullptr, "RAM 中最多保留的校准样本数，只限制新增样本，不限制计算。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupCalibration, kNamespace, kKeyCalibrationMaxCaptureMin, "校准最长分钟", def.calibrationMaxCaptureMin, 1, 15, 1, "min", "单次校准出水最长允许时间，超过后样本无效并自动关阀。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupCalibration, kNamespace, kKeyCalibrationDetailCaptureSec, "明细采集秒", def.calibrationDetailCaptureSec, 5, 60, 1, "s", "保存原始脉冲时间差的最长时长，只用于启动阶段识别。", false, nullptr});
    (void)Esp32BaseAppConfig::addInt({kGroupCalibration, kNamespace, kKeyCalibrationDetailPulseLimit, "明细脉冲上限", def.calibrationDetailPulseLimit, 100, 5000, 100, nullptr, "保存原始脉冲时间差的最大条数，达到后仍继续统计总脉冲。", false, nullptr});
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
    if ((config.maxWateringDurationSec % 60UL) != 0 ||
        (config.manualDefaultDurationSec % 60UL) != 0) {
        return false;
    }
    if (config.scheduleGraceSec < 1 || config.scheduleGraceSec > 60) {
        return false;
    }
    if (config.manualDefaultDurationSec < 1 || config.manualDefaultDurationSec > config.maxWateringDurationSec) {
        return false;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        if (config.durationPresets[i] < 60UL ||
            config.durationPresets[i] > config.maxWateringDurationSec ||
            (config.durationPresets[i] % 60UL) != 0) {
            return false;
        }
    }
    return config.idleLeakWindowSec >= 1 &&
           config.idleLeakWindowSec <= 300 &&
           config.idleLeakPulseThreshold >= 1 &&
           config.idleLeakPulseThreshold <= 1000 &&
           config.calibrationSampleTarget >= 2 &&
           config.calibrationSampleTarget <= 5 &&
           config.calibrationMaxCaptureMin >= 1 &&
           config.calibrationMaxCaptureMin <= 15 &&
           config.calibrationDetailCaptureSec >= 5 &&
           config.calibrationDetailCaptureSec <= 60 &&
           config.calibrationDetailPulseLimit >= 100 &&
           config.calibrationDetailPulseLimit <= 5000;
}

bool set(const Irrigation::SystemConfig& config) {
    if (!validate(config)) {
        return false;
    }
    bool ok = true;
    ok = writeU32(kKeyMaxDurationMin, secondsToMinutes(config.maxWateringDurationSec)) && ok;
    ok = writeU32(kKeyGrace, config.scheduleGraceSec) && ok;
    ok = writeU32(kKeyManualDefaultMin, secondsToMinutes(config.manualDefaultDurationSec)) && ok;
    for (uint8_t i = 0; i < 6; ++i) {
        ok = writeU32(kPresetMinuteKeys[i], secondsToMinutes(config.durationPresets[i])) && ok;
    }
    ok = writeBoolStored(kKeyLeakEnabled, config.idleLeakDetectionEnabled) && ok;
    ok = writeU32(kKeyLeakWindow, config.idleLeakWindowSec) && ok;
    ok = writeU32(kKeyLeakPulse, config.idleLeakPulseThreshold) && ok;
    ok = writeU32(kKeyCalibrationSampleTarget, config.calibrationSampleTarget) && ok;
    ok = writeU32(kKeyCalibrationMaxCaptureMin, config.calibrationMaxCaptureMin) && ok;
    ok = writeU32(kKeyCalibrationDetailCaptureSec, config.calibrationDetailCaptureSec) && ok;
    ok = writeU32(kKeyCalibrationDetailPulseLimit, config.calibrationDetailPulseLimit) && ok;
    if (!ok) {
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
