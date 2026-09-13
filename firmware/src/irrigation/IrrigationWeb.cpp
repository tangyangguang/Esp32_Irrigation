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
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeStyle);
    IrrigationWebAssets::send(IrrigationWebAssets::Asset::HomeScript);
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
        case WateringSource::Manual: return "手动浇水";
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
    html("<nav class='day-nav' aria-label='选择日期'><a class='btnlink secondary' href='?date="); dayText(day - 1);
    html("' aria-label='前一天'>‹</a><form method='get'><input aria-label='日期' type='date' name='date' value='"); dayText(day);
    html("' onchange='this.form.submit()'><noscript><button>查看</button></noscript></form><a class='btnlink secondary' href='?date="); dayText(day + 1);
    html("' aria-label='后一天'>›</a><a href='?'>今天</a></nav>");
}
void conditions() {
    html("<section id='conditions' class='conditions' aria-label='设备状态'>");
    if (!g_app->businessReady()) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,"灌溉功能未就绪","输出保持关闭；请到设备设置检查配置、存储及系统状态。");
    if (g_app->recordStorageFault()) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,"浇水记录无法可靠保存","暂不能开始新任务。正常历史轮转不会导致此状态；请检查存储或任务恢复信息。");
    if (g_app->eventStorageFault()) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN,"操作记录保存异常","部分配置操作暂不可用，请检查设备存储。");
    if (g_app->schedulerStorageFault()) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,"自动调度状态不可用","自动计划不能执行，请检查设备状态。");
    const auto time = g_app->schedulerTimeState();
    if (time != WateringScheduler::TimeState::Ready) {
        html("<details class='issue'><summary>");
        html(time == WateringScheduler::TimeState::RtcRollback ? "设备时间倒退，自动计划暂停" : "设备时间尚不可信，自动计划暂停");
        html("</summary><p>等待硬件时钟或网络校时恢复；手动浇水仍受现场保护。错过的计划不会自动补浇。</p></details>");
    }
    const auto rtc = g_app->eventConditionState(1);
    if (rtc == IrrigationEvents::ConditionDisplayState::Active || rtc == IrrigationEvents::ConditionDisplayState::ConfirmingRecovery)
        html("<details class='issue'><summary>硬件时钟不可用</summary><p>有可信网络时间时仍可自动执行；失去可信时间后自动计划暂停。请检查 RTC 接线与电池。</p></details>");
    if (g_app->unexpectedFlowAlarm()) html("<details class='issue danger'><summary>关闭输出后仍检测到水流</summary><p>可能存在阀未关严、余流或其他水流来源，请检查现场；必要时关闭上游水源。设备无法仅凭总流量计判定具体漏水位置。</p></details>");
    html("</section>");
}
void renderDay(uint32_t day) {
    if (!day) { html("<p class='muted'>设备时间未知，日期统计暂不可用。仍可查看全部浇水记录。</p>"); return; }
    const auto daily = g_app->wateringDay(day); const auto* config = g_app->configuration();
    if (!daily.readable) { html("<p class='issue'>记录暂时无法读取，不能据此判断没有浇水。</p>"); return; }
    html("<div class='daily-grid'>");
    bool shown = false;
    for (size_t i = 0; i < daily.zones.size(); ++i) {
        const auto& z = daily.zones[i];
        if ((!config || !config->zones[i].enabled) && !z.count && !z.failures && !z.unknown && !z.active) continue;
        shown = true;
        html("<a class='daily-zone' href='/irrigation/records?date="); dayText(day); html("&zone="); sendUnsigned(i + 1); html("'><span class='zone-title'>");
        sendZoneName(config, i); html("</span><strong>");
        if (z.unknown && !z.count) html("时长未知"); else sendDuration(z.seconds);
        html("</strong><span>"); sendUnsigned(z.count); html(" 次出水");
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
    if (daily.truncated) html("<p class='issue'>较早历史已滚动淘汰，本日统计可能不完整。</p>");
    if (daily.unknownTimeCount) { html("<p class='muted'>另有 "); sendUnsigned(daily.unknownTimeCount); html(" 条时间未知的记录未计入日期统计。</p>"); }
    html("<p class='caption'>按各水路开始日期归属；跨日任务不拆分。时长为实际浇水时间，水量为估算值。</p>");
}
const char* outcome(const WateringRecordPayload& p) {
    if (p.result == WateringResult::Incomplete) return "运行中断 · 结果不完整";
    return recordOutcomeName(p);
}
void recordDetail(const StoredWateringRecord& record) {
    const auto& p = record.payload; const auto* config = g_app->configuration();
    html("<p><a href='/irrigation/records'>‹ 浇水记录</a></p><section class='panel'><h2>"); escaped(outcome(p));
    html("</h2><p>"); escaped(stopReasonName(p.stopReason)); html("</p><div class='detail-overview'><div>开始<br><b>"); epochText(p.startedEpoch);
    html("</b></div><div>方式<br><b>"); escaped(sourceName(p.source)); if(p.targetMode==WateringTargetMode::Volume)html(" · 按水量"); html("</b></div><div>实际浇水<br><b>");
    const auto total = WateringRecordCodec::calculateTotals(p);
    if (p.result == WateringResult::Incomplete) html("未知"); else sendDuration(total.actualWateringSec);
    html("</b></div><div>估算用水<br><b>"); if (p.result == WateringResult::Incomplete) html("未知"); else sendWaterVolume(total.estimatedWaterMl);
    html("</b></div></div>");
    if (p.result == WateringResult::Incomplete) html("<p class='issue'>设备重启时发现未收尾任务。可能尚未出水，也可能中途停止；未保存的执行进度与水量无法恢复。设备不会自动续浇。</p>");
    else { html("<p class='caption'>任务历时 "); sendDuration(record.timing.durationSec); html("，包含启动、等待出水和水路切换。</p>"); }
    html("</section><section class='panel'><h2>各水路结果</h2>");
    for (size_t i = 0; i < p.zones.size(); ++i) {
        const auto& z = p.zones[i]; if (!z.plannedDurationSec) continue;
        html("<article class='zone-detail'><h3>"); sendZoneName(config,i); html("</h3><p>");
        const bool unknown = z.flags & WateringRecordCodec::kZoneFlagUnknown;
        escaped(unknown ? "执行结果未知" : zoneResultName(z.result)); html("</p><div class='detail-overview'><div>实际 / 目标时长<br><b>");
        if (unknown) html("未知"); else sendDuration(z.actualWateringSec);
        html(" / "); sendDuration(z.plannedDurationSec); html("</b></div><div>估算用水");
        if(z.targetWaterMl) html(" / 目标水量"); html("<br><b>");
        if(unknown) html("未知"); else sendWaterVolume(z.estimatedWaterMl);
        if(z.targetWaterMl) { html(" / "); sendWaterVolume(z.targetWaterMl); }
        html("</b></div><div>整段平均流量<br><b>"); if(unknown) html("未知"); else sendFlowRate(z.averageFlowMlPerMinute);
        html("</b></div><div>当时基准<br><b>");
        if(z.flags & WateringRecordCodec::kZoneFlagFlowBaselineAvailable) sendFlowRate(z.baselineFlowMlPerMinute); else html("未设置或未执行");
        html("</b></div></div>");
        if(z.flags & WateringRecordCodec::kZoneFlagLowFlow) html("<p class='issue'>本次出现低流量报警</p>");
        if(z.flags & WateringRecordCodec::kZoneFlagHighFlow) html("<p class='issue'>本次出现高流量报警</p>");
        html("<details><summary>计量依据</summary><p>实际浇水时长从水流建立后计算；水量包含实际采集的出水脉冲。累计脉冲：");
        if(unknown) html("未知"); else sendUnsigned(z.pulseCount);
        html("。未触发报警不等于流量一定正常；没有基准时不作高低流量判断。</p></details></article>");
    }
    html("<p class='caption'>历史时长、水量和基准使用当时保存的值；水路名称显示当前配置。</p></section>");
}
struct HistoryRows { uint32_t day=0, zone=0, offset=0, matched=0, shown=0; const char* result=nullptr; };
void historyRow(const StoredWateringRecord& record, void* user) {
    auto& q = *static_cast<HistoryRows*>(user); const auto& p = record.payload;
    bool matches = !q.day && !q.zone;
    for(size_t i=0;i<p.zones.size();++i) {
        const auto& z=p.zones[i]; if(!z.plannedDurationSec || (q.zone && q.zone != i+1)) continue;
        if(!q.day || (p.startedEpoch && WateringHistory::localDay(WateringHistory::zoneEpoch(p,i))==q.day)) matches=true;
    }
    if(!matches) return;
    if(q.result && !strcmp(q.result,"issues") && p.result==WateringResult::Completed && !recordFlowAlertZoneCount(p)) return;
    if(q.matched++ < q.offset || q.shown>=20) return; ++q.shown;
    html("<a class='history-row' href='/irrigation/records?id="); sendUnsigned(record.recordId); html("'><div><b>"); epochText(p.startedEpoch); html(" · "); escaped(sourceName(p.source));
    html("</b><p>"); escaped(outcome(p)); html(" · "); escaped(stopReasonName(p.stopReason)); html("</p></div><div class='history-metric'><b>");
    if(p.result==WateringResult::Incomplete) html("时长未知"); else sendDuration(WateringRecordCodec::calculateTotals(p).actualWateringSec);
    html("</b><small>"); if(p.result==WateringResult::Incomplete) html("水量未知"); else sendWaterVolume(WateringRecordCodec::calculateTotals(p).estimatedWaterMl); html("</small></div></a>");
}
struct AuditRows { uint32_t day=0, offset=0, matched=0, shown=0; bool skippedOnly=false; };
void auditRow(const IrrigationEvents::EventRecord& event, void* user) {
    auto& q=*static_cast<AuditRows*>(user); uint32_t epoch=0;
    Esp32BaseRecordStore::resolveCompletedEpoch(event.timing,epoch);
    if(q.day && (!epoch || WateringHistory::localDay(epoch)!=q.day)) return;
    if(q.skippedOnly && event.eventCode!=uint32_t(IrrigationEvents::EventCode::AutomaticPlanSkipped)) return;
    if(q.matched++ < q.offset || q.shown>=20) return; ++q.shown;
    char title[192]{}, summary[256]{};
    IrrigationEvents::formatTitle(event,title,sizeof(title)); IrrigationEvents::formatSummary(event,summary,sizeof(summary));
    html("<article class='history-row'><div><b>"); escaped(title); html("</b><p>"); escaped(summary); html("</p></div><small>"); epochText(epoch); html("</small></article>");
}

} // namespace

