#include "IrrigationParameterConfig.h"

#include <Esp32Base.h>

#include <cstring>

#include "IrrigationConfig.h"

namespace {

constexpr const char* kNamespace = "irr_params";
constexpr std::size_t kRegisteredGroupCount = 6;
constexpr std::size_t kRegisteredFieldCount = 25;
static_assert(ESP32BASE_APP_CONFIG_MAX_GROUPS >= kRegisteredGroupCount,
              "Increase ESP32BASE_APP_CONFIG_MAX_GROUPS when adding a group");
static_assert(ESP32BASE_APP_CONFIG_MAX_FIELDS >= kRegisteredFieldCount,
              "Increase ESP32BASE_APP_CONFIG_MAX_FIELDS when adding a parameter");
constexpr const char* kPullIn = "pull_ms";
constexpr const char* kSwitchDelay = "switch_ms";
constexpr const char* kPwm = "pwm_hz";
constexpr const char* kHold = "hold_pct";
constexpr const char* kPumpEnabled = "pump_on";
constexpr const char* kPumpStart = "pump_start";
constexpr const char* kPumpStop = "pump_stop";
constexpr const char* kCoefficient = "pulse_l";
constexpr const char* kStartupPulses = "cal_start_p";
constexpr const char* kStartupWater = "cal_start_ml";
constexpr const char* kCalibrationWindow = "cal_window";
constexpr const char* kCalibrationWindows = "cal_windows";
constexpr const char* kCalibrationVariation = "cal_variation";
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
constexpr char kCoefficientLabel[] = "稳态流量系数";
constexpr char kCoefficientHelp[] =
    "稳定出水时每升水的脉冲数；校准用多组水量拟合扣除启动影响。";
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
    READ_INT(kSwitchDelay, config.valveDrive.switchDelayMs);
    READ_INT(kPwm, config.valveDrive.pwmFrequencyHz);
    READ_INT(kHold, config.valveDrive.holdDutyPercent);
    if (!Esp32BaseAppConfig::submittedBool(kNamespace, kPumpEnabled, boolean)) return false;
    config.pump.enabled = boolean;
    READ_INT(kPumpStart, config.pump.startDelayMs);
    READ_INT(kPumpStop, config.pump.stopToValveCloseDelayMs);
    if (!Esp32BaseAppConfig::submittedDecimal(kNamespace, kCoefficient, value)) return false;
    config.flowMeter.pulsesPerLiterX100 = static_cast<uint32_t>(value);
    READ_INT(kCalibrationWindow, config.calibrationStability.windowSec);
    READ_INT(kCalibrationWindows, config.calibrationStability.requiredWindows);
    READ_INT(kCalibrationVariation, config.calibrationStability.allowedVariationPercent);
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
    g_defaults = IrrigationConfigRules::createDefault();
    const IrrigationConfig& defaults = g_defaults;
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
           Esp32BaseAppConfig::addInt({"meter", kNamespace, kCalibrationWindow, "稳态检测窗口", defaults.calibrationStability.windowSec, 1, 10, 1, "s", "按原始脉冲速率判断稳态的窗口长度，范围 1～10 s。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"meter", kNamespace, kCalibrationWindows, "连续稳定窗口", defaults.calibrationStability.requiredWindows, 2, 10, 1, "个", "连续多少个窗口满足条件后确认稳态，范围 2～10 个。", false, nullptr}) &&
           Esp32BaseAppConfig::addInt({"meter", kNamespace, kCalibrationVariation, "稳态允许波动", defaults.calibrationStability.allowedVariationPercent, 1, 30, 1, "%", "窗口脉冲速率最大与最小值的允许波动，范围 1%～30%。", false, nullptr}) &&
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

bool IrrigationParameterConfig::applyStored(IrrigationConfig& config) {
    if (!Esp32BaseConfig::isReady()) return false;
    const IrrigationConfig& defaults = g_defaults;
#define GET_INT(key, def, target) target = static_cast<decltype(target)>(Esp32BaseConfig::getInt(kNamespace, key, def))
    GET_INT(kPullIn, defaults.valveDrive.pullInTimeMs, config.valveDrive.pullInTimeMs);
    GET_INT(kSwitchDelay, defaults.valveDrive.switchDelayMs, config.valveDrive.switchDelayMs);
    GET_INT(kPwm, defaults.valveDrive.pwmFrequencyHz, config.valveDrive.pwmFrequencyHz);
    GET_INT(kHold, defaults.valveDrive.holdDutyPercent, config.valveDrive.holdDutyPercent);
    config.pump.enabled = Esp32BaseConfig::getBool(kNamespace, kPumpEnabled, defaults.pump.enabled);
    GET_INT(kPumpStart, defaults.pump.startDelayMs, config.pump.startDelayMs);
    GET_INT(kPumpStop, defaults.pump.stopToValveCloseDelayMs, config.pump.stopToValveCloseDelayMs);
    GET_INT(kCoefficient, defaults.flowMeter.pulsesPerLiterX100, config.flowMeter.pulsesPerLiterX100);
    GET_INT(kStartupPulses, defaults.flowMeter.calibrationStartupPulseCount, config.flowMeter.calibrationStartupPulseCount);
    GET_INT(kStartupWater, defaults.flowMeter.calibrationStartupWaterMl, config.flowMeter.calibrationStartupWaterMl);
    GET_INT(kCalibrationWindow, defaults.calibrationStability.windowSec, config.calibrationStability.windowSec);
    GET_INT(kCalibrationWindows, defaults.calibrationStability.requiredWindows, config.calibrationStability.requiredWindows);
    GET_INT(kCalibrationVariation, defaults.calibrationStability.allowedVariationPercent, config.calibrationStability.allowedVariationPercent);
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
    return IrrigationConfigRules::validate(config);
}

bool IrrigationParameterConfig::saveFlowCalibrationParameters(
    const FlowMeterConfig& parameters) {
    if (parameters.pulsesPerLiterX100 < 1U ||
        parameters.pulsesPerLiterX100 > 10000000U ||
        parameters.calibrationStartupPulseCount > 10000000U ||
        parameters.calibrationStartupWaterMl > 1000000U) {
        return false;
    }
    const int32_t previousStartupPulses = Esp32BaseConfig::getInt(
        kNamespace,
        kStartupPulses,
        static_cast<int32_t>(g_defaults.flowMeter.calibrationStartupPulseCount));
    const int32_t previousStartupWater = Esp32BaseConfig::getInt(
        kNamespace,
        kStartupWater,
        static_cast<int32_t>(g_defaults.flowMeter.calibrationStartupWaterMl));
    const int32_t previousCoefficient = Esp32BaseConfig::getInt(
        kNamespace,
        kCoefficient,
        static_cast<int32_t>(g_defaults.flowMeter.pulsesPerLiterX100));

    const bool startupPulsesSaved = Esp32BaseConfig::setInt(
        kNamespace,
        kStartupPulses,
        static_cast<int32_t>(parameters.calibrationStartupPulseCount));
    const bool startupWaterSaved = Esp32BaseConfig::setInt(
        kNamespace,
        kStartupWater,
        static_cast<int32_t>(parameters.calibrationStartupWaterMl));
    const bool coefficientSaved = Esp32BaseConfig::setInt(
        kNamespace,
        kCoefficient,
        static_cast<int32_t>(parameters.pulsesPerLiterX100));
    const bool verified =
        Esp32BaseConfig::getInt(kNamespace, kStartupPulses, -1) ==
            static_cast<int32_t>(parameters.calibrationStartupPulseCount) &&
        Esp32BaseConfig::getInt(kNamespace, kStartupWater, -1) ==
            static_cast<int32_t>(parameters.calibrationStartupWaterMl) &&
        Esp32BaseConfig::getInt(kNamespace, kCoefficient, -1) ==
            static_cast<int32_t>(parameters.pulsesPerLiterX100);
    if (startupPulsesSaved && startupWaterSaved && coefficientSaved && verified) {
        return true;
    }

    Esp32BaseConfig::setInt(kNamespace, kStartupPulses, previousStartupPulses);
    Esp32BaseConfig::setInt(kNamespace, kStartupWater, previousStartupWater);
    Esp32BaseConfig::setInt(kNamespace, kCoefficient, previousCoefficient);
    return false;
}

bool IrrigationParameterConfig::validatePage(char* error, size_t errorLength) {
    g_validationScratch = g_defaults;
    if (!readSubmitted(g_validationScratch) ||
        !IrrigationConfigRules::validate(g_validationScratch)) {
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

void IrrigationParameterConfig::handleSaved(const Esp32BaseAppConfig::SaveSummary& summary) {
    if (summary.savedCount != 0 && g_callback) g_callback(g_callbackUser);
}
