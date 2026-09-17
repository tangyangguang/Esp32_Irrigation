#include "IrrigationParameterConfig.h"

#include <Esp32Base.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "IrrigationConfig.h"

namespace {

constexpr const char* kNamespace = IrrigationParameterConfig::kNamespace;
constexpr const char* kPullIn = "pull_ms";
constexpr const char* kSwitchDelay = "switch_ms";
constexpr const char* kPwm = "pwm_hz";
constexpr const char* kHold = "hold_pct";
constexpr const char* kPumpEnabled = "pump_on";
constexpr const char* kPumpStart = "pump_start";
constexpr const char* kPumpStop = "pump_stop";
constexpr const char* kCoefficient = "pulse_l";
constexpr const char* kFlowStart = "flow_start";
constexpr const char* kNoFlow = "no_flow";
constexpr const char* kLeakDelay = "leak_delay";
constexpr const char* kLeakWindow = "leak_window";
constexpr const char* kLeakPulses = "leak_pulses";
constexpr const char* kDeviation = "dev_confirm";
constexpr const char* kLowPercent = "low_pct";
constexpr const char* kHighPercent = "high_pct";
constexpr const char* kLowAction = "low_action";
constexpr const char* kHighAction = "high_action";
constexpr const char* kRtcRollback = "rtc_rollback";
constexpr const char* kAliveHours = "alive_hours";
constexpr const char* kMaximumZoneMinutes = "max_zone_min";
constexpr const char* kMaximumOutputLiters = "max_output_l";

enum class FieldType : uint8_t { Integer, Boolean, Enumeration };

struct SystemFieldDescriptor {
    const char* contractName;
    const char* nvsKey;
    FieldType type;
    void (*read)(const IrrigationParameters&, int32_t& integer, bool& boolean, char* text, std::size_t textSize);
    void (*write)(IrrigationParameters&, int32_t integer, bool boolean, const char* text);
};

constexpr std::size_t kActionTextSize = 16;

void readFlowAction(FlowAlertAction action, char* text, std::size_t textSize) {
    std::snprintf(text, textSize, "%s",
                  action == FlowAlertAction::StopWatering ? "stop" : "alert");
}

FlowAlertAction parseFlowAction(const char* text) {
    return text && std::strcmp(text, "stop") == 0 ? FlowAlertAction::StopWatering
                                                  : FlowAlertAction::AlertOnly;
}

// Explicit accessors keyed by the same NVS keys as the local page; the table
// maps contract names to them.
int32_t getInteger(const IrrigationParameters& p, const char* key);
void setInteger(IrrigationParameters& p, const char* key, int32_t value);
bool getBoolean(const IrrigationParameters& p, const char* key);
void setBoolean(IrrigationParameters& p, const char* key, bool value);
const char* getEnumText(const IrrigationParameters& p, const char* key, char* text, std::size_t size);
void setEnumText(IrrigationParameters& p, const char* key, const char* text);

const SystemFieldDescriptor kSystemFields[] = {
    {"valve.pullInTimeMs", kPullIn, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kPullIn); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kPullIn, i); }},
    {"valve.switchDelayMs", kSwitchDelay, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kSwitchDelay); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kSwitchDelay, i); }},
    {"valve.pwmFrequencyHz", kPwm, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kPwm); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kPwm, i); }},
    {"valve.holdDutyPercent", kHold, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kHold); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kHold, i); }},
    {"pump.enabled", kPumpEnabled, FieldType::Boolean,
     [](const IrrigationParameters& p, int32_t&, bool& b, char*, std::size_t) { b = getBoolean(p, kPumpEnabled); },
     [](IrrigationParameters& p, int32_t, bool b, const char*) { setBoolean(p, kPumpEnabled, b); }},
    {"pump.startDelayMs", kPumpStart, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kPumpStart); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kPumpStart, i); }},
    {"pump.stopToValveCloseDelayMs", kPumpStop, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kPumpStop); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kPumpStop, i); }},
    {"meter.pulsesPerLiterX100", kCoefficient, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kCoefficient); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kCoefficient, i); }},
    {"meter.flowStartTimeoutSeconds", kFlowStart, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kFlowStart); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kFlowStart, i); }},
    {"meter.noFlowTimeoutSeconds", kNoFlow, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kNoFlow); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kNoFlow, i); }},
    {"flow.unexpectedFlowDelaySeconds", kLeakDelay, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kLeakDelay); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kLeakDelay, i); }},
    {"flow.unexpectedFlowWindowSeconds", kLeakWindow, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kLeakWindow); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kLeakWindow, i); }},
    {"flow.unexpectedFlowPulseCount", kLeakPulses, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kLeakPulses); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kLeakPulses, i); }},
    {"flow.deviationConfirmSeconds", kDeviation, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kDeviation); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kDeviation, i); }},
    {"flow.lowFlowPercent", kLowPercent, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kLowPercent); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kLowPercent, i); }},
    {"flow.highFlowPercent", kHighPercent, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kHighPercent); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kHighPercent, i); }},
    {"flow.lowFlowAction", kLowAction, FieldType::Enumeration,
     [](const IrrigationParameters& p, int32_t&, bool&, char* t, std::size_t n) { getEnumText(p, kLowAction, t, n); },
     [](IrrigationParameters& p, int32_t, bool, const char* t) { setEnumText(p, kLowAction, t); }},
    {"flow.highFlowAction", kHighAction, FieldType::Enumeration,
     [](const IrrigationParameters& p, int32_t&, bool&, char* t, std::size_t n) { getEnumText(p, kHighAction, t, n); },
     [](IrrigationParameters& p, int32_t, bool, const char* t) { setEnumText(p, kHighAction, t); }},
    {"limits.maximumZoneDurationMinutes", kMaximumZoneMinutes, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kMaximumZoneMinutes); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kMaximumZoneMinutes, i); }},
    {"limits.maximumSingleOutputLiters", kMaximumOutputLiters, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kMaximumOutputLiters); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kMaximumOutputLiters, i); }},
    {"system.rtcRollbackThresholdMinutes", kRtcRollback, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kRtcRollback); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kRtcRollback, i); }},
    {"system.aliveCheckpointHours", kAliveHours, FieldType::Integer,
     [](const IrrigationParameters& p, int32_t& i, bool&, char*, std::size_t) { i = getInteger(p, kAliveHours); },
     [](IrrigationParameters& p, int32_t i, bool, const char*) { setInteger(p, kAliveHours, i); }},
};
constexpr std::size_t kSystemFieldCount =
    sizeof(kSystemFields) / sizeof(kSystemFields[0]);
