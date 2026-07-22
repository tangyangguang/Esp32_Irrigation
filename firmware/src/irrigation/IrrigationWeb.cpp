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
    IrrigationConfig next = *current;
    WateringPlan& plan = next.plans[planId - 1U];
    if (actionIs("delete")) {
        plan = {};
        plan.id = static_cast<uint8_t>(planId);
        plan.startMinutes.fill(kUnusedStartMinute);
        return g_app->saveConfiguration(
            next,
            revision,
            IrrigationEvents::ConfigurationChange::PlanDeleted,
            static_cast<uint8_t>(planId));
    }
    if (!actionIs("save")) {
        return false;
    }
    char name[kObjectNameCapacity]{};
    if (!getParam("name", name, sizeof(name))) {
        return false;
    }
    const bool creating = !plan.configured;
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
        if (!current->zones[index].enabled) {
            plan.zoneDurationMinutes[index] = 0;
            continue;
        }
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
    return g_app->saveConfiguration(
        next,
        revision,
        creating ? IrrigationEvents::ConfigurationChange::PlanCreated
                 : IrrigationEvents::ConfigurationChange::PlanUpdated,
        static_cast<uint8_t>(planId));
}

bool saveZoneFromRequest() {
    const IrrigationConfig* current = g_app->configuration();
    uint32_t zoneId = 0, revision = 0;
    char name[kObjectNameCapacity]{};
    if (!current || !uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId) ||
        !uintParam("revision", 1, UINT32_MAX, revision) ||
        !getParam("name", name, sizeof(name))) {
        return false;
    }
    IrrigationConfig next = *current;
    ZoneConfig& zone = next.zones[zoneId - 1U];
    zone.enabled = Esp32BaseWeb::hasParam("enabled");
    std::snprintf(zone.name.data(), zone.name.size(), "%s", name);
    return g_app->saveConfiguration(next,
                                    revision,
                                    IrrigationEvents::ConfigurationChange::ZoneUpdated,
                                    static_cast<uint8_t>(zoneId));
}

bool actionIs(const char* expected) {
    char action[32]{};
    return getParam("action", action, sizeof(action)) && std::strcmp(action, expected) == 0;
}

bool paramIs(const char* name, const char* expected) {
    char value[16]{};
    return getParam(name, value, sizeof(value)) && std::strcmp(value, expected) == 0;
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
        case WateringState::StartingZone: return "区域启动中";
        case WateringState::WaitingForFlow: return "等待水流";
        case WateringState::WateringZone: return "正在浇水";
        case WateringState::StoppingZone: return "区域停止中";
        case WateringState::SwitchingZone: return "水路切换中";
    }
    return "未知";
}

const char* stopReasonName(WateringStopReason reason) {
    switch (reason) {
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
        case WateringSource::ManualZones: return "手动浇水";
        case WateringSource::SingleOutput: return "单次出水";
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
    if (payload.result == WateringResult::Stopped || recordHasFlowAlert(payload)) {
        return "warn";
    }
    return payload.result == WateringResult::Completed ? "ok" : "info";
}

const char* recordOutcomeName(const WateringRecordPayload& payload) {
    if (payload.result == WateringResult::Failed) return "失败";
    if (payload.result == WateringResult::Stopped) return "已停止";
    if (payload.result == WateringResult::Completed &&
        recordHasFlowAlert(payload)) {
        return "完成但有报警";
    }
    return payload.result == WateringResult::Completed ? "已完成" : "未知";
}

void sendRecordOutcomeSummary(const WateringRecordPayload& payload,
                              const IrrigationConfig* config) {
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
        } else if (payload.source == WateringSource::SingleOutput) {
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
    sendRecordTime(record.timing, "%m月%d日 %H:%M");
    Esp32BaseWeb::sendChunk("</h2></div><button type='button' class='secondary record-detail-close' onclick='this.closest(\"dialog\").close()'>关闭</button></div><div class='record-detail-result'><span class='tag ");
    Esp32BaseWeb::sendChunk(recordOutcomeTone(record.payload));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(recordOutcomeName(record.payload));
    Esp32BaseWeb::sendChunk("</span><span>");
    sendRecordOutcomeSummary(record.payload, config);
    Esp32BaseWeb::sendChunk("</span></div><div class='record-detail-metrics'><div><span>实际浇水</span><b>");
    sendDuration(totals.actualWateringSec);
    Esp32BaseWeb::sendChunk("</b></div><div><span>估算用水量</span><b>");
    if (recordHasCappedEstimate(record.payload)) {
        Esp32BaseWeb::sendChunk("至少 ");
    }
    sendWaterVolume(totals.estimatedWaterMl);
    Esp32BaseWeb::sendChunk("</b></div><div><span>执行水路</span><b>");
    sendUnsigned(startedZoneCount);
    Esp32BaseWeb::sendChunk(" / ");
    sendUnsigned(plannedZoneCount);
    Esp32BaseWeb::sendChunk(" 路</b></div><div><span>高低流量报警</span><b>");
    if (alertZoneCount == 0) {
        Esp32BaseWeb::sendChunk("无");
    } else {
        sendUnsigned(alertZoneCount);
        Esp32BaseWeb::sendChunk(" 路");
    }
    Esp32BaseWeb::sendChunk("</b><small>");
    sendUnsigned(baselineZoneCount);
    Esp32BaseWeb::sendChunk(" / ");
    sendUnsigned(startedZoneCount);
    Esp32BaseWeb::sendChunk(" 路设置了基准</small></div></div><div class='record-detail-section'><h3>执行时间</h3><div class='record-detail-grid'><div class='record-time-range'><b>开始 — 完成</b><span>");
    sendRecordTimeRange(record.timing);
    Esp32BaseWeb::sendChunk("</span></div><div><b>执行目标</b><span>");
    const uint32_t targetWaterMl = recordTargetWaterMl(record.payload);
    if (targetWaterMl != 0) sendWaterVolume(targetWaterMl);
    else sendDuration(totals.plannedDurationSec);
    Esp32BaseWeb::sendChunk("</span></div><div><b>任务总历时</b><span>");
    sendDuration(record.timing.durationSec);
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
        Esp32BaseWeb::sendChunk(zoneResultName(zone.result));
        Esp32BaseWeb::sendChunk("</td><td data-label='实际 / 目标' class='record-duration-pair'><b>");
        sendDuration(zone.actualWateringSec);
        Esp32BaseWeb::sendChunk("</b><span>/</span><small>");
        if (zone.targetWaterMl != 0) sendWaterVolume(zone.targetWaterMl);
        else sendDuration(zone.plannedDurationSec);
        Esp32BaseWeb::sendChunk("</small></td><td data-label='估算用水量'>");
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
            Esp32BaseWeb::sendChunk("至少 ");
        }
        sendWaterVolume(zone.estimatedWaterMl);
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
            Esp32BaseWeb::sendChunk("<small class='warn'>达到记录上限</small>");
        }
        Esp32BaseWeb::sendChunk("</td><td data-label='当时基准流量' class='record-baseline'>");
        const bool baselineAvailable =
            (zone.flags &
             WateringRecordCodec::kZoneFlagFlowBaselineAvailable) != 0;
        if (zone.result == ZoneWateringResult::NotStarted) {
            Esp32BaseWeb::sendChunk("<span class='muted'>—</span>");
        } else if (baselineAvailable) {
            sendFlowRate(zone.baselineFlowMlPerMinute);
        } else {
            Esp32BaseWeb::sendChunk("<span class='muted'>未设置</span><small>本次不进行高低流量判定</small>");
        }
        Esp32BaseWeb::sendChunk("</td><td data-label='流量表现'><div class='record-flow-performance'>");
        if (zone.result == ZoneWateringResult::NotStarted) {
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
            if ((zone.flags &
                 WateringRecordCodec::kZoneFlagTerminalFlowAvailable) != 0) {
                Esp32BaseWeb::sendChunk("<span>末段流量 ");
                sendFlowRate(zone.terminalFlowMlPerMinute);
                if (baselineAvailable) {
                    char comparison[48]{};
                    formatFlowChange(zone.terminalFlowMlPerMinute,
                                     zone.baselineFlowMlPerMinute,
                                     comparison,
                                     sizeof(comparison));
                    Esp32BaseWeb::sendChunk("，较基准 ");
                    Esp32BaseWeb::writeHtmlEscaped(comparison);
                }
                Esp32BaseWeb::sendChunk(" <small class='record-stability ");
                if ((zone.flags &
                     WateringRecordCodec::kZoneFlagTerminalFlowStable) != 0) {
                    Esp32BaseWeb::sendChunk("ok'>稳定");
                } else {
                    Esp32BaseWeb::sendChunk("muted'>未确认稳定");
                }
                Esp32BaseWeb::sendChunk("</small></span>");
                if ((zone.flags &
                     WateringRecordCodec::kZoneFlagTerminalFlowStable) == 0) {
                    Esp32BaseWeb::sendChunk("<span class='muted'>末段范围 ");
                    sendFlowRate(zone.terminalMinimumFlowMlPerMinute);
                    Esp32BaseWeb::sendChunk("～");
                    sendFlowRate(zone.terminalMaximumFlowMlPerMinute);
                    Esp32BaseWeb::sendChunk("</span>");
                }
            } else {
                Esp32BaseWeb::sendChunk("<span class='muted'>无末段流量数据</span>");
            }
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
            baselineAvailable) {
            Esp32BaseWeb::sendChunk("<span class='tag ok'>未触发高低流量报警</span>");
        }
        Esp32BaseWeb::sendChunk("</div></div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><p class='record-detail-note'>相对基准差值用于解释流量表现，不等同于报警结果；报警以浇水当时的阈值和连续判定结果为准。</p></div><details class='record-technical'><summary>指标与技术说明</summary><p class='record-technical-note'>整段平均只统计水流建立后的实际浇水阶段；末段流量来自结束前最多 5 个完整的 5 秒窗口，“未确认稳定”可能是窗口不足或波动较大；估算用水量还包含建立水流和关阀尾水阶段的实际脉冲。</p><div class='record-technical-grid'><div><b>总脉冲</b><span>");
    sendUnsigned64(totals.pulseCount);
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
        sendUnsigned(zone.pulseCount);
        Esp32BaseWeb::sendChunk(" 脉冲</span></div>");
    }
    Esp32BaseWeb::sendChunk("</div></details><div class='actions record-detail-bottom-close'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>关闭</button></div></dialog>");
}

void sendRecordRow(const StoredWateringRecord& record, void* user) {
    RecordRowsContext* context = static_cast<RecordRowsContext*>(user);
    const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(record.payload);
    if (context) ++context->emitted;
    Esp32BaseWeb::sendChunk("<tr><td data-label='完成时间' class='record-time'>");
    sendRecordTime(record.timing);
    Esp32BaseWeb::sendChunk("</td><td data-label='浇水任务'>");
    sendRecordSource(record.payload, context ? context->config : nullptr);
    Esp32BaseWeb::sendChunk("</td><td data-label='执行水路' class='record-zones'>");
    sendRecordWateredZones(record.payload, context ? context->config : nullptr);
    Esp32BaseWeb::sendChunk("</td><td data-label='执行结果'><span class='tag ");
    Esp32BaseWeb::sendChunk(recordOutcomeTone(record.payload));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(recordOutcomeName(record.payload));
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
    sendDuration(totals.actualWateringSec);
    Esp32BaseWeb::sendChunk("</span><small>/ ");
    const uint32_t targetWaterMl = recordTargetWaterMl(record.payload);
    if (targetWaterMl != 0) sendCompactWaterVolume(targetWaterMl);
    else sendDuration(totals.plannedDurationSec);
    Esp32BaseWeb::sendChunk("</small></td><td data-label='估算用水量' class='record-number'>");
    if (recordHasCappedEstimate(record.payload)) Esp32BaseWeb::sendChunk("至少 ");
    sendCompactWaterVolume(totals.estimatedWaterMl);
    Esp32BaseWeb::sendChunk("</td><td data-label='操作' class='record-action'><button type='button' class='btnlink info compact' onclick=\"document.getElementById('record-detail-");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("').showModal()\">查看详情</button>");
    sendRecordDetailDialog(record, context ? context->config : nullptr, "record-detail-");
    Esp32BaseWeb::sendChunk("</td></tr>");
}

struct EventFilter {
    uint8_t level = 0;
    int8_t category = -1;
    char query[64]{};
};

struct EventRowsContext {
    const EventFilter* filter;
    const IrrigationConfig* config;
    uint32_t offset;
    uint32_t limit;
    uint32_t matched;
    uint32_t emitted;
};

const char* eventToneClass(Esp32BaseAppEvents::Level level) {
    switch (level) {
        case Esp32BaseAppEvents::Level::Warning: return "warn";
        case Esp32BaseAppEvents::Level::Error: return "danger";
        case Esp32BaseAppEvents::Level::Info:
        default: return "info";
    }
}

const char* conditionStateName(
    IrrigationEvents::ConditionDisplayState state) {
    switch (state) {
        case IrrigationEvents::ConditionDisplayState::Normal: return "正常";
        case IrrigationEvents::ConditionDisplayState::Active: return "异常中";
        case IrrigationEvents::ConditionDisplayState::ConfirmingActivation:
            return "异常确认中";
        case IrrigationEvents::ConditionDisplayState::ConfirmingRecovery:
            return "恢复确认中";
        case IrrigationEvents::ConditionDisplayState::Unknown:
        default: return "等待判断";
    }
}

const char* conditionStateTone(
    IrrigationEvents::ConditionDisplayState state) {
    switch (state) {
        case IrrigationEvents::ConditionDisplayState::Normal: return "ok";
        case IrrigationEvents::ConditionDisplayState::Active: return "danger";
        case IrrigationEvents::ConditionDisplayState::ConfirmingActivation:
        case IrrigationEvents::ConditionDisplayState::ConfirmingRecovery:
            return "warn";
        case IrrigationEvents::ConditionDisplayState::Unknown:
        default: return "info";
    }
}

void sendEventTime(const Esp32BaseRecordStore::RecordTiming& timing) {
    uint32_t epoch = 0;
    char text[32]{};
    if (Esp32BaseRecordStore::resolveCompletedEpoch(timing, epoch) &&
        Esp32BaseTime::formatEpoch(epoch, text, sizeof(text), "%m-%d %H:%M:%S")) {
        Esp32BaseWeb::sendChunk(text);
        return;
    }
    Esp32BaseWeb::sendChunk("设备启动后 ");
    sendUnsigned(timing.completedUptimeSec);
    Esp32BaseWeb::sendChunk(" 秒");
}

bool eventMatches(const Esp32BaseAppEvents::EventRecord& event,
                  const EventFilter& filter) {
    if (filter.level != 0 && static_cast<uint8_t>(event.level) != filter.level) return false;
    return filter.category < 0 ||
           static_cast<uint8_t>(IrrigationEvents::category(event)) ==
               static_cast<uint8_t>(filter.category);
}

void sendEventDetailDialog(const Esp32BaseAppEvents::EventRecord& event,
                           const char* dialogId,
                           const IrrigationConfig* config) {
    char title[192]{};
    char summary[160]{};
    const uint8_t planId = IrrigationEvents::wateringPlanId(event);
    const char* eventPlanName = planNameById(config, planId);
    const char* eventZoneName =
        config && event.objectId >= 1 &&
                event.objectId <= BoardPins::kZoneCount
            ? config->zones[BoardPins::zoneIndex(
                  static_cast<uint8_t>(event.objectId))].name.data()
            : nullptr;
    IrrigationEvents::formatTitle(event,
                                  title,
                                  sizeof(title),
                                  eventPlanName,
                                  eventZoneName);
    IrrigationEvents::formatSummary(event, summary, sizeof(summary));
    Esp32BaseWeb::sendChunk("<dialog id='");
    Esp32BaseWeb::sendChunk(dialogId);
    Esp32BaseWeb::sendChunk("' class='panel eb-modal event-detail' data-eb-light-dismiss='1'><div class='event-detail-head'><div class='event-detail-heading'><span class='tag ");
    Esp32BaseWeb::sendChunk(eventToneClass(event.level));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(IrrigationEvents::levelName(event.level));
    Esp32BaseWeb::sendChunk("</span><h2>");
    Esp32BaseWeb::writeHtmlEscaped(title);
    Esp32BaseWeb::sendChunk("</h2></div><button type='button' class='secondary compact event-detail-close' onclick='this.closest(\"dialog\").close()'>关闭</button></div><p class='event-detail-summary'>");
    Esp32BaseWeb::writeHtmlEscaped(summary);
    Esp32BaseWeb::sendChunk("</p><section class='event-detail-section'><h3>事件概况</h3><div class='event-detail-grid event-detail-overview'><div><b>发生时间</b><span class='event-time-value'>");
    sendEventTime(event.timing);
    Esp32BaseWeb::sendChunk("</span></div><div><b>严重程度</b><span><span class='tag ");
    Esp32BaseWeb::sendChunk(eventToneClass(event.level));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(IrrigationEvents::levelName(event.level));
    Esp32BaseWeb::sendChunk("</span></span></div><div><b>业务分类</b><span>");
    Esp32BaseWeb::sendChunk(IrrigationEvents::categoryName(IrrigationEvents::category(event)));
    Esp32BaseWeb::sendChunk("</span></div></div></section>");
    const IrrigationEvents::EventCode eventCode =
        static_cast<IrrigationEvents::EventCode>(event.eventCode);
    if (eventCode == IrrigationEvents::EventCode::WateringStoppedAbnormally) {
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>关联浇水</h3><div class='event-detail-grid event-detail-business'><div><b>来源</b><span>");
        if (!IrrigationEvents::hasWateringContext(event)) {
            Esp32BaseWeb::sendChunk("未记录");
        } else if (IrrigationEvents::wateringSource(event) ==
            WateringSource::AutomaticPlan) {
            Esp32BaseWeb::sendChunk("自动计划");
            if (eventPlanName) {
                Esp32BaseWeb::sendChunk(" · ");
                Esp32BaseWeb::writeHtmlEscaped(eventPlanName);
            } else {
                Esp32BaseWeb::sendChunk(" ");
                sendUnsigned(planId);
                Esp32BaseWeb::sendChunk("（已删除）");
            }
        } else {
            Esp32BaseWeb::sendChunk(sourceName(IrrigationEvents::wateringSource(event)));
        }
        Esp32BaseWeb::sendChunk("</span></div><div><b>发生问题的水路</b><span>");
        if (event.objectId == 0) {
            Esp32BaseWeb::sendChunk("整次任务");
        } else if (eventZoneName) {
            Esp32BaseWeb::writeHtmlEscaped(eventZoneName);
            Esp32BaseWeb::sendChunk("（水路 ");
            sendUnsigned(event.objectId);
            Esp32BaseWeb::sendChunk("）");
        } else {
            Esp32BaseWeb::sendChunk("水路 ");
            sendUnsigned(event.objectId);
        }
        Esp32BaseWeb::sendChunk("</span></div><div><b>实际浇水</b><span>");
        sendDuration(event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2));
        Esp32BaseWeb::sendChunk("</span></div></div><div class='actions'><a class='btnlink secondary compact' href='/irrigation/records'>查看浇水记录</a></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::FlowDeviation) {
        const uint32_t actual =
            event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1);
        const uint32_t baseline =
            event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2);
        char comparison[48]{};
        formatFlowChange(actual, baseline, comparison, sizeof(comparison));
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>流量判断</h3><div class='event-detail-grid event-detail-business'><div><b>来源</b><span>");
        if (!IrrigationEvents::hasWateringContext(event)) {
            Esp32BaseWeb::sendChunk("未记录");
        } else if (IrrigationEvents::wateringSource(event) ==
            WateringSource::AutomaticPlan) {
            Esp32BaseWeb::sendChunk("自动计划");
            if (eventPlanName) {
                Esp32BaseWeb::sendChunk(" · ");
                Esp32BaseWeb::writeHtmlEscaped(eventPlanName);
            } else {
                Esp32BaseWeb::sendChunk(" ");
                sendUnsigned(planId);
            }
        } else {
            Esp32BaseWeb::sendChunk(sourceName(IrrigationEvents::wateringSource(event)));
        }
        Esp32BaseWeb::sendChunk("</span></div><div><b>水路</b><span>");
        if (eventZoneName) {
            Esp32BaseWeb::writeHtmlEscaped(eventZoneName);
            Esp32BaseWeb::sendChunk("（水路 ");
            sendUnsigned(event.objectId);
            Esp32BaseWeb::sendChunk("）");
        } else {
            Esp32BaseWeb::sendChunk("水路 ");
            sendUnsigned(event.objectId);
        }
        Esp32BaseWeb::sendChunk("</span></div><div><b>检测流量</b><span>");
        sendFlowRate(actual);
        Esp32BaseWeb::sendChunk("</span></div><div><b>当时基准流量</b><span>");
        sendFlowRate(baseline);
        Esp32BaseWeb::sendChunk("</span></div><div><b>相对基准</b><span>");
        Esp32BaseWeb::writeHtmlEscaped(comparison);
        Esp32BaseWeb::sendChunk("</span></div><div><b>处理结果</b><span>");
        Esp32BaseWeb::sendChunk(
            (event.flags & (1U << 1U)) != 0 ? "已停止本次浇水"
                                            : "继续本次浇水");
        Esp32BaseWeb::sendChunk("</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::ClosedValveFlow) {
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关数据</h3><div class='event-detail-grid event-detail-business'><div><b>窗口脉冲</b><span>");
        sendUnsigned(event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1));
        Esp32BaseWeb::sendChunk(" 个</span></div><div><b>检测窗口</b><span>");
        sendUnsigned(event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2));
        Esp32BaseWeb::sendChunk(" 秒</span></div><div><b>报警阈值</b><span>");
        sendUnsigned(event.objectId);
        Esp32BaseWeb::sendChunk(" 个脉冲</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::FlowCalibrationSaved) {
        const uint32_t previous =
            event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1);
        const uint32_t coefficient =
            event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2);
        char previousText[24]{};
        char coefficientText[24]{};
        std::snprintf(previousText, sizeof(previousText), "%lu.%02lu P/L",
                      static_cast<unsigned long>(previous / 100U),
                      static_cast<unsigned long>(previous % 100U));
        std::snprintf(coefficientText, sizeof(coefficientText), "%lu.%02lu P/L",
                      static_cast<unsigned long>(coefficient / 100U),
                      static_cast<unsigned long>(coefficient % 100U));
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>参数变化</h3><div class='event-detail-grid event-detail-business'><div><b>原稳态流量系数</b><span>");
        Esp32BaseWeb::sendChunk(previousText);
        Esp32BaseWeb::sendChunk("</span></div><div><b>新稳态流量系数</b><span>");
        Esp32BaseWeb::sendChunk(coefficientText);
        Esp32BaseWeb::sendChunk("</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::ZoneFlowSaved) {
        char previousFlow[20]{};
        char flow[20]{};
        IrrigationConfigRules::formatLitersPerMinute(
            event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1),
            previousFlow, sizeof(previousFlow));
        IrrigationConfigRules::formatLitersPerMinute(
            event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2),
            flow, sizeof(flow));
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关数据</h3><div class='event-detail-grid event-detail-business'><div><b>水路</b><span>");
        sendUnsigned(event.objectId);
        Esp32BaseWeb::sendChunk("</span></div><div><b>原基准流量</b><span>");
        if (event.value1 == 0) {
            Esp32BaseWeb::sendChunk("未设置");
        } else {
            Esp32BaseWeb::sendChunk(previousFlow);
            Esp32BaseWeb::sendChunk(" L/min");
        }
        Esp32BaseWeb::sendChunk("</span></div><div><b>新基准流量</b><span>");
        if (event.value2 == 0) {
            Esp32BaseWeb::sendChunk("已清除");
        } else {
            Esp32BaseWeb::sendChunk(flow);
            Esp32BaseWeb::sendChunk(" L/min");
        }
        Esp32BaseWeb::sendChunk("</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::AutomaticPlanSkipped) {
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关对象</h3><div class='event-detail-grid event-detail-business'><div><b>计划</b><span>");
        if (eventPlanName) {
            Esp32BaseWeb::writeHtmlEscaped(eventPlanName);
            Esp32BaseWeb::sendChunk("（计划 ");
            sendUnsigned(event.objectId);
            Esp32BaseWeb::sendChunk("）");
        } else {
            Esp32BaseWeb::sendChunk("计划 ");
            sendUnsigned(event.objectId);
        }
        Esp32BaseWeb::sendChunk("</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::ConfigurationChanged &&
               event.objectId != 0) {
        const IrrigationEvents::ReasonCode reason =
            static_cast<IrrigationEvents::ReasonCode>(event.reasonCode);
        const bool zoneChanged = reason == IrrigationEvents::ReasonCode::ZoneUpdated;
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关对象</h3><div class='event-detail-grid event-detail-business'><div><b>");
        Esp32BaseWeb::sendChunk(zoneChanged ? "水路" : "计划");
        Esp32BaseWeb::sendChunk("</b><span>");
        sendUnsigned(event.objectId);
        Esp32BaseWeb::sendChunk("</span></div></div></section>");
    }
    Esp32BaseWeb::sendChunk("<details class='event-technical'><summary>技术信息</summary><div class='event-detail-grid event-detail-technical-grid'><div><b>记录编号</b><span>#");
    sendUnsigned(event.recordId);
    Esp32BaseWeb::sendChunk("</span></div><div><b>事件码</b><span>");
    sendUnsigned(event.eventCode);
    Esp32BaseWeb::sendChunk("</span></div><div><b>原因码</b><span>");
    sendUnsigned(event.reasonCode);
    Esp32BaseWeb::sendChunk("</span></div><div><b>对象 ID</b><span>");
    sendUnsigned(event.objectId);
    Esp32BaseWeb::sendChunk("</span></div><div><b>数值 1</b><span>");
    char signedValue[20];
    std::snprintf(signedValue, sizeof(signedValue), "%ld", static_cast<long>(event.value1));
    Esp32BaseWeb::sendChunk(signedValue);
    Esp32BaseWeb::sendChunk("</span></div><div><b>数值 2</b><span>");
    std::snprintf(signedValue, sizeof(signedValue), "%ld", static_cast<long>(event.value2));
    Esp32BaseWeb::sendChunk(signedValue);
    Esp32BaseWeb::sendChunk("</span></div><div><b>标记</b><span>");
    sendUnsigned(event.flags);
    Esp32BaseWeb::sendChunk("</span></div><div><b>条件 ID</b><span>");
    sendUnsigned(event.conditionId);
    Esp32BaseWeb::sendChunk("</span></div><div><b>事件类型</b><span>");
    sendUnsigned(static_cast<uint32_t>(event.eventKind));
    Esp32BaseWeb::sendChunk("</span></div><div><b>启动编号</b><span>");
    sendUnsigned(event.timing.completedBootId);
    Esp32BaseWeb::sendChunk("</span></div><div><b>启动后时间</b><span>");
    sendUnsigned(event.timing.completedUptimeSec);
    Esp32BaseWeb::sendChunk(" 秒</span></div><div><b>持续时间</b><span>");
    sendUnsigned(event.timing.durationSec);
    Esp32BaseWeb::sendChunk(" 秒</span></div></div></details></dialog>");
}

