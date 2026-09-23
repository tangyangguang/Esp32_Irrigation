#include "IrrigationWeb.h"

#include <Esp32Base.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "IrrigationApp.h"
#include "IrrigationConfig.h"
#include "IrrigationTime.h"
#include "generated/IrrigationWebAssets.h"

namespace {

IrrigationApp* g_app = nullptr;

bool getParam(const char* name, char* output, std::size_t outputSize) {
    return Esp32BaseWeb::getParam(name, output, outputSize);
}

bool parseUint(const char* text, uint32_t minimum, uint32_t maximum, uint32_t& value) {
    if (!text || *text == '\0' || *text == '-' || *text == '+') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool uintParam(const char* name, uint32_t minimum, uint32_t maximum, uint32_t& value) {
    char text[16]{};
    return getParam(name, text, sizeof(text)) && parseUint(text, minimum, maximum, value);
}

bool formatChineseDate(uint32_t epoch, char* output, std::size_t outputSize) {
    char date[16]{};
    if (!Esp32BaseTime::formatEpoch(epoch, date, sizeof(date), "%Y-%m-%d") ||
        std::strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    const unsigned month = static_cast<unsigned>((date[5] - '0') * 10 + date[6] - '0');
    const unsigned day = static_cast<unsigned>((date[8] - '0') * 10 + date[9] - '0');
    const int written = std::snprintf(output, outputSize, "%c%c%c%c年%u月%u日",
                                      date[0], date[1], date[2], date[3], month, day);
    return written > 0 && static_cast<std::size_t>(written) < outputSize;
}

bool actionIs(const char* expected);

bool parseStartMinute(const char* text, uint16_t& minute) {
    if (!text || *text == '\0') {
        minute = kUnusedStartMinute;
        return true;
    }
    if (std::strlen(text) != 5 || text[2] != ':' ||
        text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '9' || text[4] < '0' || text[4] > '9') {
        return false;
    }
    const uint16_t hour = static_cast<uint16_t>((text[0] - '0') * 10 + text[1] - '0');
    const uint16_t valueMinute = static_cast<uint16_t>((text[3] - '0') * 10 + text[4] - '0');
    if (hour > 23 || valueMinute > 59) {
        return false;
    }
    minute = static_cast<uint16_t>(hour * 60U + valueMinute);
    return true;
}

bool savePlanFromRequest() {
    const IrrigationConfig* current = g_app->configuration();
    uint32_t planId = 0, revision = 0;
    if (!current || !uintParam("plan_id", 1, kWateringPlanCount, planId) ||
        !uintParam("revision", 1, UINT32_MAX, revision)) {
        return false;
    }
    if (actionIs("delete")) {
        WateringPlan plan{};
        plan.id = static_cast<uint8_t>(planId);
        return g_app->savePlanSlot(plan, true, revision) ==
               IrrigationApp::ConfigSaveError::Ok;
    }
    if (!actionIs("save")) {
        return false;
    }
    char name[kObjectNameCapacity]{};
    if (!getParam("name", name, sizeof(name))) {
        return false;
    }
    WateringPlan plan = current->plans[planId - 1U];
    plan.id = static_cast<uint8_t>(planId);
    plan.configured = true;
    plan.scheduleEnabled = Esp32BaseWeb::hasParam("schedule_enabled");
    std::snprintf(plan.name.data(), plan.name.size(), "%s", name);
    for (uint8_t index = 0; index < plan.startMinutes.size(); ++index) {
        char field[8];
        char value[8]{};
        std::snprintf(field, sizeof(field), "time%u", index + 1U);
        if (!getParam(field, value, sizeof(value)) ||
            !parseStartMinute(value, plan.startMinutes[index])) {
            return false;
        }
    }
    for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
        if (!current->zones[index].enabled) continue;
        char field[8];
        uint32_t duration = 0;
        std::snprintf(field, sizeof(field), "zone%u", index + 1U);
        if (!uintParam(field,
                       0,
                       current->runLimits.maximumZoneDurationMinutes,
                       duration)) {
            return false;
        }
        plan.zoneDurationMinutes[index] = static_cast<uint16_t>(duration);
    }
    return g_app->savePlanSlot(plan, false, revision) ==
           IrrigationApp::ConfigSaveError::Ok;
}

bool saveZoneFromRequest() {
    uint32_t zoneId = 0, revision = 0;
    char name[kObjectNameCapacity]{};
    if (!uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId) ||
        !uintParam("revision", 1, UINT32_MAX, revision) ||
        !getParam("name", name, sizeof(name))) {
        return false;
    }
    return g_app->saveZoneInfo(static_cast<uint8_t>(zoneId),
                               name,
                               Esp32BaseWeb::hasParam("enabled"),
                               revision) == IrrigationApp::ConfigSaveError::Ok;
}

bool actionIs(const char* expected) {
    char action[32]{};
    return getParam("action", action, sizeof(action)) && std::strcmp(action, expected) == 0;
}

// Manual watering dialog: each enabled zone row posts modeN (duration|volume)
// and zoneN (minutes or litres). Zero means the zone does not run this time.
// Duration and volume steps may be mixed; they execute in zone number order.
bool buildManualWateringRequest(const IrrigationConfig& config, WateringRequest& request) {
    request = {};
    request.source = WateringSource::LocalWeb;
    request.purpose = WateringPurpose::Normal;
    bool hasVolume = false;
    bool hasDuration = false;
    for (uint8_t index = 0; index < BoardPins::kZoneCount; ++index) {
        if (!config.zones[index].enabled) continue;
        char modeName[8]{};
        char valueName[8]{};
        char mode[10]{};
        char valueText[16]{};
        std::snprintf(modeName, sizeof(modeName), "mode%u", static_cast<unsigned>(index + 1U));
        std::snprintf(valueName, sizeof(valueName), "zone%u", static_cast<unsigned>(index + 1U));
        if (getParam(modeName, mode, sizeof(mode)) && std::strcmp(mode, "volume") == 0) {
            uint32_t waterMl = 0;
            if (!getParam(valueName, valueText, sizeof(valueText)) ||
                !IrrigationConfigRules::parseWaterVolumeLiters(valueText, waterMl)) {
                return false;
            }
            if (waterMl == 0) continue;
            request.steps[request.stepCount++] = {
                static_cast<uint8_t>(index + 1U),
                static_cast<uint32_t>(config.runLimits.maximumZoneDurationMinutes) * 60U,
                waterMl};
            hasVolume = true;
        } else {
            uint32_t minutes = 0;
            if (!getParam(valueName, valueText, sizeof(valueText)) ||
                !parseUint(valueText, 0, config.runLimits.maximumZoneDurationMinutes, minutes)) {
                return false;
            }
            if (minutes == 0) continue;
            request.steps[request.stepCount++] = {
                static_cast<uint8_t>(index + 1U), minutes * 60U, 0U};
            hasDuration = true;
        }
        if (request.stepCount > request.steps.size()) return false;
    }
    if (request.stepCount == 0) return false;
    request.targetMode = hasVolume
                             ? (hasDuration ? WateringTargetMode::Mixed
                                            : WateringTargetMode::Volume)
                             : WateringTargetMode::Duration;
    return WateringController::validateRequest(request, config) ==
           WateringStartResult::Started;
}

bool manualRowParam(char* output, std::size_t outputSize, const char* prefix, uint8_t zoneId) {
    char name[8]{};
    std::snprintf(name, sizeof(name), "%s%u", prefix, static_cast<unsigned>(zoneId));
    return getParam(name, output, outputSize);
}

void redirectResult(const char* path, bool success) {
    char location[96];
    std::snprintf(location, sizeof(location), "%s?result=%s", path, success ? "ok" : "error");
    Esp32BaseWeb::redirectSeeOther(location);
}

bool beginPage(const char* title, const char* subtitle = nullptr) {
    if (!Esp32BaseWeb::checkAuth()) {
        return false;
    }
    Esp32BaseWeb::sendHeader(title);
    Esp32BaseWeb::sendPageTitle(title, subtitle);
    char result[12]{};
    if (getParam("result", result, sizeof(result))) {
        if (std::strcmp(result, "ok") == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "操作已完成");
        } else if (std::strcmp(result, "error") == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                     "操作失败",
                                     "请检查当前状态、参数和配置修订号。");
        }
    }
    return true;
}

void endPage() {
    Esp32BaseWeb::sendFooter();
}

const char* wateringStateName(WateringState state) {
    switch (state) {
        case WateringState::Idle: return "空闲";
        case WateringState::StartingZone: return "水路启动中";
        case WateringState::WaitingForFlow: return "等待水流";
        case WateringState::WateringZone: return "正在浇水";
        case WateringState::StoppingZone: return "水路停止中";
        case WateringState::SwitchingZone: return "水路切换中";
    }
    return "未知";
}

const char* stopReasonName(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::RebootInterrupted: return "设备重启，未保存的进度与水量未知";
        case WateringStopReason::Completed: return "正常完成";
        case WateringStopReason::UserStopped: return "用户停止";
        case WateringStopReason::FlowStartTimeout: return "启动后未检测到水流";
        case WateringStopReason::NoFlowTimeout: return "浇水过程中水流中断";
        case WateringStopReason::LowFlow: return "流量过低";
        case WateringStopReason::HighFlow: return "流量过高";
        case WateringStopReason::LearningTimeout: return "流量学习超时";
        case WateringStopReason::HardwareFailure: return "硬件故障";
        case WateringStopReason::MaintenanceInterrupted: return "维护操作中断";
        case WateringStopReason::TargetVolumeTimeout: return "达到最长运行时间，未达到目标水量";
        case WateringStopReason::BusyManualWatering: return "当时正在手动浇水，计划未执行";
        case WateringStopReason::BusyAutomaticWatering: return "当时另一计划正在运行，计划未执行";
        case WateringStopReason::BusyZoneFlowLearning: return "当时正在学习水路基准，计划未执行";
        case WateringStopReason::PreviousResultPending: return "上一任务结果尚未完成保存，计划未执行";
        case WateringStopReason::ControllerNotReady: return "设备或记录存储未就绪，计划未执行";
        case WateringStopReason::InvalidRequest: return "计划参数或水路配置无效，计划未执行";
        default: return "未知原因";
    }
}

const char* zoneResultName(ZoneWateringResult result) {
    switch (result) {
        case ZoneWateringResult::Completed: return "完成";
        case ZoneWateringResult::Stopped: return "停止";
        case ZoneWateringResult::Failed: return "失败";
        default: return "未开始";
    }
}

const char* sourceName(WateringSource source) {
    switch (source) {
        case WateringSource::LocalWeb: return "本地手动";
        case WateringSource::WechatMiniprogram: return "小程序";
        case WateringSource::AutomaticPlan: return "自动计划";
    }
    return "未知";
}

void sendUnsigned(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(text);
}

void sendUnsigned64(uint64_t value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%llu",
                  static_cast<unsigned long long>(value));
    Esp32BaseWeb::sendChunk(text);
}

void formatSignedHundredths(int64_t value,
                            char* output,
                            std::size_t outputSize,
                            bool alwaysShowSign = false) {
    const bool negative = value < 0;
    const uint64_t magnitude = negative
                                   ? static_cast<uint64_t>(-(value + 1)) + 1U
                                   : static_cast<uint64_t>(value);
    std::snprintf(output,
                  outputSize,
                  "%s%llu.%02llu",
                  negative ? "-" : (alwaysShowSign ? "+" : ""),
                  static_cast<unsigned long long>(magnitude / 100U),
                  static_cast<unsigned long long>(magnitude % 100U));
}

void formatTenThousandths(uint32_t value,
                          char* output,
                          std::size_t outputSize) {
    std::snprintf(output,
                  outputSize,
                  "%lu.%04lu",
                  static_cast<unsigned long>(value / 10000U),
                  static_cast<unsigned long>(value % 10000U));
}

void formatIntegerChange(uint32_t current,
                         uint32_t previous,
                         char* output,
                         std::size_t outputSize) {
    const int64_t change =
        static_cast<int64_t>(current) - static_cast<int64_t>(previous);
    std::snprintf(output,
                  outputSize,
                  "%s%llu",
                  change < 0 ? "−" : "+",
                  static_cast<unsigned long long>(
                      change < 0 ? -change : change));
}

void formatFlowChange(uint32_t current,
                      uint32_t previous,
                      char* output,
                      std::size_t outputSize) {
    const int64_t change =
        static_cast<int64_t>(current) - static_cast<int64_t>(previous);
    const uint64_t magnitude =
        static_cast<uint64_t>(change < 0 ? -change : change);
    if (previous == 0) {
        std::snprintf(output,
                      outputSize,
                      "%s%llu.%03llu L/min",
                      change < 0 ? "−" : "+",
                      static_cast<unsigned long long>(magnitude / 1000U),
                      static_cast<unsigned long long>(magnitude % 1000U));
        return;
    }
    const uint64_t percentTenths =
        (magnitude * 1000ULL + previous / 2U) / previous;
    std::snprintf(output,
                  outputSize,
                  "%s%llu.%03llu L/min (%s%llu.%llu%%)",
                  change < 0 ? "−" : "+",
                  static_cast<unsigned long long>(magnitude / 1000U),
                  static_cast<unsigned long long>(magnitude % 1000U),
                  change < 0 ? "−" : "+",
                  static_cast<unsigned long long>(percentTenths / 10U),
                  static_cast<unsigned long long>(percentTenths % 10U));
}

void sendMilliseconds(uint32_t value) {
    char text[24];
    const uint32_t roundedTenths = (value + 50U) / 100U;
    std::snprintf(text,
                  sizeof(text),
                  "%lu.%lu 秒",
                  static_cast<unsigned long>(roundedTenths / 10U),
                  static_cast<unsigned long>(roundedTenths % 10U));
    Esp32BaseWeb::sendChunk(text);
}

void formatPulseRate(uint32_t pulses,
                     uint32_t durationMs,
                     char* output,
                     size_t outputLength) {
    if (!output || outputLength == 0) return;
    if (durationMs == 0) {
        strlcpy(output, "—", outputLength);
        return;
    }
    const uint64_t scaled =
        (static_cast<uint64_t>(pulses) * 100000ULL + durationMs / 2U) /
        durationMs;
    formatSignedHundredths(scaled > INT64_MAX
                               ? INT64_MAX
                               : static_cast<int64_t>(scaled),
                           output,
                           outputLength);
}

uint32_t calculateFlowMlPerMinute(uint32_t pulses,
                                  uint32_t durationMs,
                                  uint32_t pulsesPerLiterX100) {
    if (durationMs == 0 || pulsesPerLiterX100 == 0) return 0;
    const double value = static_cast<double>(pulses) * 6000000000.0 /
                         (static_cast<double>(durationMs) * pulsesPerLiterX100);
    if (value <= 0.0) return 0;
    return value >= UINT32_MAX ? UINT32_MAX
                               : static_cast<uint32_t>(value + 0.5);
}

void sendRecordTime(const Esp32BaseRecordStore::RecordTiming& timing,
                    const char* format = "%m-%d %H:%M") {
    uint32_t epoch = 0;
    char text[32]{};
    if (Esp32BaseRecordStore::resolveCompletedEpoch(timing, epoch) &&
        Esp32BaseTime::formatEpoch(epoch, text, sizeof(text), format)) {
        Esp32BaseWeb::sendChunk(text);
        return;
    }
    Esp32BaseWeb::sendChunk("启动 ");
    sendUnsigned(timing.completedBootId);
    Esp32BaseWeb::sendChunk(" +");
    sendUnsigned(timing.completedUptimeSec);
    Esp32BaseWeb::sendChunk(" 秒");
}

const char* planNameById(const IrrigationConfig* config, uint8_t planId) {
    if (!config || planId == 0 || planId > config->plans.size()) return nullptr;
    const WateringPlan& plan = config->plans[planId - 1U];
    return plan.configured ? plan.name.data() : nullptr;
}

bool formatFullDateTime(uint32_t epoch, char* output, std::size_t outputSize) {
    return Esp32BaseTime::formatEpoch(epoch, output, outputSize,
                                      "%Y年%m月%d日 %H:%M");
}

bool formatInputDateTime(uint32_t epoch, char* output, std::size_t outputSize) {
    return Esp32BaseTime::formatEpoch(epoch, output, outputSize, "%Y-%m-%dT%H:%M");
}

bool formatFriendlyDateTime(uint32_t epoch,
                            uint32_t nowEpoch,
                            char* output,
                            std::size_t outputSize) {
    static constexpr uint32_t kUtcOffsetSec = 8UL * 3600UL;
    static constexpr uint32_t kSecondsPerDay = 24UL * 3600UL;
    const uint32_t targetDay = (epoch + kUtcOffsetSec) / kSecondsPerDay;
    const uint32_t currentDay = (nowEpoch + kUtcOffsetSec) / kSecondsPerDay;
    char time[8]{};
    if (!Esp32BaseTime::formatEpoch(epoch, time, sizeof(time), "%H:%M")) return false;
    if (targetDay == currentDay) {
        std::snprintf(output, outputSize, "今天 %s", time);
        return true;
    }
    if (targetDay == currentDay + 1U) {
        std::snprintf(output, outputSize, "明天 %s", time);
        return true;
    }
    if (targetDay == currentDay + 2U) {
        std::snprintf(output, outputSize, "后天 %s", time);
        return true;
    }
    return Esp32BaseTime::formatEpoch(epoch, output, outputSize, "%m月%d日 %H:%M");
}

void formatElapsed(uint32_t seconds, char* output, std::size_t outputSize) {
    if (seconds < 60U) {
        std::snprintf(output, outputSize, "%lu 秒", static_cast<unsigned long>(seconds));
    } else if (seconds < 3600U) {
        std::snprintf(output, outputSize, "%lu 分 %lu 秒",
                      static_cast<unsigned long>(seconds / 60U),
                      static_cast<unsigned long>(seconds % 60U));
    } else {
        std::snprintf(output, outputSize, "%lu 小时 %lu 分",
                      static_cast<unsigned long>(seconds / 3600U),
                      static_cast<unsigned long>((seconds % 3600U) / 60U));
    }
}

void sendDuration(uint32_t seconds) {
    char text[32]{};
    formatElapsed(seconds, text, sizeof(text));
    Esp32BaseWeb::sendChunk(text);
}

void sendWaterVolume(uint64_t milliliters) {
    if (milliliters < 1000ULL) {
        sendUnsigned64(milliliters);
        Esp32BaseWeb::sendChunk(" mL");
        return;
    }
    const uint64_t liters = milliliters / 1000ULL;
    const uint64_t remainder = milliliters % 1000ULL;
    sendUnsigned64(liters);
    if (remainder != 0) {
        char decimals[5];
        std::snprintf(decimals, sizeof(decimals), ".%03u",
                      static_cast<unsigned>(remainder));
        std::size_t length = std::strlen(decimals);
        while (length > 1 && decimals[length - 1U] == '0') {
            decimals[--length] = '\0';
        }
        Esp32BaseWeb::sendChunk(decimals);
    }
    Esp32BaseWeb::sendChunk(" L");
}

void sendCompactWaterVolume(uint64_t milliliters) {
    if (milliliters < 1000ULL) {
        sendUnsigned64(milliliters);
        Esp32BaseWeb::sendChunk(" mL");
        return;
    }
    const uint64_t roundedTenths = (milliliters + 50ULL) / 100ULL;
    sendUnsigned64(roundedTenths / 10ULL);
    Esp32BaseWeb::sendChunk(".");
    sendUnsigned64(roundedTenths % 10ULL);
    Esp32BaseWeb::sendChunk(" L");
}

void sendLiters(uint64_t milliliters) {
    char text[32];
    std::snprintf(text,
                  sizeof(text),
                  "%llu.%03llu L",
                  static_cast<unsigned long long>(milliliters / 1000ULL),
                  static_cast<unsigned long long>(milliliters % 1000ULL));
    Esp32BaseWeb::sendChunk(text);
}

void sendFlowRate(uint32_t millilitersPerMinute) {
    sendLiters(millilitersPerMinute);
    Esp32BaseWeb::sendChunk("/min");
}

void sendRecordTimeRange(const Esp32BaseRecordStore::RecordTiming& timing) {
    uint32_t startedEpoch = 0;
    uint32_t completedEpoch = 0;
    char started[32]{};
    char completed[16]{};
    if (Esp32BaseRecordStore::resolveStartedEpoch(timing, startedEpoch) &&
        Esp32BaseRecordStore::resolveCompletedEpoch(timing, completedEpoch) &&
        Esp32BaseTime::formatEpoch(startedEpoch,
                                   started,
                                   sizeof(started),
                                   "%m月%d日 %H:%M:%S") &&
        Esp32BaseTime::formatEpoch(completedEpoch,
                                   completed,
                                   sizeof(completed),
                                   "%H:%M:%S")) {
        Esp32BaseWeb::writeHtmlEscaped(started);
        Esp32BaseWeb::sendChunk("–");
        Esp32BaseWeb::writeHtmlEscaped(completed);
        return;
    }
    sendRecordTime(timing, "%m月%d日 %H:%M:%S");
}

void sendRecordSource(const WateringRecordPayload& payload,
                      const IrrigationConfig* config) {
    Esp32BaseWeb::sendChunk(sourceName(payload.source));
    if (payload.targetMode == WateringTargetMode::Volume) Esp32BaseWeb::sendChunk(" · 按水量");
    if (payload.planId == 0) return;
    Esp32BaseWeb::sendChunk(" · ");
    if (config && payload.planId <= config->plans.size()) {
        const WateringPlan& plan = config->plans[payload.planId - 1U];
        if (plan.configured) {
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            return;
        }
    }
    Esp32BaseWeb::sendChunk("计划 ");
    sendUnsigned(payload.planId);
    Esp32BaseWeb::sendChunk("（已删除）");
}

bool recordHasCappedEstimate(const WateringRecordPayload& payload) {
    for (const ZoneWateringRecord& zone : payload.zones) {
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
            return true;
        }
    }
    return false;
}

uint8_t recordPlannedZoneCount(const WateringRecordPayload& payload) {
    uint8_t count = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        if (zone.plannedDurationSec != 0) ++count;
    }
    return count;
}

uint32_t recordTargetWaterMl(const WateringRecordPayload& payload) {
    for (const ZoneWateringRecord& zone : payload.zones) {
        if (zone.targetWaterMl != 0) return zone.targetWaterMl;
    }
    return 0;
}