static_assert(kSystemFieldCount == 22,
              "system field table must cover all 22 registered parameters");

const SystemFieldDescriptor* findField(const char* contractName) {
    for (const SystemFieldDescriptor& field : kSystemFields) {
        if (std::strcmp(field.contractName, contractName) == 0) return &field;
    }
    return nullptr;
}

const SystemFieldDescriptor* findFieldByKey(const char* nvsKey) {
    for (const SystemFieldDescriptor& field : kSystemFields) {
        if (std::strcmp(field.nvsKey, nvsKey) == 0) return &field;
    }
    return nullptr;
}

int32_t getInteger(const IrrigationParameters& p, const char* key) {
    if (std::strcmp(key, kPullIn) == 0) return p.valveDrive.pullInTimeMs;
    if (std::strcmp(key, kSwitchDelay) == 0) return p.valveDrive.switchDelayMs;
    if (std::strcmp(key, kPwm) == 0) return static_cast<int32_t>(p.valveDrive.pwmFrequencyHz);
    if (std::strcmp(key, kHold) == 0) return p.valveDrive.holdDutyPercent;
    if (std::strcmp(key, kPumpStart) == 0) return p.pump.startDelayMs;
    if (std::strcmp(key, kPumpStop) == 0) return p.pump.stopToValveCloseDelayMs;
    if (std::strcmp(key, kCoefficient) == 0) return static_cast<int32_t>(p.flowMeter.pulsesPerLiterX100);
    if (std::strcmp(key, kFlowStart) == 0) return p.flowProtection.flowStartTimeoutSec;
    if (std::strcmp(key, kNoFlow) == 0) return p.flowProtection.noFlowTimeoutSec;
    if (std::strcmp(key, kLeakDelay) == 0) return p.flowProtection.unexpectedFlowDelaySec;
    if (std::strcmp(key, kLeakWindow) == 0) return p.flowProtection.unexpectedFlowWindowSec;
    if (std::strcmp(key, kLeakPulses) == 0) return p.flowProtection.unexpectedFlowPulseCount;
    if (std::strcmp(key, kDeviation) == 0) return p.flowProtection.flowDeviationConfirmSec;
    if (std::strcmp(key, kLowPercent) == 0) return p.flowProtection.lowFlowPercent;
    if (std::strcmp(key, kHighPercent) == 0) return p.flowProtection.highFlowPercent;
    if (std::strcmp(key, kMaximumZoneMinutes) == 0) return p.runLimits.maximumZoneDurationMinutes;
    if (std::strcmp(key, kMaximumOutputLiters) == 0) return p.runLimits.maximumSingleOutputLiters;
    if (std::strcmp(key, kRtcRollback) == 0) return p.timeSafety.rtcRollbackThresholdMinutes;
    if (std::strcmp(key, kAliveHours) == 0) return p.timeSafety.aliveCheckpointHours;
    return 0;
}