void sendEventRow(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    EventRowsContext* context = static_cast<EventRowsContext*>(user);
    if (!context || !context->filter || !eventMatches(event, *context->filter)) return;
    const uint32_t index = context->matched++;
    if (index < context->offset || context->emitted >= context->limit) return;
    ++context->emitted;
    char title[192]{};
    char summary[160]{};
    char dialogId[28]{};
    const uint8_t planId = IrrigationEvents::wateringPlanId(event);
    const char* eventPlanName = planNameById(context->config, planId);
    const char* eventZoneName =
        context->config && event.objectId >= 1 &&
                event.objectId <= BoardPins::kZoneCount
            ? context->config->zones[BoardPins::zoneIndex(
                  static_cast<uint8_t>(event.objectId))].name.data()
            : nullptr;
    IrrigationEvents::formatTitle(event,
                                  title,
                                  sizeof(title),
                                  eventPlanName,
                                  eventZoneName);
    IrrigationEvents::formatSummary(event, summary, sizeof(summary));
    std::snprintf(dialogId, sizeof(dialogId), "event-detail-%lu",
                  static_cast<unsigned long>(event.recordId));
    Esp32BaseWeb::sendChunk("<tr><td data-label='时间' class='event-time'>");
    sendEventTime(event.timing);
    Esp32BaseWeb::sendChunk("</td><td data-label='等级' class='event-level'><span class='tag ");
    Esp32BaseWeb::sendChunk(eventToneClass(event.level));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(IrrigationEvents::levelName(event.level));
    Esp32BaseWeb::sendChunk("</span></td><td data-label='分类' class='event-category'>");
    Esp32BaseWeb::sendChunk(IrrigationEvents::categoryName(IrrigationEvents::category(event)));
    Esp32BaseWeb::sendChunk("</td><td data-label='事件' class='event-title'>");
    Esp32BaseWeb::writeHtmlEscaped(title);
    Esp32BaseWeb::sendChunk("</td><td data-label='说明' class='event-summary'>");
    Esp32BaseWeb::writeHtmlEscaped(summary);
    Esp32BaseWeb::sendChunk("</td><td data-label='操作' class='event-action'><button type='button' class='btnlink info compact' onclick=\"document.getElementById('");
    Esp32BaseWeb::sendChunk(dialogId);
    Esp32BaseWeb::sendChunk("').showModal()\">查看详情</button>");
    sendEventDetailDialog(event, dialogId, context->config);
    Esp32BaseWeb::sendChunk("</td></tr>");
}

}  // namespace

bool IrrigationWeb::registerRoutes(IrrigationApp& app) {
    g_app = &app;
    Esp32BaseWeb::setDeviceName("智能浇水");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP);
    Esp32BaseWeb::setHomePath("/irrigation");
    return Esp32BaseWeb::addPage("/irrigation", "首页", overview) &&
           Esp32BaseWeb::addPage("/irrigation/plans", "计划", plans) &&
           Esp32BaseWeb::addPage("/irrigation/zones", "水路", zones) &&
           Esp32BaseWeb::addPage("/irrigation/records", "记录", records) &&
           Esp32BaseWeb::addPage("/irrigation/events", "事件", events) &&
           Esp32BaseWeb::addPage("/irrigation/settings", "设置", settings) &&
           Esp32BaseWeb::addRoute("/irrigation", Esp32BaseWeb::METHOD_POST, overview) &&
           Esp32BaseWeb::addRoute("/irrigation/plans", Esp32BaseWeb::METHOD_POST, plans) &&
           Esp32BaseWeb::addRoute("/irrigation/zones", Esp32BaseWeb::METHOD_POST, zones) &&
           Esp32BaseWeb::addRoute("/irrigation/zones/flow-calibration",
                                  Esp32BaseWeb::METHOD_GET,
                                  flowCalibration) &&
           Esp32BaseWeb::addRoute("/irrigation/zones/flow-calibration",
                                  Esp32BaseWeb::METHOD_POST,
                                  flowCalibration) &&
           Esp32BaseWeb::addRoute("/irrigation/zones/learning",
                                  Esp32BaseWeb::METHOD_GET,
                                  zoneLearning) &&
           Esp32BaseWeb::addRoute("/irrigation/zones/learning",
                                  Esp32BaseWeb::METHOD_POST,
                                  zoneLearning) &&
           Esp32BaseWeb::addApi("/irrigation/api/status", statusApi) &&
           Esp32BaseWeb::addApi("/irrigation/api/flow-history", flowHistoryApi);
}

void IrrigationWeb::overview() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_manual_watering")) return;
        bool success = false;
        if (actionIs("stop")) {
            const WateringStatus status = g_app->wateringStatus();
            success = status.active && status.purpose == WateringPurpose::Normal &&
                      g_app->stopWatering();
        } else if (actionIs("start_zones")) {
            std::array<uint16_t, BoardPins::kZoneCount> durations{};
            success = true;
            for (uint8_t index = 0; index < durations.size(); ++index) {
                char name[8];
                std::snprintf(name, sizeof(name), "zone%u", index + 1U);
                uint32_t duration = 0;
                if (Esp32BaseWeb::hasParam(name) &&
                    (!g_app->configuration() ||
                     !uintParam(name,
                                0,
                                g_app->configuration()->runLimits.maximumZoneDurationMinutes,
                                duration))) {
                    success = false;
                    break;
                }
                durations[index] = static_cast<uint16_t>(duration);
            }
            success = success && g_app->startManualWatering(durations) ==
                                     WateringStartResult::Started;
        }
        redirectResult("/irrigation", success);
        return;
    }
    const WateringStatus watering = g_app->wateringStatus();
    if (watering.active) {
        activeTask();
        return;
    }
    if (!Esp32BaseWeb::checkAuth()) return;
    Esp32BaseWeb::sendHeader("智能浇水");
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.home-hero{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:18px;align-items:center;padding:20px;border:1px solid var(--eb-line);border-radius:12px;background:linear-gradient(135deg,#f4faf7,#fff);margin:12px 0}
.home-hero.warn{background:linear-gradient(135deg,var(--eb-warn-soft),#fff);border-color:#efcf96}.home-hero.danger{background:linear-gradient(135deg,var(--eb-danger-soft),#fff);border-color:#efc0ba}.home-hero.info{background:linear-gradient(135deg,var(--eb-info-soft),#fff);border-color:#cbdde5}
.home-eyebrow{display:block;margin-bottom:5px;color:var(--eb-muted);font-size:12px;font-weight:750}.home-hero h1{margin:0 0 6px;font-size:24px}.home-hero p{margin:0;color:var(--eb-muted)}.home-monitor{display:block;margin-top:7px;color:var(--eb-muted);font-size:12px}.home-monitor.danger{color:var(--eb-danger)}.home-hero .btnlink{min-height:38px;padding:0 16px}.home-action.hidden{display:none}
.home-hero-side{display:flex;align-items:center;justify-content:flex-end;gap:20px}.home-clock{width:170px}.home-clock.has-warning{width:250px}.home-clock-time{display:block;color:inherit;font-size:24px;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.1}.home-clock-date{display:block;margin-top:5px;color:var(--eb-muted);font-size:12px;font-weight:400;white-space:nowrap}.home-clock-warning{display:inline-flex;margin-top:7px;padding:3px 7px;border:1px solid #efcf96;border-radius:999px;background:var(--eb-warn-soft);color:#8a5708;font-size:11px;font-weight:500;line-height:1.35;text-decoration:none}.home-clock.pending .home-clock-time{color:var(--eb-warn);font-size:16px}.home-clock.pending .home-clock-date{white-space:normal}
.home-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin:12px 0}.home-card{display:flex;flex-direction:column;min-height:230px;padding:18px;border:1px solid var(--eb-line);border-radius:12px;background:#fff}.home-card-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:14px}.home-card-head h2{margin:0;font-size:17px}.home-main{font-size:22px;font-weight:760;line-height:1.3;overflow-wrap:anywhere}.home-sub{margin:6px 0 0;color:var(--eb-muted);overflow-wrap:anywhere}.home-plan{margin-top:14px;padding:11px 12px;border-radius:9px;background:var(--eb-soft)}.home-plan span{display:block;color:var(--eb-muted);font-size:11px}.home-plan b{display:block;margin-top:3px;font-size:17px;font-weight:600;overflow-wrap:anywhere}.home-outcome{margin:14px 0 0;padding:10px 12px;border-radius:9px;background:var(--eb-soft);font-size:14px;line-height:1.55}.home-card.danger{border-color:#efc0ba;background:linear-gradient(150deg,#fff,var(--eb-danger-soft))}.home-card.warn{border-color:#efcf96;background:linear-gradient(150deg,#fff,var(--eb-warn-soft))}.home-card.ok{border-color:#c8e4d5}.home-card.just-finished{animation:home-result-highlight 3.5s ease-out}.home-facts{display:grid;gap:7px;margin:14px 0 0}.home-fact{display:grid;grid-template-columns:6.5em minmax(0,1fr);gap:8px;font-size:13px}.home-fact span:first-child{color:var(--eb-muted)}.home-card-actions{display:flex;align-items:center;gap:8px;margin-top:auto;padding-top:16px}.home-card-actions form{margin:0}.home-empty{color:var(--eb-muted);line-height:1.7}.home-note{margin-top:10px;padding:10px 12px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:13px}@keyframes home-result-highlight{0%{box-shadow:0 0 0 4px rgba(17,123,139,.22)}100%{box-shadow:0 0 0 0 rgba(17,123,139,0)}}
.manual-modal{width:min(760px,calc(100vw - 28px));padding:20px!important}.manual-template{margin:14px 0}.manual-template-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:7px}.manual-template-head h3{margin:0;font-size:14px;font-weight:500}.manual-template-head a{font-size:12px}.manual-template-list{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.manual-template-card{display:block;min-width:0;min-height:76px;padding:10px 11px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft);color:var(--eb-text);text-align:left}.manual-template-card:hover{border-color:#a9cbd3;background:#f5fafb}.manual-template-card.selected{border-color:var(--eb-primary);background:var(--eb-primary-soft);box-shadow:0 0 0 1px var(--eb-primary)}.manual-template-card:disabled{opacity:.58;cursor:not-allowed}.manual-template-card b,.manual-template-card span,.manual-template-card small{display:block}.manual-template-card b{font-size:14px;font-weight:500}.manual-template-card span{margin-top:4px;color:#526071;font-size:12px;line-height:1.4;overflow-wrap:anywhere}.manual-template-card small{margin-top:3px;color:var(--eb-muted);font-size:11px}.manual-template-empty{padding:10px 11px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:13px}.manual-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px}.manual-zone{display:grid;grid-template-columns:minmax(0,1fr) 100px auto;gap:8px;align-items:center;padding:10px 11px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft)}.manual-zone label{margin:0;overflow-wrap:anywhere}.manual-zone input{width:100%;max-width:none;min-height:38px;margin:0}.manual-zone span{color:var(--eb-muted);font-weight:650}.manual-summary{margin-top:12px;padding:11px 13px;border:1px solid #cbdde5;border-radius:8px;background:var(--eb-info-soft)}.manual-summary b,.manual-summary span{display:block}.manual-summary span{margin-top:3px;color:var(--eb-muted);font-size:12px}.manual-modal .actions{margin-top:14px}
@media(max-width:760px){.home-hero,.home-grid{grid-template-columns:1fr}.home-hero{padding:16px}.home-hero h1{font-size:21px}.home-hero-side{align-items:stretch;flex-direction:column;gap:14px}.home-clock,.home-clock.has-warning{width:100%}.home-hero .btnlink{width:100%}.home-card{min-height:0;padding:15px}.home-fact{grid-template-columns:1fr;gap:2px}.manual-modal{width:calc(100vw - 20px);padding:14px!important}.manual-template-list,.manual-grid{grid-template-columns:1fr}.manual-zone{grid-template-columns:minmax(0,1fr) 86px auto}.manual-modal .actions{display:grid;grid-template-columns:1fr 1fr}.manual-modal .actions button,.manual-modal .actions input{width:100%;margin:0}}
</style>)HTML");
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
        heroHref = "/irrigation/zones";
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
        heroHref = "/esp32base";
        heroAction = "查看系统状态";
    } else if (schedulerTime == WateringScheduler::TimeState::RtcRollback) {
        heroTone = " warn";
        heroTitle = "设备时间异常，自动浇水已停止";
        heroDescription = "检测到 RTC 时间明显倒退，等待 NTP 校时后自动恢复判断。";
        heroHref = "/esp32base";
        heroAction = "查看时间状态";
    } else if (!timeTrusted) {
        heroTone = " warn";
        heroTitle = "设备时间尚未就绪";
        heroDescription = "自动计划暂时不会运行，手动浇水仍可使用。";
        heroHref = "/esp32base";
        heroAction = "查看时间状态";
    } else if (storageFault) {
        heroTone = " warn";
        heroTitle = "设备可以浇水，但部分数据存储异常";
        heroDescription = "请查看系统状态；自动计划或历史记录可能受到影响。";
        heroHref = "/esp32base";
        heroAction = "查看系统状态";
    }
    const char* defaultHeroTone = heroTone;
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
    Esp32BaseWeb::sendChunk("' id='home-hero' data-default-tone='");
    Esp32BaseWeb::writeHtmlEscaped(defaultHeroTone);
    Esp32BaseWeb::sendChunk("' data-default-title='");
    Esp32BaseWeb::writeHtmlEscaped(defaultHeroTitle);
    Esp32BaseWeb::sendChunk("' data-default-description='");
    Esp32BaseWeb::writeHtmlEscaped(defaultHeroDescription);
    Esp32BaseWeb::sendChunk("' data-rtc-unavailable='");
    Esp32BaseWeb::sendChunk(rtcUnavailable ? "1" : "0");
    Esp32BaseWeb::sendChunk("'><div><span class='home-eyebrow'>");
    Esp32BaseWeb::writeHtmlEscaped(heroEyebrow);
    Esp32BaseWeb::sendChunk("</span><h1 id='home-hero-title'>");
    Esp32BaseWeb::writeHtmlEscaped(heroTitle);
    Esp32BaseWeb::sendChunk("</h1><p id='home-hero-description'>");
    Esp32BaseWeb::writeHtmlEscaped(heroDescription);
    if (watering.active) {
        Esp32BaseWeb::sendChunk(" · ");
        Esp32BaseWeb::writeHtmlEscaped(wateringStateName(watering.state));
        Esp32BaseWeb::sendChunk(watering.flowEstablished ? " · 水流正常" : " · 正在等待水流");
    }
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
        Esp32BaseWeb::sendChunk("<a class='home-clock-warning' href='/irrigation/events'>硬件时钟不可用 · 断网后计划可能暂停</a>");
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
    Esp32BaseWeb::sendChunk("</span><a id='home-alarm-action' class='home-action btnlink info");
    if (!flowAlarm) Esp32BaseWeb::sendChunk(" hidden");
    Esp32BaseWeb::sendChunk("' href='/irrigation/events'>查看事件</a></div></section>");
    const bool heroShowsOtherPriority = watering.active || g_app->unexpectedFlowAlarm();
    if (heroShowsOtherPriority && g_app->schedulerStorageFault()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "自动浇水暂不可用",
                                 "调度状态无法可靠保存；当前或手动浇水不受影响。");
    } else if (heroShowsOtherPriority && schedulerTime == WateringScheduler::TimeState::RtcRollback) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "设备时间异常",
                                 "自动计划已停止，等待 NTP 校时后恢复判断。");
    } else if (heroShowsOtherPriority && !timeTrusted) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "设备时间尚未就绪",
                                 "自动计划暂时不会运行，手动浇水仍可使用。");
    }
    if (heroShowsOtherPriority && storageFault && !g_app->schedulerStorageFault()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "部分数据存储异常",
                                 "请在当前浇水结束后查看系统状态。");
    }
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
        Esp32BaseWeb::sendChunk(recordOutcomeName(latest.record.payload));
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
        sendRecordTimeRange(latest.record.timing);
        Esp32BaseWeb::sendChunk("</p><p class='home-outcome'>");
        sendRecordOutcomeSummary(latest.record.payload, config);
        Esp32BaseWeb::sendChunk("</p><div class='home-facts'><div class='home-fact'><span>执行目标</span><b>");
        const uint32_t latestTargetWaterMl = recordTargetWaterMl(latest.record.payload);
        if (latestTargetWaterMl != 0) sendCompactWaterVolume(latestTargetWaterMl);
        else { formatElapsed(totals.plannedDurationSec, value, sizeof(value)); Esp32BaseWeb::writeHtmlEscaped(value); }
        Esp32BaseWeb::sendChunk("</b></div><div class='home-fact'><span>实际浇水</span><b>");
        formatElapsed(totals.actualWateringSec, value, sizeof(value));
        Esp32BaseWeb::writeHtmlEscaped(value);
        Esp32BaseWeb::sendChunk("</b></div><div class='home-fact'><span>估算用水量</span><b>");
        sendCompactWaterVolume(totals.estimatedWaterMl);
        Esp32BaseWeb::sendChunk("</b></div></div><div class='home-card-actions'><a class='btnlink info' href='/irrigation/records?id=");
        sendUnsigned(latest.record.recordId);
        Esp32BaseWeb::sendChunk("'>查看完整记录</a><a class='btnlink secondary' href='/irrigation/records'>全部记录</a></div>");
    }
    Esp32BaseWeb::sendChunk("</section></div>");

    const uint32_t checkpointEpoch = g_app->lastKnownAliveEpoch();
    const char* resetReason = Esp32BaseSystem::resetReason();
    if (now.synced && checkpointEpoch != 0 && now.bootStartEpochSec >= checkpointEpoch &&
        (std::strcmp(resetReason, "poweron") == 0 ||
         std::strcmp(resetReason, "brownout") == 0)) {
        char checkpointText[24]{};
        char bootText[24]{};
        char message[96]{};
        if (Esp32BaseTime::formatEpoch(checkpointEpoch, checkpointText,
                                       sizeof(checkpointText), "%Y-%m-%d %H:%M") &&
            Esp32BaseTime::formatEpoch(now.bootStartEpochSec, bootText,
                                       sizeof(bootText), "%Y-%m-%d %H:%M")) {
            std::snprintf(message, sizeof(message), "%s 至 %s；仅为可能范围，不是精确停电时间",
                          checkpointText, bootText);
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "可能断电范围", message);
        }
    }
    if (config && hasEnabledZone) {
        Esp32BaseWeb::sendChunk("<dialog id='manual-watering' class='panel eb-modal manual-modal' data-eb-light-dismiss='1'><h2>手动浇水</h2><p class='muted'>可以从计划填入时长，也可以直接设置；最终只按这里提交的时长执行，不会修改计划。</p><form id='manual-form' method='post' action='/irrigation' onsubmit='return submitManualWatering(this)'><input type='hidden' name='action' value='start_zones'>");
        bool hasPlan = false;
        for (const WateringPlan& plan : config->plans) if (plan.configured) hasPlan = true;
        Esp32BaseWeb::sendChunk("<div class='manual-template'><div class='manual-template-head'><h3>快速填入计划</h3><a href='/irrigation/plans'>管理计划</a></div>");
        if (hasPlan) {
            Esp32BaseWeb::sendChunk("<div class='manual-template-list'>");
            for (const WateringPlan& plan : config->plans) {
                if (!plan.configured) continue;
                uint16_t activeZoneCount = 0;
                uint32_t totalMinutes = 0;
                for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                    if (!config->zones[index].enabled || plan.zoneDurationMinutes[index] == 0) continue;
                    ++activeZoneCount;
                    totalMinutes += plan.zoneDurationMinutes[index];
                }
                Esp32BaseWeb::sendChunk("<button type='button' class='manual-template-card' data-plan-name='");
                Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
                Esp32BaseWeb::sendChunk("' data-durations='");
                for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                    if (index != 0) Esp32BaseWeb::sendChunk(",");
                    sendUnsigned(plan.zoneDurationMinutes[index]);
                }
                Esp32BaseWeb::sendChunk("'");
                if (activeZoneCount == 0) Esp32BaseWeb::sendChunk(" disabled");
                Esp32BaseWeb::sendChunk("><b>");
                Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
                Esp32BaseWeb::sendChunk("</b><span>");
                bool emittedZone = false;
                for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                    if (!config->zones[index].enabled || plan.zoneDurationMinutes[index] == 0) continue;
                    if (emittedZone) Esp32BaseWeb::sendChunk(" · ");
                    Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
                    Esp32BaseWeb::sendChunk(" ");
                    sendUnsigned(plan.zoneDurationMinutes[index]);
                    Esp32BaseWeb::sendChunk("分");
                    emittedZone = true;
                }
                if (!emittedZone) Esp32BaseWeb::sendChunk("当前无可执行水路");
                Esp32BaseWeb::sendChunk("</span><small>");
                sendUnsigned(activeZoneCount);
                Esp32BaseWeb::sendChunk(" 路 · 共 ");
                sendUnsigned(totalMinutes);
                Esp32BaseWeb::sendChunk(" 分钟</small></button>");
            }
            Esp32BaseWeb::sendChunk("</div>");
        } else {
            Esp32BaseWeb::sendChunk("<div class='manual-template-empty'>还没有可用计划，可以直接设置下方各水路时长。</div>");
        }
        Esp32BaseWeb::sendChunk("</div>");
        Esp32BaseWeb::sendChunk("<div class='manual-grid'>");
        for (uint8_t index = 0; index < config->zones.size(); ++index) {
            if (!config->zones[index].enabled) continue;
            Esp32BaseWeb::sendChunk("<div class='manual-zone'><label>");
            Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
            Esp32BaseWeb::sendChunk("</label><input class='manual-duration' data-zone-index='"); sendUnsigned(index);
            Esp32BaseWeb::sendChunk("' type='number' min='0' max='"); sendUnsigned(config->runLimits.maximumZoneDurationMinutes); Esp32BaseWeb::sendChunk("' name='zone"); sendUnsigned(index + 1U);
            Esp32BaseWeb::sendChunk("' value='0' inputmode='numeric'><span>分钟</span></div>");
        }
        Esp32BaseWeb::sendChunk("</div><div class='manual-summary'><b id='manual-summary'>尚未选择水路</b><span id='manual-template-note'>每条水路范围 0～"); sendUnsigned(config->runLimits.maximumZoneDurationMinutes); Esp32BaseWeb::sendChunk(" 分钟，0 表示本次不执行。</span></div><div class='actions'><button id='manual-clear' type='button' class='secondary'>全部清零</button><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button><input id='manual-submit' type='submit' value='确认并开始浇水' disabled></div></form></dialog>");
    } else if (config) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "还没有启用水路", "请先到水路页面启用实际安装的水路。");
        Esp32BaseWeb::sendChunk("<div class='actions'><a class='btnlink info' href='/irrigation/zones'>前往水路设置</a></div>");
    }
    Esp32BaseWeb::sendChunk(R"HTML(<script>(function(){
var recentCard=document.getElementById('home-recent-card');try{if(recentCard&&sessionStorage.getItem('irrigationJustFinished')==='1'){sessionStorage.removeItem('irrigationJustFinished');recentCard.classList.add('just-finished');setTimeout(function(){recentCard.classList.remove('just-finished')},3600)}}catch(ignore){}
var clock=document.getElementById('home-clock'),clockTime=document.getElementById('home-clock-time'),clockDate=document.getElementById('home-clock-date');if(clock&&clock.dataset.epoch){var clockBase=Number(clock.dataset.epoch),clockStarted=performance.now();function clockPad(v){return String(v).padStart(2,'0')}function updateClock(){var epoch=clockBase+Math.floor((performance.now()-clockStarted)/1000),d=new Date((epoch+28800)*1000);if(clockTime)clockTime.textContent=clockPad(d.getUTCHours())+':'+clockPad(d.getUTCMinutes())+':'+clockPad(d.getUTCSeconds());if(clockDate)clockDate.textContent=d.getUTCFullYear()+'年'+(d.getUTCMonth()+1)+'月'+d.getUTCDate()+'日'}updateClock();setInterval(updateClock,1000)}
var inputs=Array.prototype.slice.call(document.querySelectorAll('.manual-duration')),submit=document.getElementById('manual-submit'),summary=document.getElementById('manual-summary'),note=document.getElementById('manual-template-note'),cards=Array.prototype.slice.call(document.querySelectorAll('.manual-template-card')),applyingTemplate=false,count=0,total=0;
function update(){count=0;total=0;inputs.forEach(function(input){var value=Math.max(0,Math.min(Number(input.max)||0,Number(input.value)||0));if(value>0){count++;total+=value}});if(summary)summary.textContent=count?('已选择 '+count+' 条水路 · 合计 '+total+' 分钟'):'尚未选择水路';if(submit)submit.disabled=count===0}
inputs.forEach(function(input){input.addEventListener('input',function(){update();if(applyingTemplate)return;cards.forEach(function(card){card.classList.remove('selected')});if(note)note.textContent='当前时长已手动调整，不会修改任何计划。'})});
cards.forEach(function(card){card.addEventListener('click',function(){if(card.disabled)return;var values=(card.dataset.durations||'').split(',');applyingTemplate=true;inputs.forEach(function(input){input.value=values[Number(input.dataset.zoneIndex)]||0});applyingTemplate=false;cards.forEach(function(item){item.classList.toggle('selected',item===card)});if(note)note.textContent='已从“'+(card.dataset.planName||'计划')+'”填入，可继续修改；本次修改不会保存到计划。';update()})});
var clear=document.getElementById('manual-clear');if(clear)clear.addEventListener('click',function(){inputs.forEach(function(input){input.value=0});cards.forEach(function(card){card.classList.remove('selected')});if(note)note.textContent='已全部清零。';update()});
window.submitManualWatering=function(form){update();if(!count){alert('请至少为一条水路设置大于 0 的时长。');return false}return confirm('确认手动浇水 '+count+' 条水路，合计 '+total+' 分钟？')&&once(form)};update();
var hero=document.getElementById('home-hero'),heroTitle=document.getElementById('home-hero-title'),heroDescription=document.getElementById('home-hero-description'),monitor=document.getElementById('home-flow-monitor'),defaultAction=document.getElementById('home-default-action'),alarmAction=document.getElementById('home-alarm-action');
function toggleHidden(element,hidden){if(element)element.classList.toggle('hidden',hidden)}
function alarmObservation(status){var seconds=Math.max(1,Number(status.unexpectedFlowObservedWindowSec)||1),pulses=Number(status.unexpectedFlowPulseCount)||0,flow=(Number(status.unexpectedFlowEstimatedMlPerMinute)||0)/1000;return'近 '+seconds+' 秒检测到 '+pulses+' 个水流脉冲 · 估算平均流量 '+flow.toFixed(3)+' L/min'}
function updateIdleStatus(status){if(status.active||!!status.rtcUnavailable!==(hero.dataset.rtcUnavailable==='1')){location.reload();return false}var alarm=!!status.unexpectedFlowAlarm;if(hero){var tone=alarm?'danger':String(hero.dataset.defaultTone||'').trim();hero.className='home-hero'+(tone?' '+tone:'')}if(heroTitle)heroTitle.textContent=alarm?'关阀后水流异常':hero.dataset.defaultTitle;if(heroDescription)heroDescription.textContent=alarm?'水泵和全部阀门均已关闭，但仍检测到水流。请检查阀门、管路或流量计。':hero.dataset.defaultDescription;toggleHidden(defaultAction,alarm);toggleHidden(alarmAction,!alarm);if(monitor){monitor.classList.toggle('danger',alarm);if(alarm)monitor.textContent=alarmObservation(status);else if(status.unexpectedFlowObservationReady)monitor.textContent='关阀后水流监测已开启';else monitor.textContent='关阀后水流监测中'}return true}
var pollTimer=0,polling=false;function schedulePoll(delay){clearTimeout(pollTimer);pollTimer=setTimeout(poll,delay)}function poll(){if(polling)return;polling=true;fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(response){return response.json()}).then(function(status){polling=false;if(updateIdleStatus(status))schedulePoll(document.hidden?15000:5000)}).catch(function(){polling=false;schedulePoll(document.hidden?15000:10000)})}
document.addEventListener('visibilitychange',function(){schedulePoll(document.hidden?15000:0)});schedulePoll(5000);
})();</script>)HTML");
    endPage();
}