uint8_t recordStartedZoneCount(const WateringRecordPayload& payload) {
    uint8_t count = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        if (zone.result != ZoneWateringResult::NotStarted) ++count;
    }
    return count;
}

uint8_t recordCompletedZoneCount(const WateringRecordPayload& payload) {
    uint8_t count = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        if (zone.result == ZoneWateringResult::Completed) ++count;
    }
    return count;
}

int8_t recordAffectedZoneIndex(const WateringRecordPayload& payload) {
    for (uint8_t index = 0; index < payload.zones.size(); ++index) {
        const ZoneWateringResult result = payload.zones[index].result;
        if (result == ZoneWateringResult::Failed ||
            result == ZoneWateringResult::Stopped) {
            return static_cast<int8_t>(index);
        }
    }
    return -1;
}

void sendZoneName(const IrrigationConfig* config, uint8_t index) {
    if (config && index < config->zones.size()) {
        Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
        return;
    }
    Esp32BaseWeb::sendChunk("水路 ");
    sendUnsigned(index + 1U);
}

bool recordHasFlowAlert(const WateringRecordPayload& payload);
void sendRecordFlowAlertSummary(const WateringRecordPayload& payload,
                                const IrrigationConfig* config);

const char* recordOutcomeTone(const WateringRecordPayload& payload) {
    if (payload.result == WateringResult::Failed) return "danger";
    if (payload.result == WateringResult::StartFailed) return "danger";
    if (payload.result == WateringResult::Incomplete) return "warn";
    if (payload.result == WateringResult::Stopped || recordHasFlowAlert(payload)) {
        return "warn";
    }
    return payload.result == WateringResult::Completed ? "ok" : "info";
}

const char* recordOutcomeName(const WateringRecordPayload& payload) {
    if (payload.result == WateringResult::StartFailed) return "启动失败";
    if (payload.result == WateringResult::Failed) return "失败";
    if (payload.result == WateringResult::Stopped) return "已停止";
    if (payload.result == WateringResult::Completed &&
        recordHasFlowAlert(payload)) {
        return "完成但有报警";
    }
    return payload.result == WateringResult::Completed ? "已完成" : "未知";
}

const char* startRejectionReasonText(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::BusyManualWatering: return "当时正在手动浇水";
        case WateringStopReason::BusyAutomaticWatering: return "当时另一个计划正在运行";
        case WateringStopReason::BusyZoneFlowLearning: return "当时正在学习水路基准";
        case WateringStopReason::PreviousResultPending: return "上一任务的结果尚未完成保存";
        case WateringStopReason::ControllerNotReady: return "当时设备或记录存储未就绪";
        case WateringStopReason::InvalidRequest: return "计划参数或水路配置无效";
        case WateringStopReason::HardwareFailure: return "控制输出启动失败";
        default: return "设备拒绝本次计划启动";
    }
}

void sendRecordOutcomeSummary(const WateringRecordPayload& payload,
                              const IrrigationConfig* config) {
    if (payload.result == WateringResult::StartFailed) {
        Esp32BaseWeb::sendChunk(startRejectionReasonText(payload.stopReason));
        Esp32BaseWeb::sendChunk("，本次计划未执行，不补浇");
        return;
    }
    if (payload.result == WateringResult::Incomplete) {
        Esp32BaseWeb::sendChunk("设备重启时发现未收尾任务，未保存的执行进度、停止时间和水量无法恢复；不会自动续浇。");
        return;
    }
    const uint8_t planned = recordPlannedZoneCount(payload);
    const uint8_t completed = recordCompletedZoneCount(payload);
    const uint8_t started = recordStartedZoneCount(payload);
    const int8_t affected = recordAffectedZoneIndex(payload);
    if (payload.result == WateringResult::Completed) {
        sendUnsigned(completed);
        Esp32BaseWeb::sendChunk(" / ");
        sendUnsigned(planned);
        Esp32BaseWeb::sendChunk(" 条水路完成");
        if (recordHasFlowAlert(payload)) {
            Esp32BaseWeb::sendChunk("，");
            sendRecordFlowAlertSummary(payload, config);
        } else if (payload.targetMode == WateringTargetMode::Volume) {
            Esp32BaseWeb::sendChunk("，按目标结束");
        } else {
            Esp32BaseWeb::sendChunk("，均按计划结束");
        }
        return;
    }
    if (affected >= 0) {
        sendZoneName(config, static_cast<uint8_t>(affected));
        switch (payload.stopReason) {
            case WateringStopReason::FlowStartTimeout:
                Esp32BaseWeb::sendChunk("启动后未检测到水流");
                break;
            case WateringStopReason::NoFlowTimeout:
                Esp32BaseWeb::sendChunk("浇水过程中水流中断");
                break;
            case WateringStopReason::LowFlow:
                Esp32BaseWeb::sendChunk("因流量过低停止");
                break;
            case WateringStopReason::HighFlow:
                Esp32BaseWeb::sendChunk("因流量过高停止");
                break;
            case WateringStopReason::UserStopped:
                Esp32BaseWeb::sendChunk("浇水时由用户停止");
                break;
            case WateringStopReason::MaintenanceInterrupted:
                Esp32BaseWeb::sendChunk("因维护操作中断");
                break;
            case WateringStopReason::TargetVolumeTimeout:
                Esp32BaseWeb::sendChunk("达到最长运行时间，未达到目标水量");
                break;
            default:
                Esp32BaseWeb::sendChunk("执行时异常停止");
                break;
        }
    } else {
        Esp32BaseWeb::sendChunk(stopReasonName(payload.stopReason));
    }
    if (planned > started) {
        Esp32BaseWeb::sendChunk("，后续 ");
        sendUnsigned(planned - started);
        Esp32BaseWeb::sendChunk(" 个水路未执行");
    } else if (payload.result == WateringResult::Failed) {
        Esp32BaseWeb::sendChunk("，整次任务已安全停止");
    }
}

void sendRecordWateredZones(const WateringRecordPayload& payload,
                            const IrrigationConfig* config) {
    if (payload.result == WateringResult::Incomplete) {
        bool emitted = false;
        for (uint8_t i=0;i<payload.zones.size();++i) {
            if (!payload.zones[i].plannedDurationSec) continue;
            if (emitted) Esp32BaseWeb::sendChunk("、");
            sendZoneName(config,i); emitted=true;
        }
        Esp32BaseWeb::sendChunk("（执行情况未知）"); return;
    }
    const uint8_t total = recordStartedZoneCount(payload);
    uint8_t emitted = 0;
    for (uint8_t index = 0; index < payload.zones.size() && emitted < 2; ++index) {
        const ZoneWateringRecord& zone = payload.zones[index];
        if (zone.result == ZoneWateringResult::NotStarted) continue;
        if (emitted != 0) Esp32BaseWeb::sendChunk("、");
        sendZoneName(config, index);
        if (zone.result == ZoneWateringResult::Failed) {
            Esp32BaseWeb::sendChunk("（失败）");
        } else if (zone.result == ZoneWateringResult::Stopped) {
            Esp32BaseWeb::sendChunk("（停止）");
        }
        ++emitted;
    }
    if (total == 0) {
        Esp32BaseWeb::sendChunk("未开始");
        return;
    }
    if (total > emitted) {
        Esp32BaseWeb::sendChunk(" 等 ");
        sendUnsigned(total);
        Esp32BaseWeb::sendChunk(" 路");
    }
}

bool recordHasFlowAlert(const WateringRecordPayload& payload) {
    for (const ZoneWateringRecord& zone : payload.zones) {
        if ((zone.flags & (WateringRecordCodec::kZoneFlagLowFlow |
                           WateringRecordCodec::kZoneFlagHighFlow)) != 0) {
            return true;
        }
    }
    return false;
}

uint8_t recordFlowAlertZoneCount(const WateringRecordPayload& payload) {
    uint8_t count = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        if ((zone.flags & (WateringRecordCodec::kZoneFlagLowFlow |
                           WateringRecordCodec::kZoneFlagHighFlow)) != 0) {
            ++count;
        }
    }
    return count;
}

uint8_t recordBaselineZoneCount(const WateringRecordPayload& payload) {
    uint8_t count = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        if (zone.result != ZoneWateringResult::NotStarted &&
            (zone.flags &
             WateringRecordCodec::kZoneFlagFlowBaselineAvailable) != 0) {
            ++count;
        }
    }
    return count;
}

void sendRecordFlowAlertSummary(const WateringRecordPayload& payload,
                                const IrrigationConfig* config) {
    const uint8_t total = recordFlowAlertZoneCount(payload);
    if (total == 0) {
        Esp32BaseWeb::sendChunk("未触发高低流量报警");
        return;
    }
    if (total > 1) {
        sendUnsigned(total);
        Esp32BaseWeb::sendChunk(" 路曾触发高低流量报警");
        return;
    }
    for (uint8_t index = 0; index < payload.zones.size(); ++index) {
        const ZoneWateringRecord& zone = payload.zones[index];
        if ((zone.flags & (WateringRecordCodec::kZoneFlagLowFlow |
                           WateringRecordCodec::kZoneFlagHighFlow)) == 0) {
            continue;
        }
        if (config) {
            Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
        } else {
            Esp32BaseWeb::sendChunk("水路 ");
            sendUnsigned(index + 1U);
        }
        const bool low =
            (zone.flags & WateringRecordCodec::kZoneFlagLowFlow) != 0;
        const bool high =
            (zone.flags & WateringRecordCodec::kZoneFlagHighFlow) != 0;
        Esp32BaseWeb::sendChunk(low && high
            ? "曾触发低流量和高流量报警"
            : high ? "曾触发高流量报警" : "曾触发低流量报警");
        return;
    }
}

void html(const char* text) { Esp32BaseWeb::sendChunk(text); }
struct LatestRecordContext {
    bool found = false;
    StoredWateringRecord record{};
};

void collectLatestRecord(const StoredWateringRecord& record, void* user) {
    auto* context = static_cast<LatestRecordContext*>(user);
    if (context && !context->found) {
        context->record = record;
        context->found = true;
    }
}

void escaped(const char* text) { Esp32BaseWeb::writeHtmlEscaped(text ? text : ""); }
void hidden(const char* name, uint32_t value) {
    html("<input type='hidden' name='"); escaped(name); html("' value='"); sendUnsigned(value); html("'>");
}
void epochText(uint32_t epoch, const char* format = "%m-%d %H:%M") {
    char text[32]{};
    if (epoch && Esp32BaseTime::formatEpoch(epoch, text, sizeof(text), format)) escaped(text);
    else html("时间未知");
}
uint32_t selectedDay() {
    char date[12]{}, dateTime[24]{}; uint32_t epoch = 0;
    if (getParam("date", date, sizeof(date)) && std::strlen(date) == 10) {
        std::snprintf(dateTime, sizeof(dateTime), "%sT00:00", date);
        if (IrrigationTime::parseLocalDateTimeUtc8(dateTime, epoch)) return WateringHistory::localDay(epoch);
    }
    const auto now = Esp32BaseTime::snapshot();
    return now.synced ? WateringHistory::localDay(now.epochSec) : 0;
}
void dayText(uint32_t day) { epochText(day * 86400U - 8U * 3600U, "%Y-%m-%d"); }
void dateNav(uint32_t day) {
    html("<nav class='actions day-nav' aria-label='选择日期'><a class='btnlink secondary' href='?date="); dayText(day - 1);
    html("' aria-label='前一天'>‹</a><form method='get' class='day-nav-form'><input class='day-nav-date' aria-label='日期' type='date' name='date' value='"); dayText(day);
    html("' onchange='this.form.submit()'><noscript><button>查看</button></noscript></form><a class='btnlink secondary' href='?date="); dayText(day + 1);
    html("' aria-label='后一天'>›</a><a class='day-nav-today' href='?'>今天</a></nav>");
}
enum class ConditionLevel : uint8_t { Danger, Warn };

struct ConditionItem {
    ConditionLevel level;
    const char* title;
};

// All current abnormalities in severity order. Details are rendered by
// renderConditionDetail; the badges and the dialog share the same item list.
uint8_t collectConditions(ConditionItem* items, uint8_t capacity) {
    uint8_t count = 0;
    auto add = [&](ConditionLevel level, const char* title) {
        if (count < capacity) items[count++] = {level, title};
    };
    const auto rtcState = g_app->eventConditionState(1);
    const bool rtcUnavailable =
        rtcState == IrrigationEvents::ConditionDisplayState::Active ||
        rtcState == IrrigationEvents::ConditionDisplayState::ConfirmingRecovery;
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    const uint32_t checkpointEpoch = g_app->lastKnownAliveEpoch();
    const char* resetReason = Esp32BaseSystem::resetReason();
    const bool possiblePowerLoss =
        now.synced && checkpointEpoch != 0 &&
        now.bootStartEpochSec >= checkpointEpoch &&
        (std::strcmp(resetReason, "poweron") == 0 ||
         std::strcmp(resetReason, "brownout") == 0);
    if (g_app->unexpectedFlowAlarm()) add(ConditionLevel::Danger, "关阀后水流异常");
    if (!g_app->businessReady()) add(ConditionLevel::Danger, "灌溉功能未就绪");
    if (g_app->recordStorageFault()) add(ConditionLevel::Danger, "浇水记录存储异常");
    if (g_app->schedulerStorageFault()) add(ConditionLevel::Danger, "自动调度不可用");
    if (g_app->eventStorageFault()) add(ConditionLevel::Warn, "操作记录保存异常");
    if (g_app->schedulerTimeState() == WateringScheduler::TimeState::RtcRollback)
        add(ConditionLevel::Warn, "设备时间倒退");
    else if (g_app->schedulerTimeState() != WateringScheduler::TimeState::Ready)
        add(ConditionLevel::Warn, "设备时间未就绪");
    if (rtcUnavailable) add(ConditionLevel::Warn, "硬件时钟不可用");
    if (possiblePowerLoss) add(ConditionLevel::Warn, "可能发生断电");
    return count;
}

void conditionDetail(const char* title) {
    html("<dt>是什么</dt><dd>");
    if (std::strcmp(title, "关阀后水流异常") == 0) {
        const uint16_t observedSec = g_app->unexpectedFlowObservedWindowSec();
        char estimatedFlow[20]{};
        IrrigationConfigRules::formatLitersPerMinute(
            g_app->unexpectedFlowEstimatedMlPerMinute(), estimatedFlow, sizeof(estimatedFlow));
        html("水泵和全部阀门均已关闭，但近 ");
        sendUnsigned(observedSec == 0 ? 1 : observedSec);
        html(" 秒的关阀后监测窗口内仍检测到 ");
        sendUnsigned(g_app->unexpectedFlowObservedPulseCount());
        html(" 个水流脉冲，估算平均流量 ");
        Esp32BaseWeb::writeHtmlEscaped(estimatedFlow);
        html(" L/min。</dd><dt>影响</dt><dd>可能存在阀门未关严、余流或其他水流来源；设备只有一个总流量计，无法判断具体是哪条水路。</dd><dt>建议处理</dt><dd>请检查阀门、管路和流量计，必要时立即关闭上游水源；排除现场问题后该状态会在后续观察恢复正常时消除。");
    } else if (std::strcmp(title, "灌溉功能未就绪") == 0) {
        const auto loadResult = g_app->configurationLoadResult();
        if (loadResult == IrrigationConfigStore::LoadResult::StorageUnavailable) {
            html("设备存储不可用，新设备首次烧录后可能尚未初始化文件系统。</dd><dt>影响</dt><dd>配置无法读取，水泵和全部阀门保持关闭，不能开始浇水。</dd><dt>建议处理</dt><dd>确认设备中没有需要保留的数据后，可到<a href='/esp32base/system'>系统工具</a>格式化 LittleFS；如果设备此前已经使用过，请勿直接格式化，先查看系统状态和日志。");
        } else if (loadResult == IrrigationConfigStore::LoadResult::InvalidConfig) {
            html("当前配置结构不兼容，或配置文件没有有效副本。</dd><dt>影响</dt><dd>全部输出保持关闭，不能开始浇水。</dd><dt>建议处理</dt><dd>如需保留现有配置请先备份，不要直接格式化；备份后到<a href='/esp32base/system'>系统工具</a>格式化 LittleFS 并重新配置。");
        } else if (loadResult == IrrigationConfigStore::LoadResult::WriteFailed) {
            html("文件系统可以读取，但配置写入或写入后校验失败。</dd><dt>影响</dt><dd>全部输出保持关闭，配置修改不能可靠保存。</dd><dt>建议处理</dt><dd>请先查看<a href='/esp32base/system'>系统状态</a>和<a href='/esp32base/logs'>系统日志</a>，不要直接格式化。");
        } else {
            html("启动检查未能完成，灌溉功能没有进入就绪状态。</dd><dt>影响</dt><dd>全部输出保持关闭，不能开始浇水。</dd><dt>建议处理</dt><dd>请查看<a href='/esp32base/system'>系统状态</a>和<a href='/esp32base/logs'>系统日志</a>，不要直接格式化。");
        }
    } else if (std::strcmp(title, "浇水记录存储异常") == 0) {
        html("浇水历史无法可靠写入或读取；正常的历史滚动淘汰不会造成这个状态。</dd><dt>影响</dt><dd>为避免丢失任务结果，设备暂时拒绝开始新的浇水任务。</dd><dt>建议处理</dt><dd>请检查<a href='/esp32base/system'>设备存储状态</a>和任务恢复信息；存储恢复后自动解除。");
    } else if (std::strcmp(title, "自动调度不可用") == 0) {
        html("自动调度防重复状态无法读取或写入。</dd><dt>影响</dt><dd>自动计划不会启动，以免同一计划被重复执行；手动浇水仍可正常使用。</dd><dt>建议处理</dt><dd>请检查<a href='/esp32base/system'>设备存储状态</a>；恢复后自动调度自行恢复。");
    } else if (std::strcmp(title, "操作记录保存异常") == 0) {
        html("操作审计事件无法可靠保存。</dd><dt>影响</dt><dd>修改计划、水路和自动总控等配置操作暂时不可用；浇水本身不受此故障影响。</dd><dt>建议处理</dt><dd>请检查<a href='/esp32base/system'>设备存储状态</a>。");
    } else if (std::strcmp(title, "设备时间倒退") == 0) {
        html("检测到设备时间相对已确认的可信时刻发生明显倒退。</dd><dt>影响</dt><dd>自动计划已停止，避免按旧时间重复浇水；手动浇水仍可使用，期间错过的计划不会补浇。</dd><dt>建议处理</dt><dd>等待 NTP 网络校时恢复后设备会自动重新判断；若反复出现请检查 RTC 接线与电池。");
    } else if (std::strcmp(title, "设备时间未就绪") == 0) {
        html("当前还没有来自 RTC 或 NTP 的可信时间。</dd><dt>影响</dt><dd>自动计划暂时不会运行；手动浇水仍可使用，现场保护照常生效。</dd><dt>建议处理</dt><dd>联网后通常由 NTP 自动校时；长期断网使用请检查 RTC 接线与电池，可在<a href='/esp32base/system'>系统状态</a>查看时间来源。");
    } else if (std::strcmp(title, "硬件时钟不可用") == 0) {
        html("RTC 硬件时钟观察为不可用（缺失或读数异常）。</dd><dt>影响</dt><dd>有可信网络时间时自动浇水仍可执行；断网失去可信时间后，自动计划会暂停。</dd><dt>建议处理</dt><dd>请检查 RTC 接线和电池；时间状态可在<a href='/esp32base/system'>系统状态</a>查看。");
    } else if (std::strcmp(title, "可能发生断电") == 0) {
        const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
        char checkpointText[24]{}, bootText[24]{};
        html("本次启动原因为上电或欠压复位，且晚于最后一次存活检查点");
        if (Esp32BaseTime::formatEpoch(g_app->lastKnownAliveEpoch(), checkpointText, sizeof(checkpointText), "%m-%d %H:%M") &&
            Esp32BaseTime::formatEpoch(now.bootStartEpochSec, bootText, sizeof(bootText), "%m-%d %H:%M")) {
            html("（可能范围 ");
            Esp32BaseWeb::writeHtmlEscaped(checkpointText);
            html(" 至 ");
            Esp32BaseWeb::writeHtmlEscaped(bootText);
            html("）");
        }
        html("。</dd><dt>影响</dt><dd>这只是根据检查点推断的可能范围，不是精确停电时间；若断电时正在浇水，对应任务会记录为中断且结果不完整。</dd><dt>建议处理</dt><dd>可到<a href='/irrigation/records'>浇水记录</a>核对中断任务；确认后无需处理，本提示仅本次启动期间显示。");
    }
    html("</dd>");
}

void conditions() {
    ConditionItem items[9]{};
    const uint8_t count = collectConditions(items, 9);
    if (count == 0) return;
    html("<section id='conditions' class='conditions' aria-label='设备状态'><div class='cond-badges'>");
    for (uint8_t i = 0; i < count; ++i) {
        html("<button type='button' class='cond-badge ");
        html(items[i].level == ConditionLevel::Danger ? "danger" : "warn");
        html("' onclick=\"document.getElementById('device-conditions').showModal()\">");
        Esp32BaseWeb::writeHtmlEscaped(items[i].title);
        html("</button>");
    }
    html("</div><dialog id='device-conditions' class='panel eb-modal cond-modal' data-eb-light-dismiss='1'>"
         "<h2>设备状态</h2><p class='muted'>以下是当前需要关注的问题，按严重程度排列；没有列出的项目状态正常。</p>");
    for (uint8_t i = 0; i < count; ++i) {
        html("<article class='cond-item ");
        html(items[i].level == ConditionLevel::Danger ? "danger" : "warn");
        html("'><div class='cond-item-head'><span class='tag ");
        html(items[i].level == ConditionLevel::Danger ? "danger" : "warn");
        html("'>");
        html(items[i].level == ConditionLevel::Danger ? "严重" : "提醒");
        html("</span><h3>");
        Esp32BaseWeb::writeHtmlEscaped(items[i].title);
        html("</h3></div><dl class='cond-detail'>");
        conditionDetail(items[i].title);
        html("</dl></article>");
    }
    html("<div class='actions'><button type='button' class='secondary' onclick=\"this.closest('dialog').close()\">知道了</button></div></dialog></section>");
}
struct DayPlanSlot {
    uint8_t planIndex;
    uint16_t minuteOfDay;
};