void setInteger(IrrigationParameters& p, const char* key, int32_t value) {
    if (std::strcmp(key, kPullIn) == 0) p.valveDrive.pullInTimeMs = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kSwitchDelay) == 0) p.valveDrive.switchDelayMs = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kPwm) == 0) p.valveDrive.pwmFrequencyHz = static_cast<uint32_t>(value);
    else if (std::strcmp(key, kHold) == 0) p.valveDrive.holdDutyPercent = static_cast<uint8_t>(value);
    else if (std::strcmp(key, kPumpStart) == 0) p.pump.startDelayMs = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kPumpStop) == 0) p.pump.stopToValveCloseDelayMs = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kCoefficient) == 0) p.flowMeter.pulsesPerLiterX100 = static_cast<uint32_t>(value);
    else if (std::strcmp(key, kFlowStart) == 0) p.flowProtection.flowStartTimeoutSec = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kNoFlow) == 0) p.flowProtection.noFlowTimeoutSec = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kLeakDelay) == 0) p.flowProtection.unexpectedFlowDelaySec = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kLeakWindow) == 0) p.flowProtection.unexpectedFlowWindowSec = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kLeakPulses) == 0) p.flowProtection.unexpectedFlowPulseCount = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kDeviation) == 0) p.flowProtection.flowDeviationConfirmSec = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kLowPercent) == 0) p.flowProtection.lowFlowPercent = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kHighPercent) == 0) p.flowProtection.highFlowPercent = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kMaximumZoneMinutes) == 0) p.runLimits.maximumZoneDurationMinutes = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kMaximumOutputLiters) == 0) p.runLimits.maximumSingleOutputLiters = static_cast<uint16_t>(value);
    else if (std::strcmp(key, kRtcRollback) == 0) p.timeSafety.rtcRollbackThresholdMinutes = static_cast<uint8_t>(value);
    else if (std::strcmp(key, kAliveHours) == 0) p.timeSafety.aliveCheckpointHours = static_cast<uint8_t>(value);
}

bool getBoolean(const IrrigationParameters& p, const char* key) {
    if (std::strcmp(key, kPumpEnabled) == 0) return p.pump.enabled;
    return false;
}