void IrrigationWeb::activeTask() {
    if (!beginPage("首页", "查看当前任务的实时状态")) return;
    Esp32BaseWeb::sendChunk(R"HTML(<style>
.run-idle-hero{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:16px;border:1px solid #cfe1e5;border-radius:10px;background:linear-gradient(135deg,#f4faf7,#fff)}.run-idle-hero h3{margin:0 0 4px;font-size:19px}.run-idle-hero p{margin:0;color:var(--eb-muted)}
.run-context{display:flex;align-items:center;justify-content:space-between;gap:16px;margin:12px 0;padding:11px 14px;border:1px solid var(--eb-line);border-radius:10px;background:#fff}.run-context-status{display:flex;align-items:center;gap:8px}.run-context-status b{font-size:14px;font-weight:600}.run-clock{text-align:right}.run-clock-time{display:block;font-size:20px;font-weight:650;font-variant-numeric:tabular-nums;line-height:1.1}.run-clock-date{display:block;margin-top:4px;color:var(--eb-muted);font-size:11px}.run-clock-warning{display:inline-flex;margin-top:5px;padding:2px 6px;border:1px solid #efcf96;border-radius:999px;background:var(--eb-warn-soft);color:#8a5708;font-size:10px;text-decoration:none}
.run-live-head{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:12px 14px;border:1px solid #cbdde5;border-radius:10px;background:linear-gradient(135deg,var(--eb-info-soft),#fff)}.run-live-title{display:flex;align-items:center;gap:9px;min-width:0}.run-live-title h3{margin:0;font-size:18px;font-weight:600;overflow-wrap:anywhere}.run-live-title .tag{flex:0 0 auto;font-size:11px;font-weight:500}.run-live-head p{margin:3px 0 0;color:var(--eb-muted);font-size:13px}.run-live-head form{flex:0 0 auto;margin:0}.run-live-head input{min-height:36px;font-weight:500}
.run-live-metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:9px;margin-top:12px}.run-live-metric{padding:12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft)}.run-live-metric span{display:block;color:var(--eb-muted);font-size:12px;font-weight:400}.run-live-metric b{display:block;margin-top:3px;font-size:18px;font-weight:500;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.run-live-metric small{display:block;margin-top:3px;color:var(--eb-muted);font-size:10px;font-weight:400;line-height:1.35}
.run-live-grid{display:grid;grid-template-columns:minmax(260px,.8fr) minmax(0,1.4fr);gap:12px;margin-top:12px}.run-current,.run-chart-card{padding:15px;border:1px solid var(--eb-line);border-radius:10px;background:#fff}.run-section-label{display:block;margin-bottom:5px;color:var(--eb-muted);font-size:12px;font-weight:400}.run-current-head{display:flex;align-items:center;justify-content:space-between;gap:10px}.run-current-head h3{margin:0;font-size:19px;font-weight:500}.run-current-head .tag{flex:0 0 auto;font-weight:500}.run-progress{height:10px;margin:13px 0 8px;border-radius:999px;background:var(--eb-line-soft);overflow:hidden}.run-progress span{display:block;height:100%;width:0;border-radius:inherit;background:var(--eb-primary);transition:width .25s ease}.run-current-detail{display:flex;justify-content:space-between;gap:10px;color:var(--eb-muted);font-size:13px;font-variant-numeric:tabular-nums}.run-flow-facts{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:13px}.run-flow-fact{padding:9px 10px;border-radius:8px;background:var(--eb-soft)}.run-flow-fact span{display:block;color:var(--eb-muted);font-size:11px;font-weight:400}.run-flow-fact b{display:block;margin-top:2px;font-size:14px;font-weight:500;font-variant-numeric:tabular-nums}.run-chart-card{min-width:0}.run-chart-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:8px}.run-chart-head h3{margin:0;font-size:16px;font-weight:500}.run-chart-head span{color:var(--eb-muted);font-size:12px;font-weight:400}.run-chart-wrap{position:relative;min-height:205px}.run-chart-wrap canvas{display:block;width:100%;height:205px}.run-chart-empty{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:var(--eb-muted);font-size:13px;pointer-events:none}
.run-steps{display:grid;gap:8px;margin-top:12px}.run-step{display:grid;grid-template-columns:34px minmax(0,1fr) auto;gap:10px;align-items:center;padding:10px 12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:#fff}.run-step-icon{display:flex;align-items:center;justify-content:center;width:28px;height:28px;border-radius:50%;background:var(--eb-soft);color:var(--eb-muted);font-size:12px;font-weight:500}.run-step.current{border-color:#9fc8d0;background:var(--eb-primary-soft)}.run-step.current .run-step-icon{background:var(--eb-primary);color:#fff}.run-step.complete .run-step-icon{background:var(--eb-ok);color:#fff}.run-step-main b,.run-step-main small{display:block}.run-step-main b{font-weight:400}.run-step.current .run-step-main b{font-weight:500}.run-step-main small,.run-step-detail{color:var(--eb-muted);font-size:12px;font-weight:400}.run-step-detail{text-align:right}.run-note{margin:10px 0 0;color:var(--eb-muted);font-size:12px;font-weight:400}
@media(max-width:900px){.run-live-metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.run-live-grid{grid-template-columns:1fr}}
@media(max-width:760px){.run-context{align-items:flex-start}.run-context-status{align-items:flex-start;flex-direction:column;gap:4px}.run-clock{max-width:62%}.run-live-head{align-items:stretch;flex-direction:column;gap:10px}.run-live-head form input{width:100%}.run-live-metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.run-live-grid{grid-template-columns:1fr}.run-step{grid-template-columns:30px minmax(0,1fr)}.run-step-detail{grid-column:2;text-align:left}}
@media(max-width:420px){.run-live-metrics,.run-flow-facts{grid-template-columns:1fr}}
</style>)HTML");
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
        Esp32BaseWeb::sendChunk("<a class='run-clock-warning' href='/irrigation/events'>硬件时钟不可用 · 断网后计划可能暂停</a>");
    }
    Esp32BaseWeb::sendChunk("</div></section><script>(function(){var clock=document.getElementById('run-clock'),time=document.getElementById('run-clock-time'),date=document.getElementById('run-clock-date');if(!clock||!clock.dataset.epoch)return;var base=Number(clock.dataset.epoch),started=performance.now();function pad(v){return String(v).padStart(2,'0')}function update(){var epoch=base+Math.floor((performance.now()-started)/1000),d=new Date((epoch+28800)*1000);if(time)time.textContent=pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())+':'+pad(d.getUTCSeconds());if(date)date.textContent=d.getUTCFullYear()+'年'+(d.getUTCMonth()+1)+'月'+d.getUTCDate()+'日'}update();setInterval(update,1000)})();</script>");
    if (status.purpose != WateringPurpose::Normal) {
        const bool calibration = status.purpose == WateringPurpose::FlowCalibration;
        Esp32BaseWeb::beginPanel("当前维护任务");
        Esp32BaseWeb::sendChunk("<div class='run-live-head'><div><span>维护任务</span><h3>");
        Esp32BaseWeb::sendChunk(calibration ? "流量计校准" : "基准流量学习");
        Esp32BaseWeb::sendChunk("</h3><p>任务正在设备上继续运行，请返回专属页面查看数据或停止。</p></div><a class='btnlink info' href='");
        if (calibration) {
            Esp32BaseWeb::sendChunk("/irrigation/zones/flow-calibration");
        } else {
            Esp32BaseWeb::sendChunk("/irrigation/zones/learning?zone=");
            sendUnsigned(status.activeZoneId);
        }
        Esp32BaseWeb::sendChunk("'>查看任务</a></div>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::sendChunk("<script>(function(){function finished(){try{sessionStorage.setItem('irrigationJustFinished','1')}catch(ignore){}location.reload()}function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active)finished();else setTimeout(poll,2000)}).catch(function(){setTimeout(poll,3000)})}setTimeout(poll,2000)})();</script>");
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
        if (status.purpose == WateringPurpose::FlowCalibration) {
            taskName = "流量计校准";
            taskSource = "维护任务";
        } else if (status.purpose == WateringPurpose::ZoneFlowLearning) {
            taskName = "基准流量学习";
            taskSource = "维护任务";
        } else if (activePlan) {
            taskName = activePlan->name.data();
        } else if (status.source == WateringSource::SingleOutput) {
            taskName = "单次出水";
            taskSource = "单次出水";
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
    Esp32BaseWeb::sendChunk(R"HTML(<script>(function(){var live=document.getElementById('run-live'),initialActive=!!live;function duration(v){v=Math.max(0,Number(v)||0);if(v<60)return v+' 秒';if(v<3600)return Math.floor(v/60)+' 分 '+(v%60)+' 秒';return Math.floor(v/3600)+' 小时 '+Math.floor((v%3600)/60)+' 分'}function liters(v){return((Number(v)||0)/1000).toFixed(3)+' L'}function flow(v){return((Number(v)||0)/1000).toFixed(3)+' L/min'}function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}var inputs=document.querySelectorAll('.run-custom-duration'),submit=document.getElementById('run-custom-submit');function updateCustom(){var count=0,total=0;inputs.forEach(function(i){var v=Math.max(0,Math.min(Number(i.max)||0,Number(i.value)||0));if(v>0){count++;total+=v}});set('run-custom-summary',count?('已选择 '+count+' 条水路 · 合计 '+total+' 分钟'):'尚未选择水路');if(submit)submit.disabled=count===0}inputs.forEach(function(i){i.addEventListener('input',updateCustom)});window.runCustomSubmit=function(form){updateCustom();if(!submit||submit.disabled){alert('请至少为一条水路设置大于 0 的时长。');return false}return confirm('确认按当前自定义时长立即开始浇水？')&&once(form)};updateCustom();if(!initialActive){function idlePoll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(s.active)location.reload();else setTimeout(idlePoll,5000)}).catch(function(){setTimeout(idlePoll,10000)})}setTimeout(idlePoll,5000);return}var canvas=document.getElementById('run-flow-chart'),empty=document.getElementById('run-chart-empty'),samples=[],generation=Number(live.dataset.generation||0),serial=Number(live.dataset.serial||0),expectedFlow=Number(live.dataset.expectedFlow||0);
function rangeDuration(value){if(value<60)return value+' 秒';if(value%60===0)return value/60+' 分钟';return Math.floor(value/60)+' 分 '+value%60+' 秒'}
function draw(){
if(!canvas)return;
var rect=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1,w=Math.max(280,rect.width),h=205,left=52,right=12,top=14,bottom=28,plotW=w-left-right,plotH=h-top-bottom;
canvas.width=w*dpr;canvas.height=h*dpr;
var c=canvas.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);c.font='11px sans-serif';c.fillStyle='#667085';c.textBaseline='middle';
var count=samples.length,coverage=Math.min(600,count*5);
set('run-chart-range',count?('最近 '+rangeDuration(coverage)+' · 5 秒/点'):'正在等待数据 · 5 秒/点');
var maximum=count?Math.max.apply(null,samples)/1000:1;if(expectedFlow>0)maximum=Math.max(maximum,expectedFlow/1000);var rough=Math.max(maximum*1.08,1)/4,power=Math.pow(10,Math.floor(Math.log(rough)/Math.LN10)),normalized=rough/power,steps=[1,2,2.5,5,10],step=steps[steps.length-1]*power;
for(var s=0;s<steps.length;s++){if(normalized<=steps[s]){step=steps[s]*power;break}}
var scale=step*4;
function yText(value){if(value===0)return '0';if(value>=10)return String(Math.round(value));return value.toFixed(value>=1?1:2)}
for(var yi=0;yi<=4;yi++){var value=step*yi,y=top+plotH*(1-yi/4);c.strokeStyle=yi===0?'#cbd7dd':'#e7edef';c.lineWidth=1;c.beginPath();c.moveTo(left,y);c.lineTo(w-right,y);c.stroke();c.fillStyle='#667085';c.textAlign='right';c.fillText(yText(value),left-8,y)}
if(count){var maximumLabels=w<500?5:6,minimumStep=coverage/Math.max(1,maximumLabels-1),timeSteps=[5,10,15,30,60,120],timeStep=timeSteps[timeSteps.length-1];for(var ts=0;ts<timeSteps.length;ts++){if(timeSteps[ts]>=minimumStep){timeStep=timeSteps[ts];break}}var ticks=[coverage],next=Math.floor((coverage-1)/timeStep)*timeStep;while(next>0){ticks.push(next);next-=timeStep}ticks.push(0);ticks.forEach(function(ago){var x=left+plotW*(1-ago/coverage);c.strokeStyle='#eef2f4';c.beginPath();c.moveTo(x,top);c.lineTo(x,top+plotH);c.stroke();var label=ago===0?'现在':rangeDuration(ago)+'前';c.fillStyle='#667085';c.textBaseline='bottom';c.textAlign=ago===coverage?'left':(ago===0?'right':'center');c.fillText(label,x,h-2)})}
if(count===0){if(empty)empty.style.display='flex';return}
if(empty)empty.style.display='none';
if(expectedFlow>0){var baseline=expectedFlow/1000,baselineY=top+plotH*(1-baseline/scale);c.save();c.setLineDash([5,4]);c.strokeStyle='#7b8d94';c.lineWidth=1;c.beginPath();c.moveTo(left,baselineY);c.lineTo(w-right,baselineY);c.stroke();c.restore();c.fillStyle='#667085';c.textAlign='right';c.textBaseline='bottom';c.fillText('基准 '+baseline.toFixed(3),w-right-4,baselineY-3)}
c.strokeStyle='#117b8b';c.lineWidth=2;c.lineJoin='round';c.lineCap='round';c.beginPath();
samples.forEach(function(value,index){var x=left+plotW*((index+1)/count),y=top+plotH*(1-(value/1000)/scale);if(index===0)c.moveTo(x,y);else c.lineTo(x,y)});
c.stroke();
var latest=samples[count-1]/1000,latestX=w-right,latestY=top+plotH*(1-latest/scale);c.fillStyle='#117b8b';c.beginPath();c.arc(latestX,latestY,3.5,0,Math.PI*2);c.fill()
}
function loadHistory(){fetch('/irrigation/api/flow-history',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(h){samples=Array.isArray(h.samples)?h.samples.slice(-120):[];generation=Number(h.generation||0);serial=Number(h.latestSerial||0);draw()}).catch(function(){setTimeout(loadHistory,2000)})}function update(s){set('run-elapsed',duration(s.elapsedSec));set('run-remaining',duration(s.plannedRemainingSec));set('run-water',liters(s.totalEstimatedWaterMl));set('run-step-count','第 '+(Number(s.currentStepIndex)+1)+' / '+s.stepCount+' 条水路');var activeZone=s.zones[s.currentStepIndex]||{},waterTarget=Number(activeZone.targetWaterMl)||0;set('run-current-elapsed','实际浇水 '+duration(s.currentZoneElapsedSec)+(waterTarget?' / 目标 '+liters(waterTarget):' / '+duration(activeZone.plannedDurationSec)));set('run-current-remaining',(waterTarget?'安全时限剩余 ':'剩余 ')+duration(s.currentZoneRemainingSec));set('run-flow',flow(s.currentFlowMlPerMinute));expectedFlow=Number(s.expectedFlowMlPerMinute||0);set('run-expected-flow',expectedFlow?flow(expectedFlow):'未设置');set('run-pulses',s.pulseCount);set('run-zone-water',liters(activeZone.estimatedWaterMl));var current=document.querySelector('[data-step-index=\"'+s.currentStepIndex+'\"]'),zoneName='水路 '+s.zoneId;if(current){var name=current.querySelector('.run-step-name');if(name)zoneName=name.textContent;set('run-current-zone',zoneName)}var states=['空闲','区域启动中','等待水流','正在浇水','区域停止中','水路切换中'],phase=Number(s.state)===3?(s.flowEstablished?'水流已建立':'等待水流建立'):(states[s.state]||'未知');set('run-state',zoneName+' · '+phase);var target=waterTarget||Number(activeZone.plannedDurationSec)||0,value=waterTarget?Number(activeZone.estimatedWaterMl):Number(s.currentZoneElapsedSec),percent=target?Math.min(100,Math.round(value*100/target)):0,bar=document.getElementById('run-current-progress');if(bar)bar.style.width=percent+'%';var flowState=document.getElementById('run-flow-state');if(flowState){var flowTone='warn',flowLabel='等待水流';if(Number(s.state)===5){flowTone='info';flowLabel='水路切换中'}else if(s.flowEstablished&&!expectedFlow){flowTone='info';flowLabel='水流已建立'}else if(s.flowEstablished&&activeZone.lowFlowActive){flowTone='warn';flowLabel='低流量'}else if(s.flowEstablished&&activeZone.highFlowActive){flowTone='danger';flowLabel='高流量'}else if(s.flowEstablished){flowTone='info';flowLabel='流量监测中'}flowState.textContent=flowLabel;flowState.className='tag '+flowTone}document.querySelectorAll('.run-step').forEach(function(row){var i=Number(row.dataset.stepIndex),z=s.zones[i]||{},icon=row.querySelector('.run-step-icon'),detail=row.querySelector('.run-step-detail');row.classList.toggle('complete',i<s.currentStepIndex);row.classList.toggle('current',i===s.currentStepIndex);if(icon)icon.textContent=i<s.currentStepIndex?'✓':String(i+1);if(detail){if(i<s.currentStepIndex)detail.textContent='实际 '+duration(z.actualWateringSec)+' · '+liters(z.estimatedWaterMl);else if(i===s.currentStepIndex)detail.textContent=Number(s.state)===5?'等待开阀':'正在执行 · '+(Number(z.targetWaterMl)?'已出 '+liters(z.estimatedWaterMl):'剩余 '+duration(s.currentZoneRemainingSec));else detail.textContent='等待执行'}});var nextGeneration=Number(s.flowHistoryGeneration||0),nextSerial=Number(s.flowSampleSerial||0);if(nextGeneration!==generation){generation=nextGeneration;loadHistory()}else if(nextSerial!==serial){if(nextSerial===serial+1){samples.push(Number(s.currentFlowMlPerMinute)||0);if(samples.length>120)samples.shift();serial=nextSerial;draw()}else loadHistory()}}function finished(){try{sessionStorage.setItem('irrigationJustFinished','1')}catch(ignore){}location.reload()}function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active){finished();return}update(s);setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}window.addEventListener('resize',draw);loadHistory();setTimeout(poll,1000)})();</script>)HTML");
    endPage();
}