// Scheduled start points of one local day, in chronological order. Mirrors the
// scheduler's configured/scheduleEnabled/start-minute filters; slots of plans
// without any runnable zone are kept and flagged in the UI, matching the
// scheduler which still selects them and records a skip when they come due.
void renderDayPlans(uint32_t day,
                    const NextAutomaticWatering& next,
                    const AutomaticWateringState& automatic,
                    WateringScheduler::TimeState schedulerTime,
                    uint32_t today,
                    uint16_t currentMinute,
                    bool currentTimeKnown) {
    if (today && day < today) return;
    const IrrigationConfig* config = g_app->configuration();
    if (!config) return;
    const bool isToday = today && day == today;
    DayPlanSlot slots[kWateringPlanCount * kPlanStartTimeCount]{};
    uint8_t slotCount = 0;
    bool anyScheduledPlan = false;
    for (uint8_t planIndex = 0; planIndex < config->plans.size(); ++planIndex) {
        const WateringPlan& plan = config->plans[planIndex];
        if (!plan.configured) continue;
        for (uint16_t minute : plan.startMinutes) {
            if (minute == kUnusedStartMinute) continue;
            if (plan.scheduleEnabled) {
                anyScheduledPlan = true;
                slots[slotCount++] = {planIndex, minute};
            }
        }
    }
    for (uint8_t i = 1; i < slotCount; ++i) {
        const DayPlanSlot value = slots[i];
        int8_t j = static_cast<int8_t>(i) - 1;
        while (j >= 0 && slots[j].minuteOfDay > value.minuteOfDay) {
            slots[j + 1] = slots[j];
            --j;
        }
        slots[j + 1] = value;
    }
    const char* heading = isToday ? "今日计划" : (today && day == today + 1U ? "明日计划" : "当日计划");
    html("<div class='home-day-plans'><div class='home-day-plans-head'><h3>");
    html(heading);
    html("</h3>");
    if (schedulerTime != WateringScheduler::TimeState::Ready) {
        html("<span class='tag warn'>时间未就绪</span>");
    } else if (automatic.mode != AutomaticWateringMode::Enabled) {
        html("<span class='tag warn'>自动浇水已暂停</span>");
    } else {
        html("<span class='tag ok'>自动浇水正常</span>");
    }
    html("</div>");
    if (automatic.mode == AutomaticWateringMode::PausedIndefinitely) {
        html("<p class='notice warn'>自动浇水已暂停，到点计划不会执行，也不会补浇。<a href='/irrigation/plans'>恢复自动浇水</a></p>");
    } else if (automatic.mode == AutomaticWateringMode::PausedUntil) {
        char pauseText[64]{};
        html("<p class='notice warn'>自动浇水暂停中");
        if (formatFullDateTime(automatic.resumeAtEpoch, pauseText, sizeof(pauseText))) {
            html("，计划于 ");
            Esp32BaseWeb::writeHtmlEscaped(pauseText);
            html(" 自动恢复");
        }
        html("。<a href='/irrigation/plans'>管理暂停</a></p>");
    }
    if (!anyScheduledPlan) {
        html("<p class='muted'>还没有开启自动执行的计划。<a href='/irrigation/plans'>去设置计划</a></p></div>");
        return;
    }
    bool listed = false;
    for (uint8_t i = 0; i < slotCount; ++i) {
        const DayPlanSlot& slot = slots[i];
        if (isToday && currentTimeKnown && slot.minuteOfDay <= currentMinute) continue;
        const WateringPlan& plan = config->plans[slot.planIndex];
        uint8_t zoneCount = 0;
        uint32_t totalMinutes = 0;
        for (uint8_t zoneIndex = 0; zoneIndex < plan.zoneDurationMinutes.size(); ++zoneIndex) {
            if (!config->zones[zoneIndex].enabled || plan.zoneDurationMinutes[zoneIndex] == 0) continue;
            ++zoneCount;
            totalMinutes += plan.zoneDurationMinutes[zoneIndex];
        }
        const uint32_t slotEpoch = day * 86400U + static_cast<uint32_t>(slot.minuteOfDay) * 60U - 8U * 3600U;
        const bool isNext = next.status == NextAutomaticWateringStatus::Available &&
                            next.planId == plan.id && next.scheduledEpoch == slotEpoch;
        html("<div class='home-day-plan");
        if (isNext) html(" is-next");
        html("'><b class='home-day-plan-time'>");
        char clock[8]{};
        std::snprintf(clock, sizeof(clock), "%02u:%02u",
                      static_cast<unsigned>(slot.minuteOfDay / 60U),
                      static_cast<unsigned>(slot.minuteOfDay % 60U));
        html(clock);
        if (isNext) html("<span class='tag info'>下一次</span>");
        html("</b><span class='home-day-plan-name'>");
        Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
        html("</span><small>");
        if (zoneCount == 0) html("计划内没有可执行水路，到点会记录一条启动失败");
        else { sendUnsigned(zoneCount); html(" 路 · 共 "); sendUnsigned(totalMinutes); html(" 分钟"); }
        html("</small></div>");
        listed = true;
    }
    if (!listed) html("<p class='muted'>今天剩余时间没有已排期的自动浇水。</p>");
    html("<p class='home-day-plans-link'><a href='/irrigation/plans'>管理计划</a></p></div>");
}

void renderDay(uint32_t day, bool includePlans) {
    if (!day) { html("<p class='muted'>设备时间未知，日期统计暂不可用。仍可查看全部浇水记录。</p>"); return; }
    const auto daily = g_app->wateringDay(day); const auto* config = g_app->configuration();
    if (!daily.readable) { html("<p class='notice warn'>记录暂时无法读取，不能据此判断没有浇水。</p>"); return; }
    html("<div class='home-grid'>");
    bool shown = false;
    for (size_t i = 0; i < daily.zones.size(); ++i) {
        const auto& z = daily.zones[i];
        if ((!config || !config->zones[i].enabled) && !z.count && !z.failures && !z.unknown && !z.active) continue;
        shown = true;
        html("<a class='home-plan' style='text-decoration:none;color:inherit' href='/irrigation/records?date="); dayText(day); html("'><span class='zone-title'>");
        sendZoneName(config, i); html("</span><b class='home-main'>");
        if (z.unknown && !z.count) html("时长未知"); else sendDuration(z.seconds);
        html("</b><span>"); sendUnsigned(z.count); html(" 次出水");
        if (z.active) html(" · 进行中，暂计");
        else if (z.failures) { html(" · "); sendUnsigned(z.failures); html(" 次停止或异常"); }
        else if (!z.count && !z.unknown) html(" · 暂无出水记录");
        if (z.unknown) { html(" · "); sendUnsigned(z.unknown); html(" 次结果不完整"); }
        html("</span><small>估算用水 ");
        if (z.unknown && !z.count) html("未知"); else { sendWaterVolume(z.waterMl); if (z.unknown) html("（仅已知部分）"); }
        html("</small></a>");
    }
    html("</div>");
    if (!shown) html("<p class='muted'>暂无启用水路或该日记录。请到设备设置配置水路。</p>");
    if (daily.truncated) html("<p class='notice warn'>较早历史已滚动淘汰，本日统计可能不完整。</p>");
    if (daily.unknownTimeCount) { html("<p class='muted'>另有 "); sendUnsigned(daily.unknownTimeCount); html(" 条时间未知的记录未计入日期统计。</p>"); }
    if (daily.startFailed) { html("<p class='notice warn'>当天有 "); sendUnsigned(daily.startFailed); html(" 次自动计划<a href='/irrigation/records?date="); dayText(day); html("&result=issues'>启动失败</a>，均未出水。</p>"); }
    html("<p class='muted'>按各水路开始日期归属；跨日任务不拆分。时长为实际浇水时间，水量为估算值。</p>");
    if (includePlans) {
        const Esp32BaseTime::Snapshot snapshot = Esp32BaseTime::snapshot();
        const uint32_t today = snapshot.synced ? WateringHistory::localDay(snapshot.epochSec) : 0U;
        const uint16_t currentMinute = snapshot.synced
                                           ? static_cast<uint16_t>(((snapshot.epochSec + 8U * 3600U) % 86400U) / 60U)
                                           : 0U;
        renderDayPlans(day,
                       g_app->nextAutomaticWatering(),
                       g_app->automaticWateringState(),
                       g_app->schedulerTimeState(),
                       today,
                       currentMinute,
                       snapshot.synced);
    }
}

void renderManualDialog(const IrrigationConfig& config) {
    bool hasConfiguredPlan = false;
    for (const WateringPlan& plan : config.plans)
        if (plan.configured) hasConfiguredPlan = true;
    const bool reopen = Esp32BaseWeb::hasParam("open_manual");
    Esp32BaseWeb::sendChunk("<dialog id='manual-watering' class='panel eb-modal manual-modal' data-eb-light-dismiss='1'");
    if (reopen) html(" open");
    html("><h2>手动浇水</h2><p class='muted'>可以从计划填入时长，也可以直接设置；最终只按这里提交的内容执行，不会修改计划。每条水路可单独选择按时长或按水量，0 表示本次不执行。</p>"
         "<form id='manual-form' method='post' action='/irrigation' onsubmit='return submitManualWatering(this)'>"
         "<input type='hidden' name='action' value='start_manual'>");
    html("<div class='manual-template'><div class='manual-template-head'><h3>快速填入计划</h3><a href='/irrigation/plans'>管理计划</a></div>");
    if (hasConfiguredPlan) {
        html("<div class='manual-template-list'>");
        for (const WateringPlan& plan : config.plans) {
            if (!plan.configured) continue;
            uint8_t activeZoneCount = 0;
            uint32_t totalMinutes = 0;
            for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                if (!config.zones[index].enabled || plan.zoneDurationMinutes[index] == 0) continue;
                ++activeZoneCount;
                totalMinutes += plan.zoneDurationMinutes[index];
            }
            html("<button type='button' class='manual-template-card' data-plan-name='");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            html("' data-durations='");
            for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                if (index != 0) html(",");
                sendUnsigned(plan.zoneDurationMinutes[index]);
            }
            html("'");
            if (activeZoneCount == 0) html(" disabled");
            html("><b>");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            html("</b><span>");
            bool emittedZone = false;
            for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                if (!config.zones[index].enabled || plan.zoneDurationMinutes[index] == 0) continue;
                if (emittedZone) html(" · ");
                Esp32BaseWeb::writeHtmlEscaped(config.zones[index].name.data());
                html(" "); sendUnsigned(plan.zoneDurationMinutes[index]); html("分");
                emittedZone = true;
            }
            if (!emittedZone) html("当前无可执行水路");
            html("</span><small>"); sendUnsigned(activeZoneCount); html(" 路 · 共 ");
            sendUnsigned(totalMinutes); html(" 分钟</small></button>");
        }
        html("</div>");
    } else {
        html("<div class='manual-template-empty'>还没有可用计划，可以直接设置下方各水路。</div>");
    }
    html("</div><div class='manual-grid'>");
    for (uint8_t index = 0; index < BoardPins::kZoneCount; ++index) {
        if (!config.zones[index].enabled) continue;
        char modeText[4]{};
        char valueText[16]{};
        char modeParam[8]{};
        char valueParam[8]{};
        std::snprintf(modeParam, sizeof(modeParam), "m%u", static_cast<unsigned>(index + 1U));
        std::snprintf(valueParam, sizeof(valueParam), "v%u", static_cast<unsigned>(index + 1U));
        const bool volumeMode = getParam(modeParam, modeText, sizeof(modeText)) &&
                                std::strcmp(modeText, "v") == 0;
        if (!getParam(valueParam, valueText, sizeof(valueText))) std::snprintf(valueText, sizeof(valueText), "0");
        html("<div class='manual-zone' data-zone-index='"); sendUnsigned(index);
        html("' data-max-minutes='"); sendUnsigned(config.runLimits.maximumZoneDurationMinutes);
        html("' data-max-liters='"); sendUnsigned(config.runLimits.maximumSingleOutputLiters);
        html("'><label>"); Esp32BaseWeb::writeHtmlEscaped(config.zones[index].name.data());
        html("</label><select class='manual-mode' name='mode"); sendUnsigned(index + 1U);
        html("' aria-label='浇水方式'><option value='duration'");
        if (!volumeMode) html(" selected");
        html(">按时长</option><option value='volume'");
        if (volumeMode) html(" selected");
        html(">按水量</option></select><input class='manual-value' name='zone");
        sendUnsigned(index + 1U);
        html("' type='number' min='0' max='");
        sendUnsigned(volumeMode ? config.runLimits.maximumSingleOutputLiters
                               : config.runLimits.maximumZoneDurationMinutes);
        html("' step='"); html(volumeMode ? "0.1" : "1");
        html("' inputmode='decimal' value='"); Esp32BaseWeb::writeHtmlEscaped(valueText);
        html("'><span class='manual-unit'>"); html(volumeMode ? "L" : "分钟");
        html("</span></div>");
    }
    html("</div><div class='manual-summary'><b id='manual-summary'>尚未选择水路</b>"
         "<span id='manual-template-note'>按时长单位为分钟，范围 0～");
    sendUnsigned(config.runLimits.maximumZoneDurationMinutes);
    html("；按水量单位为升，步进 0.1，上限 ");
    sendUnsigned(config.runLimits.maximumSingleOutputLiters);
    html(" L。0 表示本次不执行。</span></div><div class='actions'>"
         "<button id='manual-clear' type='button' class='secondary'>全部清零</button>"
         "<button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button>"
         "<input id='manual-submit' type='submit' value='确认并开始浇水' disabled></div></form></dialog>");
}
const char* outcome(const WateringRecordPayload& p) {
    if (p.result == WateringResult::Incomplete) return "运行中断 · 结果不完整";
    return recordOutcomeName(p);
}
struct RecordRowsContext {
    const IrrigationConfig* config;
    uint32_t emitted;
};

void sendRecordDetailDialog(const StoredWateringRecord& record,
                            const IrrigationConfig* config,
                            const char* dialogPrefix) {
    const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(record.payload);
    const uint8_t plannedZoneCount = recordPlannedZoneCount(record.payload);
    const uint8_t startedZoneCount = recordStartedZoneCount(record.payload);
    const uint8_t baselineZoneCount = recordBaselineZoneCount(record.payload);
    const uint8_t alertZoneCount = recordFlowAlertZoneCount(record.payload);
    Esp32BaseWeb::sendChunk("<dialog id='");
    Esp32BaseWeb::sendChunk(dialogPrefix);
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("' class='panel eb-modal record-detail-dialog' data-eb-light-dismiss='1'><div class='record-detail-head'><div><span class='muted'>");
    sendRecordSource(record.payload, config);
    Esp32BaseWeb::sendChunk("</span><h2>");
    epochText(record.payload.startedEpoch, "%m月%d日 %H:%M");
    Esp32BaseWeb::sendChunk("</h2></div><button type='button' class='secondary record-detail-close' onclick='this.closest(\"dialog\").close()'>关闭</button></div><div class='record-detail-result'><span class='tag ");
    Esp32BaseWeb::sendChunk(recordOutcomeTone(record.payload));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(outcome(record.payload));
    Esp32BaseWeb::sendChunk("</span><span>");
    sendRecordOutcomeSummary(record.payload, config);
    Esp32BaseWeb::sendChunk("</span></div><div class='record-detail-metrics'><div><span>实际浇水</span><b>");
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else if (record.payload.result == WateringResult::StartFailed) html("—");
    else sendDuration(totals.actualWateringSec);
    Esp32BaseWeb::sendChunk("</b></div><div><span>估算用水量</span><b>");
    if (recordHasCappedEstimate(record.payload)) {
        Esp32BaseWeb::sendChunk("至少 ");
    }
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else if (record.payload.result == WateringResult::StartFailed) html("—");
    else sendWaterVolume(totals.estimatedWaterMl);
    Esp32BaseWeb::sendChunk("</b></div><div><span>执行水路</span><b>");
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else sendUnsigned(startedZoneCount);
    Esp32BaseWeb::sendChunk(" / ");
    sendUnsigned(plannedZoneCount);
    Esp32BaseWeb::sendChunk(" 路</b></div><div><span>高低流量报警</span><b>");
    if (record.payload.result == WateringResult::Incomplete) { html("未能确认"); }
    else if (record.payload.result == WateringResult::StartFailed) { html("—"); }
    else if (alertZoneCount == 0) {
        Esp32BaseWeb::sendChunk("无");
    } else {
        sendUnsigned(alertZoneCount);
        Esp32BaseWeb::sendChunk(" 路");
    }
    Esp32BaseWeb::sendChunk("</b><small>");
    sendUnsigned(baselineZoneCount);
    Esp32BaseWeb::sendChunk(" / ");
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else sendUnsigned(startedZoneCount);
    Esp32BaseWeb::sendChunk(" 路设置了基准</small></div></div><div class='record-detail-section'><h3>执行时间</h3><div class='record-detail-grid'><div class='record-time-range'><b>开始 — 完成</b><span>");
    if (record.payload.result == WateringResult::Incomplete) { epochText(record.payload.startedEpoch); html(" — 中断时间未知"); }
    else sendRecordTimeRange(record.timing);
    Esp32BaseWeb::sendChunk("</span></div><div><b>执行目标</b><span>");
    const uint32_t targetWaterMl = recordTargetWaterMl(record.payload);
    if (targetWaterMl != 0) sendWaterVolume(targetWaterMl);
    else sendDuration(totals.plannedDurationSec);
    Esp32BaseWeb::sendChunk("</span></div><div><b>任务总历时</b><span>");
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else sendDuration(record.timing.durationSec);
    Esp32BaseWeb::sendChunk("</span></div></div></div><div class='record-detail-section'><h3>水路明细</h3><p>计划和水路名称按当前设置显示；流量、基准和用水量均保留浇水当时的记录。</p><div class='tablewrap'><table class='record-zone-table'><thead><tr><th>水路</th><th>执行结果</th><th>实际 / 目标</th><th>估算用水量</th><th>当时基准流量</th><th>流量表现</th></tr></thead><tbody>");
    for (uint8_t index = 0; index < record.payload.zones.size(); ++index) {
        const ZoneWateringRecord& zone = record.payload.zones[index];
        if (zone.plannedDurationSec == 0) continue;
        Esp32BaseWeb::sendChunk("<tr><td data-label='水路'><b>");
        if (config) {
            Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
        } else {
            Esp32BaseWeb::sendChunk("水路 ");
            sendUnsigned(index + 1U);
        }
        Esp32BaseWeb::sendChunk("</b></td><td data-label='执行结果'>");
        const bool unknown = (zone.flags & WateringRecordCodec::kZoneFlagUnknown) != 0;
        html(unknown ? "执行结果未知" : zoneResultName(zone.result));
        Esp32BaseWeb::sendChunk("</td><td data-label='实际 / 目标' class='record-duration-pair'><b>");
        if (unknown) html("未知"); else sendDuration(zone.actualWateringSec);
        Esp32BaseWeb::sendChunk("</b><span>/</span><small>");
        if (zone.targetWaterMl != 0) sendWaterVolume(zone.targetWaterMl);
        else sendDuration(zone.plannedDurationSec);
        Esp32BaseWeb::sendChunk("</small></td><td data-label='估算用水量'>");
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
            Esp32BaseWeb::sendChunk("至少 ");
        }
        if (unknown) html("未知"); else sendWaterVolume(zone.estimatedWaterMl);
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
            Esp32BaseWeb::sendChunk("<small class='warn'>达到记录上限</small>");
        }
        Esp32BaseWeb::sendChunk("</td><td data-label='当时基准流量' class='record-baseline'>");
        const bool baselineAvailable =
            (zone.flags &
             WateringRecordCodec::kZoneFlagFlowBaselineAvailable) != 0;
        if (unknown) { html("未知"); }
        else if (zone.result == ZoneWateringResult::NotStarted) {
            Esp32BaseWeb::sendChunk("<span class='muted'>—</span>");
        } else if (baselineAvailable) {
            sendFlowRate(zone.baselineFlowMlPerMinute);
        } else {
            Esp32BaseWeb::sendChunk("<span class='muted'>未设置</span><small>本次不进行高低流量判定</small>");
        }
        Esp32BaseWeb::sendChunk("</td><td data-label='流量表现'><div class='record-flow-performance'>");
        if (unknown) { html("未知"); }
        else if (zone.result == ZoneWateringResult::NotStarted) {
            Esp32BaseWeb::sendChunk("<span class='muted'>—</span>");
        } else if (zone.actualWateringSec == 0) {
            Esp32BaseWeb::sendChunk("<span class='muted'>未建立有效水流</span>");
        } else {
            Esp32BaseWeb::sendChunk("<span>整段平均 ");
            sendFlowRate(zone.averageFlowMlPerMinute);
            if (baselineAvailable) {
                char comparison[48]{};
                formatFlowChange(zone.averageFlowMlPerMinute,
                                 zone.baselineFlowMlPerMinute,
                                 comparison,
                                 sizeof(comparison));
                Esp32BaseWeb::sendChunk("，较基准 ");
                Esp32BaseWeb::writeHtmlEscaped(comparison);
            }
            Esp32BaseWeb::sendChunk("</span>");
            Esp32BaseWeb::sendChunk(
                "<span class='muted'>本地记录仅保留整段平均流量</span>");
        }
        Esp32BaseWeb::sendChunk("<div class='record-flags'>");
        if ((zone.flags & WateringRecordCodec::kZoneFlagLowFlow) != 0) {
            Esp32BaseWeb::sendChunk("<span class='tag warn'>曾触发低流量报警</span>");
        }
        if ((zone.flags & WateringRecordCodec::kZoneFlagHighFlow) != 0) {
            Esp32BaseWeb::sendChunk("<span class='tag danger'>曾触发高流量报警</span>");
        }
        if ((zone.flags & (WateringRecordCodec::kZoneFlagLowFlow |
                           WateringRecordCodec::kZoneFlagHighFlow)) == 0 &&
            baselineAvailable && !unknown) {
            Esp32BaseWeb::sendChunk("<span class='tag ok'>未触发高低流量报警</span>");
        }
        Esp32BaseWeb::sendChunk("</div></div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><p class='record-detail-note'>相对基准差值用于解释流量表现，不等同于报警结果；报警以浇水当时的阈值和连续判定结果为准。</p></div><details class='record-technical' open><summary>指标与技术说明</summary><p class='record-technical-note'>整段平均只统计水流建立后的实际浇水阶段；估算用水量还包含建立水流和关阀尾水阶段的实际脉冲。未触发报警不等于流量一定正常，没有基准时不作高低流量判定。</p><div class='record-technical-grid'><div><b>总脉冲</b><span>");
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else sendUnsigned64(totals.pulseCount);
    Esp32BaseWeb::sendChunk("</span></div>");
    for (uint8_t index = 0; index < record.payload.zones.size(); ++index) {
        const ZoneWateringRecord& zone = record.payload.zones[index];
        if (zone.plannedDurationSec == 0) continue;
        Esp32BaseWeb::sendChunk("<div><b>");
        if (config) {
            Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
        } else {
            Esp32BaseWeb::sendChunk("水路 ");
            sendUnsigned(index + 1U);
        }
        Esp32BaseWeb::sendChunk("</b><span>");
        if (zone.flags & WateringRecordCodec::kZoneFlagUnknown) html("未知");
        else sendUnsigned(zone.pulseCount);
        Esp32BaseWeb::sendChunk(" 脉冲</span></div>");
    }
    Esp32BaseWeb::sendChunk("</div></details><div class='actions record-detail-bottom-close'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>关闭</button></div></dialog>");
}