void setBoolean(IrrigationParameters& p, const char* key, bool value) {
    if (std::strcmp(key, kPumpEnabled) == 0) p.pump.enabled = value;
}

const char* getEnumText(const IrrigationParameters& p,
                        const char* key,
                        char* text,
                        std::size_t size) {
    FlowAlertAction action = FlowAlertAction::AlertOnly;
    if (std::strcmp(key, kLowAction) == 0) action = p.flowProtection.lowFlowAction;
    else if (std::strcmp(key, kHighAction) == 0) action = p.flowProtection.highFlowAction;
    else return nullptr;
    readFlowAction(action, text, size);
    return text;
}

void setEnumText(IrrigationParameters& p, const char* key, const char* text) {
    const FlowAlertAction action = parseFlowAction(text);
    if (std::strcmp(key, kLowAction) == 0) p.flowProtection.lowFlowAction = action;
    else if (std::strcmp(key, kHighAction) == 0) p.flowProtection.highFlowAction = action;
}
constexpr std::size_t kRegisteredGroupCount = 6;
constexpr std::size_t kRegisteredFieldCount = 22;
static_assert(ESP32BASE_APP_CONFIG_MAX_GROUPS >= kRegisteredGroupCount,
              "Increase ESP32BASE_APP_CONFIG_MAX_GROUPS when adding a group");
static_assert(ESP32BASE_APP_CONFIG_MAX_FIELDS >= kRegisteredFieldCount,
              "Increase ESP32BASE_APP_CONFIG_MAX_FIELDS when adding a parameter");
constexpr char kCoefficientLabel[] = "每升脉冲数";
constexpr char kCoefficientHelp[] =
    "填写校准得到的每升脉冲数；水量=累计脉冲÷每升脉冲数。";
static_assert(sizeof(kCoefficientLabel) - 1U <= Esp32BaseAppConfig::LABEL_MAX_LENGTH,
              "flow coefficient label exceeds Esp32Base App Config limit");
static_assert(sizeof(kCoefficientHelp) - 1U <= Esp32BaseAppConfig::HELP_MAX_LENGTH,
              "flow coefficient help exceeds Esp32Base App Config limit");

const Esp32BaseAppConfig::EnumOption kFlowActions[] = {
    {"alert", "只报警"},
    {"stop", "停止浇水"},
};

IrrigationParameterConfig::SavedCallback g_callback = nullptr;
IrrigationParameterConfig::ValidateCallback g_validateCallback = nullptr;
void* g_callbackUser = nullptr;
IrrigationParameters g_defaults{};
IrrigationParameters g_validationScratch{};