void IrrigationWeb::plans() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_plans")) return;
        bool success = false;
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
        redirectResult("/irrigation/plans", success);
        return;
    }
    if (!beginPage("浇水计划", "管理自动执行，并为首页手动浇水提供可编辑的时长模板")) return;
    Esp32BaseWeb::sendChunk(
        "<style>"
        ".plan-toolbar{display:flex;align-items:center;justify-content:flex-end;margin:-4px 0 12px}"
        ".plan-auto{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:14px 14px 14px 16px;border:1px solid #cfe1e5;border-left:4px solid var(--eb-ok);border-radius:10px;background:linear-gradient(135deg,#f4faf7,#fff)}"
        ".plan-auto.paused{border-color:#efcf96;border-left-color:var(--eb-warn);background:linear-gradient(135deg,var(--eb-warn-soft),#fff)}"
        ".plan-auto h3{margin:0 0 4px;font-size:17px}.plan-auto p{margin:0;color:var(--eb-muted)}.plan-auto-actions{display:flex;gap:8px;flex:0 0 auto}.plan-auto-actions form{margin:0}"
        ".plan-pause-modal{width:min(620px,calc(100vw - 28px))}.plan-pause-options{display:grid;gap:10px;margin-top:14px}.plan-pause-option{padding:14px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft)}.plan-pause-option h3{margin:0 0 3px;font-size:15px}.plan-pause-option>small{display:block;margin-bottom:12px;color:var(--eb-muted)}.plan-pause-fields{display:grid;gap:12px}.plan-pause-field{margin:0}.plan-pause-field input{width:100%;max-width:none;margin:5px 0 0}.plan-pause-shortcuts{display:flex;flex-wrap:wrap;gap:7px;margin-top:7px}.plan-pause-shortcuts button{min-height:32px;padding:5px 10px}.plan-pause-shortcuts button.selected{border-color:var(--eb-primary);background:var(--eb-primary-soft);color:var(--eb-primary)}.plan-pause-unavailable{margin:0;padding:9px 11px;border-radius:7px;background:var(--eb-warn-soft);color:var(--eb-warn);font-size:13px}.plan-pause-submit{display:flex;justify-content:flex-end;margin-top:12px}.plan-pause-indefinite{display:flex;align-items:center;justify-content:space-between;gap:14px}.plan-pause-indefinite h3{margin-bottom:3px}.plan-pause-indefinite p{margin:0}.plan-pause-indefinite form{margin:0;flex:0 0 auto}"
        ".plan-toolbar .btnlink{min-height:36px}"
        ".plan-list{display:grid;gap:12px}"
        ".plan-card{padding:16px;border:1px solid #cfe1e5;border-left:4px solid var(--eb-ok);border-radius:10px;background:linear-gradient(135deg,#f6fbfc 0,#fff 58%)}"
        ".plan-card.disabled{border-color:#d8dee5;border-left-color:#8b96a6;background:linear-gradient(135deg,#f3f5f7 0,#fff 62%)}"
        ".plan-card.disabled .plan-card-body{border-top-color:#dfe3e8}.plan-card.disabled .plan-time-chip{border-color:#d8dee5;background:#f7f8f9;color:#667085}.plan-card.disabled .plan-zone-item{background:#f7f8f9}.plan-status-off{border-color:#d1d6dd!important;background:#eef1f4!important;color:#566170!important}"
        ".plan-card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}"
        ".plan-card-title{min-width:0}.plan-card-title-row{display:flex;align-items:center;flex-wrap:wrap;gap:8px}"
        ".plan-card-title h3{margin:0;font-size:18px;line-height:1.35;overflow-wrap:anywhere}"
        ".plan-card-title small{display:block;margin-top:3px;color:var(--eb-muted)}"
        ".plan-card-edit{flex:0 0 auto;min-width:68px}"
        ".plan-card-body{display:grid;grid-template-columns:minmax(170px,.65fr) minmax(0,2fr);gap:18px;margin-top:14px;padding-top:14px;border-top:1px solid var(--eb-line-soft)}"
        ".plan-summary-label{display:block;margin-bottom:7px;color:var(--eb-muted);font-size:12px;font-weight:650}"
        ".plan-time-list{display:flex;flex-wrap:wrap;gap:7px}"
        ".plan-time-chip{display:inline-flex;align-items:center;min-height:30px;padding:4px 10px;border:1px solid #cfe1e5;border-radius:999px;background:#fff;color:var(--eb-primary);font-weight:650;font-variant-numeric:tabular-nums}"
        ".plan-empty-value{color:var(--eb-muted)}"
        ".plan-zone-summary{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:7px}"
        ".plan-zone-item{display:flex;align-items:baseline;justify-content:space-between;gap:10px;padding:8px 10px;border:1px solid var(--eb-line-soft);border-radius:7px;background:#fff}"
        ".plan-zone-item b{min-width:0;font-size:13px;font-weight:600;overflow-wrap:anywhere}.plan-zone-item span{flex:0 0 auto;color:var(--eb-primary);font-weight:650;white-space:nowrap}"
        ".plan-zone-item.zero span{color:var(--eb-muted);font-weight:500}"
        ".plan-modal{width:min(820px,calc(100vw - 28px))}"
        ".plan-editor{display:grid;gap:14px;margin-top:12px}"
        ".plan-group{padding:14px;border:1px solid var(--eb-line);border-radius:9px;background:var(--eb-soft)}"
        ".plan-group h3{margin:0 0 3px;font-size:15px}"
        ".plan-group>small{display:block;margin-bottom:12px;color:var(--eb-muted)}"
        ".plan-basic{display:grid;grid-template-columns:minmax(0,2fr) minmax(220px,1fr);gap:14px}"
        ".plan-basic .field,.plan-zone{margin:0}"
        ".plan-basic input[type=text]{margin-bottom:0}"
        ".plan-switch{padding:10px 12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}"
        ".plan-switch label{display:flex;align-items:center;gap:8px;margin:0}"
        ".plan-switch small{display:block;margin-top:5px;color:var(--eb-muted)}"
        ".plan-times,.plan-zones{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}"
        ".plan-time,.plan-zone{padding:10px 12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}"
        ".plan-time-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center}"
        ".plan-time input[type=time]{width:100%;min-width:0;min-height:34px;margin:0;padding:5px 8px}"
        ".plan-time .clear-time{min-width:56px}"
        ".plan-zone input{width:100%;max-width:none;margin:0}"
        ".plan-zone small{display:block;margin-top:5px;color:var(--eb-muted)}"
        ".plan-form-actions{padding-top:2px}"
        "@media(max-width:760px){"
        ".plan-auto{align-items:stretch;flex-direction:column}.plan-auto-actions,.plan-auto-actions form,.plan-auto-actions .btnlink{width:100%}.plan-pause-indefinite{align-items:stretch;flex-direction:column}.plan-pause-indefinite form,.plan-pause-indefinite input,.plan-pause-submit input{width:100%}"
        ".plan-toolbar{margin-top:0}.plan-toolbar .btnlink{width:100%}"
        ".plan-card{padding:13px}.plan-card-head{gap:10px}.plan-card-title h3{font-size:17px}.plan-card-edit{min-width:60px}"
        ".plan-card-body{grid-template-columns:1fr;gap:13px;margin-top:12px;padding-top:12px}"
        ".plan-zone-summary{grid-template-columns:repeat(2,minmax(0,1fr))}"
        ".plan-modal{width:calc(100vw - 20px)}"
        ".plan-basic,.plan-times,.plan-zones{grid-template-columns:1fr}"
        ".plan-group{padding:12px}"
        "}"
        "</style>");
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
            Esp32BaseWeb::sendChunk("<dialog id='plan-pause' class='panel eb-modal plan-pause-modal' data-eb-light-dismiss='1'><h2>暂停自动浇水</h2><p class='muted'>只影响之后的自动计划，不停止当前任务，也不影响首页的手动浇水。</p><div class='plan-pause-options'><form id='plan-pause-timed' class='plan-pause-option' method='post' action='/irrigation/plans' data-now-epoch='");
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
            Esp32BaseWeb::sendChunk(R"HTML(<script>(function(){
var form=document.getElementById('plan-pause-timed'),hours=document.getElementById('pause-hours'),resume=document.getElementById('pause-resume-at');if(!form||!hours||!resume)return;var base=Number(form.dataset.nowEpoch)||0,started=performance.now(),hourButtons=Array.prototype.slice.call(form.querySelectorAll('[data-pause-hours]')),dayButtons=Array.prototype.slice.call(form.querySelectorAll('[data-pause-day]'));
function nowEpoch(){return base+Math.floor((performance.now()-started)/1000)}function pad(v){return String(v).padStart(2,'0')}function formatUtc8(epoch){var d=new Date((epoch+28800)*1000);return d.getUTCFullYear()+'-'+pad(d.getUTCMonth()+1)+'-'+pad(d.getUTCDate())+'T'+pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())}function selectHours(value){hourButtons.forEach(function(button){button.classList.toggle('selected',Number(button.dataset.pauseHours)===value)});dayButtons.forEach(function(button){button.classList.remove('selected')})}function clearHours(){hours.value='';hours.setCustomValidity('');hourButtons.forEach(function(button){button.classList.remove('selected')})}function updateFromHours(value){if(hours.value===''){hours.setCustomValidity('');hourButtons.forEach(function(button){button.classList.remove('selected')});return}if(!Number.isInteger(value)||value<1||value>8760){hours.setCustomValidity('请输入 1～8760 的整数小时。');hourButtons.forEach(function(button){button.classList.remove('selected')});return}hours.setCustomValidity('');resume.value=formatUtc8(nowEpoch()+value*3600);selectHours(value)}
hours.addEventListener('input',function(){updateFromHours(Number(hours.value))});hourButtons.forEach(function(button){button.addEventListener('click',function(){var value=Number(button.dataset.pauseHours);hours.value=value;updateFromHours(value)})});dayButtons.forEach(function(button){button.addEventListener('click',function(){var shifted=new Date((nowEpoch()+28800)*1000),day=Number(button.dataset.pauseDay),target=Date.UTC(shifted.getUTCFullYear(),shifted.getUTCMonth(),shifted.getUTCDate()+day,6,0,0)/1000-28800;clearHours();resume.value=formatUtc8(target);dayButtons.forEach(function(item){item.classList.toggle('selected',item===button)})})});resume.addEventListener('input',function(){clearHours();dayButtons.forEach(function(button){button.classList.remove('selected')})});window.submitTimedPause=function(target){if(!target.reportValidity())return false;return confirm('确认暂停自动浇水至 '+resume.value.replace('T',' ')+'（UTC+8）？')&&once(target)};
})();</script>)HTML");
        }
        int firstAvailable = -1;
        bool anyConfigured = false;
        for (const WateringPlan& plan : config->plans) {
            if (plan.configured) anyConfigured = true;
            else if (firstAvailable < 0) firstAvailable = plan.id - 1;
        }
        Esp32BaseWeb::beginPanel("计划列表");
        Esp32BaseWeb::sendChunk("<p class='muted'>计划用于自动执行，也可以在首页的“手动浇水”中填入各路时长；填入后可临时修改，不会反向保存。</p>");
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

        for (const WateringPlan& plan : config->plans) {
            if (!plan.configured && static_cast<int>(plan.id - 1U) != firstAvailable) continue;
            Esp32BaseWeb::sendChunk("<dialog id='plan-"); sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("' class='panel eb-modal plan-modal' data-eb-light-dismiss='1'><h2>");
            Esp32BaseWeb::writeHtmlEscaped(plan.configured ? "编辑浇水计划" : "新增浇水计划");
            Esp32BaseWeb::sendChunk("</h2><form method='post' action='/irrigation/plans' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='plan_id' value='"); sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><div class='plan-editor'><section class='plan-group'><h3>基本信息</h3><small>计划名称用于运行和浇水记录；自动执行关闭后仍可手动启动。</small><div class='plan-basic'><p class='field'><label>计划名称</label><input type='text' name='name' maxlength='63' required value='");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("'></p><div class='plan-switch'><label><input type='checkbox' name='schedule_enabled' value='1' ");
            if (plan.scheduleEnabled) Esp32BaseWeb::sendChunk("checked");
            Esp32BaseWeb::sendChunk("> 自动执行</label><small>仅控制定时执行；关闭后仍可在首页作为手动浇水模板。</small></div></div></section><section class='plan-group'><h3>自动执行时间</h3><small>每天最多执行 4 次；留空表示不使用。点击“清除”可删除已经设置的时间。</small><div class='plan-times'>");
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
            if (plan.configured) { Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans' onsubmit=\"return confirm('确认删除该计划？')&&once(this)\"><input type='hidden' name='action' value='delete'><input type='hidden' name='plan_id' value='"); sendUnsigned(plan.id); Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision); Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='删除计划'></div></form>"); }
            Esp32BaseWeb::sendChunk("</dialog>");
        }
    }
    endPage();
}