void sendRecordRow(const StoredWateringRecord& record, void* user) {
    RecordRowsContext* context = static_cast<RecordRowsContext*>(user);
    const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(record.payload);
    if (context) ++context->emitted;
    Esp32BaseWeb::sendChunk("<tr><td data-label='开始时间' class='record-time'>");
    epochText(record.payload.startedEpoch);
    Esp32BaseWeb::sendChunk("</td><td data-label='浇水任务'>");
    sendRecordSource(record.payload, context ? context->config : nullptr);
    Esp32BaseWeb::sendChunk("</td><td data-label='执行水路' class='record-zones'>");
    sendRecordWateredZones(record.payload, context ? context->config : nullptr);
    Esp32BaseWeb::sendChunk("</td><td data-label='执行结果'><span class='tag ");
    Esp32BaseWeb::sendChunk(recordOutcomeTone(record.payload));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(outcome(record.payload));
    Esp32BaseWeb::sendChunk("</span>");
    if (record.payload.result != WateringResult::Completed) {
        Esp32BaseWeb::sendChunk("<small class='record-result-reason'>");
        sendRecordOutcomeSummary(
            record.payload, context ? context->config : nullptr);
        Esp32BaseWeb::sendChunk("</small>");
    } else if (recordHasFlowAlert(record.payload)) {
        Esp32BaseWeb::sendChunk("<small class='record-result-reason'>");
        sendRecordFlowAlertSummary(
            record.payload, context ? context->config : nullptr);
        Esp32BaseWeb::sendChunk("</small>");
    }
    Esp32BaseWeb::sendChunk("</td><td data-label='实际 / 目标' class='record-number record-list-duration'><span>");
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else if (record.payload.result == WateringResult::StartFailed) html("—");
    else sendDuration(totals.actualWateringSec);
    Esp32BaseWeb::sendChunk("</span><small>/ ");
    const uint32_t targetWaterMl = recordTargetWaterMl(record.payload);
    if (targetWaterMl != 0) sendCompactWaterVolume(targetWaterMl);
    else sendDuration(totals.plannedDurationSec);
    Esp32BaseWeb::sendChunk("</small></td><td data-label='估算用水量' class='record-number'>");
    if (recordHasCappedEstimate(record.payload)) Esp32BaseWeb::sendChunk("至少 ");
    if (record.payload.result == WateringResult::Incomplete) html("未知");
    else if (record.payload.result == WateringResult::StartFailed) html("—");
    else sendCompactWaterVolume(totals.estimatedWaterMl);
    Esp32BaseWeb::sendChunk("</td><td data-label='操作' class='record-action'><button type='button' class='btnlink info compact' onclick=\"document.getElementById('record-detail-");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("').showModal()\">查看详情</button>");
    sendRecordDetailDialog(record, context ? context->config : nullptr, "record-detail-");
    Esp32BaseWeb::sendChunk("</td></tr>");
}

struct HistoryRows { uint32_t day=0, offset=0, matched=0, shown=0; const char* result=nullptr; };
void historyRow(const StoredWateringRecord& record, void* user) {
    auto& q = *static_cast<HistoryRows*>(user); const auto& p = record.payload;
    if (q.day) {
        bool inDay = false;
        for(size_t i=0;i<p.zones.size();++i) {
            const auto& z=p.zones[i];
            if(z.plannedDurationSec && p.startedEpoch && WateringHistory::localDay(WateringHistory::zoneEpoch(p,i))==q.day){inDay=true;break;}
        }
        if(!inDay && p.startedEpoch && WateringHistory::localDay(p.startedEpoch)==q.day){inDay=true;}
        if(!inDay) return;
    }
    if(q.result && !strcmp(q.result,"issues") && p.result==WateringResult::Completed && !recordFlowAlertZoneCount(p)) return;
    if(q.result && !strcmp(q.result,"ok") && (p.result!=WateringResult::Completed || recordFlowAlertZoneCount(p))) return;
    if(q.matched++ < q.offset || q.shown>=20) return; ++q.shown;
    RecordRowsContext row{g_app->configuration(), 0};
    sendRecordRow(record, &row);
}
struct AuditRows { uint32_t day=0, offset=0, matched=0, shown=0; uint8_t category=0; };
void auditRow(const IrrigationEvents::EventRecord& event, void* user) {
    auto& q=*static_cast<AuditRows*>(user); uint32_t epoch=0;
    Esp32BaseRecordStore::resolveCompletedEpoch(event.timing,epoch);
    if(q.day && (!epoch || WateringHistory::localDay(epoch)!=q.day)) return;
    if(q.category && static_cast<uint8_t>(IrrigationEvents::category(event)) + 1U != q.category) return;
    if(q.matched++ < q.offset || q.shown>=20) return; ++q.shown;
    char title[192]{}, summary[256]{};
    const auto* config=g_app->configuration();
    const char* zoneName=config && event.objectId>=1 && event.objectId<=BoardPins::kZoneCount ? config->zones[event.objectId-1].name.data() : nullptr;
    IrrigationEvents::formatTitle(event,title,sizeof(title),nullptr,zoneName);
    IrrigationEvents::formatSummary(event,summary,sizeof(summary));
    html("<tr><td data-label='时间' class='event-time'>"); epochText(epoch);
    html("</td><td data-label='等级' class='event-level'><span class='tag ");
    html(event.level==IrrigationEvents::Level::Error ? "danger" : event.level==IrrigationEvents::Level::Warning ? "warn" : "info");
    html("'>"); escaped(IrrigationEvents::levelName(event.level)); html("</span></td><td data-label='事件' class='event-title'>");
    escaped(title); html("</td><td data-label='说明' class='event-summary'>"); escaped(summary); html("</td></tr>");
}

} // namespace

bool IrrigationWeb::registerRoutes(IrrigationApp& app) {
    g_app=&app;
    Esp32BaseWeb::setDeviceName("智能浇水"); Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP); Esp32BaseWeb::setHomePath("/irrigation");
    return IrrigationWebAssets::registerAssets() &&
        Esp32BaseWeb::addPage("/irrigation","首页",overview) &&
        Esp32BaseWeb::addPage("/irrigation/plans","计划",plans) &&
        Esp32BaseWeb::addPage("/irrigation/records","记录",records) &&
        Esp32BaseWeb::addPage("/irrigation/settings","设置",settings) &&
        Esp32BaseWeb::addRoute("/irrigation",Esp32BaseWeb::METHOD_POST,overview) &&
        Esp32BaseWeb::addRoute("/irrigation/plans",Esp32BaseWeb::METHOD_POST,plans) &&
        Esp32BaseWeb::addRoute("/irrigation/zones",Esp32BaseWeb::METHOD_GET,zones) &&
        Esp32BaseWeb::addRoute("/irrigation/zones",Esp32BaseWeb::METHOD_POST,zones) &&
        Esp32BaseWeb::addRoute("/irrigation/events",Esp32BaseWeb::METHOD_GET,events) &&
        Esp32BaseWeb::addRoute("/irrigation/zones/learning",Esp32BaseWeb::METHOD_GET,zoneLearning) &&
        Esp32BaseWeb::addRoute("/irrigation/zones/learning",Esp32BaseWeb::METHOD_POST,zoneLearning) &&
        Esp32BaseWeb::addApi("/irrigation/api/status",statusApi) &&
        Esp32BaseWeb::addApi("/irrigation/api/flow-history",flowHistoryApi);
}
void IrrigationWeb::overview() {
    if(Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if(!Esp32BaseWeb::checkPostAllowed(actionIs("start_manual")
                                              ? "irrigation_manual"
                                              : "irrigation_stop")) return;
        if (actionIs("stop")) {
            redirectResult("/irrigation", g_app->stopWatering());
            return;
        }
        if (actionIs("start_manual")) {
            const IrrigationConfig* config = g_app->configuration();
            WateringRequest request{};
            const WateringStartResult result =
                config && buildManualWateringRequest(*config, request)
                    ? g_app->startWatering(request)
                    : WateringStartResult::InvalidRequest;
            if (result == WateringStartResult::Started) {
                Esp32BaseWeb::redirectSeeOther("/irrigation");
                return;
            }
            // Keep the dialog open with the submitted values so the user can adjust them.
            char location[224]{"/irrigation?result=error&open_manual=1"};
            const IrrigationConfig* failedConfig = g_app->configuration();
            if (failedConfig) {
                for (uint8_t index = 0; index < BoardPins::kZoneCount; ++index) {
                    if (!failedConfig->zones[index].enabled) continue;
                    char mode[10]{};
                    char value[16]{};
                    if (!manualRowParam(mode, sizeof(mode), "mode", index + 1U) ||
                        !manualRowParam(value, sizeof(value), "zone", index + 1U)) continue;
                    bool numeric = value[0] != '\0';
                    for (const char* p = value; *p; ++p)
                        numeric = numeric && ((*p >= '0' && *p <= '9') || *p == '.');
                    if (!numeric) continue;
                    std::snprintf(location + std::strlen(location),
                                  sizeof(location) - std::strlen(location),
                                  "&m%u=%s&v%u=%s",
                                  static_cast<unsigned>(index + 1U),
                                  std::strcmp(mode, "volume") == 0 ? "v" : "t",
                                  static_cast<unsigned>(index + 1U), value);
                }
            }
            Esp32BaseWeb::redirectSeeOther(location);
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/irrigation?result=error");
        return;
    }
    const WateringStatus watering = g_app->wateringStatus();
    if (watering.active) {
        activeTask();
        return;
    }
    if (!Esp32BaseWeb::checkAuth()) return;
    Esp32BaseWeb::sendHeader("智能浇水");
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeStyle);
    const AutomaticWateringState automatic = g_app->automaticWateringState();
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    const WateringScheduler::TimeState schedulerTime = g_app->schedulerTimeState();
    const bool timeTrusted = now.synced && schedulerTime == WateringScheduler::TimeState::Ready;
    const bool storageFault = g_app->recordStorageFault() || g_app->eventStorageFault() ||
                              g_app->schedulerStorageFault() || g_app->checkpointStorageFault();
    const IrrigationEvents::ConditionDisplayState rtcCondition =
        g_app->eventConditionState(1);
    const bool rtcUnavailable =
        rtcCondition == IrrigationEvents::ConditionDisplayState::Active ||
        rtcCondition == IrrigationEvents::ConditionDisplayState::ConfirmingRecovery;
    const IrrigationConfig* config = g_app->configuration();
    bool hasEnabledZone = false;
    if (config) {
        for (const ZoneConfig& zone : config->zones) {
            if (zone.enabled) hasEnabledZone = true;
        }
    }
    const NextAutomaticWatering next = g_app->nextAutomaticWatering();
    LatestRecordContext latest;
    if (!g_app->recordStorageFault()) {
        g_app->readLatestWateringRecords(0, 1, collectLatestRecord, &latest);
    }
    char value[96]{};
    char secondary[96]{};

    char result[12]{};
    if (getParam("result", result, sizeof(result))) {
        if (std::strcmp(result, "ok") == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "操作已完成");
        } else if (std::strcmp(result, "error") == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "操作失败",
                                     "请检查设备时间和当前状态后重试。");
        }
    }

    const char* heroTone = "";
    const char* heroEyebrow = "当前状态";
    const char* heroTitle = "当前没有浇水";
    const char* heroDescription = "自动计划会按设定时间运行，也可以随时手动开始。";
    const char* heroHref = nullptr;
    const char* heroAction = "手动浇水";
    if (!hasEnabledZone) {
        heroDescription = "请先启用实际安装的水路，再开始浇水或配置计划。";
        heroHref = "/irrigation/settings";
        heroAction = "设置水路";
    }
    if (!g_app->businessReady()) {
        heroTone = " danger";
        const IrrigationConfigStore::LoadResult loadResult =
            g_app->configurationLoadResult();
        if (loadResult == IrrigationConfigStore::LoadResult::StorageUnavailable) {
            heroTitle = "设备存储不可用";
            heroDescription = "新设备首次烧录后可能需要初始化文件系统。全部输出已保持关闭。确认设备中没有需要保留的数据后，请到系统工具格式化 LittleFS；如果设备此前已经使用过，请勿直接格式化。";
        } else if (loadResult == IrrigationConfigStore::LoadResult::InvalidConfig) {
            heroTitle = "灌溉配置需要重新建立";
            heroDescription = "当前配置结构不兼容或配置文件没有有效副本，全部输出已保持关闭。如需保留现有数据，请勿直接格式化；完成备份后再到系统工具格式化 LittleFS 并重新配置。";
        } else if (loadResult == IrrigationConfigStore::LoadResult::WriteFailed) {
            heroTitle = "灌溉配置无法保存";
            heroDescription = "文件系统可以读取，但配置写入或校验失败，全部输出已保持关闭。请先查看系统状态和日志，不要直接格式化。";
        } else {
            heroTitle = "灌溉功能未就绪";
            heroDescription = "启动检查未能完成，全部输出已保持关闭。请查看系统状态和日志，不要直接格式化。";
        }
        heroHref = "/esp32base/system";
        heroAction = "打开系统工具";
    } else if (g_app->schedulerStorageFault()) {
        heroTone = " danger";
        heroTitle = "自动浇水暂不可用";
        heroDescription = "调度状态无法可靠保存；手动浇水仍可使用。";
        heroHref = "/esp32base/system";
        heroAction = "查看系统状态";
    } else if (schedulerTime == WateringScheduler::TimeState::RtcRollback) {
        heroTone = " warn";
        heroTitle = "设备时间异常，自动浇水已停止";
        heroDescription = "检测到 RTC 时间明显倒退，等待 NTP 校时后自动恢复判断。";
        heroHref = "/esp32base/system";
        heroAction = "查看时间状态";
    } else if (!timeTrusted) {
        heroTone = " warn";
        heroTitle = "设备时间尚未就绪";
        heroDescription = "自动计划暂时不会运行，手动浇水仍可使用。";
        heroHref = "/esp32base/system";
        heroAction = "查看时间状态";
    } else if (storageFault) {
        heroTone = " warn";
        heroTitle = "部分数据存储异常";
        heroDescription = "请查看下方状态徽章中的具体说明；记录写入故障会阻止新的浇水任务。";
        heroHref = "/esp32base/system";
        heroAction = "查看系统状态";
    }
    const char* defaultHeroTitle = heroTitle;
    const char* defaultHeroDescription = heroDescription;
    const char* defaultHeroHref = heroHref;
    const char* defaultHeroAction = heroAction;
    const bool flowAlarm = g_app->businessReady() && g_app->unexpectedFlowAlarm();
    if (flowAlarm) {
        heroTone = " danger";
        heroTitle = "关阀后水流异常";
        heroDescription = "水泵和全部阀门均已关闭，但仍检测到水流。请检查阀门、管路或流量计。";
    }
    Esp32BaseWeb::sendChunk("<section class='home-hero");
    Esp32BaseWeb::sendChunk(heroTone);
    Esp32BaseWeb::sendChunk("' id='home-hero' data-flow-alarm='");
    html(flowAlarm ? "1" : "0");
    html("' data-rtc-unavailable='");
    Esp32BaseWeb::sendChunk(rtcUnavailable ? "1" : "0");
    Esp32BaseWeb::sendChunk("'><div><span class='home-eyebrow'>");
    Esp32BaseWeb::writeHtmlEscaped(heroEyebrow);
    Esp32BaseWeb::sendChunk("</span><h1 id='home-hero-title'>");
    Esp32BaseWeb::writeHtmlEscaped(heroTitle);
    Esp32BaseWeb::sendChunk("</h1><p id='home-hero-description'>");
    Esp32BaseWeb::writeHtmlEscaped(heroDescription);
    Esp32BaseWeb::sendChunk("</p><span id='home-flow-monitor' class='home-monitor");
    if (flowAlarm) Esp32BaseWeb::sendChunk(" danger");
    Esp32BaseWeb::sendChunk("'>");
    if (flowAlarm) {
        const uint16_t observedSec =
            g_app->unexpectedFlowObservedWindowSec();
        const uint32_t pulseCount =
            g_app->unexpectedFlowObservedPulseCount();
        char estimatedFlow[20]{};
        IrrigationConfigRules::formatLitersPerMinute(
            g_app->unexpectedFlowEstimatedMlPerMinute(),
            estimatedFlow,
            sizeof(estimatedFlow));
        Esp32BaseWeb::sendChunk("近 ");
        sendUnsigned(observedSec == 0 ? 1 : observedSec);
        Esp32BaseWeb::sendChunk(" 秒检测到 ");
        sendUnsigned(pulseCount);
        Esp32BaseWeb::sendChunk(" 个水流脉冲 · 估算平均流量 ");
        Esp32BaseWeb::writeHtmlEscaped(estimatedFlow);
        Esp32BaseWeb::sendChunk(" L/min");
    } else if (g_app->unexpectedFlowObservationReady()) {
        Esp32BaseWeb::sendChunk("关阀后水流监测已开启");
    } else {
        Esp32BaseWeb::sendChunk("关阀后水流监测中");
    }
    Esp32BaseWeb::sendChunk("</span></div><div class='home-hero-side'><div id='home-clock' class='home-clock");
    if (!timeTrusted) Esp32BaseWeb::sendChunk(" pending");
    if (rtcUnavailable) Esp32BaseWeb::sendChunk(" has-warning");
    Esp32BaseWeb::sendChunk("'");
    if (timeTrusted) {
        Esp32BaseWeb::sendChunk(" data-epoch='");
        sendUnsigned(now.epochSec);
        Esp32BaseWeb::sendChunk("'");
    }
    Esp32BaseWeb::sendChunk("><b id='home-clock-time' class='home-clock-time'>");
    if (timeTrusted && Esp32BaseTime::formatEpoch(now.epochSec, value, sizeof(value), "%H:%M:%S")) {
        Esp32BaseWeb::writeHtmlEscaped(value);
    } else {
        Esp32BaseWeb::sendChunk("尚未就绪");
    }
    Esp32BaseWeb::sendChunk("</b><span class='home-clock-date'><span id='home-clock-date'>");
    if (timeTrusted && formatChineseDate(now.epochSec, value, sizeof(value))) {
        Esp32BaseWeb::writeHtmlEscaped(value);
        Esp32BaseWeb::sendChunk("</span>");
        Esp32BaseWeb::sendChunk(now.source == Esp32BaseTime::SOURCE_NTP ? " · NTP 校时" : " · RTC 时间");
    } else {
        Esp32BaseWeb::sendChunk("等待 RTC 或 NTP 提供可信时间");
        Esp32BaseWeb::sendChunk("</span>");
    }
    Esp32BaseWeb::sendChunk("</span>");
    if (rtcUnavailable) {
        Esp32BaseWeb::sendChunk("<button type='button' class='home-clock-warning' onclick=\"document.getElementById('device-conditions').showModal()\">硬件时钟不可用 · 断网后计划可能暂停</button>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::sendChunk("<span id='home-default-action' class='home-action");
    if (flowAlarm) Esp32BaseWeb::sendChunk(" hidden");
    Esp32BaseWeb::sendChunk("'>");
    if (defaultHeroHref) {
        Esp32BaseWeb::sendChunk("<a class='btnlink info' href='");
        Esp32BaseWeb::writeHtmlEscaped(defaultHeroHref);
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(defaultHeroAction);
        Esp32BaseWeb::sendChunk("</a>");
    } else if (config && hasEnabledZone) {
        Esp32BaseWeb::sendChunk("<button type='button' class='btnlink info' onclick=\"document.getElementById('manual-watering').showModal()\">");
        Esp32BaseWeb::writeHtmlEscaped(defaultHeroAction);
        Esp32BaseWeb::sendChunk("</button>");
    }
    Esp32BaseWeb::sendChunk("</span><button type='button' id='home-alarm-action' class='home-action btnlink info");
    if (!flowAlarm) Esp32BaseWeb::sendChunk(" hidden");
    Esp32BaseWeb::sendChunk("' onclick=\"document.getElementById('device-conditions').showModal()\">查看异常说明</button></div></section>");
    conditions();
    Esp32BaseWeb::beginPanel("每日浇水");
    const uint32_t day = selectedDay();
    if (day) dateNav(day);
    renderDay(day, true);
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::sendChunk("<div class='home-grid'>");

    Esp32BaseWeb::sendChunk("<section class='home-card'><div class='home-card-head'><h2>下一次自动浇水</h2>");
    if (automatic.mode == AutomaticWateringMode::Enabled) {
        Esp32BaseWeb::sendChunk("<span class='tag ok'>自动浇水正常</span></div>");
    } else {
        Esp32BaseWeb::sendChunk("<span class='tag warn'>自动浇水已暂停</span></div>");
    }
    if (automatic.mode == AutomaticWateringMode::PausedIndefinitely) {
        Esp32BaseWeb::sendChunk("<div class='home-main'>等待你手动恢复</div><p class='home-sub'>暂停期间到点的计划不会执行，也不会补执行。</p>");
    } else {
        if (automatic.mode == AutomaticWateringMode::PausedUntil) {
            if (formatFriendlyDateTime(automatic.resumeAtEpoch, now.epochSec, value, sizeof(value)) &&
                formatFullDateTime(automatic.resumeAtEpoch, secondary, sizeof(secondary))) {
                Esp32BaseWeb::sendChunk("<div class='home-main'>将在");
                Esp32BaseWeb::writeHtmlEscaped(value);
                Esp32BaseWeb::sendChunk("自动恢复</div><p class='home-sub'>");
                Esp32BaseWeb::writeHtmlEscaped(secondary);
                if (!timeTrusted) Esp32BaseWeb::sendChunk("；设备时间恢复可信后才会判断是否到期");
                Esp32BaseWeb::sendChunk("</p>");
            }
        }
        if (next.status == NextAutomaticWateringStatus::Available &&
            formatFriendlyDateTime(next.scheduledEpoch, now.epochSec, value, sizeof(value)) &&
            formatFullDateTime(next.scheduledEpoch, secondary, sizeof(secondary))) {
            const WateringPlan* nextPlan =
                config && next.planId != 0 && next.planId <= config->plans.size()
                    ? &config->plans[next.planId - 1U]
                    : nullptr;
            if (automatic.mode == AutomaticWateringMode::Enabled) {
                Esp32BaseWeb::sendChunk("<div class='home-main'>");
                Esp32BaseWeb::writeHtmlEscaped(value);
                Esp32BaseWeb::sendChunk("</div><p class='home-sub'>");
                Esp32BaseWeb::writeHtmlEscaped(secondary);
                Esp32BaseWeb::sendChunk("</p>");
            }
            Esp32BaseWeb::sendChunk("<div class='home-plan'><span>");
            Esp32BaseWeb::sendChunk(automatic.mode == AutomaticWateringMode::Enabled ? "执行计划" : "恢复后的计划");
            Esp32BaseWeb::sendChunk("</span><b>");
            const char* nextPlanName = planNameById(config, next.planId);
            if (nextPlanName) {
                Esp32BaseWeb::writeHtmlEscaped(nextPlanName);
            } else {
                Esp32BaseWeb::sendChunk("计划 ");
                sendUnsigned(next.planId);
            }
            Esp32BaseWeb::sendChunk("</b></div>");
            uint8_t nextZoneCount = 0;
            uint32_t nextTotalMinutes = 0;
            if (nextPlan) {
                for (uint8_t index = 0;
                     index < nextPlan->zoneDurationMinutes.size();
                     ++index) {
                    if (!config->zones[index].enabled ||
                        nextPlan->zoneDurationMinutes[index] == 0) {
                        continue;
                    }
                    ++nextZoneCount;
                    nextTotalMinutes += nextPlan->zoneDurationMinutes[index];
                }
            }
            Esp32BaseWeb::sendChunk("<div class='home-facts'><div class='home-fact'><span>执行内容</span><b>");
            sendUnsigned(nextZoneCount);
            Esp32BaseWeb::sendChunk(" 个水路 · 预计 ");
            sendUnsigned(nextTotalMinutes);
            Esp32BaseWeb::sendChunk(" 分钟</b></div>");
            if (nextPlan && nextZoneCount != 0) {
                Esp32BaseWeb::sendChunk("<div class='home-fact'><span>水路安排</span><b>");
                uint8_t emitted = 0;
                for (uint8_t index = 0;
                     index < nextPlan->zoneDurationMinutes.size() && emitted < 3;
                     ++index) {
                    const uint16_t duration =
                        nextPlan->zoneDurationMinutes[index];
                    if (!config->zones[index].enabled || duration == 0) continue;
                    if (emitted != 0) Esp32BaseWeb::sendChunk(" · ");
                    Esp32BaseWeb::writeHtmlEscaped(
                        config->zones[index].name.data());
                    Esp32BaseWeb::sendChunk(" ");
                    sendUnsigned(duration);
                    Esp32BaseWeb::sendChunk(" 分");
                    ++emitted;
                }
                if (nextZoneCount > emitted) {
                    Esp32BaseWeb::sendChunk(" · 另有 ");
                    sendUnsigned(nextZoneCount - emitted);
                    Esp32BaseWeb::sendChunk(" 个水路");
                }
                Esp32BaseWeb::sendChunk("</b></div>");
            }
            if (automatic.mode == AutomaticWateringMode::PausedUntil) {
                Esp32BaseWeb::sendChunk("<div class='home-fact'><span>下一次执行</span><b>");
                Esp32BaseWeb::writeHtmlEscaped(value);
                Esp32BaseWeb::sendChunk("</b></div>");
            }
            Esp32BaseWeb::sendChunk("</div>");
        } else if (next.status == NextAutomaticWateringStatus::NoEnabledPlans) {
            Esp32BaseWeb::sendChunk("<div class='home-empty'>还没有开启自动执行的计划。设置计划和启动时间后，下一次浇水会显示在这里。</div>");
        } else if (next.status == NextAutomaticWateringStatus::RtcRollback) {
            Esp32BaseWeb::sendChunk("<div class='home-empty'>设备时间发生倒退，暂时无法计算下一次浇水。</div>");
        } else if (next.status == NextAutomaticWateringStatus::TimeUnavailable) {
            Esp32BaseWeb::sendChunk("<div class='home-empty'>设备时间尚未就绪，暂时无法计算下一次浇水。</div>");
        }
    }
    Esp32BaseWeb::sendChunk("<div class='home-card-actions'><a class='btnlink secondary' href='/irrigation/plans'>管理计划</a></div></section>");

    Esp32BaseWeb::sendChunk("<section id='home-recent-card' class='home-card");
    if (latest.found) {
        Esp32BaseWeb::sendChunk(" ");
        Esp32BaseWeb::sendChunk(recordOutcomeTone(latest.record.payload));
    }
    Esp32BaseWeb::sendChunk("'><div class='home-card-head'><h2>最近一次浇水</h2>");
    if (latest.found) {
        Esp32BaseWeb::sendChunk("<span class='tag ");
        Esp32BaseWeb::sendChunk(recordOutcomeTone(latest.record.payload));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(outcome(latest.record.payload));
        Esp32BaseWeb::sendChunk("</span>");
    } else {
        Esp32BaseWeb::sendChunk("<a class='btnlink compact secondary' href='/irrigation/records'>全部记录</a>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    if (g_app->recordStorageFault()) {
        Esp32BaseWeb::sendChunk("<div class='home-empty'>浇水记录存储异常，暂时无法读取最近记录。</div>");
    } else if (!latest.found) {
        Esp32BaseWeb::sendChunk("<div class='home-empty'>还没有浇水记录。第一次浇水执行结束后，无论完成、停止或失败，结果都会显示在这里。</div>");
    } else {
        const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(
            latest.record.payload);
        Esp32BaseWeb::sendChunk("<div class='home-main'>");
        Esp32BaseWeb::writeHtmlEscaped(sourceName(latest.record.payload.source));
        const char* recordPlanName = planNameById(config, latest.record.payload.planId);
        if (recordPlanName) {
            Esp32BaseWeb::sendChunk(" · ");
            Esp32BaseWeb::writeHtmlEscaped(recordPlanName);
        }
        Esp32BaseWeb::sendChunk("</div><p class='home-sub'>");
        if (latest.record.payload.result == WateringResult::Incomplete) { epochText(latest.record.payload.startedEpoch); html(" · 中断时间未知"); }
        else sendRecordTimeRange(latest.record.timing);
        Esp32BaseWeb::sendChunk("</p><p class='home-outcome'>");
        sendRecordOutcomeSummary(latest.record.payload, config);
        Esp32BaseWeb::sendChunk("</p><div class='home-facts'><div class='home-fact'><span>执行目标</span><b>");
        const uint32_t latestTargetWaterMl = recordTargetWaterMl(latest.record.payload);
        if (latestTargetWaterMl != 0) sendCompactWaterVolume(latestTargetWaterMl);
        else { formatElapsed(totals.plannedDurationSec, value, sizeof(value)); Esp32BaseWeb::writeHtmlEscaped(value); }
        Esp32BaseWeb::sendChunk("</b></div><div class='home-fact'><span>实际浇水</span><b>");
        if (latest.record.payload.result == WateringResult::Incomplete) std::snprintf(value, sizeof(value), "未知");
        else formatElapsed(totals.actualWateringSec, value, sizeof(value));
        Esp32BaseWeb::writeHtmlEscaped(value);
        Esp32BaseWeb::sendChunk("</b></div><div class='home-fact'><span>估算用水量</span><b>");
        if (latest.record.payload.result == WateringResult::Incomplete) html("未知");
        else sendCompactWaterVolume(totals.estimatedWaterMl);
        Esp32BaseWeb::sendChunk("</b></div></div><div class='home-card-actions'><a class='btnlink info' href='/irrigation/records?id=");
        sendUnsigned(latest.record.recordId);
        Esp32BaseWeb::sendChunk("'>查看完整记录</a><a class='btnlink secondary' href='/irrigation/records'>全部记录</a></div>");
    }
    Esp32BaseWeb::sendChunk("</section></div>");

    if (config && hasEnabledZone && g_app->businessReady()) renderManualDialog(*config);
    Esp32BaseRecordStore::StoreStatus history{};
    g_app->readWateringRecordStoreStatus(history);
    html("<div class='muted' data-home-watch='idle' data-record-next='"); sendUnsigned(history.nextRecordId); html("'></div>");
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeScript);
    endPage();
}

// Water-course management body shared by the settings page and by the
// POST /irrigation/zones failure response. On failure the submitted values
// are read from the POST body and the matching edit dialog reopens.
void renderZoneManagement(bool failed, uint32_t postedZone) {
    const IrrigationConfig* config = g_app->configuration();
    if (!config) return;
    char coefficient[20]{};
    IrrigationConfigRules::formatPulsesPerLiter(
        config->flowMeter.pulsesPerLiterX100, coefficient, sizeof(coefficient));
    Esp32BaseWeb::beginPanel("水路管理");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part zone-table'><thead><tr><th>水路</th><th>名称</th><th>启用状态</th><th>基准流量</th><th>操作</th></tr></thead><tbody>");
    for (const ZoneConfig& zone : config->zones) {
        char value[20]{};
        uint32_t flowMlPerMinute = 0;
        if (zone.baselinePulseRateX10000 != 0 &&
            FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
                zone.baselinePulseRateX10000,
                config->flowMeter.pulsesPerLiterX100,
                flowMlPerMinute)) {
            IrrigationConfigRules::formatLitersPerMinute(
                flowMlPerMinute, value, sizeof(value));
        }
        Esp32BaseWeb::sendChunk("<tr><td>水路 "); sendUnsigned(zone.id);
        Esp32BaseWeb::sendChunk("</td><td>"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
        Esp32BaseWeb::sendChunk("</td><td><span class='tag ");
        Esp32BaseWeb::sendChunk(zone.enabled ? "ok'>已启用" : "'>未启用");
        Esp32BaseWeb::sendChunk("</span></td><td>");
        if (zone.baselinePulseRateX10000 == 0) Esp32BaseWeb::sendChunk("<span class='muted'>未设置</span>");
        else if (value[0] == '\0') Esp32BaseWeb::sendChunk("<span class='muted'>超出显示范围</span>");
        else { Esp32BaseWeb::writeHtmlEscaped(value); Esp32BaseWeb::sendChunk(" L/min（已设置）"); }
        Esp32BaseWeb::sendChunk("</td><td><div class='fsactions'><button type='button' class='btnlink info compact' onclick=\"document.getElementById('zone-"); sendUnsigned(zone.id);
        Esp32BaseWeb::sendChunk("').showModal()\">修改</button>");
        Esp32BaseWeb::sendChunk("<a class='btnlink ok compact' href='/irrigation/zones/learning?zone="); sendUnsigned(zone.id); Esp32BaseWeb::sendChunk("'>"); Esp32BaseWeb::sendChunk(zone.enabled ? "学习基准流量" : "设置基准流量"); Esp32BaseWeb::sendChunk("</a>");
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::beginPanel("流量计维护");
    Esp32BaseWeb::sendChunk("<div class='zone-meter'><div class='zone-meter-main'><p class='zone-meter-label'>稳态流量系数</p><div class='zone-meter-value'><span class='zone-meter-number'>");
    Esp32BaseWeb::writeHtmlEscaped(coefficient);
    Esp32BaseWeb::sendChunk("</span><span class='zone-meter-unit'>P/L</span></div></div><a class='btnlink ok compact' href='/esp32base/app-config'>设置每升脉冲数</a></div>");
    Esp32BaseWeb::endPanel();
    for (const ZoneConfig& savedZone : config->zones) {
        ZoneConfig zone = savedZone;
        const bool retry = failed && postedZone == zone.id;
        uint32_t formRevision = config->revision;
        if (retry) {
            getParam("name", zone.name.data(), zone.name.size());
            zone.enabled = Esp32BaseWeb::hasParam("enabled");
            uintParam("revision", 1, UINT32_MAX, formRevision);
        }
        Esp32BaseWeb::sendChunk("<dialog id='zone-"); sendUnsigned(zone.id);
        Esp32BaseWeb::sendChunk("' class='panel eb-modal' data-eb-light-dismiss='1'><h2>修改水路 "); sendUnsigned(zone.id);
        html("</h2>");
        if (retry) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "未保存修改", formRevision != config->revision ? "配置已在其他页面更新，请关闭表单并刷新后重试。" : g_app->configurationError());
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='zone_id' value='"); sendUnsigned(zone.id);
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(formRevision);
        Esp32BaseWeb::sendChunk("'><div class='fieldgrid'><p class='field full'><label>水路名称</label><input name='name' maxlength='63' required value='"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
        Esp32BaseWeb::sendChunk("'><small>用于计划、运行和记录页面显示。</small></p><p class='field full'><label><input type='checkbox' name='enabled' value='1' "); if (zone.enabled) Esp32BaseWeb::sendChunk("checked");
        Esp32BaseWeb::sendChunk("> 启用这条水路</label><small>未安装的水路保持关闭，正常使用页面不会显示。</small></p></div><div class='actions'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='保存'></div></form>");
        Esp32BaseWeb::sendChunk("</dialog>");
    }
    if (failed && postedZone) {
        html("<script>var d=document.getElementById('zone-"); sendUnsigned(postedZone);
        html("');if(d)d.showModal();</script>");
    } else if (failed) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "未保存修改", "水路参数无效。");
    }
}

