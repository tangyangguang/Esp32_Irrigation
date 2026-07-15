#include "IrrigationParameterConfig.h"

#include <Esp32Base.h>

#include <cstring>

#include "IrrigationConfig.h"

namespace {

constexpr const char* kNamespace = "irr_params";
constexpr const char* kPullIn = "pull_ms";
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

const Esp32BaseAppConfig::EnumOption kFlowActions[] = {
    {"alert", "只报警"},
    {"stop", "停止浇水"},
};

IrrigationParameterConfig::SavedCallback g_callback = nullptr;
void* g_callbackUser = nullptr;
IrrigationConfig g_defaults{};
IrrigationConfig g_validationScratch{};

bool readSubmitted(IrrigationConfig& config) {
    int32_t value = 0;
    bool boolean = false;
    char action[Esp32BaseAppConfig::ENUM_VALUE_MAX_LENGTH + 1]{};
#define READ_INT(key, target) \
    do { if (!Esp32BaseAppConfig::submittedInt(kNamespace, key, value)) return false; \
         target = static_cast<decltype(target)>(value); } while (false)
    READ_INT(kPullIn, config.valveDrive.pullInTimeMs);
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
#undef READ_INT
    return true;
}

}  // namespace

bool IrrigationParameterConfig::registerFields(SavedCallback callback, void* user) {
    g_callback = callback;
    g_callbackUser = user;
    g_defaults = IrrigationConfigRules::createDefault();
    const IrrigationConfig& defaults = g_defaults;
    return Esp32BaseAppConfig::setTitle("系统参数") &&
           Esp32BaseAppConfig::setPageValidateCallback(validatePage) &&
           Esp32BaseAppConfig::setSaveCallback(handleSaved) &&
           Esp32BaseAppConfig::addGroup({"valve", "阀门驱动"}) &&
           Esp32BaseAppConfig::addGroup({"pump", "水泵控制"}) &&
           Esp32BaseAppConfig::addGroup({"meter", "流量计与停水保护"}) &&
           Esp32BaseAppConfig::addGroup({"flow", "流量异常保护"}) &&
           Esp32BaseAppConfig::addGroup({"system", "时间与存储"}) &&
           Esp32BaseAppConfig::addInt({"valve", kNamespace, kPullIn, "全功率吸合时间", defaults.valveDrive.pullInTimeMs, 100, 10000, 100, "ms", "开阀时先以全功率驱动的时长，范围 100～10000 ms。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"valve", kNamespace, kPwm, "PWM 频率", static_cast<int32_t>(defaults.valveDrive.pwmFrequencyHz), 1000, 25000, 100, "Hz", "电磁阀维持阶段的 PWM 频率，范围 1000～25000 Hz。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"valve", kNamespace, kHold, "维持占空比", defaults.valveDrive.holdDutyPercent, 1, 100, 1, "%", "吸合结束后的维持功率，范围 1%～100%。", false, nullptr}) &&
           Esp32BaseAppConfig::addBool({"pump", kNamespace, kPumpEnabled, "启用外部水泵", defaults.pump.enabled, "仅在接有受控水泵时启用；水塔重力供水保持关闭。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"pump", kNamespace, kPumpStart, "水泵启动延时", defaults.pump.startDelayMs, 0, 60000, 100, "ms", "开阀后等待多久启动水泵，范围 0～60000 ms。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"pump", kNamespace, kPumpStop, "停泵后关阀延时", defaults.pump.stopToValveCloseDelayMs, 0, 10000, 100, "ms", "停泵后继续保持阀门开启的时间，范围 0～10000 ms。", false, nullptr}) &&
           Esp32BaseAppConfig::addDecimal({"meter", kNamespace, kCoefficient, "流量系数", static_cast<int32_t>(defaults.flowMeter.pulsesPerLiterX100), 1, 10000000, 1, 2, "pulse/L", "每升水对应的脉冲数，支持两位小数；可先运行流量校准。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"meter", kNamespace, kFlowStart, "流量建立超时", defaults.flowProtection.flowStartTimeoutSec, 1, 120, 1, "s", "开始出水后未检测到脉冲的最长等待时间，范围 1～120 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"meter", kNamespace, kNoFlow, "运行无流量超时", defaults.flowProtection.noFlowTimeoutSec, 1, 60, 1, "s", "浇水过程中连续无脉冲多久后停机，范围 1～60 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLeakDelay, "关阀后检测延时", defaults.flowProtection.unexpectedFlowDelaySec, 0, 300, 1, "s", "全部关闭后延迟多久开始检测非计划流量，范围 0～300 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLeakWindow, "检测窗口", defaults.flowProtection.unexpectedFlowWindowSec, 1, 300, 1, "s", "统计非计划流量脉冲的时间窗口，范围 1～300 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLeakPulses, "脉冲报警阈值", defaults.flowProtection.unexpectedFlowPulseCount, 1, 65535, 1, "pulse", "检测窗口内达到该脉冲数时报警，范围 1～65535。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kDeviation, "偏差确认时间", defaults.flowProtection.flowDeviationConfirmSec, 1, 300, 1, "s", "低流量或高流量持续多久才确认异常，范围 1～300 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kLowPercent, "低流量阈值", defaults.flowProtection.lowFlowPercent, 1, 99, 1, "%", "低于已学习基准流量的该百分比时判为偏低，范围 1%～99%。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"flow", kNamespace, kHighPercent, "高流量阈值", defaults.flowProtection.highFlowPercent, 101, 1000, 1, "%", "高于已学习基准流量的该百分比时判为偏高，范围 101%～1000%。", false, nullptr}) &&
           Esp32BaseAppConfig::addEnum({"flow", kNamespace, kLowAction, "低流量动作", "alert", kFlowActions, 2, "异常确认后只报警，或同时停止整次浇水。", false, nullptr}) &&
           Esp32BaseAppConfig::addEnum({"flow", kNamespace, kHighAction, "高流量动作", "alert", kFlowActions, 2, "异常确认后只报警，或同时停止整次浇水。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"system", kNamespace, kRtcRollback, "RTC 倒退阈值", defaults.timeSafety.rtcRollbackThresholdMinutes, 1, 60, 1, "min", "RTC 比最后可信时间倒退超过该值时暂停自动计划，范围 1～60 min。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"system", kNamespace, kAliveHours, "在线检查点间隔", defaults.timeSafety.aliveCheckpointHours, 0, 168, 1, "h", "空闲达到该时长才写检查点；0 表示关闭，范围 0～168 h。", false, nullptr});
}