void IrrigationWeb::zones() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_zone_save")) return;
        bool success = false;
        if (actionIs("save")) {
            success = saveZoneFromRequest();
        } else if (actionIs("start_single_output")) {
            const IrrigationConfig* config = g_app->configuration();
            uint32_t zoneId = 0;
            char mode[12]{};
            if (config &&
                uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId) &&
                getParam("target_mode", mode, sizeof(mode))) {
                uint32_t targetDurationSec = 0;
                uint32_t targetWaterMl = 0;
                if (std::strcmp(mode, "time") == 0) {
                    uint32_t minutes = 0;
                    uint32_t seconds = 0;
                    if (uintParam("duration_minutes",
                                  0,
                                  config->runLimits.maximumZoneDurationMinutes,
                                  minutes) &&
                        uintParam("duration_seconds", 0, 59, seconds)) {
                        targetDurationSec = minutes * 60U + seconds;
                        if (targetDurationSec == 0 ||
                            targetDurationSec >
                                static_cast<uint32_t>(config->runLimits.maximumZoneDurationMinutes) * 60U) {
                            targetDurationSec = 0;
                        }
                    }
                } else if (std::strcmp(mode, "volume") == 0) {
                    char liters[24]{};
                    if (getParam("target_liters", liters, sizeof(liters)) &&
                        IrrigationConfigRules::parseWaterVolumeLiters(
                            liters, targetWaterMl) &&
                        targetWaterMl % 100U == 0 &&
                        targetWaterMl <=
                            static_cast<uint32_t>(config->runLimits.maximumSingleOutputLiters) * 1000U) {
                        targetDurationSec =
                            static_cast<uint32_t>(config->runLimits.maximumZoneDurationMinutes) * 60U;
                    } else {
                        targetWaterMl = 0;
                    }
                }
                success = targetDurationSec != 0 &&
                          g_app->startSingleOutput(static_cast<uint8_t>(zoneId),
                                                   targetDurationSec,
                                                   targetWaterMl) ==
                              WateringStartResult::Started;
            }
        } else if (actionIs("stop_single_output")) {
            const WateringStatus status = g_app->wateringStatus();
            success = status.active &&
                      status.source == WateringSource::SingleOutput &&
                      status.purpose == WateringPurpose::Normal &&
                      g_app->stopWatering();
        }
        redirectResult("/irrigation/zones", success);
        return;
    }
    if (!beginPage("水路设置", "启用实际安装的水路，并学习各水路的基准流量")) return;
    Esp32BaseWeb::sendChunk(
        "<style>"
        ".zone-meter{display:flex;align-items:center;justify-content:space-between;gap:16px}"
        ".zone-meter-main{display:flex;align-items:baseline;flex-wrap:wrap;gap:8px}"
        ".zone-meter-label{margin:0;color:var(--eb-muted);font-size:13px}"
        ".zone-meter-value{display:flex;align-items:baseline;gap:6px;color:var(--eb-primary);line-height:1}"
        ".zone-meter-number{font-size:18px;font-weight:650}"
        ".zone-meter-unit{font-size:14px;color:var(--eb-muted)}"
        ".zone-table th{font-weight:600}.zone-table td{font-weight:400}"
        ".zone-table .tag{font-weight:500}"
        ".zone-table .btnlink{font-weight:500}"
        ".single-output-intro{margin:0 0 15px;color:var(--eb-muted);font-size:13px;line-height:1.6}"
        ".single-output-group{margin:0 0 16px}.single-output-group>span{display:block;margin-bottom:7px;font-size:13px;font-weight:600}"
        ".single-output-zones,.single-output-modes{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}"
        ".single-output-option{position:relative;display:block;margin:0}.single-output-option input{position:absolute;opacity:0;pointer-events:none}"
        ".single-output-card{display:block;height:100%;padding:12px;border:1px solid var(--eb-line);border-radius:9px;background:#fff;cursor:pointer}"
        ".single-output-card b,.single-output-card small{display:block}.single-output-card small{margin-top:3px;color:var(--eb-muted);font-size:11px;line-height:1.45}"
        ".single-output-option input:checked+.single-output-card{border-color:var(--eb-primary);background:var(--eb-primary-soft);box-shadow:0 0 0 1px var(--eb-primary)}"
        ".single-output-option input:focus-visible+.single-output-card{outline:2px solid var(--eb-primary);outline-offset:2px}"
        ".single-output-target{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:10px}.single-output-target .field{margin:0}.single-output-target input{width:100%;max-width:none}"
        ".single-output-estimate{min-height:24px;margin:12px 0 0;padding:9px 11px;border-radius:7px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px}"
        ".single-output-live{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.single-output-live>div{padding:11px;border-radius:8px;background:var(--eb-soft)}.single-output-live span,.single-output-live b{display:block}.single-output-live span{color:var(--eb-muted);font-size:11px}.single-output-live b{margin-top:3px;font-size:15px;font-weight:550}"
        ".single-output-progress{height:8px;margin-top:14px;border-radius:999px;background:var(--eb-soft);overflow:hidden}.single-output-progress>span{display:block;height:100%;width:0;background:var(--eb-primary);transition:width .3s}"
        "@media(max-width:760px){"
        ".zone-meter{align-items:stretch;flex-direction:column;gap:10px}.zone-meter .btnlink{width:100%}"
        ".single-output-zones,.single-output-modes,.single-output-live{grid-template-columns:repeat(2,minmax(0,1fr))}.single-output-target{grid-template-columns:1fr}"
        "}"
        "</style>");
    const IrrigationConfig* config = g_app->configuration();
    if (config) {
        char coefficient[20]{};
        IrrigationConfigRules::formatPulsesPerLiter(
            config->flowMeter.pulsesPerLiterX100, coefficient, sizeof(coefficient));
        Esp32BaseWeb::beginPanel("水路列表");
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
        Esp32BaseWeb::sendChunk("</span><span class='zone-meter-unit'>P/L</span></div></div><a class='btnlink ok compact' href='/irrigation/zones/flow-calibration'>流量计校准</a></div>");
        Esp32BaseWeb::endPanel();
        const WateringStatus outputStatus = g_app->wateringStatus();
        Esp32BaseWeb::beginPanel("单次出水");
        if (outputStatus.active &&
            outputStatus.source == WateringSource::SingleOutput &&
            outputStatus.purpose == WateringPurpose::Normal) {
            const ZoneWateringSummary& zone = outputStatus.zones[outputStatus.currentStepIndex];
            Esp32BaseWeb::sendChunk("<p class='single-output-intro'>正在执行单次出水；保留无流量、流量异常和最长运行时间保护。</p><div id='single-output-live' class='single-output-live' data-target-water='");
            sendUnsigned(zone.targetWaterMl);
            Esp32BaseWeb::sendChunk("' data-target-time='"); sendUnsigned(zone.plannedDurationSec);
            Esp32BaseWeb::sendChunk("'><div><span>当前水路</span><b>");
            if (zone.zoneId >= 1 && zone.zoneId <= config->zones.size()) {
                Esp32BaseWeb::writeHtmlEscaped(config->zones[zone.zoneId - 1U].name.data());
            } else {
                Esp32BaseWeb::sendChunk("水路 "); sendUnsigned(zone.zoneId);
            }
            Esp32BaseWeb::sendChunk("</b></div><div><span>目标</span><b id='single-live-target'>—</b></div><div><span>实际出水</span><b id='single-live-time'>—</b></div><div><span>估算水量</span><b id='single-live-water'>—</b></div></div><div class='single-output-progress'><span id='single-live-progress'></span></div><div class='actions'><form method='post' action='/irrigation/zones' onsubmit=\"return confirm('确认停止当前单次出水？')&&once(this)\"><input type='hidden' name='action' value='stop_single_output'><input class='danger' type='submit' value='停止出水'></form></div><script>(function(){var live=document.getElementById('single-output-live'),bar=document.getElementById('single-live-progress');if(!live)return;function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}function duration(v){v=Math.max(0,Number(v)||0);if(v<60)return v+' 秒';return Math.floor(v/60)+' 分 '+v%60+' 秒'}function liters(v){return(Math.max(0,Number(v)||0)/1000).toFixed(1)+' L'}var targetWater=Number(live.dataset.targetWater)||0,targetTime=Number(live.dataset.targetTime)||0;set('single-live-target',targetWater?liters(targetWater):duration(targetTime));function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active||Number(s.source)!==1){location.reload();return}var z=s.zones&&s.zones[s.currentStepIndex]||{};set('single-live-time',duration(s.currentZoneElapsedSec));set('single-live-water',liters(z.estimatedWaterMl));var value=targetWater?Number(z.estimatedWaterMl):Number(s.currentZoneElapsedSec),target=targetWater||targetTime;if(bar)bar.style.width=(target?Math.min(100,Math.round(value*100/target)):0)+'%';setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}poll()})();</script>");
        } else if (outputStatus.active) {
            Esp32BaseWeb::sendChunk("<p class='single-output-intro'>设备正在执行其他浇水或维护任务，结束后才能开始单次出水。</p><a class='btnlink secondary compact' href='/irrigation'>查看当前任务</a>");
        } else {
            Esp32BaseWeb::sendChunk("<p class='single-output-intro'>选择一条已启用水路，按时长或目标估算水量自动停止；不会修改浇水计划。</p><form id='single-output-form' method='post' action='/irrigation/zones' onsubmit='return submitSingleOutput(this)'><input type='hidden' name='action' value='start_single_output'><div class='single-output-group'><span>选择水路</span><div class='single-output-zones'>");
            bool firstEnabledZone = true;
            for (const ZoneConfig& zone : config->zones) {
                if (!zone.enabled) continue;
                uint32_t baselineMlPerMinute = 0;
                FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
                    zone.baselinePulseRateX10000,
                    config->flowMeter.pulsesPerLiterX100,
                    baselineMlPerMinute);
                Esp32BaseWeb::sendChunk("<label class='single-output-option'><input type='radio' name='zone_id' value='"); sendUnsigned(zone.id);
                Esp32BaseWeb::sendChunk("' data-flow='"); sendUnsigned(baselineMlPerMinute); Esp32BaseWeb::sendChunk("'");
                if (firstEnabledZone) Esp32BaseWeb::sendChunk(" checked");
                Esp32BaseWeb::sendChunk("><span class='single-output-card'><b>"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
                Esp32BaseWeb::sendChunk("</b><small>水路 "); sendUnsigned(zone.id);
                if (baselineMlPerMinute != 0) {
                    Esp32BaseWeb::sendChunk(" · 基准 ");
                    char baseline[20]{};
                    IrrigationConfigRules::formatLitersPerMinute(
                        baselineMlPerMinute, baseline, sizeof(baseline));
                    Esp32BaseWeb::writeHtmlEscaped(baseline);
                    Esp32BaseWeb::sendChunk(" L/min");
                }
                Esp32BaseWeb::sendChunk("</small></span></label>");
                firstEnabledZone = false;
            }
            if (firstEnabledZone) {
                Esp32BaseWeb::sendChunk("<p class='muted'>当前没有已启用水路，请先在上方水路列表中启用实际水路。</p>");
            }
            Esp32BaseWeb::sendChunk("</div></div><div class='single-output-group'><span>目标方式</span><div class='single-output-modes'><label class='single-output-option'><input type='radio' name='target_mode' value='time' checked><span class='single-output-card'><b>按时长</b><small>到达设定时间后自动停止</small></span></label><label class='single-output-option'><input type='radio' name='target_mode' value='volume'><span class='single-output-card'><b>按水量</b><small>达到目标估算水量后自动停止</small></span></label></div><div id='single-time-target' class='single-output-target'><p class='field'><label>分钟</label><input type='number' name='duration_minutes' min='0' max='"); sendUnsigned(config->runLimits.maximumZoneDurationMinutes); Esp32BaseWeb::sendChunk("' step='1' value='1' inputmode='numeric'></p><p class='field'><label>秒</label><input type='number' name='duration_seconds' min='0' max='59' step='1' value='0' inputmode='numeric'></p></div><div id='single-volume-target' class='single-output-target' hidden><p class='field'><label>目标水量（L）</label><input type='number' name='target_liters' min='0.1' max='"); sendUnsigned(config->runLimits.maximumSingleOutputLiters); Esp32BaseWeb::sendChunk("' step='0.1' value='5.0' inputmode='decimal' disabled></p></div><p id='single-output-estimate' class='single-output-estimate' hidden></p></div><p class='muted'>按水量停止使用当前流量计系数估算，实际结果可能受校准、阀门响应和管路余流影响；所有模式最长运行 "); sendUnsigned(config->runLimits.maximumZoneDurationMinutes); Esp32BaseWeb::sendChunk(" 分钟。</p><div class='actions'><input type='submit' value='开始出水'></div></form><script>(function(){var form=document.getElementById('single-output-form'),time=document.getElementById('single-time-target'),volume=document.getElementById('single-volume-target'),estimate=document.getElementById('single-output-estimate');if(!form)return;function selected(name){return form.querySelector('input[name=\"'+name+'\"]:checked')}function update(){var mode=selected('target_mode'),byVolume=mode&&mode.value==='volume',timeInputs=time.querySelectorAll('input'),volumeInput=volume.querySelector('input');time.hidden=byVolume;volume.hidden=!byVolume;timeInputs.forEach(function(i){i.disabled=byVolume});volumeInput.disabled=!byVolume;var zone=selected('zone_id'),flow=zone?Number(zone.dataset.flow)||0:0;if(!flow){estimate.hidden=true;estimate.textContent='';return}if(byVolume){var liters=Number(volumeInput.value)||0;if(liters<=0){estimate.hidden=true;return}var seconds=Math.ceil(liters*60000/flow),maximum="); sendUnsigned(static_cast<uint32_t>(config->runLimits.maximumZoneDurationMinutes) * 60U); Esp32BaseWeb::sendChunk(";estimate.textContent='按当前基准流量估算：约需 '+(seconds<60?seconds+' 秒':Math.floor(seconds/60)+' 分 '+seconds%60+' 秒')+(seconds>maximum?'；可能无法在最长运行时间内达到目标水量':'');estimate.hidden=false}else{var minutes=Number(timeInputs[0].value)||0,seconds=Number(timeInputs[1].value)||0,total=minutes*60+seconds;if(total<=0){estimate.hidden=true;return}estimate.textContent='按当前基准流量估算：约出水 '+(flow*total/60000).toFixed(1)+' L';estimate.hidden=false}}form.querySelectorAll('input').forEach(function(i){i.addEventListener('input',update);i.addEventListener('change',update)});window.submitSingleOutput=function(){var mode=selected('target_mode'),zone=selected('zone_id');if(!mode||!zone)return false;var detail;if(mode.value==='volume'){detail='目标估算水量 '+Number(form.target_liters.value).toFixed(1)+' L'}else{var total=(Number(form.duration_minutes.value)||0)*60+(Number(form.duration_seconds.value)||0);if(total<1){alert('出水时长至少为 1 秒。');return false}detail='目标时长 '+(total<60?total+' 秒':Math.floor(total/60)+' 分 '+total%60+' 秒')}return confirm('确认从所选水路开始出水，'+detail+'？')&&once(form)};update()})();</script>");
        }
        Esp32BaseWeb::endPanel();
        for (const ZoneConfig& zone : config->zones) {
            Esp32BaseWeb::sendChunk("<dialog id='zone-"); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("' class='panel eb-modal' data-eb-light-dismiss='1'><h2>修改水路 "); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("</h2><form method='post' action='/irrigation/zones' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='zone_id' value='"); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><div class='fieldgrid'><p class='field full'><label>水路名称</label><input name='name' maxlength='63' required value='"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
            Esp32BaseWeb::sendChunk("'><small>用于计划、运行和记录页面显示。</small></p><p class='field full'><label><input type='checkbox' name='enabled' value='1' "); if (zone.enabled) Esp32BaseWeb::sendChunk("checked");
            Esp32BaseWeb::sendChunk("> 启用这条水路</label><small>未安装的水路保持关闭，正常使用页面不会显示。</small></p></div><div class='actions'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='保存'></div></form>");
            Esp32BaseWeb::sendChunk("</dialog>");
        }
    }
    endPage();
}