void IrrigationWeb::settings() {
    if(!beginPage("设置","水路与系统参数")) return;
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeStyle);
    char result[12]{};
    if (getParam("result", result, sizeof(result)) && std::strcmp(result, "ok") == 0) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "已保存", "水路设置已更新。");
    }
    conditions();
    renderZoneManagement(false, 0);
    Esp32BaseWeb::beginPanel("系统配置");
    html("<div class='fieldgrid'><p class='field full'><a class='btnlink secondary' href='/esp32base/app-config'><b>计量、保护与硬件参数</b></a><small>每升脉冲数、流量保护、单水路最长运行与单次水量上限、阀与泵等系统参数</small></p></div>");
    Esp32BaseWeb::endPanel();
    endPage();
}
void IrrigationWeb::plans() {
    bool failed=false; uint32_t postedPlan=0;
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_plans")) return;
        bool success = false;
        uintParam("plan_id",1,kWateringPlanCount,postedPlan);
        if (actionIs("pause_indefinitely")) {
            success = g_app->pauseAutomaticWateringIndefinitely();
        } else if (actionIs("resume")) {
            success = g_app->resumeAutomaticWatering();
        } else if (actionIs("pause_until")) {
            char localDateTime[20]{};
            uint32_t resumeAt = 0;
            success = getParam("resume_at", localDateTime, sizeof(localDateTime)) &&
                      IrrigationTime::parseLocalDateTimeUtc8(localDateTime, resumeAt) &&
                      g_app->pauseAutomaticWateringUntil(resumeAt);
        } else {
            success = savePlanFromRequest();
        }
        if(success) { redirectResult("/irrigation/plans",true); return; }
        failed=true;
    }
    if (!beginPage("浇水计划", "管理自动执行，并为手动浇水提供可编辑的时长模板")) return;
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::PlansStyle);
    if(failed) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,"操作未完成",g_app->configurationError());
    const IrrigationConfig* config = g_app->configuration();
    if (config) {
        const AutomaticWateringState automatic = g_app->automaticWateringState();
        const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
        const bool timeTrusted = now.synced &&
                                 g_app->schedulerTimeState() == WateringScheduler::TimeState::Ready;
        char automaticDetail[128]{};
        if (automatic.mode == AutomaticWateringMode::Enabled) {
            std::snprintf(automaticDetail, sizeof(automaticDetail), "已启用的计划会在设定时间自动执行。");
        } else if (automatic.mode == AutomaticWateringMode::PausedIndefinitely) {
            std::snprintf(automaticDetail, sizeof(automaticDetail), "已暂停，等待手动恢复；手动浇水不受影响。");
        } else {
            char resumeTime[40]{};
            if (formatFullDateTime(automatic.resumeAtEpoch,
                                   resumeTime,
                                   sizeof(resumeTime))) {
                std::snprintf(automaticDetail, sizeof(automaticDetail),
                              "已暂停，将于 %s 自动恢复；手动浇水不受影响。",
                              resumeTime);
            } else {
                std::snprintf(automaticDetail, sizeof(automaticDetail),
                              "已定时暂停，设备时间就绪后自动恢复。");
            }
        }
        Esp32BaseWeb::beginPanel("自动浇水");
        Esp32BaseWeb::sendChunk("<div class='plan-auto");
        if (automatic.mode != AutomaticWateringMode::Enabled) {
            Esp32BaseWeb::sendChunk(" paused");
        }
        Esp32BaseWeb::sendChunk("'><div><h3><span class='tag ");
        Esp32BaseWeb::sendChunk(automatic.mode == AutomaticWateringMode::Enabled
                                    ? "ok'>自动浇水正常运行"
                                    : "warn'>自动浇水已暂停");
        Esp32BaseWeb::sendChunk("</span></h3><p>");
        Esp32BaseWeb::writeHtmlEscaped(automaticDetail);
        Esp32BaseWeb::sendChunk("</p></div><div class='plan-auto-actions'>");
        if (automatic.mode == AutomaticWateringMode::Enabled) {
            Esp32BaseWeb::sendChunk("<button type='button' class='btnlink info' onclick=\"document.getElementById('plan-pause').showModal()\">暂停自动浇水</button>");
        } else {
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans' onsubmit='return once(this)'><input type='hidden' name='action' value='resume'><input class='btnlink ok' type='submit' value='恢复自动浇水'></form>");
        }
        Esp32BaseWeb::sendChunk("</div></div>");
        Esp32BaseWeb::endPanel();
        if (automatic.mode == AutomaticWateringMode::Enabled) {
            const uint32_t initialResumeEpoch = timeTrusted && now.epochSec <= UINT32_MAX - 86400U
                                                    ? now.epochSec + 86400U
                                                    : 1767196800UL;
            char inputDateTime[20]{};
            char minimumDateTime[20]{};
            formatInputDateTime(initialResumeEpoch, inputDateTime, sizeof(inputDateTime));
            formatInputDateTime(timeTrusted && now.epochSec <= UINT32_MAX - 60U
                                    ? now.epochSec + 60U
                                    : 1767196800UL,
                                minimumDateTime,
                                sizeof(minimumDateTime));
            Esp32BaseWeb::sendChunk("<dialog id='plan-pause' class='panel eb-modal plan-pause-modal' data-eb-light-dismiss='1'><h2>暂停自动浇水</h2><p class='muted'>只影响之后的自动计划，不停止当前任务，也不影响手动浇水。</p><div class='plan-pause-options'><form id='plan-pause-timed' class='plan-pause-option' method='post' action='/irrigation/plans' data-now-epoch='");
            sendUnsigned(now.epochSec);
            Esp32BaseWeb::sendChunk("' onsubmit='return submitTimedPause(this)'><input type='hidden' name='action' value='pause_until'><h3>暂停至恢复时间</h3><small>小时数和常用时间只用于快捷填写，最终以恢复时间为准。</small><div class='plan-pause-fields'><p class='field plan-pause-field'><label for='pause-hours'>暂停时长（小时）</label><input id='pause-hours' type='number' min='1' max='8760' step='1' value='24' inputmode='numeric'");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk("><span class='plan-pause-shortcuts' aria-label='暂停时长快捷选择'><button type='button' class='secondary' data-pause-hours='12'");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk(">12 小时</button><button type='button' class='secondary selected' data-pause-hours='24'");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk(">24 小时</button><button type='button' class='secondary' data-pause-hours='48'");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk(">48 小时</button></span></p><p class='field plan-pause-field'><label for='pause-resume-at'>恢复时间（UTC+8）</label><input id='pause-resume-at' type='datetime-local' name='resume_at' min='");
            Esp32BaseWeb::writeHtmlEscaped(minimumDateTime);
            Esp32BaseWeb::sendChunk("' max='2099-12-31T23:59' value='");
            Esp32BaseWeb::writeHtmlEscaped(inputDateTime);
            Esp32BaseWeb::sendChunk("' required");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk("><span class='plan-pause-shortcuts' aria-label='恢复时间快捷选择'><button type='button' class='secondary' data-pause-day='1'");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk(">明天早上 6:00</button><button type='button' class='secondary' data-pause-day='2'");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk(">后天早上 6:00</button></span></p>");
            if (!timeTrusted) Esp32BaseWeb::sendChunk("<p class='plan-pause-unavailable'>设备时间当前不可信，暂时不能设置自动恢复时间。</p>");
            Esp32BaseWeb::sendChunk("</div><div class='plan-pause-submit'><input type='submit' value='确认暂停'");
            if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk("></div></form><div class='plan-pause-option plan-pause-indefinite'><div><h3>无限期暂停</h3><p class='muted'>不依赖设备时间，之后需要回到本页手动恢复。</p></div><form method='post' action='/irrigation/plans' onsubmit=\"return confirm('确认无限期暂停自动浇水？')&&once(this)\"><input type='hidden' name='action' value='pause_indefinitely'><input type='submit' value='无限期暂停'></form></div></div><div class='actions'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button></div></dialog>");
            IrrigationWebAssets::send(IrrigationWebAssets::Asset::PauseScript);
        }
        int firstAvailable = -1;
        bool anyConfigured = false;
        for (const WateringPlan& plan : config->plans) {
            if (plan.configured) anyConfigured = true;
            else if (firstAvailable < 0) firstAvailable = plan.id - 1;
        }
        Esp32BaseWeb::beginPanel("计划列表");
        Esp32BaseWeb::sendChunk("<p class='muted'>计划用于自动执行，也可以在“手动浇水”页中填入各路时长；填入后可临时修改，不会反向保存。</p>");
        if (firstAvailable >= 0) {
            Esp32BaseWeb::sendChunk("<div class='plan-toolbar'><button type='button' class='btnlink' onclick=\"document.getElementById('plan-"); sendUnsigned(firstAvailable + 1U);
            Esp32BaseWeb::sendChunk("').showModal()\">新增计划</button></div>");
        }
        if (anyConfigured) Esp32BaseWeb::sendChunk("<div class='plan-list'>");
        for (const WateringPlan& plan : config->plans) {
            if (!plan.configured) continue;
            Esp32BaseWeb::sendChunk("<article class='plan-card");
            if (!plan.scheduleEnabled) Esp32BaseWeb::sendChunk(" disabled");
            Esp32BaseWeb::sendChunk("'><div class='plan-card-head'><div class='plan-card-title'><div class='plan-card-title-row'><h3>");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("</h3><span class='tag ");
            Esp32BaseWeb::sendChunk(plan.scheduleEnabled
                                        ? "ok'>自动执行已开启"
                                        : "plan-status-off'>自动执行已关闭");
            Esp32BaseWeb::sendChunk("</span></div><small>计划 "); sendUnsigned(plan.id);
            if (!plan.scheduleEnabled) {
                Esp32BaseWeb::sendChunk(" · 仍可用于手动浇水");
            }
            Esp32BaseWeb::sendChunk("</small></div><button type='button' class='btnlink info compact plan-card-edit' onclick=\"document.getElementById('plan-"); sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("').showModal()\">编辑</button></div><div class='plan-card-body'><section><span class='plan-summary-label'>每日启动时间</span><div class='plan-time-list'>");
            bool hasStartTime = false;
            for (uint16_t minute : plan.startMinutes) {
                if (minute == kUnusedStartMinute) continue;
                hasStartTime = true;
                char time[8];
                std::snprintf(time, sizeof(time), "%02u:%02u", minute / 60U, minute % 60U);
                Esp32BaseWeb::sendChunk("<span class='plan-time-chip'>"); Esp32BaseWeb::sendChunk(time); Esp32BaseWeb::sendChunk("</span>");
            }
            if (!hasStartTime) Esp32BaseWeb::sendChunk("<span class='plan-empty-value'>未设置</span>");
            Esp32BaseWeb::sendChunk("</div></section><section><span class='plan-summary-label'>各水路浇水时长</span><div class='plan-zone-summary'>");
            bool hasEnabledZone = false;
            for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                if (!config->zones[index].enabled) continue;
                hasEnabledZone = true;
                Esp32BaseWeb::sendChunk("<div class='plan-zone-item");
                if (plan.zoneDurationMinutes[index] == 0) Esp32BaseWeb::sendChunk(" zero");
                Esp32BaseWeb::sendChunk("'><b>"); Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
                Esp32BaseWeb::sendChunk("</b><span>"); sendUnsigned(plan.zoneDurationMinutes[index]); Esp32BaseWeb::sendChunk(" 分钟</span></div>");
            }
            if (!hasEnabledZone) Esp32BaseWeb::sendChunk("<span class='plan-empty-value'>暂无启用水路</span>");
            Esp32BaseWeb::sendChunk("</div></section></div></article>");
        }
        if (anyConfigured) Esp32BaseWeb::sendChunk("</div>");
        else Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "尚无浇水计划", "新增后可用于自动执行，也可作为手动浇水的时长模板。");
        Esp32BaseWeb::endPanel();

        for (const WateringPlan& savedPlan : config->plans) {
            WateringPlan plan=savedPlan;
            const bool retry=failed && postedPlan==plan.id && actionIs("save");
            uint32_t formRevision=config->revision;
            if(retry) {
                getParam("name",plan.name.data(),plan.name.size());
                plan.scheduleEnabled=Esp32BaseWeb::hasParam("schedule_enabled");
                uintParam("revision",1,UINT32_MAX,formRevision);
                for(size_t i=0;i<plan.startMinutes.size();++i) {
                    char field[12],value[8]{}; std::snprintf(field,sizeof(field),"time%u",unsigned(i+1));
                    if(getParam(field,value,sizeof(value))) parseStartMinute(value,plan.startMinutes[i]);
                }
                for(size_t i=0;i<plan.zoneDurationMinutes.size();++i) {
                    char field[12];uint32_t value=0;std::snprintf(field,sizeof(field),"zone%u",unsigned(i+1));
                    if(uintParam(field,0,config->runLimits.maximumZoneDurationMinutes,value)) plan.zoneDurationMinutes[i]=value;
                }
            }
            if (!plan.configured && static_cast<int>(plan.id - 1U) != firstAvailable) continue;
            Esp32BaseWeb::sendChunk("<dialog id='plan-"); sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("' class='panel eb-modal plan-modal' data-eb-light-dismiss='1'><h2>");
            Esp32BaseWeb::writeHtmlEscaped(plan.configured ? "编辑浇水计划" : "新增浇水计划");
            html("</h2>");
            if(retry) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN,"未保存修改",formRevision!=config->revision ? "配置已在其他页面更新，请关闭表单并刷新后重试。" : g_app->configurationError());
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='plan_id' value='"); sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(formRevision);
            Esp32BaseWeb::sendChunk("'><div class='plan-editor'><section class='plan-group'><h3>基本信息</h3><small>计划名称用于运行和浇水记录；自动执行关闭后仍可填入手动浇水时长。</small><div class='plan-basic'><p class='field'><label>计划名称</label><input type='text' name='name' maxlength='63' required value='");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("'></p><div class='plan-switch'><label><input type='checkbox' name='schedule_enabled' value='1' ");
            if (plan.scheduleEnabled) Esp32BaseWeb::sendChunk("checked");
            Esp32BaseWeb::sendChunk("> 自动执行</label><small>仅控制定时执行；关闭后仍可在手动浇水页作为手动浇水模板。</small></div></div></section><section class='plan-group'><h3>自动执行时间</h3><small>每天最多执行 4 次；留空表示不使用。点击“清除”可删除已经设置的时间。</small><div class='plan-times'>");
            for (uint8_t index = 0; index < plan.startMinutes.size(); ++index) {
                Esp32BaseWeb::sendChunk("<div class='plan-time'><label>启动时间 "); sendUnsigned(index + 1U);
                Esp32BaseWeb::sendChunk("</label><div class='plan-time-row'><input type='time' name='time"); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("' value='");
                if (plan.startMinutes[index] != kUnusedStartMinute) { char time[8]; std::snprintf(time, sizeof(time), "%02u:%02u", plan.startMinutes[index] / 60U, plan.startMinutes[index] % 60U); Esp32BaseWeb::sendChunk(time); }
                Esp32BaseWeb::sendChunk("'><button type='button' class='secondary clear-time' onclick=\"this.previousElementSibling.value=''\">清除</button></div></div>");
            }
            Esp32BaseWeb::sendChunk("</div></section><section class='plan-group'><h3>各水路浇水时长</h3><small>这里只显示已启用水路；设置为 0 分钟表示本计划不浇该水路。</small><div class='plan-zones'>");
            for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                if (!config->zones[index].enabled) continue;
                Esp32BaseWeb::sendChunk("<p class='plan-zone'><label>"); Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
                Esp32BaseWeb::sendChunk("</label><input type='number' name='zone"); sendUnsigned(index + 1U);
                Esp32BaseWeb::sendChunk("' min='0' max='"); sendUnsigned(config->runLimits.maximumZoneDurationMinutes); Esp32BaseWeb::sendChunk("' value='"); sendUnsigned(plan.zoneDurationMinutes[index]);
                Esp32BaseWeb::sendChunk("'><small>单位：分钟，范围 0～"); sendUnsigned(config->runLimits.maximumZoneDurationMinutes); Esp32BaseWeb::sendChunk("。</small></p>");
            }
            Esp32BaseWeb::sendChunk("</div></section></div><div class='actions plan-form-actions'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='保存计划'></div></form>");
            if (plan.configured) { Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans' onsubmit=\"return confirm('确认删除该计划？')&&once(this)\"><input type='hidden' name='action' value='delete'><input type='hidden' name='plan_id' value='"); sendUnsigned(plan.id); Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(formRevision); Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='删除计划'></div></form>"); }
            Esp32BaseWeb::sendChunk("</dialog>");
        }
    }
    // Overlap hint: the device only rejects the same start minute; plans whose
    // estimated run windows overlap can still both be saved (the later start
    // is skipped as busy). Warn before saving instead of silently allowing it.
    html("<script>window.IRR_PLAN_WINDOWS=[");
    bool firstWindow = true;
    for (const WateringPlan& savedPlan : config->plans) {
        if (!savedPlan.configured || !savedPlan.scheduleEnabled) continue;
        uint32_t totalMinutes = 0;
        for (uint16_t duration : savedPlan.zoneDurationMinutes) totalMinutes += duration;
        if (totalMinutes == 0) continue;
        for (uint16_t start : savedPlan.startMinutes) {
            if (start == kUnusedStartMinute) continue;
            if (!firstWindow) html(",");
            firstWindow = false;
            html("{id:"); sendUnsigned(savedPlan.id);
            // Names are validated UTF-8 without control chars; still escape the
            // three characters that could break out of a JS single-quoted string.
            html(",name:'");
            for (const char* p = savedPlan.name.data(); *p; ++p) {
                if (*p == '\\' || *p == '\'' || *p == '"') html("\\");
                char ch[2] = {*p, 0};
                html(ch);
            }
            html("',start:"); sendUnsigned(start);
            html(",minutes:"); sendUnsigned(totalMinutes); html("}");
        }
    }
    html("];document.querySelectorAll('form[action=\"/irrigation/plans\"]').forEach(function(form){"
         "if(form.elements['action'].value!=='save')return;"
         "form.addEventListener('submit',function(e){"
         "if(form.dataset.confirmed==='1')return;"
         "var pid=Number(form.elements['plan_id'].value);"
         "if(!form.elements['schedule_enabled'].checked)return;"
         "var starts=[];for(var i=1;i<=4;i++){var v=form.elements['time'+i].value;if(v){var h=Number(v.slice(0,2)),m=Number(v.slice(3,5));if(!isNaN(h)&&!isNaN(m))starts.push(h*60+m);}}"
         "var minutes=0;var z=form.querySelectorAll('input[name^=zone]');for(var j=0;j<z.length;j++){minutes+=Number(z[j].value)||0;}"
         "if(!starts.length||!minutes)return;"
         "var conflicts=[];window.IRR_PLAN_WINDOWS.forEach(function(w){"
         "if(w.id===pid)return;"
         "starts.forEach(function(s){"
         "var aS=s,aE=s+minutes,bS=w.start,bE=w.start+w.minutes;"
         "if(aS<bE&&bS<aE)conflicts.push(w.name);});});"
         "if(conflicts.length&&!confirm('估算浇水时段可能与计划“'+conflicts.join('、')+'”重叠；重叠时后启动的计划会因设备忙碌被跳过。仍要保存？')){e.preventDefault();return;}"
         "form.dataset.confirmed='1';});});</script>");
    if(failed && postedPlan) { html("<script>var d=document.getElementById('plan-");sendUnsigned(postedPlan);html("');if(d)d.showModal();</script>"); }
    endPage();
}