bool IrrigationParameterConfig::applyStored(IrrigationConfig& config) {
    if (!Esp32BaseConfig::isReady()) return false;
    const IrrigationConfig& defaults = g_defaults;
#define GET_INT(key, def, target) target = static_cast<decltype(target)>(Esp32BaseConfig::getInt(kNamespace, key, def))
    GET_INT(kPullIn, defaults.valveDrive.pullInTimeMs, config.valveDrive.pullInTimeMs);
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
#undef GET_INT
    char action[16]{};
    Esp32BaseConfig::getStr(kNamespace, kLowAction, action, sizeof(action), "alert");
    config.flowProtection.lowFlowAction = std::strcmp(action, "stop") == 0 ? FlowAlertAction::StopWatering : FlowAlertAction::AlertOnly;
    Esp32BaseConfig::getStr(kNamespace, kHighAction, action, sizeof(action), "alert");
    config.flowProtection.highFlowAction = std::strcmp(action, "stop") == 0 ? FlowAlertAction::StopWatering : FlowAlertAction::AlertOnly;
    return IrrigationConfigRules::validate(config);
}

bool IrrigationParameterConfig::saveFlowCoefficient(uint32_t pulsesPerLiterX100) {
    return pulsesPerLiterX100 >= 1U && pulsesPerLiterX100 <= 10000000U &&
           Esp32BaseConfig::setInt(kNamespace,
                                   kCoefficient,
                                   static_cast<int32_t>(pulsesPerLiterX100));
}

bool IrrigationParameterConfig::validatePage(char* error, size_t errorLength) {
    g_validationScratch = g_defaults;
    if (!readSubmitted(g_validationScratch) ||
        !IrrigationConfigRules::validate(g_validationScratch)) {
        strlcpy(error, "参数组合无效，请检查范围和相互关系。", errorLength);
        return false;
    }
    return true;
}

void IrrigationParameterConfig::handleSaved(const Esp32BaseAppConfig::SaveSummary& summary) {
    if (summary.savedCount != 0 && g_callback) g_callback(g_callbackUser);
}