void IrrigationWeb::flowCalibration() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_flow_calibration")) return;
        bool success = false;
        const WateringStatus status = g_app->wateringStatus();
        if (actionIs("start")) {
            uint32_t zoneId = 0;
            char stopMode[12]{};
            success = uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId) &&
                      getParam("stop_mode", stopMode, sizeof(stopMode));
            uint32_t durationMinutes = 10;
            uint32_t targetWaterMl = 0;
            if (success && std::strcmp(stopMode, "time") == 0) {
                success = uintParam("duration_minutes", 1, 10, durationMinutes);
            } else if (success && std::strcmp(stopMode, "volume") == 0) {
                char targetLiters[20]{};
                success = getParam("target_liters", targetLiters, sizeof(targetLiters)) &&
                          IrrigationConfigRules::parseWaterVolumeLiters(
                              targetLiters, targetWaterMl);
            } else {
                success = false;
            }
            success = success &&
                      g_app->startFlowCalibration(static_cast<uint8_t>(zoneId),
                                                  static_cast<uint16_t>(durationMinutes),
                                                  targetWaterMl) ==
                          WateringStartResult::Started;
        } else if (actionIs("stop")) {
            success = status.active && status.purpose == WateringPurpose::FlowCalibration &&
                      g_app->stopWatering();
        } else if (actionIs("measurement")) {
            uint32_t measuredMl = 0;
            success = uintParam("measured_ml",
                                FlowCalibrationService::kMinimumMeasuredWaterMl,
                                FlowCalibrationService::kMaximumMeasuredWaterMl,
                                measuredMl) &&
                      g_app->submitFlowCalibrationMeasurement(measuredMl);
        } else if (actionIs("invalid")) {
            success = g_app->markFlowCalibrationSampleInvalid();
        } else if (actionIs("discard")) {
            success = g_app->discardFlowCalibrationMeasurement();
        } else if (actionIs("update")) {
            uint32_t sampleNumber = 0;
            uint32_t measuredMl = 0;
            success = uintParam("sample", 1, FlowCalibrationService::kMaximumSamples,
                                sampleNumber) &&
                      uintParam("measured_ml",
                                FlowCalibrationService::kMinimumMeasuredWaterMl,
                                FlowCalibrationService::kMaximumMeasuredWaterMl,
                                measuredMl) &&
                      g_app->updateFlowCalibrationMeasurement(
                          static_cast<uint8_t>(sampleNumber - 1U), measuredMl);
        } else if (actionIs("delete")) {
            uint32_t sampleNumber = 0;
            success = uintParam("sample", 1, FlowCalibrationService::kMaximumSamples,
                                sampleNumber) &&
                      g_app->deleteFlowCalibrationSample(
                          static_cast<uint8_t>(sampleNumber - 1U));
        } else if (actionIs("apply")) {
            success = g_app->applyFlowCalibrationResult();
        } else if (actionIs("parameters")) {
            char coefficientText[20]{};
            FlowMeterConfig parameters{};
            success = getParam("coefficient", coefficientText, sizeof(coefficientText)) &&
                      IrrigationConfigRules::parsePulsesPerLiter(
                          coefficientText, parameters.pulsesPerLiterX100) &&
                      uintParam("startup_pulses", 0, 10000000,
                                parameters.calibrationStartupPulseCount) &&
                      uintParam("startup_water_ml", 0, 1000000,
                                parameters.calibrationStartupWaterMl) &&
                      g_app->saveFlowCalibrationParameters(parameters);
        } else if (actionIs("clear")) {
            g_app->resetFlowCalibration();
            success = !g_app->wateringStatus().active;
        }
        redirectResult("/irrigation/zones/flow-calibration", success);
        return;
    }
    if (!beginPage("流量计校准", "识别稳定出水阶段，用多组实测总水量计算稳态流量系数")) return;
    Esp32BaseWeb::sendChunk(
        "<style>"
        ".cal-current-toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:9px}.cal-current-toolbar p{margin:0;color:var(--eb-muted);font-size:12px;line-height:1.5}.cal-current-toolbar button{flex:0 0 auto;white-space:nowrap}.cal-current{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0;align-items:stretch;border:1px solid var(--eb-line-soft);border-radius:9px;background:#fff;overflow:hidden}"
        ".cal-current-fact{min-width:0;padding:10px 13px;border-right:1px solid var(--eb-line-soft)}.cal-current-fact:last-child{border-right:0}.cal-current-label{display:block;color:var(--eb-muted);font-size:11px}.cal-current-value{display:block;margin-top:3px;color:var(--eb-text);font-size:17px;font-weight:400;line-height:1.35}.cal-current-unit{font-size:12px;color:var(--eb-muted)}"
        ".cal-steps{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-bottom:14px}"
        ".cal-step{display:grid;grid-template-columns:28px minmax(0,1fr);gap:9px;align-items:start;padding:12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft);font-size:13px;line-height:1.55}"
        ".cal-step-num{display:flex;align-items:center;justify-content:center;width:26px;height:26px;border-radius:50%;background:var(--eb-primary-soft);color:var(--eb-primary);font-weight:600}"
        ".cal-action{padding:13px 14px;border:1px solid var(--eb-line-soft);border-radius:10px;background:#fff}"
        ".cal-start-form{display:grid;grid-template-columns:minmax(620px,1.45fr) minmax(260px,.55fr);gap:18px;align-items:stretch}.cal-start-main{display:grid;grid-template-columns:minmax(190px,.58fr) minmax(360px,1.42fr);gap:16px;align-items:start}.cal-zone-field{margin:0}.cal-zone-field label,.cal-stop-group legend,.cal-stop-value label{font-weight:500}.cal-zone-field select{width:100%;margin-top:7px}.cal-stop-group{min-width:0;margin:0;padding:0;border:0}.cal-stop-group legend{margin:0 0 7px;padding:0}.cal-stop-options{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px}.cal-stop-option{position:relative;display:block;min-width:0;cursor:pointer}.cal-stop-option input{position:absolute;opacity:0;pointer-events:none}.cal-stop-card{display:block;height:100%;padding:11px 12px;border:1px solid var(--eb-line);border-radius:9px;background:#fff;transition:border-color .15s ease,background .15s ease,box-shadow .15s ease}.cal-stop-option input:checked+.cal-stop-card{border-color:var(--eb-primary);background:var(--eb-primary-soft);box-shadow:0 0 0 1px var(--eb-primary)}.cal-stop-option input:focus-visible+.cal-stop-card{outline:2px solid var(--eb-primary);outline-offset:2px}.cal-stop-title{display:flex;align-items:center;justify-content:space-between;gap:8px;color:var(--eb-text);font-size:14px;font-weight:500}.cal-stop-mark{display:flex;align-items:center;justify-content:center;width:18px;height:18px;border:1px solid var(--eb-line);border-radius:50%;background:#fff}.cal-stop-mark:after{content:'';width:8px;height:8px;border-radius:50%;background:transparent}.cal-stop-option input:checked+.cal-stop-card .cal-stop-mark{border-color:var(--eb-primary)}.cal-stop-option input:checked+.cal-stop-card .cal-stop-mark:after{background:var(--eb-primary)}.cal-stop-desc{display:block;margin-top:4px;color:var(--eb-muted);font-size:11px;line-height:1.4}.cal-stop-value{display:flex;align-items:flex-end;gap:10px;margin-top:10px;padding:10px 12px;border-radius:9px;background:var(--eb-soft)}.cal-stop-value[hidden]{display:none}.cal-stop-value>div{min-width:0}.cal-stop-value input{width:140px;margin-top:6px;background:#fff}.cal-stop-value small{display:block;padding-bottom:9px;color:var(--eb-muted);font-size:11px;line-height:1.4}.cal-start-picker>.actions{margin-top:12px}.cal-start-info{display:flex;flex-direction:column;justify-content:center;padding-left:18px;border-left:1px solid var(--eb-line-soft);color:var(--eb-muted);font-size:12px;line-height:1.55}.cal-start-info b{color:var(--eb-text);font-size:13px;font-weight:500}.cal-start-info span{display:block;margin-top:4px}"
        ".cal-live-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.cal-live-head h3{margin:0 0 3px;font-size:17px}.cal-live-head p{margin:0;color:var(--eb-muted);font-size:13px}"
        ".cal-live .metrics{grid-template-columns:repeat(auto-fit,minmax(140px,1fr))}.cal-live .metric b{font-weight:500}.cal-live .metric small{display:block;margin-top:4px;color:var(--eb-muted);font-size:11px;font-weight:400;line-height:1.4}.cal-live .actions{margin-top:12px}.cal-live-note{display:flex;flex-wrap:wrap;gap:8px 18px;margin-top:10px;padding:10px 12px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px;line-height:1.5}.cal-live-note b{color:var(--eb-text);font-weight:400}"
        ".cal-pending{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(280px,.75fr);gap:18px;align-items:start}.cal-pending-summary{padding:12px 13px;border-radius:9px;background:var(--eb-soft)}.cal-pending-summary h3{margin:0 0 3px;font-size:16px;font-weight:500}.cal-pending-route{margin:0 0 9px;color:var(--eb-muted);font-size:12px}.cal-phase-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.cal-phase{padding:8px 9px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.cal-phase-label{display:block;color:var(--eb-muted);font-size:11px}.cal-phase-value{display:block;margin-top:3px;color:var(--eb-text);font-size:13px;font-weight:400;line-height:1.4}.cal-phase-note{display:block;margin-top:2px;color:var(--eb-muted);font-size:11px;line-height:1.35}.cal-pending-total{margin:8px 0 0;color:var(--eb-muted);font-size:12px}.cal-pending-warning{color:var(--eb-danger)}.cal-pending label{font-weight:500}"
        ".cal-sample-toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px}.cal-counts{display:flex;flex-wrap:wrap;gap:7px}.cal-count{padding:5px 9px;border-radius:999px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px}.cal-count b{color:var(--eb-text);font-weight:500}.cal-count.ok{background:var(--eb-ok-soft);color:var(--eb-ok)}.cal-count.bad{background:var(--eb-danger-soft);color:var(--eb-danger)}"
        ".cal-sample-guide{margin:-2px 0 10px;color:var(--eb-muted);font-size:11px;line-height:1.5}.cal-sample-list{border:1px solid var(--eb-line-soft);border-radius:9px;overflow:hidden}.cal-sample-columns,.cal-sample{display:grid;grid-template-columns:minmax(95px,.65fr) minmax(135px,.9fr) minmax(160px,1.05fr) minmax(210px,1.35fr) minmax(200px,1.25fr) auto;gap:12px;align-items:center}.cal-sample-columns{padding:7px 11px;background:var(--eb-soft);color:var(--eb-muted);font-size:11px}.cal-sample{padding:10px 11px;background:#fff;border-top:1px solid var(--eb-line-soft)}.cal-sample:first-of-type{border-top:0}.cal-sample-cell{min-width:0}.cal-sample-main{display:block;color:var(--eb-text);font-size:13px;font-weight:400;line-height:1.4;overflow-wrap:anywhere}.cal-sample-sub{display:block;margin-top:2px;color:var(--eb-muted);font-size:11px;font-weight:400;line-height:1.4;overflow-wrap:anywhere}.cal-sample-sub.warn{color:var(--eb-danger)}.cal-sample-actions{display:flex;align-items:center;justify-content:flex-end;gap:6px;white-space:nowrap}.cal-sample-actions form{margin:0}.cal-sample-actions button,.cal-sample-actions input{min-height:30px;padding:5px 9px;font-size:12px}.cal-status{display:inline-flex;align-items:center;margin-left:5px;padding:2px 7px;border-radius:999px;font-size:11px;font-weight:400}.cal-status.ok{background:var(--eb-ok-soft);color:var(--eb-ok)}.cal-status.bad{background:var(--eb-danger-soft);color:var(--eb-danger)}"
        ".cal-empty{padding:20px;text-align:center;border:1px dashed #c9d6dc;border-radius:9px;background:var(--eb-soft)}"
        ".cal-empty-title{margin:0;color:#344054;font-size:15px;font-weight:500}"
        ".cal-empty-text{margin:5px 0 0;color:var(--eb-muted);font-size:13px}"
        ".cal-result{padding:12px 13px;border:1px solid var(--eb-line-soft);border-radius:9px;background:#fff}.cal-result-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px}.cal-result-head-left{display:flex;align-items:center;gap:8px}.cal-result-head p{margin:0;color:var(--eb-muted);font-size:12px}.cal-result-head form{margin:0}.cal-result-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));border:1px solid var(--eb-line-soft);border-radius:8px;overflow:hidden}.cal-result-fact{min-width:0;padding:9px 10px;border-right:1px solid var(--eb-line-soft);background:var(--eb-soft)}.cal-result-fact:last-child{border-right:0}.cal-result-fact span{display:block;color:var(--eb-muted);font-size:11px}.cal-result-fact b{display:block;margin-top:3px;color:var(--eb-text);font-size:15px;font-weight:400}.cal-result-meta{display:flex;flex-wrap:wrap;gap:5px 16px;margin-top:8px;color:var(--eb-muted);font-size:11px;line-height:1.5}.cal-result-note{margin:8px 0 0;color:var(--eb-muted);font-size:11px;line-height:1.5}"
        ".cal-sample-toolbar form{margin:0}.cal-sample-toolbar input{min-height:30px;padding:5px 10px;font-size:12px}"
        ".cal-edit{width:min(460px,calc(100vw - 28px))}.cal-edit h2{margin-bottom:4px}.cal-edit>p{margin-top:0}.cal-parameters{width:min(760px,calc(100vw - 28px))}.cal-parameter-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px;margin-top:16px}.cal-parameter-grid .field{min-width:0;margin:0;padding:12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft)}.cal-parameter-grid label{font-weight:500}.cal-parameter-grid input{width:100%;margin:6px 0 0;background:#fff}.cal-parameter-grid small{display:block;margin-top:5px;color:var(--eb-muted);font-size:11px;line-height:1.4}.cal-parameters .actions{margin-top:18px}"
        "@media(max-width:1150px){.cal-sample-columns{display:none}.cal-sample{grid-template-columns:repeat(2,minmax(0,1fr)) auto}.cal-sample-actions{grid-column:3;grid-row:1/span 3}}"
        "@media(max-width:900px){.cal-start-form{grid-template-columns:1fr}.cal-start-info{padding:11px 0 0;border-left:0;border-top:1px solid var(--eb-line-soft)}.cal-phase-grid{grid-template-columns:1fr}.cal-result-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.cal-result-fact:nth-child(2){border-right:0}.cal-result-fact:nth-child(-n+2){border-bottom:1px solid var(--eb-line-soft)}}"
        "@media(max-width:760px){"
        ".cal-steps{grid-template-columns:1fr}"
        ".cal-start-main{grid-template-columns:1fr}.cal-live .metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.cal-pending{grid-template-columns:1fr}.cal-sample-toolbar{align-items:flex-start}.cal-sample{grid-template-columns:1fr auto}.cal-sample-actions{grid-column:2;grid-row:1/span 5}.cal-parameter-grid{grid-template-columns:1fr}"
        "}"
        "@media(max-width:560px){.cal-stop-options{grid-template-columns:1fr}.cal-stop-value{align-items:stretch;flex-direction:column}.cal-stop-value input{width:100%}.cal-stop-value small{padding-bottom:0}.cal-current-toolbar{align-items:stretch;flex-direction:column}.cal-current-toolbar button{width:100%}.cal-current{grid-template-columns:1fr}.cal-current-fact{border-right:0;border-bottom:1px solid var(--eb-line-soft)!important}.cal-current-fact:last-child{border-bottom:0!important}.cal-result-head{align-items:flex-start}.cal-result-grid{grid-template-columns:1fr}.cal-result-fact{border-right:0;border-bottom:1px solid var(--eb-line-soft)!important}.cal-result-fact:last-child{border-bottom:0!important}.cal-sample{grid-template-columns:1fr}.cal-sample-actions{grid-column:1;grid-row:auto;justify-content:flex-start}}"
        "</style>");
    const IrrigationConfig* config = g_app->configuration();
    if (!config) { endPage(); return; }
    const WateringStatus status = g_app->wateringStatus();
    const FlowCalibrationService& calibration = g_app->flowCalibration();
    const bool active = status.active && status.purpose == WateringPurpose::FlowCalibration;
    const bool managementLocked = status.active || calibration.hasPendingMeasurement();
    char coefficient[20]{};
    IrrigationConfigRules::formatPulsesPerLiter(
        config->flowMeter.pulsesPerLiterX100, coefficient, sizeof(coefficient));
    Esp32BaseWeb::beginPanel("设备校准参数");
    Esp32BaseWeb::sendChunk("<div class='cal-current-toolbar'><p>三项设备参数统一保存；各水路的原始脉冲基准在水路学习中独立管理。</p>");
    if (!managementLocked) Esp32BaseWeb::sendChunk("<button class='secondary' type='button' onclick=\"document.getElementById('cal-parameters').showModal()\">修改参数</button>");
    Esp32BaseWeb::sendChunk("</div><div class='cal-current'><div class='cal-current-fact'><span class='cal-current-label'>启动脉冲</span><span class='cal-current-value'>");
    sendUnsigned(config->flowMeter.calibrationStartupPulseCount); Esp32BaseWeb::sendChunk(" <span class='cal-current-unit'>个</span>");
    Esp32BaseWeb::sendChunk("</span></div><div class='cal-current-fact'><span class='cal-current-label'>估算启动水量</span><span class='cal-current-value'>");
    sendUnsigned(config->flowMeter.calibrationStartupWaterMl); Esp32BaseWeb::sendChunk(" <span class='cal-current-unit'>mL</span>");
    Esp32BaseWeb::sendChunk("</span></div><div class='cal-current-fact'><span class='cal-current-label'>稳态流量系数</span><span class='cal-current-value'>"); Esp32BaseWeb::writeHtmlEscaped(coefficient); Esp32BaseWeb::sendChunk(" <span class='cal-current-unit'>P/L</span></span></div></div>");
    Esp32BaseWeb::endPanel();
    if (!managementLocked) {
        Esp32BaseWeb::sendChunk("<dialog id='cal-parameters' class='panel eb-modal cal-edit cal-parameters' data-eb-light-dismiss='1'><h2>修改校准参数</h2><p class='muted'>三项参数统一保存并读回核对；只有稳态流量系数参与流量和水量换算。</p><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='parameters'><div class='cal-parameter-grid'><p class='field'><label>启动脉冲（个）</label><input type='number' name='startup_pulses' min='0' max='10000000' step='1' required value='"); sendUnsigned(config->flowMeter.calibrationStartupPulseCount); Esp32BaseWeb::sendChunk("'><small>0 表示未设置。</small></p><p class='field'><label>估算启动水量（mL）</label><input type='number' name='startup_water_ml' min='0' max='1000000' step='1' required value='"); sendUnsigned(config->flowMeter.calibrationStartupWaterMl); Esp32BaseWeb::sendChunk("'><small>0 表示未设置。</small></p><p class='field'><label>稳态流量系数（P/L）</label><input type='number' name='coefficient' min='0.01' max='100000.00' step='0.01' required value='"); Esp32BaseWeb::writeHtmlEscaped(coefficient); Esp32BaseWeb::sendChunk("'><small>用于脉冲、流量和水量换算。</small></p></div><div class='actions'><button class='secondary' type='button' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='保存参数'></div></form></dialog>");
    }
    Esp32BaseWeb::beginPanel("开始校准");
    Esp32BaseWeb::sendChunk("<div class='cal-steps'><div class='cal-step'><span class='cal-step-num'>1</span><span>从完全停止状态开始接水，系统自动识别进入稳态的时刻。</span></div><div class='cal-step'><span class='cal-step-num'>2</span><span>停止后填写实测总水量；至少两个不同水量，建议三个样本。</span></div><div class='cal-step'><span class='cal-step-num'>3</span><span>仅用稳态阶段脉冲拟合，确认使用后才写入设备参数。</span></div></div><div class='cal-action'>");
    if (status.active && !active) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "设备正在执行其它任务", "请等待当前任务结束后再校准。");
    } else if (active) {
        const uint8_t zoneId = status.activeZoneId;
        const ZoneWateringSummary& liveZone = status.zones[status.currentStepIndex];
        const char* zoneName = zoneId >= 1 && zoneId <= config->zones.size()
                                   ? config->zones[zoneId - 1U].name.data()
                                   : "当前水路";
        char liveRate[24]{};
        formatSignedHundredths(liveZone.calibrationLatestPulseRateX100,
                               liveRate,
                               sizeof(liveRate));
        char liveSteadyRate[24]{};
        formatPulseRate(liveZone.calibrationSteadyPulses,
                        liveZone.calibrationSteadyDurationMs,
                        liveSteadyRate,
                        sizeof(liveSteadyRate));
        Esp32BaseWeb::sendChunk("<div id='cal-live' class='cal-live'><div class='cal-live-head'><div><h3>"); Esp32BaseWeb::writeHtmlEscaped(zoneName); Esp32BaseWeb::sendChunk("</h3><p id='cal-state'>"); Esp32BaseWeb::writeHtmlEscaped(wateringStateName(status.state)); Esp32BaseWeb::sendChunk("</p></div><span class='tag warn'>正在采样</span></div><div class='metrics'><div class='metric'><b id='cal-elapsed'>"); sendDuration(status.elapsedSec); Esp32BaseWeb::sendChunk("</b><span>已运行</span></div><div class='metric'><b id='cal-pulses'>"); sendUnsigned(status.pulseCount); Esp32BaseWeb::sendChunk("</b><span>完整脉冲</span></div><div class='metric'><b id='cal-water'>"); sendLiters(liveZone.estimatedWaterMl); Esp32BaseWeb::sendChunk("</b><span>估算出水量</span><small id='cal-target-note'>"); if (status.currentZoneTargetWaterMl != 0) { Esp32BaseWeb::sendChunk("目标 "); sendLiters(status.currentZoneTargetWaterMl); } else Esp32BaseWeb::sendChunk("按时间自动停止"); Esp32BaseWeb::sendChunk("</small></div><div class='metric'><b id='cal-steady-state'>");
        if (liveZone.calibrationSteadyDetected) {
            Esp32BaseWeb::sendChunk("已确认");
        } else if (!status.flowEstablished) {
            Esp32BaseWeb::sendChunk("等待水流");
        } else if (liveZone.calibrationCollectedWindows >=
                   liveZone.calibrationRequiredWindows) {
            Esp32BaseWeb::sendChunk("波动偏大");
        } else {
            Esp32BaseWeb::sendChunk("已采集 ");
            sendUnsigned(liveZone.calibrationCollectedWindows);
            Esp32BaseWeb::sendChunk(" / ");
            sendUnsigned(liveZone.calibrationRequiredWindows);
            Esp32BaseWeb::sendChunk(" 个窗口");
        }
        Esp32BaseWeb::sendChunk("</b><span>稳态确认进度</span><small id='cal-steady-help'>"); if (liveZone.calibrationSteadyDetected) Esp32BaseWeb::sendChunk("后续波动仍会继续监测"); else if (!status.flowEstablished) Esp32BaseWeb::sendChunk("检测到脉冲后开始统计"); else if (liveZone.calibrationCollectedWindows >= liveZone.calibrationRequiredWindows) { Esp32BaseWeb::sendChunk("最近 "); sendUnsigned(liveZone.calibrationRequiredWindows); Esp32BaseWeb::sendChunk(" 个窗口尚未满足波动要求，继续识别"); } else { Esp32BaseWeb::sendChunk("收满 "); sendUnsigned(liveZone.calibrationRequiredWindows); Esp32BaseWeb::sendChunk(" 个窗口后比较速率波动"); } Esp32BaseWeb::sendChunk("</small></div><div class='metric'><b id='cal-remaining'>"); sendDuration(status.currentZoneRemainingSec); Esp32BaseWeb::sendChunk("</b><span>剩余上限</span></div></div><div class='cal-live-note'><span>检测规则：<b>每个窗口 "); sendUnsigned(liveZone.calibrationWindowSec); Esp32BaseWeb::sendChunk(" 秒，需连续 "); sendUnsigned(liveZone.calibrationRequiredWindows); Esp32BaseWeb::sendChunk(" 个，窗口速率波动不超过 "); sendUnsigned(liveZone.calibrationAllowedVariationPercent); Esp32BaseWeb::sendChunk("%</b></span><span>启动阶段：<b id='cal-startup-phase'>"); if (liveZone.calibrationSteadyDetected) { sendMilliseconds(liveZone.calibrationSteadyStartedMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(liveZone.calibrationStartupPulses); Esp32BaseWeb::sendChunk(" 脉冲"); } else Esp32BaseWeb::sendChunk("等待稳态确认"); Esp32BaseWeb::sendChunk("</b></span><span>稳态阶段：<b id='cal-steady-phase'>"); if (liveZone.calibrationSteadyDetected) { sendMilliseconds(liveZone.calibrationSteadyDurationMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(liveZone.calibrationSteadyPulses); Esp32BaseWeb::sendChunk(" 脉冲 · "); Esp32BaseWeb::writeHtmlEscaped(liveSteadyRate); Esp32BaseWeb::sendChunk(" P/s"); } else Esp32BaseWeb::sendChunk("尚未开始"); Esp32BaseWeb::sendChunk("</b></span><span>最近窗口：<b id='cal-rate'>"); Esp32BaseWeb::writeHtmlEscaped(liveRate); Esp32BaseWeb::sendChunk(" P/s</b></span></div><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='stop'><div class='actions'><input class='danger' type='submit' value='停止本次采样'></div></form></div>");
    } else if (calibration.hasPendingMeasurement()) {
        const uint8_t pendingZoneId = calibration.pendingZoneId();
        const char* pendingZoneName = pendingZoneId >= 1 && pendingZoneId <= config->zones.size()
                                          ? config->zones[pendingZoneId - 1U].name.data()
                                          : "未知水路";
        const FlowCalibrationService::Sample* pending = calibration.pendingSample();
        char pendingSteadyRate[24]{};
        formatPulseRate(pending->steadyPulseCount, pending->steadyDurationMs,
                        pendingSteadyRate, sizeof(pendingSteadyRate));
        Esp32BaseWeb::sendChunk("<div class='cal-pending'><div class='cal-pending-summary'><h3>本次采样已停止</h3><p class='cal-pending-route'>采样水路："); Esp32BaseWeb::writeHtmlEscaped(pendingZoneName); Esp32BaseWeb::sendChunk("</p><div class='cal-phase-grid'><div class='cal-phase'><span class='cal-phase-label'>启动阶段</span><span class='cal-phase-value'>"); sendMilliseconds(pending->steadyStartedMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(pending->startupPulseCount); Esp32BaseWeb::sendChunk(" 脉冲</span><span class='cal-phase-note'>从开阀到进入稳态</span></div><div class='cal-phase'><span class='cal-phase-label'>稳态阶段</span><span class='cal-phase-value'>"); sendMilliseconds(pending->steadyDurationMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(pending->steadyPulseCount); Esp32BaseWeb::sendChunk(" 脉冲 · "); Esp32BaseWeb::writeHtmlEscaped(pendingSteadyRate); Esp32BaseWeb::sendChunk(" P/s</span><span class='cal-phase-note'>从进入稳态到停止命令</span></div></div><p class='cal-pending-total'>完整采样："); sendDuration(calibration.pendingElapsedSec()); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(calibration.pendingPulseCount()); Esp32BaseWeb::sendChunk(" 脉冲"); if (pending->stopPulseCount != 0) { Esp32BaseWeb::sendChunk("<br><span class='cal-pending-warning'>停止后仍检测到 "); sendUnsigned(pending->stopPulseCount); Esp32BaseWeb::sendChunk(" 个脉冲，估算启动水量可能包含少量关闭尾水。</span>"); } Esp32BaseWeb::sendChunk("</p></div><div><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='measurement'><div class='fieldgrid'><p class='field full'><label>本次实测总水量</label><input type='number' name='measured_ml' min='1000' max='1000000' step='1' inputmode='numeric' required autofocus><small>填写启动和稳态期间实际接到的全部水量，单位 mL。</small></p></div><div class='actions'><input type='submit' value='保存有效样本'></div></form><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='discard'><div class='actions'><input class='secondary' type='submit' value='放弃本次，不保存'></div></form><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return confirm(&quot;确认本次接水无效？该样本会保留，但不参与计算。&quot;)&&once(this)'><input type='hidden' name='action' value='invalid'><div class='actions'><input class='secondary' type='submit' value='本次接水无效'></div></form></div></div>");
    } else if (calibration.sampleCount() < FlowCalibrationService::kMaximumSamples) {
        bool hasEligibleZone = false;
        for (const ZoneConfig& zone : config->zones) {
            hasEligibleZone = hasEligibleZone || zone.enabled;
        }
        if (hasEligibleZone) {
            Esp32BaseWeb::sendChunk("<form id='cal-start-form' class='cal-start-form' method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认开始新样本？所选水路会立即出水。')&&once(this)\"><input type='hidden' name='action' value='start'><div class='cal-start-picker'><div class='cal-start-main'><p class='field cal-zone-field'><label>校准水路</label><select name='zone_id'>");
            for (const ZoneConfig& zone : config->zones) if (zone.enabled) { Esp32BaseWeb::sendChunk("<option value='"); sendUnsigned(zone.id); Esp32BaseWeb::sendChunk("'>"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data()); Esp32BaseWeb::sendChunk("</option>"); }
            Esp32BaseWeb::sendChunk("</select></p><fieldset class='cal-stop-group'><legend>自动停止方式</legend><div class='cal-stop-options'><label class='cal-stop-option'><input type='radio' name='stop_mode' value='time' checked><span class='cal-stop-card'><span class='cal-stop-title'>按最长时间<span class='cal-stop-mark'></span></span><span class='cal-stop-desc'>运行到设定时间后自动停止</span></span></label><label class='cal-stop-option'><input type='radio' name='stop_mode' value='volume'><span class='cal-stop-card'><span class='cal-stop-title'>按目标容量<span class='cal-stop-mark'></span></span><span class='cal-stop-desc'>估算出水量达到目标后自动停止</span></span></label></div><div id='cal-time-field' class='cal-stop-value'><div><label>最长出水时间</label><input type='number' name='duration_minutes' min='1' max='10' step='1' value='10' required></div><small>分钟 · 到时自动停止，运行中也可随时停止</small></div><div id='cal-volume-field' class='cal-stop-value' hidden><div><label>目标容量</label><input type='number' name='target_liters' min='1.000' max='1000.000' step='0.001' value='5.000' inputmode='decimal' disabled></div><small>L · 按当前校准系数估算，仍保留 10 分钟硬上限</small></div></fieldset></div><div class='actions'><input type='submit' value='开始新样本'></div></div><div class='cal-start-info'><b>已保存 "); sendUnsigned(calibration.sampleCount()); Esp32BaseWeb::sendChunk(" / 10 个样本，还可增加 "); sendUnsigned(FlowCalibrationService::kMaximumSamples - calibration.sampleCount()); Esp32BaseWeb::sendChunk(" 个</b><span>每个样本可重新选择水路。稳态判定：每个窗口 "); sendUnsigned(config->calibrationStability.windowSec); Esp32BaseWeb::sendChunk(" 秒，需连续 "); sendUnsigned(config->calibrationStability.requiredWindows); Esp32BaseWeb::sendChunk(" 个，窗口速率波动不超过 "); sendUnsigned(config->calibrationStability.allowedVariationPercent); Esp32BaseWeb::sendChunk("%。目标容量只是自动停止依据，实际接水量仍以量具读数为准。</span></div></form><script>(function(){var modes=document.querySelectorAll('input[name=\"stop_mode\"]'),time=document.getElementById('cal-time-field'),volume=document.getElementById('cal-volume-field');if(!modes.length||!time||!volume)return;function sync(){var selected=document.querySelector('input[name=\"stop_mode\"]:checked'),byVolume=selected&&selected.value==='volume',timeInput=time.querySelector('input'),volumeInput=volume.querySelector('input');time.hidden=byVolume;volume.hidden=!byVolume;timeInput.disabled=byVolume;timeInput.required=!byVolume;volumeInput.disabled=!byVolume;volumeInput.required=byVolume}modes.forEach(function(mode){mode.addEventListener('change',sync)});sync()})();</script>");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN,
                                     "没有可用于校准的水路",
                                     "请先启用至少一条水路。");
        }
    } else {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "已达到 10 个样本", "请删除不需要的样本，或全部清空后重新开始。");
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("校准样本");
    Esp32BaseWeb::sendChunk("<div class='cal-sample-toolbar'><div class='cal-counts'><span class='cal-count'><b>"); sendUnsigned(calibration.sampleCount()); Esp32BaseWeb::sendChunk("/10</b> 全部样本</span><span class='cal-count ok'><b>"); sendUnsigned(calibration.validSampleCount()); Esp32BaseWeb::sendChunk("</b> 有效</span><span class='cal-count bad'><b>"); sendUnsigned(calibration.sampleCount() - calibration.validSampleCount()); Esp32BaseWeb::sendChunk("</b> 无效</span></div>");
    if (calibration.sampleCount() != 0 && !managementLocked) Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认清空全部校准样本？设备当前使用的流量系数不会改变。')&&once(this)\"><input type='hidden' name='action' value='clear'><input class='danger' type='submit' value='全部清空'></form>");
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::sendChunk("<p class='cal-sample-guide'>阶段数据格式为“时长 · 脉冲”；稳态另显示平均 P/s 和换算流量。稳态判定：每个窗口 "); sendUnsigned(config->calibrationStability.windowSec); Esp32BaseWeb::sendChunk(" 秒，连续 "); sendUnsigned(config->calibrationStability.requiredWindows); Esp32BaseWeb::sendChunk(" 个窗口的速率波动不超过 "); sendUnsigned(config->calibrationStability.allowedVariationPercent); Esp32BaseWeb::sendChunk("%。无效样本保留记录但不参与计算。</p>");
    if (calibration.sampleCount() == 0) {
        Esp32BaseWeb::sendChunk("<div class='cal-empty'><p class='cal-empty-title'>尚无校准样本</p><p class='cal-empty-text'>完成第一次采样后，有效和无效样本都会显示在这里。</p></div>");
    } else {
        Esp32BaseWeb::sendChunk("<div class='cal-sample-list'><div class='cal-sample-columns'><span>样本</span><span>接水记录</span><span>启动阶段</span><span>稳态阶段</span><span>拟合诊断</span><span>操作</span></div>");
        for (uint8_t index = 0; index < calibration.sampleCount(); ++index) {
            const FlowCalibrationService::Sample* sample = calibration.sample(index);
            const char* sampleZoneName = sample->zoneId >= 1 && sample->zoneId <= config->zones.size()
                                             ? config->zones[sample->zoneId - 1U].name.data()
                                             : "未知水路";
            char steadyRate[24]{};
            formatPulseRate(sample->steadyPulseCount, sample->steadyDurationMs,
                            steadyRate, sizeof(steadyRate));
            char sampleFlow[20]{};
            const uint32_t sampleFlowMlPerMinute = calibration.resultReady()
                                                        ? calculateFlowMlPerMinute(
                                                              sample->steadyPulseCount,
                                                              sample->steadyDurationMs,
                                                              calibration.combinedPulsesPerLiterX100())
                                                        : 0;
            const bool sampleFlowReady = sampleFlowMlPerMinute <= 100000U &&
                                         IrrigationConfigRules::formatLitersPerMinute(
                                             sampleFlowMlPerMinute,
                                             sampleFlow,
                                             sizeof(sampleFlow));
            Esp32BaseWeb::sendChunk("<article class='cal-sample'><div class='cal-sample-cell'><span class='cal-sample-main'>样本 "); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("<span class='cal-status "); Esp32BaseWeb::sendChunk(sample->valid ? "ok'>有效" : "bad'>无效"); Esp32BaseWeb::sendChunk("</span></span><span class='cal-sample-sub'>"); Esp32BaseWeb::writeHtmlEscaped(sampleZoneName); Esp32BaseWeb::sendChunk("</span></div><div class='cal-sample-cell'><span class='cal-sample-main'>");
            if (sample->valid) { sendUnsigned(sample->measuredWaterMl); Esp32BaseWeb::sendChunk(" mL"); } else Esp32BaseWeb::sendChunk("未填写水量");
            Esp32BaseWeb::sendChunk("</span><span class='cal-sample-sub'>"); sendUnsigned(sample->pulseCount); Esp32BaseWeb::sendChunk(" 脉冲 · "); sendDuration(sample->elapsedSec); Esp32BaseWeb::sendChunk("</span>"); if (sample->stopPulseCount != 0) { Esp32BaseWeb::sendChunk("<span class='cal-sample-sub warn'>停止后仍有 "); sendUnsigned(sample->stopPulseCount); Esp32BaseWeb::sendChunk(" 个脉冲</span>"); } Esp32BaseWeb::sendChunk("</div><div class='cal-sample-cell'><span class='cal-sample-main'>");
            if (sample->steadyDetected) { sendMilliseconds(sample->steadyStartedMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(sample->startupPulseCount); Esp32BaseWeb::sendChunk(" 脉冲"); } else Esp32BaseWeb::sendChunk("未识别稳态");
            Esp32BaseWeb::sendChunk("</span><span class='cal-sample-sub'>首次出水 "); sendMilliseconds(sample->flowEstablishedMs); Esp32BaseWeb::sendChunk("</span></div><div class='cal-sample-cell'><span class='cal-sample-main'>");
            if (sample->steadyDetected) { sendMilliseconds(sample->steadyDurationMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(sample->steadyPulseCount); Esp32BaseWeb::sendChunk(" 脉冲</span><span class='cal-sample-sub'>"); Esp32BaseWeb::writeHtmlEscaped(steadyRate); Esp32BaseWeb::sendChunk(" P/s"); if (sampleFlowReady && sampleFlowMlPerMinute != 0) { Esp32BaseWeb::sendChunk(" · "); Esp32BaseWeb::writeHtmlEscaped(sampleFlow); Esp32BaseWeb::sendChunk(" L/min"); } if (sample->steadyLaterUnstable) Esp32BaseWeb::sendChunk(" · 后续有波动"); } else Esp32BaseWeb::sendChunk("—</span><span class='cal-sample-sub'>不参与计算"); Esp32BaseWeb::sendChunk("</span></div><div class='cal-sample-cell'>");
            if (!sample->valid) {
                Esp32BaseWeb::sendChunk("<span class='cal-sample-main'>不参与计算</span><span class='cal-sample-sub'>"); Esp32BaseWeb::writeHtmlEscaped(sample->stopReason == WateringStopReason::Completed ? "达到自动停止条件" : stopReasonName(sample->stopReason)); Esp32BaseWeb::sendChunk("</span>");
            } else if (!calibration.resultReady()) {
                Esp32BaseWeb::sendChunk("<span class='cal-sample-main'>等待计算</span><span class='cal-sample-sub'>需要两个不同水量的有效样本</span>");
            } else {
                char residualPulse[32]{}, residualPercent[32]{};
                formatSignedHundredths(sample->residualPulseX100,
                                       residualPulse,
                                       sizeof(residualPulse),
                                       true);
                formatSignedHundredths(sample->residualPercentX100,
                                       residualPercent,
                                       sizeof(residualPercent),
                                       true);
                char startupWater[32]{};
                formatSignedHundredths(sample->estimatedStartupWaterMlX100,
                                       startupWater,
                                       sizeof(startupWater),
                                       true);
                Esp32BaseWeb::sendChunk("<span class='cal-sample-main'>估算启动水量 "); Esp32BaseWeb::writeHtmlEscaped(startupWater); Esp32BaseWeb::sendChunk(" mL</span><span class='cal-sample-sub'>偏差 "); Esp32BaseWeb::writeHtmlEscaped(residualPulse); Esp32BaseWeb::sendChunk(" 脉冲 · "); Esp32BaseWeb::writeHtmlEscaped(residualPercent); Esp32BaseWeb::sendChunk("%</span>");
            }
            Esp32BaseWeb::sendChunk("</div><div class='cal-sample-actions'>");
            if (!managementLocked) {
                if (sample->valid) { Esp32BaseWeb::sendChunk("<button class='secondary' type='button' onclick=\"document.getElementById('cal-edit-"); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("').showModal()\">修改</button>"); }
                Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认删除这个样本？删除后无法恢复。')&&once(this)\"><input type='hidden' name='action' value='delete'><input type='hidden' name='sample' value='"); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("'><input class='danger' type='submit' value='删除'></form>");
            }
            Esp32BaseWeb::sendChunk("</div></article>");
        }
        Esp32BaseWeb::sendChunk("</div>");
    }
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("计算结果");
    if (!calibration.resultReady()) {
        if (calibration.validSampleCount() < 2) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "还不能计算结果", "至少需要两个不同水量的有效样本；无效样本不会参与计算。");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "当前样本无法形成有效结果", "请检查水量是否存在差异、脉冲是否随水量增加，或删除有问题的样本后重试。");
        }
    } else {
        char result[20]{};
        IrrigationConfigRules::formatPulsesPerLiter(
            calibration.combinedPulsesPerLiterX100(), result, sizeof(result));
        const bool canApply = calibration.combinedStartupPulseCount() <= 10000000U &&
                              calibration.combinedStartupWaterMl() <= 1000000U;
        const bool matchesCurrent =
            config->flowMeter.pulsesPerLiterX100 ==
                calibration.combinedPulsesPerLiterX100() &&
            config->flowMeter.calibrationStartupPulseCount ==
                calibration.combinedStartupPulseCount() &&
            config->flowMeter.calibrationStartupWaterMl ==
                calibration.combinedStartupWaterMl();
        const bool appliedInSession = calibration.appliedCoefficientX100() ==
                                      calibration.combinedPulsesPerLiterX100() &&
                                      calibration.appliedEpoch() != 0;
        char maximumResidual[32]{};
        formatSignedHundredths(calibration.maximumResidualPercentX100(),
                               maximumResidual,
                               sizeof(maximumResidual));
        Esp32BaseWeb::sendChunk("<div class='cal-result'><div class='cal-result-head'><div class='cal-result-head-left'><p>当前样本计算结果</p><span class='tag "); Esp32BaseWeb::sendChunk(matchesCurrent ? "ok'>设备正在使用" : "warn'>尚未应用"); Esp32BaseWeb::sendChunk("</span></div>");
        if (!matchesCurrent && canApply && !managementLocked) Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认保存并使用当前样本生成的三项校准参数？')&&once(this)\"><input type='hidden' name='action' value='apply'><input type='submit' value='使用此结果'></form>");
        Esp32BaseWeb::sendChunk("</div><div class='cal-result-grid'><div class='cal-result-fact'><span>启动脉冲</span><b>"); sendUnsigned(calibration.combinedStartupPulseCount()); Esp32BaseWeb::sendChunk(" 个</b></div><div class='cal-result-fact'><span>估算启动水量</span><b>"); sendUnsigned(calibration.combinedStartupWaterMl()); Esp32BaseWeb::sendChunk(" mL</b></div><div class='cal-result-fact'><span>稳态流量系数</span><b>"); Esp32BaseWeb::writeHtmlEscaped(result); Esp32BaseWeb::sendChunk(" P/L</b></div></div><div class='cal-result-meta'><span>有效样本："); sendUnsigned(calibration.validSampleCount()); Esp32BaseWeb::sendChunk(" 个</span><span>涉及水路："); sendUnsigned(calibration.validZoneCount()); Esp32BaseWeb::sendChunk(" 条</span><span>水量跨度："); sendUnsigned(calibration.volumeSpanMl()); Esp32BaseWeb::sendChunk(" mL</span><span>最大拟合残差："); if (calibration.validSampleCount() == 2) Esp32BaseWeb::sendChunk("暂无法检验"); else { Esp32BaseWeb::writeHtmlEscaped(maximumResidual); Esp32BaseWeb::sendChunk("%"); } Esp32BaseWeb::sendChunk("</span><span>结果更新：");
        char timeText[32]{};
        if (calibration.resultUpdatedEpoch() != 0 && Esp32BaseTime::formatEpoch(calibration.resultUpdatedEpoch(), timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S")) Esp32BaseWeb::writeHtmlEscaped(timeText); else Esp32BaseWeb::sendChunk("设备时间不可用");
        Esp32BaseWeb::sendChunk("</span>");
        if (calibration.appliedEpoch() != 0) { Esp32BaseWeb::sendChunk(appliedInSession ? "<span>应用时间：" : "<span>上次应用："); if (Esp32BaseTime::formatEpoch(calibration.appliedEpoch(), timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S")) Esp32BaseWeb::writeHtmlEscaped(timeText); else Esp32BaseWeb::sendChunk("设备时间不可用"); Esp32BaseWeb::sendChunk("</span>"); }
        Esp32BaseWeb::sendChunk("</div><p class='cal-result-note'>计算关系：实测总水量 = 估算启动水量 + 稳态脉冲 ÷ 稳态流量系数。启动脉冲只记录进入稳态前的实际脉冲；若停止后仍有脉冲，样本会单独提示关闭尾水。</p></div>");
        if (!canApply) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "部分结果超出可保存范围", "请检查样本，或使用上方“修改参数”手工设置合理数值。");
        const uint8_t flags = calibration.qualityFlags();
        if (calibration.volumeSpanMl() >= 1000U) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "水量跨度达到 1 L", "这组样本对量杯读数误差更不敏感。");
        if (flags & FlowCalibrationService::kQualityOnlyTwoSamples) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "当前只有两个有效样本", "两点必然完全落在拟合直线上，暂时无法检验一致性；结果可以使用，建议增加第三个样本。");
        if (flags & FlowCalibrationService::kQualitySmallVolumeSpan) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "水量差小于 500 mL", "量杯误差容易被放大，建议增加差距更大的样本。");
        if (flags & FlowCalibrationService::kQualityNonMonotonic) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "样本趋势不完全一致", "建议检查接水读数，或删除有问题的样本。");
        if (flags & FlowCalibrationService::kQualityResidualHigh) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "拟合残差较大", "建议检查或补充样本后再应用。");
        if (flags & FlowCalibrationService::kQualityPostSteadyUnstable) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "部分样本进入稳态后仍有波动", "结果仍可使用，请结合样本中的稳态速率和拟合偏差判断。");
        if (flags & FlowCalibrationService::kQualityNegativeStartupWater) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "无法得到正的启动水量", "当前样本与阶段模型不完全一致，建议启动水量已按 0 mL 生成；可以检查样本或手工修改参数。");
        if (calibration.validZoneCount() > 1) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "样本来自不同水路", "不同水路的启动特性可能不同，请结合每条样本的拟合偏差判断；系统不会限制应用。");
    }
    Esp32BaseWeb::endPanel();

    for (uint8_t index = 0; index < calibration.sampleCount(); ++index) {
        const FlowCalibrationService::Sample* sample = calibration.sample(index);
        if (!sample || !sample->valid || managementLocked) continue;
        Esp32BaseWeb::sendChunk("<dialog id='cal-edit-"); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("' class='panel eb-modal cal-edit' data-eb-light-dismiss='1'><h2>修改样本 "); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("</h2><p class='muted'>只能修正实测总水量；设备记录的脉冲、时间和停止原因不会改变。</p><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='update'><input type='hidden' name='sample' value='"); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("'><div class='fieldgrid'><p class='field full'><label>实测总水量</label><input type='number' name='measured_ml' min='1000' max='1000000' step='1' inputmode='numeric' required value='"); sendUnsigned(sample->measuredWaterMl); Esp32BaseWeb::sendChunk("'><small>单位 mL，保存后结果会自动重新计算。</small></p></div><div class='actions'><button class='secondary' type='button' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='保存修改'></div></form></dialog>");
    }
    if (active) Esp32BaseWeb::sendChunk("<script>(function(){function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}function duration(v){v=Math.max(0,Number(v)||0);if(v<60)return v+' 秒';return Math.floor(v/60)+' 分 '+(v%60)+' 秒'}function liters(v){return (Math.max(0,Number(v)||0)/1000).toFixed(3)+' L'}function millis(v){v=Math.max(0,Number(v)||0);return (v/1000).toFixed(1)+' 秒'}function rate(p,m){m=Number(m)||0;return m>0?(Number(p||0)*1000/m).toFixed(2):'—'}function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active||s.purpose!==1){location.reload();return}var states=['空闲','区域启动中','等待水流','正在采样','正在停止'];set('cal-state',states[s.state]||'状态未知');set('cal-elapsed',duration(s.elapsedSec));set('cal-remaining',duration(s.currentZoneRemainingSec));set('cal-pulses',s.pulseCount);set('cal-target-note',Number(s.currentZoneTargetWaterMl||0)?'目标 '+liters(s.currentZoneTargetWaterMl):'按时间自动停止');var z=s.zones&&s.zones[s.currentStepIndex];if(z){set('cal-water',liters(z.estimatedWaterMl));var detected=!!z.calibrationSteadyDetected,flow=!!s.flowEstablished,full=Number(z.calibrationCollectedWindows||0)>=Number(z.calibrationRequiredWindows||0);set('cal-steady-state',detected?'已确认':(!flow?'等待水流':(full?'波动偏大':'已采集 '+z.calibrationCollectedWindows+' / '+z.calibrationRequiredWindows+' 个窗口')));set('cal-steady-help',detected?'后续波动仍会继续监测':(!flow?'检测到脉冲后开始统计':(full?'最近 '+z.calibrationRequiredWindows+' 个窗口尚未满足波动要求，继续识别':'收满 '+z.calibrationRequiredWindows+' 个窗口后比较速率波动')));set('cal-startup-phase',detected?millis(z.calibrationSteadyStartedMs)+' · '+z.calibrationStartupPulses+' 脉冲':'等待稳态确认');set('cal-steady-phase',detected?millis(z.calibrationSteadyDurationMs)+' · '+z.calibrationSteadyPulses+' 脉冲 · '+rate(z.calibrationSteadyPulses,z.calibrationSteadyDurationMs)+' P/s':'尚未开始');set('cal-rate',(Number(z.calibrationLatestPulseRateX100||0)/100).toFixed(2)+' P/s')}setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}setTimeout(poll,1000)})();</script>");
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
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.learning-panel{padding-bottom:16px}.learning-context{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0;margin:-2px 0 14px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft)}.learning-context>div{min-width:0;padding:10px 12px;border-right:1px solid var(--eb-line-soft)}.learning-context>div:last-child{border-right:0}.learning-label{display:block;color:var(--eb-muted);font-size:11px;font-weight:400}.learning-context-value{display:block;margin-top:2px;font-size:14px;font-weight:400;overflow-wrap:anywhere}.learning-context-help{display:block;margin-top:2px;color:var(--eb-muted);font-size:11px;font-weight:400}
.learning-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:0 0 10px}.learning-head h3{margin:0;font-size:15px;font-weight:500}.learning-head .tag{font-weight:400}.learning-metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.learning-metric{min-width:0;padding:10px 11px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.learning-value{display:block;margin-top:3px;font-size:18px;font-weight:500;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.learning-rule{margin:10px 0 0;padding:9px 11px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px;font-weight:400;line-height:1.55}.learning-rule span{color:var(--eb-text);font-weight:400}
.learning-debug{margin-top:14px}.learning-debug h3{margin:0 0 2px;font-size:14px;font-weight:500}.learning-debug>p{margin:0 0 7px;color:var(--eb-muted);font-size:11px}.learning-table{width:100%;border-collapse:collapse;font-size:12px;font-variant-numeric:tabular-nums}.learning-table th,.learning-table td{padding:7px 8px;border-bottom:1px solid var(--eb-line-soft);text-align:right;font-weight:400;white-space:nowrap}.learning-table th{color:var(--eb-muted);font-size:11px}.learning-table th:first-child,.learning-table td:first-child{text-align:left}.learning-table tbody tr:last-child td{border-bottom:0}.learning-table tr.learning-decision td{background:#f6faf8}.learning-table tr.learning-decision-start td{border-top:1px solid #cfe5da}.learning-inline{display:inline-flex;align-items:baseline;justify-content:flex-end;gap:7px}.learning-change{color:var(--eb-muted);font-size:10px;font-weight:400}.learning-latest{margin-left:5px;color:#16794a;font-size:10px;font-weight:400}.learning-table .learning-empty{text-align:center;color:var(--eb-muted);padding:12px}.learning-summary{display:flex;flex-wrap:wrap;gap:5px 18px;margin:8px 0 0;color:var(--eb-muted);font-size:11px}.learning-summary span{font-weight:400}.learning-result{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-bottom:10px}.learning-result>div{padding:11px;border:1px solid var(--eb-line-soft);border-radius:8px}.learning-result-value{display:block;margin-top:3px;font-size:18px;font-weight:500;font-variant-numeric:tabular-nums}.learning-actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}.learning-actions form,.learning-actions .actions{margin:0}.learning-panel input,.learning-panel .btnlink{font-weight:500}.learning-manual{width:min(520px,calc(100vw - 28px))}.learning-manual>p{margin-top:0}.learning-manual .field{margin:14px 0 0}.learning-manual input[type=number]{width:100%;margin-top:6px}.learning-manual-preview{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin-top:10px;padding:9px 11px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px}.learning-manual-preview output{color:var(--eb-text);font-size:16px;font-variant-numeric:tabular-nums}
@media(max-width:760px){.learning-context{grid-template-columns:1fr 1fr}.learning-context>div{border-bottom:1px solid var(--eb-line-soft)}.learning-context>div:nth-child(2){border-right:0}.learning-context>div:last-child{grid-column:1/-1;border-bottom:0}.learning-metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.learning-table{display:block}.learning-table thead{display:none}.learning-table tbody{display:grid;gap:7px}.learning-table tr{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));padding:7px 9px;border:1px solid var(--eb-line-soft);border-radius:7px}.learning-table td{display:grid;grid-template-columns:6.8em minmax(0,1fr);gap:6px;padding:3px 0;border:0;text-align:right}.learning-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:11px;text-align:left}.learning-table td:first-child{text-align:right}.learning-table .learning-empty{display:block;grid-column:1/-1;text-align:center}.learning-table .learning-empty::before{display:none}}
@media(max-width:420px){.learning-context,.learning-metrics,.learning-result{grid-template-columns:1fr}.learning-context>div{border-right:0}.learning-context>div:last-child{grid-column:auto}.learning-value,.learning-result-value{font-size:17px}.learning-table tr{grid-template-columns:1fr}}
</style>)HTML");
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
    Esp32BaseWeb::sendChunk("<p><a class='btnlink secondary' href='/irrigation/zones'>返回水路设置</a></p>");
    if (active) {
        Esp32BaseWeb::sendChunk(R"HTML(<script>
(function(){
function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}
function liters(v){return (Math.max(0,Number(v)||0)/1000).toFixed(3)+' L/min'}
function rate(v){return (Math.max(0,Number(v)||0)/100).toFixed(2)+' P/s'}
function seconds(v){return (Math.max(0,Number(v)||0)/1000).toFixed(1)+' 秒'}
function duration(v){v=Math.max(0,Number(v)||0);return v<60?v+' 秒':Math.floor(v/60)+' 分 '+v%60+' 秒'}
function signed(v,digits){v=Number(v)||0;return (v<0?'−':'+')+Math.abs(v).toFixed(digits)}
function pulseChange(current,previous){return signed(Number(current)-Number(previous),0)}
function flowChange(current,previous){current=Number(current)||0;previous=Number(previous)||0;var change=current-previous,text=signed(change/1000,3);return previous?text+' ('+signed(change/previous*100,1)+'%)':text}
function updateWindows(s){
var rows=s.learningWindows||[],empty=document.getElementById('learn-window-empty'),start=Math.max(0,rows.length-5);
if(empty)empty.style.display=rows.length?'none':'';
for(var i=0;i<10;i++){
var row=document.getElementById('learn-window-'+i),w=rows[i];
if(!row)continue;
row.style.display=w?'':'none';
row.className=w&&i>=start?'learning-decision'+(i===start?' learning-decision-start':''):'';
if(!w)continue;
set('learn-window-index-'+i,'#'+w.sequence+(i===rows.length-1?' 最新':''));
set('learn-window-duration-'+i,seconds(w.windowMs));
set('learn-window-pulses-'+i,w.pulseCount);
set('learn-window-pulse-change-'+i,i?pulseChange(w.pulseCount,rows[i-1].pulseCount):'—');
set('learn-window-rate-'+i,rate(w.pulseRateX100));
set('learn-window-flow-'+i,liters(w.flowMlPerMinute));
set('learn-window-flow-change-'+i,i?flowChange(w.flowMlPerMinute,rows[i-1].flowMlPerMinute):'—')
}
}
function updateRule(s){
var n=Number(s.learningTotalWindowCount)||0;
if(!n)return '窗口 #1 正在采集；每个窗口约 5 秒且互不重叠。';
var min=Number(s.learningMinimumPulseRateX100)||0,max=Number(s.learningMaximumPulseRateX100)||0,allow=Number(s.learningAllowedPulseRateSpreadX100)||0,first=Math.max(1,n-4);
return '已采集 '+n+' 个完整窗口，窗口 #'+(n+1)+' 正在采集；当前使用 #'+first+'～#'+n+' 判断。原始脉冲速率为 '+rate(min)+'～'+rate(max)+'，跨度 '+rate(max-min)+'，允许 '+rate(allow)+'。收满 5 窗、每窗均有脉冲且跨度不超过允许值时完成。'
}
function poll(){
fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){
if(!s.active||s.purpose!==2){location.reload();return}
var n=Number(s.learningTotalWindowCount)||0;
set('learn-elapsed',duration(s.elapsedSec));
set('learn-current',liters(s.currentFlowMlPerMinute));
set('learn-average',liters(s.learningAverageMlPerMinute));
set('learn-state',n<5?'采集中 '+n+'/5':'波动偏大');
set('learn-rule',updateRule(s));
set('learn-total-windows',n);
set('learn-total-pulses',s.pulseCount);
updateWindows(s);
setTimeout(poll,1000)
}).catch(function(){setTimeout(poll,2000)})
}
setTimeout(poll,1000)
})();
</script>)HTML");
    }
    endPage();
}