void IrrigationWeb::records() {
    if(!beginPage("浇水记录","实际时长为主，估算水量为辅"))return;
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::RecordsStyle);
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::EventsStyle);
    HistoryRows q{};
    char date[12]{}, result[16]{};
    if(getParam("date",date,sizeof(date)) && date[0]) q.day=selectedDay();
    uintParam("offset",0,UINT32_MAX-20,q.offset);
    getParam("result",result,sizeof(result)); q.result=result;
    html("<form method='get' class='event-filter'><label>日期<input type='date' name='date' value='");escaped(date);html("'></label><label>结果<select name='result'><option value=''>全部结果</option><option value='ok'");if(!strcmp(result,"ok"))html(" selected");html(">仅正常完成</option><option value='issues'");if(!strcmp(result,"issues"))html(" selected");html(">停止、异常或不完整</option></select></label><button>筛选</button><a href='/irrigation/records'>清除</a></form><section class='panel'><h2>历史记录</h2><div class='tablewrap'><table class='record-table'><thead><tr><th>开始时间</th><th>浇水任务</th><th>执行水路</th><th>执行结果</th><th>实际 / 目标</th><th>估算用水量</th><th>操作</th></tr></thead><tbody>");
    Esp32BaseRecordStore::StoreStatus state{};const bool ok=g_app->readWateringRecordStoreStatus(state)&&state.ready&&(state.recordCount==0||g_app->readLatestWateringRecords(0,state.recordCount,historyRow,&q));
    html("</tbody></table></div>");
    if(!ok)html("<p class='notice warn'>记录读取失败，不能据此判断没有浇水。</p>");else if(!q.shown)html("<p>当前条件下暂无浇水记录。</p>");html("</section>");
    if(q.offset || q.matched>q.offset+q.shown){html("<nav class='actions'>");for(int d=-1;d<=1;d+=2){if(d<0&&!q.offset)continue;if(d>0&&q.matched<=q.offset+q.shown)continue;html("<a class='btnlink secondary' href='?date=");escaped(date);html("&result=");escaped(result);html("&offset=");sendUnsigned(d<0?(q.offset>20?q.offset-20:0):q.offset+20);html("'>");html(d<0?"上一页":"下一页");html("</a>");}html("</nav>");}
    if(state.oldestRecordId>1)html("<p class='muted'>本地历史按预算滚动保留，较早记录可能已淘汰。</p>");
    html("<p><a href='/irrigation/events'>操作与设备异常历史 ›</a></p>");
    uint32_t detailId=0;
    if (uintParam("id",1,UINT32_MAX,detailId)) {
        StoredWateringRecord detail{};
        if (g_app->readWateringRecordById(detailId,detail)==Esp32BaseRecordStore::RecordReadResult::Found) {
            sendRecordDetailDialog(detail,g_app->configuration(),"record-direct-detail-");
            html("<script>document.getElementById('record-direct-detail-"); sendUnsigned(detailId); html("').showModal();</script>");
        } else Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN,"找不到该浇水记录","记录无法读取或已被滚动淘汰。");
    }
    endPage();
}
void IrrigationWeb::events() {
    if(!beginPage("操作与设备异常历史","查看影响计划和水路的必要变化"))return;
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::EventsStyle);
    html("<p><a href='/irrigation/records'>‹ 浇水记录</a></p>");
    char date[12]{};
    AuditRows q{};
    uint32_t category = 0;
    if (getParam("date", date, sizeof(date)) && std::strlen(date) == 10) {
        char dateTime[24]{};
        uint32_t dayEpoch = 0;
        std::snprintf(dateTime, sizeof(dateTime), "%sT00:00", date);
        if (IrrigationTime::parseLocalDateTimeUtc8(dateTime, dayEpoch)) q.day = WateringHistory::localDay(dayEpoch);
    }
    if (uintParam("category", 1, 4, category)) q.category = static_cast<uint8_t>(category);
    uintParam("offset", 0, UINT32_MAX - 20, q.offset);
    html("<form method='get' class='event-filter'><label>日期<input type='date' name='date' value='"); escaped(date);
    html("'></label><label>类别<select name='category'><option value='0'>全部类别</option>");
    for (uint8_t index = 0; index < 4; ++index) {
        const auto categoryEnum = static_cast<IrrigationEvents::Category>(index);
        html("<option value='"); sendUnsigned(index + 1U); html("'");
        if (q.category == index + 1U) html(" selected");
        html(">"); escaped(IrrigationEvents::categoryName(categoryEnum)); html("</option>");
    }
    html("</select></label><button>筛选</button><a href='/irrigation/events'>清除</a></form>");
    html("<section class='panel'><h2>事件记录</h2><div class='tablewrap'><table class='event-table'><thead><tr><th>时间</th><th>等级</th><th>事件</th><th>说明</th></tr></thead><tbody>");
    IrrigationEvents::EventStatus status{};
    const bool readable=g_app->readEventStatus(status) && status.eventStore.ready &&
        (!status.eventStore.recordCount || g_app->readLatestEvents(0,status.eventStore.recordCount,auditRow,&q));
    html("</tbody></table></div>");
    if(!readable) html("<p>历史暂时无法读取，不能据此判断没有发生过事项。</p>");
    else if(!q.shown) html("<p>当前筛选条件下暂无事件。</p>");
    html("</section>");
    if(q.offset || q.matched>q.offset+q.shown) {
        auto pagingLink = [&](uint32_t nextOffset, const char* label) {
            html("<a class='btnlink secondary' href='?date="); escaped(date);
            html("&category="); sendUnsigned(q.category);
            html("&offset="); sendUnsigned(nextOffset); html("'>"); html(label); html("</a> ");
        };
        html("<nav class='actions'>");
        if (q.offset) pagingLink(q.offset>20?q.offset-20:0, "上一页");
        if(q.matched>q.offset+q.shown) pagingLink(q.offset+20, "更早记录 ›");
        html("</nav>");
    }
    endPage();
}
void IrrigationWeb::zones() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        bool failed = false;
        uint32_t postedZone = 0;
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_zones")) return;
        uintParam("zone_id", 1, BoardPins::kZoneCount, postedZone);
        if (actionIs("save") && saveZoneFromRequest()) {
            Esp32BaseWeb::redirectSeeOther("/irrigation/settings?result=ok");
            return;
        }
        failed = true;
        // Render the settings page with the failed dialog reopened and refilled.
        if (!beginPage("设置", "水路与系统参数")) return;
        IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeStyle);
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "操作失败", "请检查水路名称后重试。");
        conditions();
        renderZoneManagement(failed, postedZone);
        Esp32BaseWeb::beginPanel("系统配置");
        html("<div class='fieldgrid'><p class='field full'><a class='btnlink secondary' href='/esp32base/app-config'><b>计量、保护与硬件参数</b></a><small>每升脉冲数、流量保护、单水路最长运行与单次水量上限、阀与泵等系统参数</small></p></div>");
        Esp32BaseWeb::endPanel();
        endPage();
        return;
    }
    // Water-course management now lives on the settings page.
    Esp32BaseWeb::redirectSeeOther("/irrigation/settings");
}