bool IrrigationWeb::registerRoutes(IrrigationApp& app) {
    g_app=&app;
    Esp32BaseWeb::setDeviceName("智能浇水"); Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP); Esp32BaseWeb::setHomePath("/irrigation");
    return IrrigationWebAssets::registerAssets() &&
        Esp32BaseWeb::addPage("/irrigation","首页",overview) &&
        Esp32BaseWeb::addPage("/irrigation/plans","计划",plans) &&
        Esp32BaseWeb::addPage("/irrigation/records","记录",records) &&
        Esp32BaseWeb::addPage("/irrigation/settings","设备设置",settings) &&
        Esp32BaseWeb::addRoute("/irrigation",Esp32BaseWeb::METHOD_POST,overview) &&
        Esp32BaseWeb::addRoute("/irrigation/manual",Esp32BaseWeb::METHOD_GET,manual) &&
        Esp32BaseWeb::addRoute("/irrigation/manual",Esp32BaseWeb::METHOD_POST,manual) &&
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
        if(!Esp32BaseWeb::checkPostAllowed("irrigation_stop")) return;
        redirectResult("/irrigation",actionIs("stop") && g_app->stopWatering()); return;
    }
    if(g_app->wateringActive()) { activeTask(); return; }
    if(!beginPage("智能浇水","本地运行 · 按计划照料每条水路")) return;
    const auto now=Esp32BaseTime::snapshot();
    html("<p class='caption'>设备时间 "); epochText(now.synced?now.epochSec:0,"%Y-%m-%d %H:%M:%S"); html(" · 页面更新于本次打开</p>");
    conditions();
    html("<section class='panel'><h2>每日浇水</h2>"); const uint32_t day=selectedDay(); if(day) dateNav(day); renderDay(day); html("</section>");
    html("<section class='panel'><h2>下一次自动浇水</h2>");
    const auto next=g_app->nextAutomaticWatering(); const auto automatic=g_app->automaticWateringState();
    if(automatic.mode!=AutomaticWateringMode::Enabled) {
        html("<p class='issue'>自动浇水已暂停");
        if(automatic.mode==AutomaticWateringMode::PausedUntil) { html("，恢复时间 "); epochText(automatic.resumeAtEpoch); }
        html("</p>");
    }
    if(next.status==NextAutomaticWateringStatus::Available) {
        html("<h3>"); epochText(next.scheduledEpoch); html("</h3><p>"); escaped(planNameById(g_app->configuration(),next.planId)); html("</p>");
        const auto* c=g_app->configuration(); uint32_t duration=0;
        if(c && next.planId) for(size_t i=0;i<c->zones.size();++i) if(c->zones[i].enabled) duration+=c->plans[next.planId-1].zoneDurationMinutes[i]*60U;
        html("<p>目标浇水 "); sendDuration(duration); html("；实际任务还包含启动与水路切换。</p>");
    } else if(next.status==NextAutomaticWateringStatus::NoEnabledPlans) html("<p>没有启用的自动计划。</p>");
    else if(automatic.mode==AutomaticWateringMode::Enabled) html("<p>时间条件暂不满足，下一次计划不可用。</p>");
    html("<a href='/irrigation/plans'>管理计划与自动暂停 ›</a></section><p><a class='btnlink secondary' href='/irrigation/manual'>手动浇水</a></p>");
    Esp32BaseRecordStore::StoreStatus history{}; g_app->readWateringRecordStoreStatus(history);
    html("<div data-home-watch='idle' data-record-next='"); sendUnsigned(history.nextRecordId); html("'></div>"); endPage();
}
void IrrigationWeb::manual() {
    const auto* c=g_app->configuration(); const bool post=Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST);
    WateringStartResult result=WateringStartResult::InvalidRequest;
    if(post) {
        if(!Esp32BaseWeb::checkPostAllowed("irrigation_manual")) return;
        if(c && paramIs("mode","volume")) {
            uint32_t zone=0; char liters[20]{}; uint32_t ml=0;
            if(uintParam("zone_id",1,BoardPins::kZoneCount,zone) && getParam("liters",liters,sizeof(liters)) &&
               IrrigationConfigRules::parseLitersPerMinute(liters,ml)) {
                WateringRequest request{}; request.source=WateringSource::Manual;
                request.targetMode=WateringTargetMode::Volume; request.purpose=WateringPurpose::Normal;
                request.stepCount=1; request.steps[0]={uint8_t(zone),uint32_t(c->runLimits.maximumZoneDurationMinutes)*60U,ml};
                result=g_app->startWatering(request);
            }
        } else if(c) {
            WateringRequest request{}; request.source=WateringSource::Manual; request.purpose=WateringPurpose::Normal;
            bool valid=true;
            for(size_t i=0;i<c->zones.size();++i) {
                if(!c->zones[i].enabled) continue;
                char name[12]; uint32_t seconds=0;
                std::snprintf(name,sizeof(name),"zone%u",unsigned(i+1));
                if(!uintParam(name,0,c->runLimits.maximumZoneDurationMinutes*60U,seconds)) { valid=false; break; }
                if(seconds) request.steps[request.stepCount++]={uint8_t(i+1),seconds,0};
            }
            if(valid) result=g_app->startWatering(request);
        }
        if(result==WateringStartResult::Started) { Esp32BaseWeb::redirectSeeOther("/irrigation"); return; }
    }
    if(!beginPage("手动浇水","只影响本次操作，不修改计划")) return;
    if(post) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,"未能开始浇水",
        result==WateringStartResult::Busy?"设备正在执行其他任务，请先查看首页。":
        result==WateringStartResult::PreviousResultPending?"上一任务正在保存结果，请检查记录存储。":
        result==WateringStartResult::NotReady?"设备或任务记录未就绪，请查看首页提示。":"请检查水路、时长和水量输入。");
    if(!c) { html("<p>设备配置未就绪。</p>"); endPage(); return; }
    bool hasZones=false; for(const auto& z:c->zones) hasZones|=z.enabled;
    if(!hasZones || g_app->wateringActive()) { html("<p>请先启用水路，并等待当前任务结束。</p><a href='/irrigation'>返回首页</a>"); endPage(); return; }
    const bool volume=paramIs("mode","volume");
    if(volume) html("<p><a href='/irrigation/manual'>‹ 返回按时间浇水</a></p>");
    else html("<details><summary>其他浇水方式</summary><a href='/irrigation/manual?mode=volume'>单条水路按水量浇水</a></details>");
    if(!volume) {
        html("<details><summary>从已有计划填入时长</summary><div class='actions'>");
        for(const auto& plan:c->plans) if(plan.configured) {
            html("<button type='button' class='secondary' data-fill-times='");
            for(size_t i=0;i<plan.zoneDurationMinutes.size();++i) { if(i) html(","); sendUnsigned(plan.zoneDurationMinutes[i]*60U); }
            html("'>"); escaped(plan.name.data()); html("</button>");
        }
        html("</div><p>只填入本次时长，之后可修改。</p></details>");
    }
    html("<form method='post' action='/irrigation/manual' class='panel' data-dirty-form><input type='hidden' name='mode' value='"); html(volume?"volume":"time"); html("'>");
    if(volume) {
        html("<label>选择水路<select name='zone_id'>"); uint32_t chosen=0; uintParam("zone_id",1,6,chosen);
        for(size_t i=0;i<c->zones.size();++i) if(c->zones[i].enabled) { html("<option value='"); sendUnsigned(i+1); html("'"); if(chosen==i+1)html(" selected");html(">");escaped(c->zones[i].name.data());html("</option>"); }
        html("</select></label><label>目标水量（L）<input name='liters' type='number' min='0.1' step='0.1' max='");sendUnsigned(c->runLimits.maximumSingleOutputLiters);html("' value='");char value[20]{};if(getParam("liters",value,sizeof(value)))escaped(value);else html("1");html("' required></label><p class='caption'>水量依据流量计估算，阀门响应与余流会影响结果。达到最长运行时间仍未达目标时，任务停止并记录失败。</p>");
    } else {
        html("<p>选择需要的水路并设置时长；0 表示不执行，按水路顺序运行。</p>");
        for(size_t i=0;i<c->zones.size();++i) if(c->zones[i].enabled) {
            html("<label class='manual-zone'><span>");escaped(c->zones[i].name.data());html("</span><input aria-label='浇水时长，秒' type='number' min='0' step='1' name='zone");sendUnsigned(i+1);html("' max='");sendUnsigned(c->runLimits.maximumZoneDurationMinutes*60U);html("' value='");
            char field[12],value[20]{};std::snprintf(field,sizeof(field),"zone%u",unsigned(i+1));if(getParam(field,value,sizeof(value)))escaped(value);else html("0");html("'><span>秒</span></label>");
        }
        html("<p id='manual-summary' aria-live='polite'></p>");
    }
    html("<button type='submit'>开始本次浇水</button></form>"); endPage();
}
void IrrigationWeb::settings() {
    if(!beginPage("设备设置","低频配置与维护"))return;
    html("<section class='panel settings-links'><a href='/irrigation/zones'><b>水路设置</b><span>名称、启用状态、流量基准</span></a><a href='/esp32base/app-config'><b>计量、保护与硬件参数</b><span>水量换算、流量保护、阀与泵</span></a><a href='/esp32base/system'><b>设备状态与系统维护</b><span>时间、版本、存储、升级与重启</span></a><a href='/esp32base/logs'><b>系统日志</b><span>设备诊断记录</span></a></section>");
    Esp32BaseRecordStore::StoreStatus status{};
    if(g_app->readWateringRecordStoreStatus(status)) { html("<section class='panel'><h2>本地浇水历史</h2><p>已保存 ");sendUnsigned(status.recordCount);html(" 条。按存储预算分段滚动保留最新历史，不等待平台同步。</p><p class='caption'>较早数据可能被淘汰；普通轮转不会阻止继续浇水。真实写入或校验故障会另行提示。</p></section>"); }
    conditions(); endPage();
}
void IrrigationWeb::plans() {
    const bool post=Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST); bool failed=false;
    uint32_t edit=0; uintParam("edit",1,kWateringPlanCount,edit);
    if(post) {
        if(!Esp32BaseWeb::checkPostAllowed("irrigation_plans"))return;
        bool ok=false;
        if(actionIs("pause_indefinitely"))ok=g_app->pauseAutomaticWateringIndefinitely();
        else if(actionIs("resume"))ok=g_app->resumeAutomaticWatering();
        else if(actionIs("pause_until")) { char value[24]{};uint32_t epoch=0;ok=getParam("resume_at",value,sizeof(value))&&IrrigationTime::parseLocalDateTimeUtc8(value,epoch)&&g_app->pauseAutomaticWateringUntil(epoch); }
        else { uintParam("plan_id",1,kWateringPlanCount,edit);ok=savePlanFromRequest(); }
        if(ok){redirectResult("/irrigation/plans",true);return;} failed=true;
    }
    if(!beginPage(edit?"编辑浇水计划":"浇水计划","计划按设备本地时间执行，不依赖平台连接"))return;
    const auto* c=g_app->configuration(); if(!c){html("<p>配置不可用。</p>");endPage();return;}
    if(failed) { html("<p class='issue'>未保存成功：");escaped(g_app->configurationError());html("。请检查时间条件、参数及设备状态；若其他页面修改了配置，请刷新后重试。</p>"); }
    if(edit) {
        const auto& p=c->plans[edit-1];html("<p><a href='/irrigation/plans'>‹ 计划列表</a></p><form class='panel' method='post' action='/irrigation/plans' data-dirty-form>");hidden("plan_id",edit);hidden("revision",c->revision);html("<input type='hidden' name='action' value='save'><label>名称<input name='name' maxlength='20' required value='");
        char name[kObjectNameCapacity]{};if(post&&getParam("name",name,sizeof(name)))escaped(name);else escaped(p.name.data());html("'></label><label><input type='checkbox' name='schedule_enabled'");if(post?Esp32BaseWeb::hasParam("schedule_enabled"):p.scheduleEnabled)html(" checked");html("> 自动按时执行</label><p class='caption'>关闭后保留内容，可用于填入手动浇水时长。</p><h3>每天开始时间</h3><div class='time-inputs'>");
        for(size_t i=0;i<p.startMinutes.size();++i) { char field[8],value[8]{};std::snprintf(field,sizeof(field),"time%u",unsigned(i+1));html("<label>时间 ");sendUnsigned(i+1);html("<input type='time' name='");escaped(field);html("' value='");
            if(post&&getParam(field,value,sizeof(value)))escaped(value);else if(p.startMinutes[i]!=kUnusedStartMinute){std::snprintf(value,sizeof(value),"%02u:%02u",p.startMinutes[i]/60,p.startMinutes[i]%60);escaped(value);} html("'></label>"); }
        html("</div><h3>各水路时长</h3>");
        for(size_t i=0;i<c->zones.size();++i)if(c->zones[i].enabled){html("<label class='manual-zone'><span>");escaped(c->zones[i].name.data());html("</span><input type='number' min='0' name='zone");sendUnsigned(i+1);html("' max='");sendUnsigned(c->runLimits.maximumZoneDurationMinutes);html("' value='");char field[8],value[20]{};std::snprintf(field,sizeof(field),"zone%u",unsigned(i+1));if(post&&getParam(field,value,sizeof(value)))escaped(value);else sendUnsigned(p.zoneDurationMinutes[i]);html("'><span>分钟</span></label>");}
        html("<p class='caption'>0 表示不执行。水路依次运行；保存不会立即出水，也不改变当前任务。不同计划执行时间过近时，后一个计划可能因设备忙而跳过，不会补浇。</p><button>保存计划</button></form>");
        if(p.configured){html("<details><summary>删除此计划</summary><form method='post' action='/irrigation/plans' onsubmit=\"return confirm('删除后不再按此计划自动启动，确认删除？')\">");hidden("plan_id",edit);hidden("revision",c->revision);html("<input type='hidden' name='action' value='delete'><button class='danger'>删除计划</button></form></details>");}endPage();return;
    }
    const auto automatic=g_app->automaticWateringState();html("<section class='panel'><h2>自动执行</h2><p>");
    html(automatic.mode==AutomaticWateringMode::Enabled?"自动执行已开启": "自动执行已暂停");if(automatic.mode==AutomaticWateringMode::PausedUntil){html("，恢复于 ");epochText(automatic.resumeAtEpoch);}html("</p>");
    if(automatic.mode!=AutomaticWateringMode::Enabled)html("<form method='post' action='/irrigation/plans'><input type='hidden' name='action' value='resume'><button>恢复自动浇水</button></form>");
    html("<details><summary>暂停或修改恢复时间</summary><p>只阻止之后的自动启动，不停止当前任务，不影响手动浇水；错过的计划不会补浇。</p><form method='post' action='/irrigation/plans'><input type='hidden' name='action' value='pause_until'><label>暂停至<input type='datetime-local' name='resume_at' required></label><button class='secondary'>按此时间恢复</button></form><form method='post' action='/irrigation/plans'><input type='hidden' name='action' value='pause_indefinitely'><button class='secondary'>持续暂停，手动恢复</button></form></details></section>");
    uint32_t empty=0;for(const auto& p:c->plans){if(!p.configured){if(!empty)empty=p.id;continue;}html("<a class='panel plan-link' href='/irrigation/plans?edit=");sendUnsigned(p.id);html("'><h3>");escaped(p.name.data());html("</h3><p>");html(p.scheduleEnabled?"自动执行 · ":"未开启定时执行 · ");uint32_t total=0;for(size_t i=0;i<c->zones.size();++i)if(c->zones[i].enabled)total+=p.zoneDurationMinutes[i]*60U;sendDuration(total);html("</p><p>");for(auto time:p.startMinutes)if(time!=kUnusedStartMinute){char value[8];std::snprintf(value,sizeof(value),"%02u:%02u ",time/60,time%60);escaped(value);}html("</p></a>");}
    if(empty){html("<a class='btnlink' href='/irrigation/plans?edit=");sendUnsigned(empty);html("'>新建计划</a>");}endPage();
}
void IrrigationWeb::records() {
    if(!beginPage("浇水记录","实际时长为主，估算水量为辅"))return;
    uint32_t id=0;if(uintParam("id",1,UINT32_MAX,id)){StoredWateringRecord record{};if(g_app->readWateringRecordById(id,record)==Esp32BaseRecordStore::RecordReadResult::Found)recordDetail(record);else html("<p class='issue'>记录无法读取或已被滚动淘汰。</p>");endPage();return;}
    HistoryRows q{};char date[12]{},result[16]{};if(getParam("date",date,sizeof(date)) && date[0])q.day=selectedDay();uintParam("zone",1,6,q.zone);uintParam("offset",0,UINT32_MAX-20,q.offset);getParam("result",result,sizeof(result));q.result=result;
    html("<form method='get' class='history-filter'><label>日期<input type='date' name='date' value='");escaped(date);html("'></label><label>水路<select name='zone'><option value=''>全部水路</option>");
    for(size_t i=0;i<BoardPins::kZoneCount;++i){html("<option value='");sendUnsigned(i+1);html("'");if(q.zone==i+1)html(" selected");html(">");sendZoneName(g_app->configuration(),i);html("</option>");}
    html("</select></label><label>结果<select name='result'><option value=''>全部结果</option><option value='issues'");if(!strcmp(result,"issues"))html(" selected");html(">停止、异常或不完整</option></select></label><button>筛选</button><a href='/irrigation/records'>清除</a></form><section class='panel'>");
    Esp32BaseRecordStore::StoreStatus state{};const bool ok=g_app->readWateringRecordStoreStatus(state)&&state.ready&&(state.recordCount==0||g_app->readLatestWateringRecords(0,state.recordCount,historyRow,&q));
    if(!ok)html("<p class='issue'>记录读取失败，不能据此判断没有浇水。</p>");else if(!q.shown)html("<p>当前条件下暂无浇水记录。</p>");html("</section>");
    if(q.offset || q.matched>q.offset+q.shown){html("<nav class='actions'>");for(int d=-1;d<=1;d+=2){if(d<0&&!q.offset)continue;if(d>0&&q.matched<=q.offset+q.shown)continue;html("<a class='btnlink secondary' href='?date=");escaped(date);html("&zone=");sendUnsigned(q.zone);html("&result=");escaped(result);html("&offset=");sendUnsigned(d<0?(q.offset>20?q.offset-20:0):q.offset+20);html("'>");html(d<0?"上一页":"下一页");html("</a>");}html("</nav>");}
    if(state.oldestRecordId>1)html("<p class='caption'>本地历史按预算滚动保留，较早记录可能已淘汰。</p>");
    html("<section class='panel'><h2>设备计划未执行</h2><p class='caption'>按日期查看，涵盖全部水路。</p>");IrrigationEvents::EventStatus status{};AuditRows a{};a.day=q.day;a.skippedOnly=true;
    const bool auditReadable=g_app->readEventStatus(status) && status.eventStore.ready &&
        (status.eventStore.recordCount==0 || g_app->readLatestEvents(0,status.eventStore.recordCount,auditRow,&a));
    if(!auditReadable) html("<p class='issue'>计划未执行记录暂时无法读取。</p>");
    else if(!a.shown)html("<p class='muted'>当前日期条件下没有已记录的跳过事项；这不证明离线或停机时段均已执行。</p>");html("</section><p><a href='/irrigation/events'>操作与设备异常历史 ›</a></p>");endPage();
}
void IrrigationWeb::events() {
    if(!beginPage("操作与设备异常历史","查看影响计划和水路的必要变化"))return;
    html("<p><a href='/irrigation/records'>‹ 浇水记录</a></p><section class='panel'>");IrrigationEvents::EventStatus status{};AuditRows q{};uintParam("offset",0,UINT32_MAX-20,q.offset);
    if(!g_app->readEventStatus(status)||!status.eventStore.ready||(status.eventStore.recordCount && !g_app->readLatestEvents(0,status.eventStore.recordCount,auditRow,&q)))html("<p>历史暂时无法读取，不能据此判断没有发生过事项。</p>");else if(!q.shown)html("<p>暂无历史事项。</p>");html("</section>");
    if(q.matched>q.offset+q.shown){html("<a href='?offset=");sendUnsigned(q.offset+20);html("'>更早记录 ›</a>");}endPage();
}
void IrrigationWeb::zones() {
    bool failed=false;
    if(Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if(!Esp32BaseWeb::checkPostAllowed("irrigation_zones")) return;
        if(saveZoneFromRequest()) { redirectResult("/irrigation/zones",true); return; }
        failed=true;
    }
    if(!beginPage("水路设置","名称、启用状态与正常预期流量"))return;
    html("<p><a href='/irrigation/settings'>‹ 设备设置</a></p>");
    if(failed) { html("<p class='issue'>保存失败："); escaped(g_app->configurationError()); html("</p>"); }
    const auto* c=g_app->configuration(); if(!c){html("<p>配置不可用。</p>");endPage();return;}
    uint32_t postedZone=0;uintParam("zone_id",1,6,postedZone);
    for(size_t i=0;i<c->zones.size();++i) {
        const auto& z=c->zones[i];html("<form class='panel' method='post' action='/irrigation/zones' data-dirty-form>");hidden("zone_id",i+1);hidden("revision",c->revision);
        html("<h2>水路 ");sendUnsigned(i+1);html("</h2><label>名称<input name='name' required maxlength='20' value='");
        char name[kObjectNameCapacity]{};
        if(failed&&postedZone==i+1&&getParam("name",name,sizeof(name)))escaped(name);else escaped(z.name.data());html("'></label><label><input type='checkbox' name='enabled'");
        if(failed&&postedZone==i+1?Esp32BaseWeb::hasParam("enabled"):z.enabled)html(" checked");html("> 启用此水路</label><div class='actions'><button class='secondary'>保存水路</button><a href='/irrigation/zones/learning?zone=");sendUnsigned(i+1);html("'>设置或学习流量基准 ›</a></div></form>");
    }
    html("<p class='caption'>停用水路不删除历史；若影响已启用计划，系统会拒绝并说明原因。修改不改变正在运行的任务。</p>");endPage();
}
void IrrigationWeb::activeTask() {
    if (!beginPage("首页", "查看当前任务的实时状态")) return;
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
        Esp32BaseWeb::sendChunk("<a class='run-clock-warning' href='/irrigation#conditions'>硬件时钟不可用 · 断网后计划可能暂停</a>");
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
    html("<section class='panel'><h2>每日浇水</h2>"); renderDay(selectedDay()); html("</section>");
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
    Esp32BaseWeb::sendChunk("<p><a class='btnlink secondary' href='/irrigation/zones'>返回水路设置</a></p>");
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