bool readSubmitted(IrrigationParameters& config) {
    int32_t value = 0;
    bool boolean = false;
    char action[Esp32BaseAppConfig::ENUM_VALUE_MAX_LENGTH + 1]{};
#define READ_INT(key, target) \
    do { if (!Esp32BaseAppConfig::submittedInt(kNamespace, key, value)) return false; \
         target = static_cast<decltype(target)>(value); } while (false)
    READ_INT(kPullIn, config.valveDrive.pullInTimeMs);
    READ_INT(kSwitchDelay, config.valveDrive.switchDelayMs);
    READ_INT(kPwm, config.valveDrive.pwmFrequencyHz);
    READ_INT(kHold, config.valveDrive.holdDutyPercent);
    if (!Esp32BaseAppConfig::submittedBool(kNamespace, kPumpEnabled, boolean)) return false;
    config.pump.enabled = boolean;
    READ_INT(kPumpStart, config.pump.startDelayMs);
    READ_INT(kPumpStop, config.pump.stopToValveCloseDelayMs);
    if (!Esp32BaseAppConfig::submittedDecimal(kNamespace, kCoefficient, value)) return false;
    config.flowMeter.pulsesPerLiterX100 = static_cast<uint32_t>(value);
    READ_INT(kFlowStart, config.flowProtection.flowStartTimeoutSec);
    READ_INT(kNoFlow, config.flowProtection.noFlowTimeoutSec);
    READ_INT(kLeakDelay, config.flowProtection.unexpectedFlowDelaySec);
    READ_INT(kLeakWindow, config.flowProtection.unexpectedFlowWindowSec);
    READ_INT(kLeakPulses, config.flowProtection.unexpectedFlowPulseCount);
    READ_INT(kDeviation, config.flowProtection.flowDeviationConfirmSec);
    READ_INT(kLowPercent, config.flowProtection.lowFlowPercent);
    READ_INT(kHighPercent, config.flowProtection.highFlowPercent);
    if (!Esp32BaseAppConfig::submittedEnum(kNamespace, kLowAction, action, sizeof(action))) return false;
    config.flowProtection.lowFlowAction = std::strcmp(action, "stop") == 0
                                              ? FlowAlertAction::StopWatering
                                              : FlowAlertAction::AlertOnly;
    if (!Esp32BaseAppConfig::submittedEnum(kNamespace, kHighAction, action, sizeof(action))) return false;
    config.flowProtection.highFlowAction = std::strcmp(action, "stop") == 0
                                               ? FlowAlertAction::StopWatering
                                               : FlowAlertAction::AlertOnly;
    READ_INT(kRtcRollback, config.timeSafety.rtcRollbackThresholdMinutes);
    READ_INT(kAliveHours, config.timeSafety.aliveCheckpointHours);
    READ_INT(kMaximumZoneMinutes, config.runLimits.maximumZoneDurationMinutes);
    READ_INT(kMaximumOutputLiters, config.runLimits.maximumSingleOutputLiters);
#undef READ_INT
    return true;
}

}  // namespace