void IrrigationWeb::activeTask() {
    if (!beginPage("首页", "查看当前任务的实时状态")) return;
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeStyle);
    conditions();
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::ActiveTaskStyle);
    const IrrigationConfig* config = g_app->configuration();
    const WateringStatus status = g_app->wateringStatus();
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    const bool timeTrusted =
        now.synced &&
        g_app->schedulerTimeState() == WateringScheduler::TimeState::Ready;
    const IrrigationEvents::ConditionDisplayState rtcCondition =
        g_app->eventConditionState(1);
    const bool rtcUnavailable =
        rtcCondition == IrrigationEvents::ConditionDisplayState::Active ||
        rtcCondition == IrrigationEvents::ConditionDisplayState::ConfirmingRecovery;
    char clockValue[24]{};
    char clockDate[32]{};
    Esp32BaseWeb::sendChunk("<section class='run-context'><div class='run-context-status'><span class='tag info'>任务运行中</span><b>");
    Esp32BaseWeb::writeHtmlEscaped(wateringStateName(status.state));
    Esp32BaseWeb::sendChunk("</b></div><div id='run-clock' class='run-clock'");
    if (timeTrusted) {
        Esp32BaseWeb::sendChunk(" data-epoch='");
        sendUnsigned(now.epochSec);
        Esp32BaseWeb::sendChunk("'");
    }
    Esp32BaseWeb::sendChunk("><b id='run-clock-time' class='run-clock-time'>");
    if (timeTrusted &&
        Esp32BaseTime::formatEpoch(now.epochSec, clockValue,
                                   sizeof(clockValue), "%H:%M:%S")) {
        Esp32BaseWeb::writeHtmlEscaped(clockValue);
    } else {
        Esp32BaseWeb::sendChunk("时间尚未就绪");
    }
    Esp32BaseWeb::sendChunk("</b><span class='run-clock-date'><span id='run-clock-date'>");
    if (timeTrusted &&
        formatChineseDate(now.epochSec, clockDate, sizeof(clockDate))) {
        Esp32BaseWeb::writeHtmlEscaped(clockDate);
        Esp32BaseWeb::sendChunk("</span>");
        Esp32BaseWeb::sendChunk(
            now.source == Esp32BaseTime::SOURCE_NTP ? " · NTP 校时"
                                                     : " · RTC 时间");
    } else {
        Esp32BaseWeb::sendChunk("等待 RTC 或 NTP 提供可信时间</span>");
    }
    Esp32BaseWeb::sendChunk("</span>");
    if (rtcUnavailable) {
        Esp32BaseWeb::sendChunk("<button type='button' class='run-clock-warning' onclick=\"document.getElementById('device-conditions').showModal()\">硬件时钟不可用 · 断网后计划可能暂停</button>");
    }
    Esp32BaseWeb::sendChunk("</div></section><script>(function(){var clock=document.getElementById('run-clock'),time=document.getElementById('run-clock-time'),date=document.getElementById('run-clock-date');if(!clock||!clock.dataset.epoch)return;var base=Number(clock.dataset.epoch),started=performance.now();function pad(v){return String(v).padStart(2,'0')}function update(){var epoch=base+Math.floor((performance.now()-started)/1000),d=new Date((epoch+28800)*1000);if(time)time.textContent=pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())+':'+pad(d.getUTCSeconds());if(date)date.textContent=d.getUTCFullYear()+'年'+(d.getUTCMonth()+1)+'月'+d.getUTCDate()+'日'}update();setInterval(update,1000)})();</script>");
    if (status.purpose != WateringPurpose::Normal) {
        Esp32BaseWeb::beginPanel("当前维护任务");
        Esp32BaseWeb::sendChunk("<div class='run-live-head'><div><span>维护任务</span><h3>");
        Esp32BaseWeb::sendChunk("基准流量学习");
        Esp32BaseWeb::sendChunk("</h3><p>任务正在设备上继续运行，请返回专属页面查看数据或停止。</p></div><a class='btnlink info' href='");
            Esp32BaseWeb::sendChunk("/irrigation/zones/learning?zone=");
            sendUnsigned(status.activeZoneId);
        Esp32BaseWeb::sendChunk("'>查看任务</a></div><form method='post' action='/irrigation'><input type='hidden' name='action' value='stop'><button class='danger'>停止当前任务</button></form>");
        Esp32BaseWeb::endPanel();
        Esp32BaseRecordStore::StoreStatus history{}; g_app->readWateringRecordStoreStatus(history);
        html("<div data-home-watch='maintenance' data-record-next='");sendUnsigned(history.nextRecordId);html("'></div>");
        IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeScript);

        endPage();
        return;
    }
    if (status.active) {
        const WateringPlan* activePlan = nullptr;
        if (config && status.planId >= 1 && status.planId <= config->plans.size() &&
            config->plans[status.planId - 1U].configured) {
            activePlan = &config->plans[status.planId - 1U];
        }
        const char* taskName = "手动浇水";
        const char* taskSource = status.source == WateringSource::AutomaticPlan
                                     ? "自动计划"
                                     : "手动浇水";
        if (activePlan) {
            taskName = activePlan->name.data();
        } else if (status.targetMode == WateringTargetMode::Volume) {
            taskName = "手动浇水";
            taskSource = "手动浇水";
        }
        const char* activeZoneName = "—";
        if (config && status.activeZoneId >= 1 && status.activeZoneId <= config->zones.size()) {
            activeZoneName = config->zones[status.activeZoneId - 1U].name.data();
        }
        const uint32_t currentTargetSec = status.currentStepIndex < status.stepCount
                                              ? status.zones[status.currentStepIndex].plannedDurationSec
                                              : 0U;
        const uint32_t currentTargetWaterMl = status.currentStepIndex < status.stepCount
                                                   ? status.zones[status.currentStepIndex].targetWaterMl
                                                   : 0U;
        const uint32_t progressTarget = currentTargetWaterMl != 0
                                            ? currentTargetWaterMl
                                            : currentTargetSec;
        const uint32_t progressValue = currentTargetWaterMl != 0 &&
                                               status.currentStepIndex < status.stepCount
                                           ? status.zones[status.currentStepIndex].estimatedWaterMl
                                           : status.currentZoneElapsedSec;
        const uint32_t progress = progressTarget == 0
                                      ? 0U
                                      : (progressValue >= progressTarget
                                             ? 100U
                                             : progressValue * 100U / progressTarget);
        Esp32BaseWeb::beginPanel("当前运行");
        Esp32BaseWeb::sendChunk("<div id='run-live' data-generation='"); sendUnsigned(status.flowHistoryGeneration);
        Esp32BaseWeb::sendChunk("' data-serial='"); sendUnsigned(status.flowSampleSerial);
        Esp32BaseWeb::sendChunk("' data-expected-flow='"); sendUnsigned(status.expectedFlowMlPerMinute);
        Esp32BaseWeb::sendChunk("'><div class='run-live-head'><div><div class='run-live-title'><h3>");
        Esp32BaseWeb::writeHtmlEscaped(taskName);
        Esp32BaseWeb::sendChunk("</h3>");
        if (std::strcmp(taskSource, taskName) != 0) {
            Esp32BaseWeb::sendChunk("<span class='tag info'>");
            Esp32BaseWeb::writeHtmlEscaped(taskSource);
            Esp32BaseWeb::sendChunk("</span>");
        }
        Esp32BaseWeb::sendChunk("</div><p id='run-state'>");
        Esp32BaseWeb::writeHtmlEscaped(activeZoneName);
        Esp32BaseWeb::sendChunk(" · ");
        if (status.state == WateringState::WateringZone) {
            Esp32BaseWeb::sendChunk(status.flowEstablished ? "水流已建立" : "等待水流建立");
        } else {
            Esp32BaseWeb::writeHtmlEscaped(wateringStateName(status.state));
        }
        Esp32BaseWeb::sendChunk("</p></div><form method='post' action='/irrigation' onsubmit=\"return confirm('确认停止当前整次浇水任务？')&&once(this)\"><input type='hidden' name='action' value='stop'><input class='danger' type='submit' value='停止当前任务'></form></div>");
        Esp32BaseWeb::sendChunk("<div class='run-live-metrics'><div class='run-live-metric'><span>任务总历时</span><b id='run-elapsed'>"); sendDuration(status.elapsedSec);
        Esp32BaseWeb::sendChunk("</b><small>含设备准备、等待水流和水路切换</small></div><div class='run-live-metric'><span>预计剩余浇水</span><b id='run-remaining'>"); sendDuration(status.plannedRemainingSec);
        Esp32BaseWeb::sendChunk("</b></div><div class='run-live-metric'><span>累计估算水量</span><b id='run-water'>"); sendLiters(status.totalEstimatedWaterMl);
        Esp32BaseWeb::sendChunk("</b></div><div class='run-live-metric'><span>执行进度</span><b id='run-step-count'>第 "); sendUnsigned(status.currentStepIndex + 1U); Esp32BaseWeb::sendChunk(" / "); sendUnsigned(status.stepCount);
        Esp32BaseWeb::sendChunk(" 条水路</b></div></div><div class='run-live-grid'><div class='run-current'><span class='run-section-label'>当前水路</span><div class='run-current-head'><h3 id='run-current-zone'>"); Esp32BaseWeb::writeHtmlEscaped(activeZoneName);
        const ZoneWateringSummary* currentZone =
            status.currentStepIndex < status.stepCount
                ? &status.zones[status.currentStepIndex]
                : nullptr;
        const char* flowStateTone = "warn";
        const char* flowStateText = "等待水流";
        if (status.state == WateringState::SwitchingZone) {
            flowStateTone = "info";
            flowStateText = "水路切换中";
        } else if (status.flowEstablished && status.expectedFlowMlPerMinute == 0) {
            flowStateTone = "info";
            flowStateText = "水流已建立";
        } else if (status.flowEstablished && currentZone && currentZone->lowFlowActive) {
            flowStateTone = "warn";
            flowStateText = "低流量";
        } else if (status.flowEstablished && currentZone && currentZone->highFlowActive) {
            flowStateTone = "danger";
            flowStateText = "高流量";
        } else if (status.flowEstablished) {
            flowStateTone = "info";
            flowStateText = "流量监测中";
        }
        Esp32BaseWeb::sendChunk("</h3><span id='run-flow-state' class='tag ");
        Esp32BaseWeb::sendChunk(flowStateTone);
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(flowStateText);
        Esp32BaseWeb::sendChunk("</span></div><div class='run-progress'><span id='run-current-progress' style='width:"); sendUnsigned(progress);
        Esp32BaseWeb::sendChunk("%'></span></div><div class='run-current-detail'><span id='run-current-elapsed'>实际浇水 "); sendDuration(status.currentZoneElapsedSec);
        if (currentTargetWaterMl != 0) {
            Esp32BaseWeb::sendChunk(" / 目标 "); sendLiters(currentTargetWaterMl);
        } else {
            Esp32BaseWeb::sendChunk(" / "); sendDuration(currentTargetSec);
        }
        Esp32BaseWeb::sendChunk("</span><span id='run-current-remaining'>");
        Esp32BaseWeb::sendChunk(currentTargetWaterMl != 0 ? "安全时限剩余 " : "剩余 "); sendDuration(status.currentZoneRemainingSec);
        Esp32BaseWeb::sendChunk("</span></div><div class='run-flow-facts'><div class='run-flow-fact'><span>当前流量</span><b id='run-flow'>");
        char flowText[20]{}; IrrigationConfigRules::formatLitersPerMinute(status.currentFlowMlPerMinute, flowText, sizeof(flowText)); Esp32BaseWeb::writeHtmlEscaped(flowText);
        Esp32BaseWeb::sendChunk(" L/min</b></div><div class='run-flow-fact'><span>基准流量</span><b id='run-expected-flow'>");
        if (status.expectedFlowMlPerMinute == 0) Esp32BaseWeb::sendChunk("未设置");
        else { char expected[20]{}; IrrigationConfigRules::formatLitersPerMinute(status.expectedFlowMlPerMinute, expected, sizeof(expected)); Esp32BaseWeb::writeHtmlEscaped(expected); Esp32BaseWeb::sendChunk(" L/min"); }
        Esp32BaseWeb::sendChunk("</b></div><div class='run-flow-fact'><span>当前水路脉冲</span><b id='run-pulses'>"); sendUnsigned(status.pulseCount);
        Esp32BaseWeb::sendChunk("</b></div><div class='run-flow-fact'><span>当前水路估算水量</span><b id='run-zone-water'>");
        if (status.currentStepIndex < status.stepCount) sendLiters(status.zones[status.currentStepIndex].estimatedWaterMl); else Esp32BaseWeb::sendChunk("0.000 L");
        Esp32BaseWeb::sendChunk("</b></div></div></div><div class='run-chart-card'><div class='run-chart-head'><div><span class='run-section-label'>当前水路</span><h3>实时流量趋势（L/min）</h3></div><span id='run-chart-range'>正在等待数据 · 5 秒/点</span></div><div class='run-chart-wrap'><canvas id='run-flow-chart' height='205'></canvas><div id='run-chart-empty' class='run-chart-empty'>正在等待流量数据</div></div></div></div><div class='run-steps'>");
        for (uint8_t index = 0; index < status.stepCount; ++index) {
            const ZoneWateringSummary& zone = status.zones[index];
            const char* stepClass = index < status.currentStepIndex ? " complete" : (index == status.currentStepIndex ? " current" : "");
            Esp32BaseWeb::sendChunk("<div class='run-step"); Esp32BaseWeb::sendChunk(stepClass); Esp32BaseWeb::sendChunk("' data-step-index='"); sendUnsigned(index);
            Esp32BaseWeb::sendChunk("' data-zone-id='"); sendUnsigned(zone.zoneId); Esp32BaseWeb::sendChunk("'><span class='run-step-icon'>");
            if (index < status.currentStepIndex) Esp32BaseWeb::sendChunk("&#10003;"); else sendUnsigned(index + 1U);
            Esp32BaseWeb::sendChunk("</span><div class='run-step-main'><b class='run-step-name'>");
            if (config && zone.zoneId >= 1 && zone.zoneId <= config->zones.size()) Esp32BaseWeb::writeHtmlEscaped(config->zones[zone.zoneId - 1U].name.data()); else { Esp32BaseWeb::sendChunk("水路 "); sendUnsigned(zone.zoneId); }
            Esp32BaseWeb::sendChunk("</b><small>");
            if (zone.targetWaterMl != 0) { Esp32BaseWeb::sendChunk("目标 "); sendLiters(zone.targetWaterMl); }
            else { Esp32BaseWeb::sendChunk("计划 "); sendDuration(zone.plannedDurationSec); }
            Esp32BaseWeb::sendChunk("</small></div><span class='run-step-detail'>");
            if (index < status.currentStepIndex) { Esp32BaseWeb::sendChunk("实际 "); sendDuration(zone.actualWateringSec); Esp32BaseWeb::sendChunk(" · "); sendLiters(zone.estimatedWaterMl); }
            else if (index == status.currentStepIndex) { Esp32BaseWeb::sendChunk("正在执行 · 剩余 "); sendDuration(status.currentZoneRemainingSec); }
            else Esp32BaseWeb::sendChunk("等待执行");
            Esp32BaseWeb::sendChunk("</span></div>");
        }
        Esp32BaseWeb::sendChunk("</div><p class='run-note'>预计剩余时间只计算计划浇水时长，不包含等待水流和设备启停延时。</p></div>");
        Esp32BaseWeb::endPanel();
    }
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::ActiveTaskScript);
    html("<section class='panel'><h2>每日浇水</h2>"); const uint32_t day=selectedDay(); if(day) dateNav(day); renderDay(day, false); html("</section>");
    endPage();
}