void IrrigationWeb::records() {
    if (!beginPage("浇水记录", "最新记录优先")) return;
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.record-toolbar{margin:0 0 10px}.record-toolbar p{margin:0;color:var(--eb-muted);font-size:13px}.record-table{width:100%;min-width:900px;border-collapse:collapse;font-size:13px}.record-table th,.record-table td{padding:10px 8px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:middle}.record-table th{color:var(--eb-muted);font-weight:500;white-space:nowrap;background:var(--eb-soft)}.record-table tbody tr:last-child td{border-bottom:0}.record-table tbody tr:hover{background:#fbfcfd}.record-number{white-space:nowrap;font-variant-numeric:tabular-nums}.record-number span,.record-number small,.record-result-reason{display:block}.record-number small,.record-result-reason{margin-top:2px;color:var(--eb-muted);font-size:11px;font-weight:400}.record-list-duration span,.record-list-duration small{display:inline;margin:0}.record-list-duration small{margin-left:3px}.record-time{width:1%;min-width:9.5em;white-space:nowrap;font-variant-numeric:tabular-nums}.record-zones{min-width:12em}.record-action{width:1%;white-space:nowrap;text-align:right!important}.record-empty-cell{padding:24px 16px!important;background:var(--eb-soft);text-align:center!important}.record-empty-cell b{display:block;margin-bottom:4px;font-size:15px;font-weight:500}.record-empty-cell span{color:var(--eb-muted)}
.record-detail-dialog{width:min(1160px,calc(100vw - 28px));text-align:left!important;white-space:normal!important}.record-detail-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:12px}.record-detail-head h2{margin:3px 0 0;font-size:20px;font-weight:500}.record-detail-head>div>span{font-size:12px}.record-detail-close{min-height:30px;font-weight:500}.record-detail-result{display:flex;align-items:center;gap:10px;margin-bottom:10px;color:var(--eb-muted);font-size:13px}.record-detail-result .tag{font-weight:500}.record-detail-metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.record-detail-metrics>div{min-width:0;padding:11px 12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft);text-align:left}.record-detail-metrics span{display:block;color:var(--eb-muted);font-size:11px}.record-detail-metrics b{display:block;margin-top:3px;font-size:16px;font-weight:500;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.record-detail-metrics small{display:block;margin-top:2px;color:var(--eb-muted);font-size:10px;font-weight:400}.record-detail-grid{display:grid;grid-template-columns:minmax(240px,2fr) repeat(2,minmax(0,1fr));gap:8px 14px;padding:11px 12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.record-detail-grid>div{min-width:0}.record-detail-grid b{display:block;color:var(--eb-muted);font-size:11px;font-weight:400}.record-detail-grid span{display:block;margin-top:3px;overflow-wrap:anywhere}.record-time-range span{white-space:nowrap;font-variant-numeric:tabular-nums}.record-detail-section{margin-top:16px}.record-detail-section>h3{margin-bottom:7px;font-size:14px;font-weight:500}.record-detail-section>p{margin:0 0 8px;color:var(--eb-muted);font-size:12px}.record-zone-table{width:100%;min-width:1080px;border-collapse:collapse;font-size:12px}.record-zone-table th,.record-zone-table td{padding:8px 7px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:top}.record-zone-table th{color:var(--eb-muted);font-weight:500;white-space:nowrap;background:var(--eb-soft)}.record-zone-table tr:last-child td{border-bottom:0}.record-zone-table td>b,.record-zone-table td>small{display:block}.record-zone-table td>b{font-weight:500}.record-zone-table td>small{margin-top:2px;color:var(--eb-muted);font-weight:400}.record-duration-pair{white-space:nowrap}.record-duration-pair b,.record-duration-pair span,.record-duration-pair small{display:inline!important;margin:0!important}.record-duration-pair span{padding:0 4px;color:var(--eb-muted)}.record-zone-table td>small.warn{color:var(--eb-warn)}.record-baseline{min-width:12em}.record-flow-performance{display:grid;gap:3px;min-width:30em}.record-flow-performance>span{display:block}.record-flow-performance .record-flags{margin-top:3px}.record-stability{display:inline-block!important;margin:0 0 0 4px!important;padding:1px 5px;border-radius:999px;background:var(--eb-soft);font-size:10px!important;white-space:nowrap}.record-stability.ok{color:var(--eb-ok)}.record-flags{display:flex;flex-wrap:wrap;gap:4px;min-width:7em}.record-technical{margin-top:13px;padding-top:10px;border-top:1px solid var(--eb-line-soft)}.record-technical summary{cursor:pointer;color:var(--eb-muted);font-size:12px}.record-technical-note{margin:8px 0 0;color:var(--eb-muted);font-size:11px;line-height:1.55}.record-technical-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;margin-top:8px}.record-technical-grid>div{padding:8px 9px;border-radius:7px;background:var(--eb-soft)}.record-technical-grid b,.record-technical-grid span{display:block}.record-technical-grid b{color:var(--eb-muted);font-size:10px;font-weight:400}.record-technical-grid span{margin-top:2px;font-size:12px}.record-detail-note{margin:10px 0 0;color:var(--eb-muted);font-size:11px}.record-detail-bottom-close{display:none}
@media(max-width:760px){.record-toolbar p{display:none}.record-table{display:block;min-width:0}.record-table thead{display:none}.record-table tbody{display:grid;gap:9px}.record-table tr{display:grid;padding:11px;border:1px solid var(--eb-line);border-radius:8px;background:#fff}.record-table tbody tr:hover{background:#fff}.record-table td{display:grid;grid-template-columns:6.5em minmax(0,1fr);gap:8px;padding:4px 0;border:0;white-space:normal;text-align:left!important}.record-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:12px;font-weight:400}.record-table .record-action{display:flex;justify-content:flex-end;width:auto;padding-top:9px;border-top:1px solid var(--eb-line-soft);margin-top:5px}.record-table .record-action::before{display:none}.record-table .record-action>.btnlink{width:100%;min-height:34px}.record-table .record-empty-row{display:block;padding:0}.record-table .record-empty-cell{display:block;padding:22px 12px!important}.record-table .record-empty-cell::before{display:none}.record-detail-dialog{width:calc(100vw - 20px);padding:12px!important}.record-detail-metrics,.record-detail-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.record-zone-table{display:block;min-width:0}.record-zone-table thead{display:none}.record-zone-table tbody{display:grid;gap:8px}.record-zone-table tr{display:grid;padding:9px;border:1px solid var(--eb-line);border-radius:7px}.record-zone-table td{display:grid;grid-template-columns:8em minmax(0,1fr);gap:7px;padding:3px 0;border:0}.record-zone-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:11px;font-weight:400}.record-zone-table td>b,.record-zone-table td>small{display:inline}.record-zone-table td>small{margin-left:4px}.record-baseline,.record-flow-performance{min-width:0}.record-flags{min-width:0}.record-detail-bottom-close{display:flex}.record-detail-dialog>.actions button{width:100%}}
@media(max-width:420px){.record-detail-metrics,.record-detail-grid{grid-template-columns:1fr}.record-detail-result{align-items:flex-start;flex-direction:column;gap:5px}.record-detail-head h2{font-size:18px}}
</style>)HTML");
    uint32_t detailId = 0;
    char idText[16]{};
    const bool detailRequested = getParam("id", idText, sizeof(idText)) &&
                                 parseUint(idText, 1, UINT32_MAX, detailId);
    uint32_t page = 1, perPage = 15;
    char text[16]{};
    if (getParam("page", text, sizeof(text))) parseUint(text, 1, UINT32_MAX, page);
    if (getParam("per", text, sizeof(text))) parseUint(text, 10, 50, perPage);
    Esp32BaseRecordStore::StoreStatus status{};
    const bool statusReady = g_app->readWateringRecordStoreStatus(status);
    if (statusReady && status.recordCount != 0) {
        const uint32_t totalPages = (status.recordCount + perPage - 1U) / perPage;
        if (page > totalPages) page = totalPages;
    } else {
        page = 1;
    }
    Esp32BaseWeb::beginPanel("历史记录");
    Esp32BaseWeb::sendChunk("<div class='record-toolbar'><p>");
    if (statusReady) {
        Esp32BaseWeb::sendChunk("设备当前保存 ");
        sendUnsigned(status.recordCount);
        Esp32BaseWeb::sendChunk(" 条记录");
    } else {
        Esp32BaseWeb::sendChunk("记录存储状态不可用");
    }
    Esp32BaseWeb::sendChunk("</p></div>");
    if (!statusReady) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                 "浇水记录暂时无法读取",
                                 "请在系统状态中检查业务记录存储。");
    } else {
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='record-table'><thead><tr><th>完成时间</th><th>浇水任务</th><th>执行水路</th><th>执行结果</th><th>实际 / 目标</th><th>估算用水量</th><th>操作</th></tr></thead><tbody>");
        RecordRowsContext context{g_app->configuration(), 0};
        bool readOk = true;
        if (status.recordCount == 0) {
            Esp32BaseWeb::sendChunk("<tr class='record-empty-row'><td class='record-empty-cell' colspan='7'><b>还没有浇水记录</b><span>第一次浇水执行结束后，无论完成、停止或失败，记录都会显示在这里。</span></td></tr>");
        } else {
            readOk = g_app->readLatestWateringRecords(
                (page - 1U) * perPage, perPage, sendRecordRow, &context);
        }
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
        if (!readOk) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                     "本页记录读取失败",
                                     "请刷新页面；如果问题持续，请检查记录存储状态。");
        } else if (status.recordCount != 0 && context.emitted == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO,
                                     "本页没有记录",
                                     "页码已经调整，请刷新后重试。");
        }
        if (status.recordCount != 0) {
            Esp32BaseWeb::Pagination pagination{};
            pagination.path = "/irrigation/records";
            pagination.query = "";
            pagination.page = page;
            pagination.perPage = perPage;
            pagination.total = status.recordCount;
            Esp32BaseWeb::sendPagination(pagination);
        }
    }
    Esp32BaseWeb::endPanel();
    if (detailRequested) {
        StoredWateringRecord detail{};
        if (g_app->readWateringRecordById(detailId, detail) ==
            Esp32BaseRecordStore::RecordReadResult::Found) {
            sendRecordDetailDialog(detail, g_app->configuration(), "record-direct-detail-");
            Esp32BaseWeb::sendChunk("<script>(function(){var d=document.getElementById('record-direct-detail-");
            sendUnsigned(detailId);
            Esp32BaseWeb::sendChunk("');if(d&&d.showModal)d.showModal();})();</script>");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                     "找不到该浇水记录",
                                     "记录可能已经轮换或被清空。");
        }
    }
    endPage();
}