bool IrrigationParameterConfig::registerFields(SavedCallback callback,
                                               ValidateCallback validateCallback,
                                               void* user) {
    g_callback = callback;
    g_validateCallback = validateCallback;
    g_callbackUser = user;
    g_defaults = IrrigationConfigRules::defaultParameters();
    const IrrigationParameters& defaults = g_defaults;
    return Esp32BaseAppConfig::setTitle("系统参数") &&
           Esp32BaseAppConfig::setPageValidateCallback(validatePage) &&
           Esp32BaseAppConfig::setSaveCallback(handleSaved) &&
           Esp32BaseAppConfig::addGroup({"valve", "阀门驱动"}) &&
           Esp32BaseAppConfig::addGroup({"pump", "水泵控制"}) &&
           Esp32BaseAppConfig::addGroup({"meter", "流量计与停水保护"}) &&
           Esp32BaseAppConfig::addGroup({"flow", "流量异常保护"}) &&
           Esp32BaseAppConfig::addGroup({"limits", "运行限制"}) &&
           Esp32BaseAppConfig::addGroup({"system", "时间与存储"}) &&
           Esp32BaseAppConfig::addInt({"valve", kNamespace, kPullIn, "全功率吸合时间", defaults.valveDrive.pullInTimeMs, 100, 10000, 100, "ms", "开阀时先以全功率驱动的时长，范围 100～10000 ms。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"valve", kNamespace, kSwitchDelay, "水路切换间隔", defaults.valveDrive.switchDelayMs, 100, 10000, 100, "ms", "上一水路关阀后等待该时长再开下一路，范围 100～10000 ms。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"valve", kNamespace, kPwm, "PWM 频率", static_cast<int32_t>(defaults.valveDrive.pwmFrequencyHz), 1000, 25000, 100, "Hz", "电磁阀维持阶段的 PWM 频率，范围 1000～25000 Hz。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"valve", kNamespace, kHold, "维持占空比", defaults.valveDrive.holdDutyPercent, 1, 100, 1, "%", "吸合结束后的维持功率，范围 1%～100%。", false, nullptr}) &&
           Esp32BaseAppConfig::addBool({"pump", kNamespace, kPumpEnabled, "启用外部水泵", defaults.pump.enabled, "仅在接有受控水泵时启用；水塔重力供水保持关闭。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"pump", kNamespace, kPumpStart, "水泵启动延时", defaults.pump.startDelayMs, 0, 60000, 100, "ms", "开阀后等待多久启动水泵，范围 0～60000 ms。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"pump", kNamespace, kPumpStop, "停泵后关阀延时", defaults.pump.stopToValveCloseDelayMs, 0, 10000, 100, "ms", "停泵后继续保持阀门开启的时间，范围 0～10000 ms。", false, nullptr}) &&
           Esp32BaseAppConfig::addDecimal({"meter", kNamespace, kCoefficient, kCoefficientLabel, static_cast<int32_t>(defaults.flowMeter.pulsesPerLiterX100), 1, 10000000, 1, 2, "P/L", kCoefficientHelp, false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"meter", kNamespace, kFlowStart, "流量建立超时", defaults.flowProtection.flowStartTimeoutSec, 1, 120, 1, "s", "开始出水后未检测到脉冲的最长等待时间，范围 1～120 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"meter", kNamespace, kNoFlow, "运行无流量超时", defaults.flowProtection.noFlowTimeoutSec, 1, 60, 1, "s", "浇水过程中连续无脉冲多久后停机，范围 1～60 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLeakDelay, "关阀后检测延时", defaults.flowProtection.unexpectedFlowDelaySec, 0, 300, 1, "s", "全部关闭后先等待该时长再检测，避免余流误报。范围 0～300 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLeakWindow, "关阀后检测窗口", defaults.flowProtection.unexpectedFlowWindowSec, 1, 300, 1, "s", "滚动统计窗口内脉冲；报警后一个完整窗口为 0 才恢复。范围 1～300 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLeakPulses, "关阀后报警脉冲数", defaults.flowProtection.unexpectedFlowPulseCount, 1, 65535, 1, "pulse", "窗口内脉冲达到阈值时报警。范围 1～65535。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kDeviation, "偏差确认时间", defaults.flowProtection.flowDeviationConfirmSec, 1, 300, 1, "s", "低流量或高流量持续多久才确认异常，范围 1～300 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLowPercent, "低流量阈值", defaults.flowProtection.lowFlowPercent, 1, 99, 1, "%", "低于已学习基准流量的该百分比时判为偏低，范围 1%～99%。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kHighPercent, "高流量阈值", defaults.flowProtection.highFlowPercent, 101, 1000, 1, "%", "高于已学习基准流量的该百分比时判为偏高，范围 101%～1000%。", false, nullptr}) &&
           Esp32BaseAppConfig::addEnum({"flow", kNamespace, kLowAction, "低流量动作", "alert", kFlowActions, 2, "异常确认后只报警，或同时停止整次浇水。", false, nullptr}) &&
           Esp32BaseAppConfig::addEnum({"flow", kNamespace, kHighAction, "高流量动作", "alert", kFlowActions, 2, "异常确认后只报警，或同时停止整次浇水。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"limits", kNamespace, kMaximumZoneMinutes, "单水路最长运行时间", defaults.runLimits.maximumZoneDurationMinutes, 1, kMaximumConfigurableZoneDurationMinutes, 1, "min", "限制计划、手动浇水和单次出水的单路运行时长，范围 1～720 min。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"limits", kNamespace, kMaximumOutputLiters, "单次出水量上限", defaults.runLimits.maximumSingleOutputLiters, 1, kMaximumConfigurableSingleOutputLiters, 1, "L", "限制单次出水按水量模式可提交的目标，范围 1～1000 L。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"system", kNamespace, kRtcRollback, "RTC 倒退阈值", defaults.timeSafety.rtcRollbackThresholdMinutes, 1, 60, 1, "min", "RTC 比最后可信时间倒退超过该值时暂停自动计划，范围 1～60 min。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"system", kNamespace, kAliveHours, "在线检查点间隔", defaults.timeSafety.aliveCheckpointHours, 0, 168, 1, "h", "空闲达到该时长才写检查点；0 表示关闭，范围 0～168 h。", false, nullptr});
}

bool IrrigationParameterConfig::applyStored(IrrigationParameters& config) {
    if (!Esp32BaseConfig::isReady()) return false;
    const IrrigationParameters& defaults = g_defaults;
#define GET_INT(key, def, target) target = static_cast<decltype(target)>(Esp32BaseConfig::getInt(kNamespace, key, def))
    GET_INT(kPullIn, defaults.valveDrive.pullInTimeMs, config.valveDrive.pullInTimeMs);
    GET_INT(kSwitchDelay, defaults.valveDrive.switchDelayMs, config.valveDrive.switchDelayMs);
    GET_INT(kPwm, defaults.valveDrive.pwmFrequencyHz, config.valveDrive.pwmFrequencyHz);
    GET_INT(kHold, defaults.valveDrive.holdDutyPercent, config.valveDrive.holdDutyPercent);
    config.pump.enabled = Esp32BaseConfig::getBool(kNamespace, kPumpEnabled, defaults.pump.enabled);
    GET_INT(kPumpStart, defaults.pump.startDelayMs, config.pump.startDelayMs);
    GET_INT(kPumpStop, defaults.pump.stopToValveCloseDelayMs, config.pump.stopToValveCloseDelayMs);
    GET_INT(kCoefficient, defaults.flowMeter.pulsesPerLiterX100, config.flowMeter.pulsesPerLiterX100);
    GET_INT(kFlowStart, defaults.flowProtection.flowStartTimeoutSec, config.flowProtection.flowStartTimeoutSec);
    GET_INT(kNoFlow, defaults.flowProtection.noFlowTimeoutSec, config.flowProtection.noFlowTimeoutSec);
    GET_INT(kLeakDelay, defaults.flowProtection.unexpectedFlowDelaySec, config.flowProtection.unexpectedFlowDelaySec);
    GET_INT(kLeakWindow, defaults.flowProtection.unexpectedFlowWindowSec, config.flowProtection.unexpectedFlowWindowSec);
    GET_INT(kLeakPulses, defaults.flowProtection.unexpectedFlowPulseCount, config.flowProtection.unexpectedFlowPulseCount);
    GET_INT(kDeviation, defaults.flowProtection.flowDeviationConfirmSec, config.flowProtection.flowDeviationConfirmSec);
    GET_INT(kLowPercent, defaults.flowProtection.lowFlowPercent, config.flowProtection.lowFlowPercent);
    GET_INT(kHighPercent, defaults.flowProtection.highFlowPercent, config.flowProtection.highFlowPercent);
    GET_INT(kRtcRollback, defaults.timeSafety.rtcRollbackThresholdMinutes, config.timeSafety.rtcRollbackThresholdMinutes);
    GET_INT(kAliveHours, defaults.timeSafety.aliveCheckpointHours, config.timeSafety.aliveCheckpointHours);
    GET_INT(kMaximumZoneMinutes, defaults.runLimits.maximumZoneDurationMinutes, config.runLimits.maximumZoneDurationMinutes);
    GET_INT(kMaximumOutputLiters, defaults.runLimits.maximumSingleOutputLiters, config.runLimits.maximumSingleOutputLiters);
#undef GET_INT
    char action[16]{};
    Esp32BaseConfig::getStr(kNamespace, kLowAction, action, sizeof(action), "alert");
    config.flowProtection.lowFlowAction = std::strcmp(action, "stop") == 0 ? FlowAlertAction::StopWatering : FlowAlertAction::AlertOnly;
    Esp32BaseConfig::getStr(kNamespace, kHighAction, action, sizeof(action), "alert");
    config.flowProtection.highFlowAction = std::strcmp(action, "stop") == 0 ? FlowAlertAction::StopWatering : FlowAlertAction::AlertOnly;
    return IrrigationConfigRules::validateParameters(config);
}

bool IrrigationParameterConfig::validatePage(char* error, size_t errorLength) {
    g_validationScratch = g_defaults;
    if (!readSubmitted(g_validationScratch) ||
        !IrrigationConfigRules::validateParameters(g_validationScratch)) {
        strlcpy(error, "参数组合无效，请检查范围和相互关系。", errorLength);
        return false;
    }
    if (g_validateCallback &&
        !g_validateCallback(g_validationScratch,
                            error,
                            errorLength,
                            g_callbackUser)) {
        return false;
    }
    return true;
}

void IrrigationParameterConfig::handleSaved(const Esp32BaseAppConfig::SaveSummary&) {
    if (g_callback) g_callback(g_callbackUser);
}

bool IrrigationParameterConfig::buildRemoteFieldCandidate(
    const char* contractField,
    bool valueIsInteger,
    int32_t integerValue,
    bool valueIsBoolean,
    bool booleanValue,
    const char* textValue,
    IrrigationParameters& candidate) {
    const SystemFieldDescriptor* field = findField(contractField);
    if (!field) return false;
    if (!applyStored(candidate)) return false;

    // Range/shape check against the same descriptor the local page registers.
    switch (field->type) {
        case FieldType::Integer:
            if (!valueIsInteger) return false;
            setInteger(candidate, field->nvsKey, integerValue);
            break;
        case FieldType::Boolean:
            if (!valueIsBoolean) return false;
            setBoolean(candidate, field->nvsKey, booleanValue);
            break;
        case FieldType::Enumeration:
            if (valueIsInteger || valueIsBoolean || !textValue) return false;
            if (std::strcmp(textValue, "alert") != 0 &&
                std::strcmp(textValue, "stop") != 0) {
                return false;
            }
            setEnumText(candidate, field->nvsKey, textValue);
            break;
    }
    return IrrigationConfigRules::validateParameters(candidate);
}

bool IrrigationParameterConfig::applyRemoteField(const char* contractField,
                                                 bool valueIsInteger,
                                                 int32_t integerValue,
                                                 bool valueIsBoolean,
                                                 bool booleanValue,
                                                 const char* textValue) {
    const SystemFieldDescriptor* field = findField(contractField);
    if (!field) return false;
    if (field->type == FieldType::Integer)
        return valueIsInteger &&
               Esp32BaseConfig::setInt(kNamespace, field->nvsKey, integerValue);
    if (field->type == FieldType::Boolean)
        return valueIsBoolean &&
               Esp32BaseConfig::setBool(kNamespace, field->nvsKey, booleanValue);
    return !valueIsInteger && !valueIsBoolean && textValue &&
           Esp32BaseConfig::setStr(kNamespace, field->nvsKey, textValue);
}

uint8_t IrrigationParameterConfig::fieldIndex(const char* contractField) {
    const SystemFieldDescriptor* field = findField(contractField);
    if (!field) return 0;
    return static_cast<uint8_t>(field - kSystemFields + 1);
}

bool IrrigationParameterConfig::writeStoredField(
    const char* contractField,
    const IrrigationParameters& parameters) {
    const SystemFieldDescriptor* field = findField(contractField);
    if (!field) return false;
    int32_t integer = 0;
    bool boolean = false;
    char action[kActionTextSize]{};
    field->read(parameters, integer, boolean, action, sizeof(action));
    if (field->type == FieldType::Integer)
        return Esp32BaseConfig::setInt(kNamespace, field->nvsKey, integer);
    if (field->type == FieldType::Boolean)
        return Esp32BaseConfig::setBool(kNamespace, field->nvsKey, boolean);
    return Esp32BaseConfig::setStr(kNamespace, field->nvsKey, action);
}