void IrrigationWeb::zoneLearning() {
    uint32_t zoneId = 0;
    char zoneText[12]{};
    if (getParam("zone", zoneText, sizeof(zoneText))) parseUint(zoneText, 1, BoardPins::kZoneCount, zoneId);
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_zone_learning")) return;
        if (!uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId)) {
            Esp32BaseWeb::sendText(400, "invalid_zone"); return;
        }
        const WateringStatus status = g_app->wateringStatus();
        bool success = false;
        if (actionIs("start")) success = g_app->startZoneFlowLearning(static_cast<uint8_t>(zoneId)) == WateringStartResult::Started;
        else if (actionIs("stop")) success = status.active && status.purpose == WateringPurpose::ZoneFlowLearning && status.activeZoneId == zoneId && g_app->stopWatering();
        else if (actionIs("save")) { uint32_t revision = 0; success = uintParam("revision", 1, UINT32_MAX, revision) && g_app->pendingLearnedZoneId() == zoneId && g_app->saveLearnedZoneFlow(revision); }
        else if (actionIs("discard")) { success = g_app->pendingLearnedZoneId() == zoneId; if (success) g_app->discardLearnedZoneFlow(); }
        else if (actionIs("manual")) {
            char flowText[20]{};
            uint32_t flowMlPerMinute = 0;
            uint32_t revision = 0;
            success = getParam("baseline_flow", flowText, sizeof(flowText)) &&
                      IrrigationConfigRules::parseLitersPerMinute(
                          flowText, flowMlPerMinute) &&
                      flowMlPerMinute != 0 &&
                      uintParam("revision", 1, UINT32_MAX, revision) &&
                      g_app->saveManualZoneBaselineFlow(
                          static_cast<uint8_t>(zoneId),
                          flowMlPerMinute,
                          revision);
        }
        else if (actionIs("clear")) { uint32_t revision = 0; success = uintParam("revision", 1, UINT32_MAX, revision) && g_app->clearLearnedZoneFlow(static_cast<uint8_t>(zoneId), revision); }
        char location[96]{};
        std::snprintf(location, sizeof(location), "/irrigation/zones/learning?zone=%lu&result=%s", static_cast<unsigned long>(zoneId), success ? "ok" : "error");
        Esp32BaseWeb::redirectSeeOther(location);
        return;
    }
    if (!beginPage("基准流量学习", "等待流量稳定后生成该水路的基准流量")) return;
    const IrrigationConfig* config = g_app->configuration();
    if (!config || zoneId == 0) { Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "水路参数无效"); endPage(); return; }
    const ZoneConfig& zone = config->zones[zoneId - 1U];
    const WateringStatus status = g_app->wateringStatus();
    const bool active = status.active && status.purpose == WateringPurpose::ZoneFlowLearning && status.activeZoneId == zoneId;
    uint32_t learnedFlowMlPerMinute = 0;
    FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
        zone.baselinePulseRateX10000,
        config->flowMeter.pulsesPerLiterX100,
        learnedFlowMlPerMinute);
    char learned[20]{};
    IrrigationConfigRules::formatLitersPerMinute(
        learnedFlowMlPerMinute, learned, sizeof(learned));
    char learnedWithUnit[32]{};
    std::snprintf(learnedWithUnit, sizeof(learnedWithUnit), "%s L/min", learned);
    char learnedRate[20]{};
    formatTenThousandths(
        zone.baselinePulseRateX10000, learnedRate, sizeof(learnedRate));
    const bool baselineManagementAvailable =
        !status.active && g_app->pendingLearnedZoneId() == 0;
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::LearningStyle);
    Esp32BaseWeb::beginPanel("基准流量学习");
    Esp32BaseWeb::sendChunk("<div class='learning-panel'><div class='learning-context'><div><span class='learning-label'>水路</span><span class='learning-context-value'>");
    Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
    Esp32BaseWeb::sendChunk("</span></div><div><span class='learning-label'>当前基准流量</span><span class='learning-context-value'>");
    Esp32BaseWeb::sendChunk(zone.baselinePulseRateX10000 == 0 ? "未设置" : learnedWithUnit);
    if (zone.baselinePulseRateX10000 != 0) {
        Esp32BaseWeb::sendChunk("</span><span class='learning-context-help'>原始脉冲基准：");
        Esp32BaseWeb::writeHtmlEscaped(learnedRate);
        Esp32BaseWeb::sendChunk(" P/s");
    }
    Esp32BaseWeb::sendChunk("</span></div><div><span class='learning-label'>安全上限</span><span class='learning-context-value'>10 分钟</span><span class='learning-context-help'>达到上限仍不稳定时自动停止</span></div></div>");
    if (!zone.enabled) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "这条水路尚未启用", "请先返回水路设置启用。");
    } else if (status.active && !active) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "设备正在执行其它任务");
    } else if (active) {
        char currentFlow[20]{};
        char averageFlow[20]{};
        IrrigationConfigRules::formatLitersPerMinute(
            status.currentFlowMlPerMinute, currentFlow, sizeof(currentFlow));
        IrrigationConfigRules::formatLitersPerMinute(
            status.learningAverageMlPerMinute, averageFlow, sizeof(averageFlow));
        Esp32BaseWeb::sendChunk("<div class='learning-head'><h3>学习状态</h3><span class='tag warn'>正在学习</span></div><div class='learning-metrics'><div class='learning-metric'><span class='learning-label'>已运行</span><span class='learning-value' id='learn-elapsed'>");
        sendDuration(status.elapsedSec);
        Esp32BaseWeb::sendChunk("</span></div><div class='learning-metric'><span class='learning-label'>最近 5 秒流量</span><span class='learning-value' id='learn-current'>");
        Esp32BaseWeb::writeHtmlEscaped(currentFlow);
        Esp32BaseWeb::sendChunk(" L/min</span></div><div class='learning-metric'><span class='learning-label'>已采集窗口平均</span><span class='learning-value' id='learn-average'>");
        Esp32BaseWeb::writeHtmlEscaped(averageFlow);
        Esp32BaseWeb::sendChunk(" L/min</span></div><div class='learning-metric'><span class='learning-label'>稳定判断</span><span class='learning-value' id='learn-state'>");
        if (status.learningTotalWindowCount < kLearningDecisionWindowCount) {
            Esp32BaseWeb::sendChunk("采集中 ");
            sendUnsigned(status.learningTotalWindowCount);
            Esp32BaseWeb::sendChunk("/5");
        } else {
            Esp32BaseWeb::sendChunk("波动偏大");
        }
        Esp32BaseWeb::sendChunk("</span></div></div><p class='learning-rule' id='learn-rule'>");
        if (status.learningWindowCount == 0) {
            Esp32BaseWeb::sendChunk("窗口 #1 正在采集；每个窗口约 5 秒且互不重叠。");
        } else {
            char minimumRate[20]{};
            char maximumRate[20]{};
            char spreadRate[20]{};
            char allowedRate[20]{};
            formatSignedHundredths(status.learningMinimumPulseRateX100,
                                   minimumRate, sizeof(minimumRate));
            formatSignedHundredths(status.learningMaximumPulseRateX100,
                                   maximumRate, sizeof(maximumRate));
            formatSignedHundredths(
                status.learningMaximumPulseRateX100 -
                    status.learningMinimumPulseRateX100,
                spreadRate, sizeof(spreadRate));
            formatSignedHundredths(status.learningAllowedPulseRateSpreadX100,
                                   allowedRate, sizeof(allowedRate));
            const uint32_t decisionFirst =
                status.learningTotalWindowCount >= kLearningDecisionWindowCount
                    ? status.learningTotalWindowCount -
                          kLearningDecisionWindowCount + 1U
                    : 1U;
            Esp32BaseWeb::sendChunk("已采集 <span>");
            sendUnsigned(status.learningTotalWindowCount);
            Esp32BaseWeb::sendChunk("</span> 个完整窗口，窗口 <span>#");
            sendUnsigned(status.learningTotalWindowCount + 1U);
            Esp32BaseWeb::sendChunk("</span> 正在采集；当前使用 <span>#");
            sendUnsigned(decisionFirst);
            Esp32BaseWeb::sendChunk("～#");
            sendUnsigned(status.learningTotalWindowCount);
            Esp32BaseWeb::sendChunk("</span> 判断。原始脉冲速率为 <span>");
            Esp32BaseWeb::writeHtmlEscaped(minimumRate);
            Esp32BaseWeb::sendChunk("～");
            Esp32BaseWeb::writeHtmlEscaped(maximumRate);
            Esp32BaseWeb::sendChunk(" P/s</span>，跨度 <span>");
            Esp32BaseWeb::writeHtmlEscaped(spreadRate);
            Esp32BaseWeb::sendChunk(" P/s</span>，允许 <span>");
            Esp32BaseWeb::writeHtmlEscaped(allowedRate);
            Esp32BaseWeb::sendChunk(" P/s</span>。收满 5 窗、每窗均有脉冲且跨度不超过允许值时完成。");
        }
        Esp32BaseWeb::sendChunk("</p>");
    } else if (g_app->pendingLearnedZoneId() == zoneId) {
        char suggestion[20]{};
        const bool validSuggestion = IrrigationConfigRules::formatLitersPerMinute(
            g_app->pendingLearnedFlowMlPerMinute(), suggestion, sizeof(suggestion));
        if (validSuggestion) {
            char baselineRate[20]{};
            formatTenThousandths(
                g_app->pendingLearnedBaselinePulseRateX10000(),
                baselineRate, sizeof(baselineRate));
            Esp32BaseWeb::sendChunk("<div class='learning-head'><h3>学习结果</h3><span class='tag ok'>已稳定</span></div><div class='learning-result'><div><span class='learning-label'>建议基准流量</span><span class='learning-result-value'>");
            Esp32BaseWeb::writeHtmlEscaped(suggestion);
            Esp32BaseWeb::sendChunk(" L/min</span></div><div><span class='learning-label'>原始脉冲基准</span><span class='learning-result-value'>");
            Esp32BaseWeb::writeHtmlEscaped(baselineRate);
            const uint8_t decisionStart =
                status.learningWindowCount - kLearningDecisionWindowCount;
            uint32_t decisionPulses = 0;
            uint32_t decisionWindowMs = 0;
            for (uint8_t index = decisionStart;
                 index < status.learningWindowCount;
                 ++index) {
                decisionPulses += status.learningWindows[index].pulseCount;
                decisionWindowMs += status.learningWindows[index].windowMs;
            }
            char spreadRate[20]{};
            char allowedRate[20]{};
            formatSignedHundredths(
                status.learningMaximumPulseRateX100 -
                    status.learningMinimumPulseRateX100,
                spreadRate, sizeof(spreadRate));
            formatSignedHundredths(status.learningAllowedPulseRateSpreadX100,
                                   allowedRate, sizeof(allowedRate));
            Esp32BaseWeb::sendChunk(" P/s</span></div></div><p class='learning-rule'>累计采集 <span>");
            sendUnsigned(status.learningTotalWindowCount);
            Esp32BaseWeb::sendChunk("</span> 窗，结果采用 <span>#");
            sendUnsigned(status.learningTotalWindowCount -
                         kLearningDecisionWindowCount + 1U);
            Esp32BaseWeb::sendChunk("～#");
            sendUnsigned(status.learningTotalWindowCount);
            Esp32BaseWeb::sendChunk("</span>；共 <span>");
            sendUnsigned(decisionPulses);
            Esp32BaseWeb::sendChunk(" 个脉冲 / ");
            sendMilliseconds(decisionWindowMs);
            Esp32BaseWeb::sendChunk("</span>。速率跨度 <span>");
            Esp32BaseWeb::writeHtmlEscaped(spreadRate);
            Esp32BaseWeb::sendChunk(" P/s</span>，允许 <span>");
            Esp32BaseWeb::writeHtmlEscaped(allowedRate);
            Esp32BaseWeb::sendChunk(" P/s</span>。流量计系数只用于换算显示流量。</p><div class='learning-actions'><form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='zone_id' value='");
            sendUnsigned(zoneId);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='");
            sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><div class='actions'><input type='submit' value='保存为基准流量'></div></form>");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                     "学习结果超出有效范围",
                                     "当前校准系数无法显示该结果，请检查流量计校准参数。");
            Esp32BaseWeb::sendChunk("<div class='learning-actions'>");
        }
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='discard'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='放弃结果'></div></form></div>");
    } else if (g_app->pendingLearnedZoneId() != 0) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN,
                                 "另一条水路有待确认的学习结果",
                                 "请先返回对应水路保存或放弃该结果。");
    } else {
        if (status.purpose == WateringPurpose::ZoneFlowLearning && status.lastZoneId == zoneId && status.lastStopReason == WateringStopReason::LearningTimeout) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "学习超时", "10 分钟内没有取得稳定流量，请检查供水和管路。");
        Esp32BaseWeb::sendChunk("<p class='muted'>系统每 5 秒统计一次原始脉冲速率；最近 5 个窗口波动不超过 10%，并容忍一个脉冲的计数误差时自动完成。</p>");
    }
    if (baselineManagementAvailable) {
        Esp32BaseWeb::sendChunk("<div class='learning-actions'>");
        if (zone.enabled) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/learning' onsubmit=\"return confirm('确认开始学习基准流量？开始后这条水路会立即出水。')&&once(this)\"><input type='hidden' name='action' value='start'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><div class='actions'><input type='submit' value='"); Esp32BaseWeb::sendChunk(zone.baselinePulseRateX10000 == 0 ? "开始学习" : "重新学习"); Esp32BaseWeb::sendChunk("'></div></form>");
        }
        Esp32BaseWeb::sendChunk("<button class='secondary' type='button' onclick=\"document.getElementById('learning-manual').showModal()\">手工设置</button>");
        if (zone.baselinePulseRateX10000 != 0) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/learning' onsubmit=\"return confirm('确认清除这条水路的基准流量？清除后将停用该水路的高低流量报警。')&&once(this)\"><input type='hidden' name='action' value='clear'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision); Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='清除基准'></div></form>");
        }
        Esp32BaseWeb::sendChunk("</div>");
    }
    const bool showLearningWindows =
        (active || g_app->pendingLearnedZoneId() == zoneId) &&
        status.learningWindowCount != 0;
    if (active || showLearningWindows) {
        Esp32BaseWeb::sendChunk("<div class='learning-debug'><h3>最近窗口明细</h3><p>最多显示最近 10 窗，浅色行参与稳定判断；变化值均与上一个可见窗口比较。</p><table class='learning-table'><thead><tr><th>窗口</th><th>实际时长</th><th>脉冲数 / 变化</th><th>脉冲速率</th><th>换算流量 / 变化</th></tr></thead><tbody id='learn-window-body'><tr id='learn-window-empty'");
        if (status.learningWindowCount != 0) {
            Esp32BaseWeb::sendChunk(" style='display:none'");
        }
        Esp32BaseWeb::sendChunk("><td class='learning-empty' colspan='5'>等待第一个完整窗口</td></tr>");
        const uint8_t decisionStart =
            status.learningWindowCount > kLearningDecisionWindowCount
                ? status.learningWindowCount - kLearningDecisionWindowCount
                : 0U;
        for (uint8_t index = 0; index < kLearningHistoryWindowCount; ++index) {
            const bool populated = index < status.learningWindowCount;
            Esp32BaseWeb::sendChunk("<tr id='learn-window-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("' class='");
            if (populated && index >= decisionStart) {
                Esp32BaseWeb::sendChunk("learning-decision");
                if (index == decisionStart) {
                    Esp32BaseWeb::sendChunk(" learning-decision-start");
                }
            }
            Esp32BaseWeb::sendChunk("'");
            if (!populated) Esp32BaseWeb::sendChunk(" style='display:none'");
            Esp32BaseWeb::sendChunk("><td data-label='窗口' id='learn-window-index-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) {
                Esp32BaseWeb::sendChunk("#");
                sendUnsigned(status.learningWindows[index].sequence);
                if (index + 1U == status.learningWindowCount) {
                    Esp32BaseWeb::sendChunk("<span class='learning-latest'>最新</span>");
                }
            }
            Esp32BaseWeb::sendChunk("</td><td data-label='实际时长' id='learn-window-duration-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) sendMilliseconds(status.learningWindows[index].windowMs);
            Esp32BaseWeb::sendChunk("</td><td data-label='脉冲数 / 变化'><span class='learning-inline'><span id='learn-window-pulses-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) sendUnsigned(status.learningWindows[index].pulseCount);
            Esp32BaseWeb::sendChunk("</span><span class='learning-change' id='learn-window-pulse-change-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) {
                if (index == 0) {
                    Esp32BaseWeb::sendChunk("—");
                } else {
                    char change[24]{};
                    formatIntegerChange(
                        status.learningWindows[index].pulseCount,
                        status.learningWindows[index - 1U].pulseCount,
                        change, sizeof(change));
                    Esp32BaseWeb::writeHtmlEscaped(change);
                }
            }
            Esp32BaseWeb::sendChunk("</span></span></td><td data-label='脉冲速率' id='learn-window-rate-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) {
                char rate[20]{};
                formatSignedHundredths(status.learningWindows[index].pulseRateX100,
                                       rate, sizeof(rate));
                Esp32BaseWeb::writeHtmlEscaped(rate);
                Esp32BaseWeb::sendChunk(" P/s");
            }
            Esp32BaseWeb::sendChunk("</td><td data-label='换算流量 / 变化'><span class='learning-inline'><span id='learn-window-flow-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) {
                char flow[20]{};
                IrrigationConfigRules::formatLitersPerMinute(
                    status.learningWindows[index].flowMlPerMinute,
                    flow, sizeof(flow));
                Esp32BaseWeb::writeHtmlEscaped(flow);
                Esp32BaseWeb::sendChunk(" L/min");
            }
            Esp32BaseWeb::sendChunk("</span><span class='learning-change' id='learn-window-flow-change-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) {
                if (index == 0) {
                    Esp32BaseWeb::sendChunk("—");
                } else {
                    char change[48]{};
                    formatFlowChange(
                        status.learningWindows[index].flowMlPerMinute,
                        status.learningWindows[index - 1U].flowMlPerMinute,
                        change, sizeof(change));
                    Esp32BaseWeb::writeHtmlEscaped(change);
                }
            }
            Esp32BaseWeb::sendChunk("</span></span></td></tr>");
        }
        char coefficient[20]{};
        IrrigationConfigRules::formatPulsesPerLiter(
            config->flowMeter.pulsesPerLiterX100,
            coefficient, sizeof(coefficient));
        const uint32_t sessionPulseCount =
            active ? status.pulseCount : status.zones[0].pulseCount;
        Esp32BaseWeb::sendChunk("</tbody></table><p class='learning-summary'><span>已采集：<span id='learn-total-windows'>");
        sendUnsigned(status.learningTotalWindowCount);
        Esp32BaseWeb::sendChunk("</span> 窗</span><span>本次学习累计脉冲：<span id='learn-total-pulses'>");
        sendUnsigned(sessionPulseCount);
        Esp32BaseWeb::sendChunk("</span>（包含建立水流阶段及已滚出窗口）</span><span>当前流量计系数：");
        Esp32BaseWeb::writeHtmlEscaped(coefficient);
        Esp32BaseWeb::sendChunk(" P/L</span></p></div>");
    }
    if (active) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='stop'><input type='hidden' name='zone_id' value='");
        sendUnsigned(zoneId);
        Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='停止学习'></div></form>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();
    if (baselineManagementAvailable) {
        char coefficient[20]{};
        IrrigationConfigRules::formatPulsesPerLiter(
            config->flowMeter.pulsesPerLiterX100,
            coefficient,
            sizeof(coefficient));
        Esp32BaseWeb::sendChunk("<dialog id='learning-manual' class='panel eb-modal learning-manual' data-eb-light-dismiss='1'><h2>手工设置基准流量</h2><p class='muted'>录入便于管理的流量值，系统按当前流量计系数换算并保存对应的原始脉冲速率。</p><form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='manual'><input type='hidden' name='zone_id' value='");
        sendUnsigned(zoneId);
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='");
        sendUnsigned(config->revision);
        Esp32BaseWeb::sendChunk("'><p class='field'><label for='baseline-flow'>基准流量（L/min）</label><input id='baseline-flow' type='number' name='baseline_flow' min='0.001' max='100.000' step='0.001' inputmode='decimal' required");
        if (zone.baselinePulseRateX10000 != 0) {
            Esp32BaseWeb::sendChunk(" value='");
            Esp32BaseWeb::writeHtmlEscaped(learned);
            Esp32BaseWeb::sendChunk("'");
        }
        Esp32BaseWeb::sendChunk("><small>允许 0.001～100.000 L/min，最多三位小数。</small></p><div class='learning-manual-preview'><span>对应原始脉冲速率</span><output id='baseline-rate'>");
        if (zone.baselinePulseRateX10000 != 0) {
            Esp32BaseWeb::writeHtmlEscaped(learnedRate);
            Esp32BaseWeb::sendChunk(" P/s");
        } else {
            Esp32BaseWeb::sendChunk("—");
        }
        Esp32BaseWeb::sendChunk("</output></div><p class='muted'>当前流量计系数：");
        Esp32BaseWeb::writeHtmlEscaped(coefficient);
        Esp32BaseWeb::sendChunk(" P/L。以后修改系数只会改变显示流量，不会改变已保存的原始脉冲基准。</p><div class='actions'><button class='secondary' type='button' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='保存基准'></div></form></dialog><script>(function(){var input=document.getElementById('baseline-flow'),output=document.getElementById('baseline-rate'),coefficientX100=");
        sendUnsigned(config->flowMeter.pulsesPerLiterX100);
        Esp32BaseWeb::sendChunk(";if(!input||!output)return;function update(){var flow=Number(input.value),rateX10000=Math.round(flow*1000*coefficientX100/600);output.textContent=Number.isFinite(flow)&&flow>0&&rateX10000>0?(rateX10000/10000).toFixed(4)+' P/s':(flow>0?'低于可保存范围':'—')}input.addEventListener('input',update);update()})();</script>");
    }
    Esp32BaseWeb::sendChunk("<p><a class='btnlink secondary' href='/irrigation/settings'>返回设置</a></p>");
    if (active) {
        IrrigationWebAssets::send(IrrigationWebAssets::Asset::LearningScript);
    }
    endPage();
}

void IrrigationWeb::statusApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const WateringStatus status = g_app->wateringStatus();
    if (!Esp32BaseWeb::beginResponse(200, "application/json")) return;
    Esp32BaseWeb::sendChunk("{\"ready\":"); Esp32BaseWeb::sendChunk(g_app->businessReady() ? "true" : "false");
    Esp32BaseRecordStore::StoreStatus history{}; g_app->readWateringRecordStoreStatus(history);
    Esp32BaseWeb::sendChunk(",\"historyNextId\":"); sendUnsigned(history.nextRecordId);
    Esp32BaseWeb::sendChunk(",\"targetMode\":"); sendUnsigned(static_cast<uint32_t>(status.targetMode));
    Esp32BaseWeb::sendChunk(",\"active\":"); Esp32BaseWeb::sendChunk(status.active ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"state\":"); sendUnsigned(static_cast<uint32_t>(status.state));
    Esp32BaseWeb::sendChunk(",\"source\":"); sendUnsigned(static_cast<uint32_t>(status.source));
    Esp32BaseWeb::sendChunk(",\"planId\":"); sendUnsigned(status.planId);
    Esp32BaseWeb::sendChunk(",\"stepCount\":"); sendUnsigned(status.stepCount);
    Esp32BaseWeb::sendChunk(",\"currentStepIndex\":"); sendUnsigned(status.currentStepIndex);
    Esp32BaseWeb::sendChunk(",\"zoneId\":"); sendUnsigned(status.activeZoneId);
    Esp32BaseWeb::sendChunk(",\"lastZoneId\":"); sendUnsigned(status.lastZoneId);
    Esp32BaseWeb::sendChunk(",\"flowEstablished\":"); Esp32BaseWeb::sendChunk(status.flowEstablished ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"purpose\":"); sendUnsigned(static_cast<uint32_t>(status.purpose));
    Esp32BaseWeb::sendChunk(",\"elapsedSec\":"); sendUnsigned(status.elapsedSec);
    Esp32BaseWeb::sendChunk(",\"currentZoneElapsedSec\":"); sendUnsigned(status.currentZoneElapsedSec);
    Esp32BaseWeb::sendChunk(",\"currentZoneRemainingSec\":"); sendUnsigned(status.currentZoneRemainingSec);
    Esp32BaseWeb::sendChunk(",\"currentZoneTargetWaterMl\":"); sendUnsigned(status.currentZoneTargetWaterMl);
    Esp32BaseWeb::sendChunk(",\"plannedRemainingSec\":"); sendUnsigned(status.plannedRemainingSec);
    Esp32BaseWeb::sendChunk(",\"pulseCount\":"); sendUnsigned(status.pulseCount);
    Esp32BaseWeb::sendChunk(",\"currentFlowMlPerMinute\":"); sendUnsigned(status.currentFlowMlPerMinute);
    Esp32BaseWeb::sendChunk(",\"expectedFlowMlPerMinute\":"); sendUnsigned(status.expectedFlowMlPerMinute);
    Esp32BaseWeb::sendChunk(",\"totalEstimatedWaterMl\":"); sendUnsigned64(status.totalEstimatedWaterMl);
    Esp32BaseWeb::sendChunk(",\"flowHistoryGeneration\":"); sendUnsigned(status.flowHistoryGeneration);
    Esp32BaseWeb::sendChunk(",\"flowSampleSerial\":"); sendUnsigned(status.flowSampleSerial);
    Esp32BaseWeb::sendChunk(",\"learningAverageMlPerMinute\":"); sendUnsigned(status.learningAverageMlPerMinute);
    Esp32BaseWeb::sendChunk(",\"learningMinimumMlPerMinute\":"); sendUnsigned(status.learningMinimumMlPerMinute);
    Esp32BaseWeb::sendChunk(",\"learningMaximumMlPerMinute\":"); sendUnsigned(status.learningMaximumMlPerMinute);
    Esp32BaseWeb::sendChunk(",\"learningAveragePulseRateX100\":"); sendUnsigned(status.learningAveragePulseRateX100);
    Esp32BaseWeb::sendChunk(",\"learningMinimumPulseRateX100\":"); sendUnsigned(status.learningMinimumPulseRateX100);
    Esp32BaseWeb::sendChunk(",\"learningMaximumPulseRateX100\":"); sendUnsigned(status.learningMaximumPulseRateX100);
    Esp32BaseWeb::sendChunk(",\"learningAllowedPulseRateSpreadX100\":"); sendUnsigned(status.learningAllowedPulseRateSpreadX100);
    Esp32BaseWeb::sendChunk(",\"learningWindowCount\":"); sendUnsigned(status.learningWindowCount);
    Esp32BaseWeb::sendChunk(",\"learningTotalWindowCount\":"); sendUnsigned(status.learningTotalWindowCount);
    Esp32BaseWeb::sendChunk(",\"learningWindows\":[");
    for (uint8_t index = 0; index < status.learningWindowCount; ++index) {
        if (index != 0) Esp32BaseWeb::sendChunk(",");
        const WateringStatus::LearningWindowSample& window =
            status.learningWindows[index];
        Esp32BaseWeb::sendChunk("{\"sequence\":"); sendUnsigned(window.sequence);
        Esp32BaseWeb::sendChunk(",\"pulseCount\":"); sendUnsigned(window.pulseCount);
        Esp32BaseWeb::sendChunk(",\"windowMs\":"); sendUnsigned(window.windowMs);
        Esp32BaseWeb::sendChunk(",\"pulseRateX100\":"); sendUnsigned(window.pulseRateX100);
        Esp32BaseWeb::sendChunk(",\"flowMlPerMinute\":"); sendUnsigned(window.flowMlPerMinute);
        Esp32BaseWeb::sendChunk("}");
    }
    Esp32BaseWeb::sendChunk("]");
    Esp32BaseWeb::sendChunk(",\"automaticMode\":"); sendUnsigned(static_cast<uint32_t>(g_app->automaticWateringState().mode));
    const IrrigationEvents::ConditionDisplayState rtcCondition =
        g_app->eventConditionState(1);
    const bool rtcUnavailable =
        rtcCondition == IrrigationEvents::ConditionDisplayState::Active ||
        rtcCondition == IrrigationEvents::ConditionDisplayState::ConfirmingRecovery;
    Esp32BaseWeb::sendChunk(",\"rtcUnavailable\":"); Esp32BaseWeb::sendChunk(rtcUnavailable ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowAlarm\":"); Esp32BaseWeb::sendChunk(g_app->unexpectedFlowAlarm() ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowObservationReady\":"); Esp32BaseWeb::sendChunk(g_app->unexpectedFlowObservationReady() ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowPulseCount\":"); sendUnsigned(g_app->unexpectedFlowObservedPulseCount());
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowObservedWindowSec\":"); sendUnsigned(g_app->unexpectedFlowObservedWindowSec());
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowEstimatedMlPerMinute\":"); sendUnsigned(g_app->unexpectedFlowEstimatedMlPerMinute());
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowDelayRemainingSec\":"); sendUnsigned(g_app->unexpectedFlowDelayRemainingSec());
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowWindowRemainingSec\":"); sendUnsigned(g_app->unexpectedFlowWindowRemainingSec());
    const IrrigationConfig* statusConfig = g_app->configuration();
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowWindowSec\":");
    sendUnsigned(statusConfig ? statusConfig->flowProtection.unexpectedFlowWindowSec : 0);
    Esp32BaseWeb::sendChunk(",\"recordStorageFault\":"); Esp32BaseWeb::sendChunk(g_app->recordStorageFault() ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"eventStorageFault\":"); Esp32BaseWeb::sendChunk(g_app->eventStorageFault() ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"schedulerStorageFault\":"); Esp32BaseWeb::sendChunk(g_app->schedulerStorageFault() ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"checkpointStorageFault\":"); Esp32BaseWeb::sendChunk(g_app->checkpointStorageFault() ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"zones\":[");
    for (uint8_t index = 0; index < status.stepCount; ++index) {
        if (index != 0) Esp32BaseWeb::sendChunk(",");
        const ZoneWateringSummary& zone = status.zones[index];
        Esp32BaseWeb::sendChunk("{\"zoneId\":"); sendUnsigned(zone.zoneId);
        Esp32BaseWeb::sendChunk(",\"result\":"); sendUnsigned(static_cast<uint32_t>(zone.result));
        Esp32BaseWeb::sendChunk(",\"plannedDurationSec\":"); sendUnsigned(zone.plannedDurationSec);
        Esp32BaseWeb::sendChunk(",\"targetWaterMl\":"); sendUnsigned(zone.targetWaterMl);
        Esp32BaseWeb::sendChunk(",\"actualWateringSec\":"); sendUnsigned(zone.actualWateringSec);
        Esp32BaseWeb::sendChunk(",\"pulseCount\":"); sendUnsigned(zone.pulseCount);
        Esp32BaseWeb::sendChunk(",\"estimatedWaterMl\":"); sendUnsigned(zone.estimatedWaterMl);
        Esp32BaseWeb::sendChunk(",\"lowFlowDetected\":"); Esp32BaseWeb::sendChunk(zone.lowFlowDetected ? "true" : "false");
        Esp32BaseWeb::sendChunk(",\"highFlowDetected\":"); Esp32BaseWeb::sendChunk(zone.highFlowDetected ? "true" : "false");
        Esp32BaseWeb::sendChunk(",\"lowFlowActive\":"); Esp32BaseWeb::sendChunk(zone.lowFlowActive ? "true" : "false");
        Esp32BaseWeb::sendChunk(",\"highFlowActive\":"); Esp32BaseWeb::sendChunk(zone.highFlowActive ? "true" : "false");
        Esp32BaseWeb::sendChunk("}");
    }
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endResponse();
}

void IrrigationWeb::flowHistoryApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const FlowHistorySnapshot history = g_app->wateringFlowHistory();
    if (!Esp32BaseWeb::beginResponse(200, "application/json")) return;
    Esp32BaseWeb::sendChunk("{\"zoneId\":"); sendUnsigned(history.zoneId);
    Esp32BaseWeb::sendChunk(",\"sampleIntervalSec\":5,\"generation\":"); sendUnsigned(history.generation);
    Esp32BaseWeb::sendChunk(",\"latestSerial\":"); sendUnsigned(history.latestSerial);
    Esp32BaseWeb::sendChunk(",\"samples\":[");
    for (uint16_t index = 0; index < history.sampleCount; ++index) {
        if (index != 0) Esp32BaseWeb::sendChunk(",");
        sendUnsigned(history.samples[index]);
    }
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endResponse();
}