void IrrigationWeb::events() {
    if (!beginPage("事件", "记录设备的重要操作、报警和异常")) return;
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.condition-summary{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin-bottom:10px}.condition-summary p{margin:0;color:var(--eb-muted);font-size:13px}.condition-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.condition-item{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.condition-item div{min-width:0}.condition-item b{display:block;font-size:13px;font-weight:500}.condition-item small{display:block;margin-top:2px;color:var(--eb-muted);font-size:11px;font-weight:400}.condition-item .tag{flex:0 0 auto}
.event-filter{display:flex;flex-wrap:wrap;align-items:flex-end;gap:10px 12px;margin-bottom:14px}.event-filter label{display:grid;gap:4px;margin:0;color:var(--eb-muted);font-size:12px}.event-filter select{width:180px;max-width:none;min-height:34px;margin:0}.event-filter-actions{display:flex;align-items:center;gap:8px}.event-filter-actions input,.event-filter-actions .btnlink{min-height:34px;margin:0}.event-table{width:100%;min-width:900px;border-collapse:collapse;table-layout:auto;font-size:13px}.event-table th,.event-table td{padding:11px 10px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:top}.event-table th{padding-top:9px;padding-bottom:9px;color:var(--eb-muted);font-weight:650;white-space:nowrap;background:var(--eb-soft)}.event-table tbody tr:last-child td{border-bottom:0}.event-table tbody tr:hover{background:#fbfcfd}.event-time{width:1%;min-width:10.5em;white-space:nowrap;font-variant-numeric:tabular-nums}.event-level{width:1%;white-space:nowrap}.event-category{width:1%;min-width:8em;white-space:nowrap}.event-title{min-width:15em;line-height:1.5;font-weight:400}.event-summary{min-width:20em;color:var(--eb-muted);line-height:1.5}.event-action{width:1%;white-space:nowrap;text-align:right!important}.event-empty{padding:26px!important;text-align:center!important;color:var(--eb-muted)}.event-detail{width:min(780px,calc(100vw - 28px));text-align:left;white-space:normal}.event-detail-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}.event-detail-heading{flex:1;min-width:0}.event-detail-head h2{margin:7px 0 0;font-size:20px;font-weight:500}.event-detail-close{flex:0 0 auto;min-height:32px}.event-detail-summary{margin:16px 0;padding:12px 14px;border-radius:8px;background:var(--eb-soft);font-size:14px;line-height:1.55}.event-detail-section{margin-top:16px}.event-detail-section h3{margin-bottom:7px;color:#344054;font-size:13px;font-weight:500}.event-detail-grid{display:grid;gap:10px 18px;padding:12px 14px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.event-detail-overview{grid-template-columns:repeat(3,minmax(0,1fr))}.event-detail-business{grid-template-columns:repeat(auto-fit,minmax(180px,1fr))}.event-detail-technical-grid{grid-template-columns:repeat(3,minmax(0,1fr));background:var(--eb-soft)}.event-detail-grid>div{min-width:0}.event-detail-grid b{display:block;color:var(--eb-muted);font-size:11px;font-weight:400}.event-detail-grid span{display:block;margin-top:3px;overflow-wrap:anywhere}.event-detail-grid span.tag{display:inline-flex}.event-time-value{white-space:nowrap;font-variant-numeric:tabular-nums}.event-technical{margin-top:18px;padding-top:13px;border-top:1px solid var(--eb-line)}.event-technical summary{cursor:pointer;color:var(--eb-muted);font-size:13px;font-weight:400}.event-technical[open] summary{margin-bottom:10px}
@media(max-width:760px){.condition-grid{grid-template-columns:1fr}.event-filter{display:grid;grid-template-columns:1fr 1fr}.event-filter label,.event-filter select{width:100%}.event-filter-actions{grid-column:1/-1}.event-filter-actions input,.event-filter-actions .btnlink{flex:1}.event-table{display:block;min-width:0}.event-table thead{display:none}.event-table tbody{display:grid;gap:9px}.event-table tr{display:grid;padding:11px;border:1px solid var(--eb-line);border-radius:8px;background:#fff}.event-table tbody tr:hover{background:#fff}.event-table td{display:grid;grid-template-columns:6em minmax(0,1fr);gap:8px;width:auto;min-width:0;padding:4px 0;border:0;text-align:left!important;white-space:normal}.event-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:12px;font-weight:500}.event-table .event-time{white-space:nowrap}.event-table .event-action{display:flex;justify-content:flex-end;padding-top:9px;border-top:1px solid var(--eb-line-soft);margin-top:5px}.event-table .event-action::before{display:none}.event-table .event-action>.btnlink{width:100%;min-height:34px}.event-empty{display:block!important}.event-empty::before{display:none}.event-detail{width:calc(100vw - 20px);padding:12px!important}.event-detail-overview,.event-detail-technical-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
@media(max-width:440px){.event-filter{grid-template-columns:1fr}.event-filter-actions{grid-column:auto}.event-detail-overview,.event-detail-business,.event-detail-technical-grid{grid-template-columns:1fr}.event-detail-head h2{font-size:18px}}
</style>)HTML");

    EventFilter filter{};
    char value[20]{};
    if (getParam("level", value, sizeof(value))) {
        if (std::strcmp(value, "info") == 0) filter.level = static_cast<uint8_t>(Esp32BaseAppEvents::Level::Info);
        else if (std::strcmp(value, "warning") == 0) filter.level = static_cast<uint8_t>(Esp32BaseAppEvents::Level::Warning);
        else if (std::strcmp(value, "error") == 0) filter.level = static_cast<uint8_t>(Esp32BaseAppEvents::Level::Error);
    }
    if (getParam("category", value, sizeof(value))) {
        if (std::strcmp(value, "watering") == 0) filter.category = static_cast<int8_t>(IrrigationEvents::Category::WateringAndFlow);
        else if (std::strcmp(value, "automatic") == 0) filter.category = static_cast<int8_t>(IrrigationEvents::Category::AutomaticWatering);
        else if (std::strcmp(value, "settings") == 0) filter.category = static_cast<int8_t>(IrrigationEvents::Category::SettingsAndCalibration);
        else if (std::strcmp(value, "time") == 0) filter.category = static_cast<int8_t>(IrrigationEvents::Category::TimeAndStorage);
    }
    const char* levelQuery = filter.level == static_cast<uint8_t>(Esp32BaseAppEvents::Level::Info) ? "info" :
                             filter.level == static_cast<uint8_t>(Esp32BaseAppEvents::Level::Warning) ? "warning" :
                             filter.level == static_cast<uint8_t>(Esp32BaseAppEvents::Level::Error) ? "error" : "";
    const char* categoryQuery = filter.category == static_cast<int8_t>(IrrigationEvents::Category::WateringAndFlow) ? "watering" :
                                filter.category == static_cast<int8_t>(IrrigationEvents::Category::AutomaticWatering) ? "automatic" :
                                filter.category == static_cast<int8_t>(IrrigationEvents::Category::SettingsAndCalibration) ? "settings" :
                                filter.category == static_cast<int8_t>(IrrigationEvents::Category::TimeAndStorage) ? "time" : "";
    if (levelQuery[0] && categoryQuery[0]) std::snprintf(filter.query, sizeof(filter.query), "level=%s&category=%s", levelQuery, categoryQuery);
    else if (levelQuery[0]) std::snprintf(filter.query, sizeof(filter.query), "level=%s", levelQuery);
    else if (categoryQuery[0]) std::snprintf(filter.query, sizeof(filter.query), "category=%s", categoryQuery);

    uint32_t page = 1;
    if (getParam("page", value, sizeof(value))) parseUint(value, 1, UINT32_MAX, page);
    Esp32BaseAppEvents::AppEventsStatus status{};
    const bool statusReady = g_app->readEventStatus(status) && status.eventStore.ready;
    if (!statusReady) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                 "事件暂时无法读取",
                                 "请在系统状态中检查事件存储。");
        endPage();
        return;
    }
    if (!status.conditionStateLoaded || status.conditionStateSavePending) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                 "事件状态保存异常",
                                 "持续问题仍会显示当前状态，但新的发生或恢复可能无法保存。");
    }

    struct ConditionCard {
        uint8_t id;
        const char* name;
        const char* description;
    };
    static constexpr ConditionCard conditions[] = {
        {1, "硬件时钟", "影响断网后的可靠计时"},
        {2, "设备时间", "影响自动计划按时执行"},
        {3, "时间倒退保护", "异常时暂停自动计划"},
        {4, "关阀后水流", "监测阀门关闭后的异常水流"},
    };
    IrrigationEvents::ConditionDisplayState conditionStates[
        sizeof(conditions) / sizeof(conditions[0])]{};
    uint8_t activeConditionCount = 0;
    for (std::size_t index = 0;
         index < sizeof(conditions) / sizeof(conditions[0]);
         ++index) {
        conditionStates[index] =
            g_app->eventConditionState(conditions[index].id);
        if (conditionStates[index] ==
                IrrigationEvents::ConditionDisplayState::Active ||
            conditionStates[index] ==
                IrrigationEvents::ConditionDisplayState::ConfirmingRecovery) {
            ++activeConditionCount;
        }
    }
    Esp32BaseWeb::beginPanel("当前持续状态");
    Esp32BaseWeb::sendChunk("<div class='condition-summary'><p>共监测 4 项；当前持续异常 ");
    sendUnsigned(activeConditionCount);
    Esp32BaseWeb::sendChunk(" 项</p><small>等待判断不计入异常；需要确认的状态会明确标出</small></div><div class='condition-grid'>");
    for (std::size_t index = 0;
         index < sizeof(conditions) / sizeof(conditions[0]);
         ++index) {
        const ConditionCard& condition = conditions[index];
        const IrrigationEvents::ConditionDisplayState state =
            conditionStates[index];
        Esp32BaseWeb::sendChunk("<div class='condition-item'><div><b>");
        Esp32BaseWeb::sendChunk(condition.name);
        Esp32BaseWeb::sendChunk("</b><small>");
        if (condition.id != 4) {
            Esp32BaseWeb::sendChunk(condition.description);
        } else if (g_app->wateringStatus().active) {
            Esp32BaseWeb::sendChunk("浇水期间暂停，结束后重新观察");
        } else if (g_app->unexpectedFlowObservationReady()) {
            Esp32BaseWeb::sendChunk("最近完整窗口已完成判断");
        } else if (g_app->unexpectedFlowDelayRemainingSec() != 0) {
            Esp32BaseWeb::sendChunk("等待余流消退，约剩 ");
            sendUnsigned(g_app->unexpectedFlowDelayRemainingSec());
            Esp32BaseWeb::sendChunk(" 秒");
        } else {
            Esp32BaseWeb::sendChunk("正在收集首个完整窗口，约剩 ");
            sendUnsigned(g_app->unexpectedFlowWindowRemainingSec());
            Esp32BaseWeb::sendChunk(" 秒");
        }
        Esp32BaseWeb::sendChunk("</small></div><span class='tag ");
        Esp32BaseWeb::sendChunk(conditionStateTone(state));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(conditionStateName(state));
        Esp32BaseWeb::sendChunk("</span></div>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("事件记录");
    Esp32BaseWeb::sendChunk("<form class='event-filter' method='get' action='/irrigation/events'><label>等级<select name='level'><option value=''>全部等级</option><option value='info'");
    if (filter.level == static_cast<uint8_t>(Esp32BaseAppEvents::Level::Info)) Esp32BaseWeb::sendChunk(" selected");
    Esp32BaseWeb::sendChunk(">信息</option><option value='warning'");
    if (filter.level == static_cast<uint8_t>(Esp32BaseAppEvents::Level::Warning)) Esp32BaseWeb::sendChunk(" selected");
    Esp32BaseWeb::sendChunk(">警告</option><option value='error'");
    if (filter.level == static_cast<uint8_t>(Esp32BaseAppEvents::Level::Error)) Esp32BaseWeb::sendChunk(" selected");
    Esp32BaseWeb::sendChunk(">错误</option></select></label><label>分类<select name='category'><option value=''>全部分类</option><option value='watering'");
    if (filter.category == static_cast<int8_t>(IrrigationEvents::Category::WateringAndFlow)) Esp32BaseWeb::sendChunk(" selected");
    Esp32BaseWeb::sendChunk(">浇水与流量</option><option value='automatic'");
    if (filter.category == static_cast<int8_t>(IrrigationEvents::Category::AutomaticWatering)) Esp32BaseWeb::sendChunk(" selected");
    Esp32BaseWeb::sendChunk(">自动计划</option><option value='settings'");
    if (filter.category == static_cast<int8_t>(IrrigationEvents::Category::SettingsAndCalibration)) Esp32BaseWeb::sendChunk(" selected");
    Esp32BaseWeb::sendChunk(">设置与校准</option><option value='time'");
    if (filter.category == static_cast<int8_t>(IrrigationEvents::Category::TimeAndStorage)) Esp32BaseWeb::sendChunk(" selected");
    Esp32BaseWeb::sendChunk(">时间与存储</option></select></label><span class='event-filter-actions'><input type='submit' value='筛选'><a class='btnlink secondary' href='/irrigation/events'>重置</a></span></form><div class='tablewrap'><table class='event-table'><thead><tr><th>时间</th><th>等级</th><th>分类</th><th>事件</th><th>说明</th><th>操作</th></tr></thead><tbody>");

    EventRowsContext context{
        &filter,
        g_app->configuration(),
        (page - 1U) * 20U,
        20U,
        0,
        0};
    const bool readOk = status.eventStore.recordCount == 0 ||
                        g_app->readLatestEvents(0,
                                                status.eventStore.recordCount,
                                                sendEventRow,
                                                &context);
    if (readOk && context.emitted == 0) {
        Esp32BaseWeb::sendChunk("<tr><td class='event-empty' colspan='6'>");
        Esp32BaseWeb::sendChunk(status.eventStore.recordCount == 0
                                    ? "还没有事件记录。"
                                    : "当前筛选条件下没有事件。"
                               );
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    if (!readOk) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                 "本页事件读取失败",
                                 "请刷新页面；如果问题持续，请检查事件存储状态。");
    } else if (context.matched != 0) {
        Esp32BaseWeb::Pagination pagination{};
        pagination.path = "/irrigation/events";
        pagination.query = filter.query;
        pagination.page = page;
        pagination.perPage = 20;
        pagination.total = context.matched;
        Esp32BaseWeb::sendPagination(pagination);
    }
    Esp32BaseWeb::endPanel();
    endPage();
}

void IrrigationWeb::settings() {
    if (!Esp32BaseWeb::checkAuth()) return;
    Esp32BaseWeb::redirectSeeOther("/esp32base/app-config");
}

void IrrigationWeb::statusApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const WateringStatus status = g_app->wateringStatus();
    if (!Esp32BaseWeb::beginResponse(200, "application/json")) return;
    Esp32BaseWeb::sendChunk("{\"ready\":"); Esp32BaseWeb::sendChunk(g_app->businessReady() ? "true" : "false");
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
        Esp32BaseWeb::sendChunk(",\"calibrationFlowEstablishedMs\":"); sendUnsigned(zone.calibrationFlowEstablishedMs);
        Esp32BaseWeb::sendChunk(",\"calibrationSteadyStartedMs\":"); sendUnsigned(zone.calibrationSteadyStartedMs);
        Esp32BaseWeb::sendChunk(",\"calibrationStartupPulses\":"); sendUnsigned(zone.calibrationStartupPulses);
        Esp32BaseWeb::sendChunk(",\"calibrationSteadyDurationMs\":"); sendUnsigned(zone.calibrationSteadyDurationMs);
        Esp32BaseWeb::sendChunk(",\"calibrationSteadyPulses\":"); sendUnsigned(zone.calibrationSteadyPulses);
        Esp32BaseWeb::sendChunk(",\"calibrationStopDurationMs\":"); sendUnsigned(zone.calibrationStopDurationMs);
        Esp32BaseWeb::sendChunk(",\"calibrationStopPulses\":"); sendUnsigned(zone.calibrationStopPulses);
        Esp32BaseWeb::sendChunk(",\"calibrationPulseRateX100\":"); sendUnsigned(zone.calibrationPulseRateX100);
        Esp32BaseWeb::sendChunk(",\"calibrationLatestPulseRateX100\":"); sendUnsigned(zone.calibrationLatestPulseRateX100);
        Esp32BaseWeb::sendChunk(",\"calibrationWindowSec\":"); sendUnsigned(zone.calibrationWindowSec);
        Esp32BaseWeb::sendChunk(",\"calibrationRequiredWindows\":"); sendUnsigned(zone.calibrationRequiredWindows);
        Esp32BaseWeb::sendChunk(",\"calibrationAllowedVariationPercent\":"); sendUnsigned(zone.calibrationAllowedVariationPercent);
        Esp32BaseWeb::sendChunk(",\"calibrationCollectedWindows\":"); sendUnsigned(zone.calibrationCollectedWindows);
        Esp32BaseWeb::sendChunk(",\"calibrationSteadyDetected\":"); Esp32BaseWeb::sendChunk(zone.calibrationSteadyDetected ? "true" : "false");
        Esp32BaseWeb::sendChunk(",\"calibrationSteadyLaterUnstable\":"); Esp32BaseWeb::sendChunk(zone.calibrationSteadyLaterUnstable ? "true" : "false");
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
