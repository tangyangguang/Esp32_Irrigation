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
        if (!uintParam(field, 0, 120, duration)) {
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
    }
    return "未知";
}

const char* resultName(WateringResult result) {
    switch (result) {
        case WateringResult::Completed: return "完成";
        case WateringResult::Stopped: return "用户停止";
        case WateringResult::Failed: return "异常停止";
        default: return "无";
    }
}

const char* resultToneClass(WateringResult result) {
    switch (result) {
        case WateringResult::Completed: return "ok";
        case WateringResult::Stopped: return "warn";
        case WateringResult::Failed: return "danger";
        default: return "";
    }
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

void sendRecordTime(const Esp32BaseRecordStore::RecordTiming& timing) {
    uint32_t epoch = 0;
    char text[32]{};
    if (Esp32BaseRecordStore::resolveCompletedEpoch(timing, epoch) &&
        Esp32BaseTime::formatEpoch(epoch, text, sizeof(text), "%Y-%m-%d %H:%M:%S")) {
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

void sendLiters(uint64_t milliliters) {
    char text[32];
    std::snprintf(text,
                  sizeof(text),
                  "%llu.%03llu L",
                  static_cast<unsigned long long>(milliliters / 1000ULL),
                  static_cast<unsigned long long>(milliliters % 1000ULL));
    Esp32BaseWeb::sendChunk(text);
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
    Esp32BaseWeb::sendChunk("<dialog id='");
    Esp32BaseWeb::sendChunk(dialogPrefix);
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("' class='panel eb-modal record-detail-dialog' data-eb-light-dismiss='1'><div class='record-detail-head'><div><span class='muted'>浇水记录</span><h2>#");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("</h2></div><button type='button' class='secondary record-detail-close' onclick='this.closest(\"dialog\").close()'>关闭</button></div><div class='record-detail-grid'><div><b>完成时间</b><span>");
    sendRecordTime(record.timing);
    Esp32BaseWeb::sendChunk("</span></div><div><b>来源</b><span>");
    sendRecordSource(record.payload, config);
    Esp32BaseWeb::sendChunk("</span></div><div><b>结果</b><span class='tag ");
    Esp32BaseWeb::sendChunk(resultToneClass(record.payload.result));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(resultName(record.payload.result));
    Esp32BaseWeb::sendChunk("</span></div><div><b>结束原因</b><span>");
    Esp32BaseWeb::sendChunk(stopReasonName(record.payload.stopReason));
    Esp32BaseWeb::sendChunk("</span></div><div><b>计划浇水时间</b><span>");
    sendDuration(totals.plannedDurationSec);
    Esp32BaseWeb::sendChunk("</span></div><div><b>实际浇水时间</b><span>");
    sendDuration(totals.actualWateringSec);
    Esp32BaseWeb::sendChunk("</span></div><div><b>整次历时</b><span>");
    sendDuration(record.timing.durationSec);
    Esp32BaseWeb::sendChunk("</span></div><div><b>总脉冲</b><span>");
    sendUnsigned64(totals.pulseCount);
    Esp32BaseWeb::sendChunk("</span></div><div><b>历史估算总水量</b><span>");
    if (recordHasCappedEstimate(record.payload)) {
        Esp32BaseWeb::sendChunk("至少 ");
    }
    sendWaterVolume(totals.estimatedWaterMl);
    Esp32BaseWeb::sendChunk("</span></div></div><div class='record-detail-section'><h3>水路明细</h3><p>名称按当前水路设置显示，历史时长、脉冲和水量保持记录当时的值。</p><div class='tablewrap'><table class='record-zone-table'><thead><tr><th>水路</th><th>结果</th><th>计划时间</th><th>实际时间</th><th>脉冲</th><th>历史估算水量</th><th>异常标记</th></tr></thead><tbody>");
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
        Esp32BaseWeb::sendChunk("</b><small>#");
        sendUnsigned(index + 1U);
        Esp32BaseWeb::sendChunk("</small></td><td data-label='结果'>");
        Esp32BaseWeb::sendChunk(zoneResultName(zone.result));
        Esp32BaseWeb::sendChunk("</td><td data-label='计划时间'>");
        sendDuration(zone.plannedDurationSec);
        Esp32BaseWeb::sendChunk("</td><td data-label='实际时间'>");
        sendDuration(zone.actualWateringSec);
        Esp32BaseWeb::sendChunk("</td><td data-label='脉冲'>");
        sendUnsigned(zone.pulseCount);
        Esp32BaseWeb::sendChunk("</td><td data-label='历史估算水量'>");
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
            Esp32BaseWeb::sendChunk("至少 ");
        }
        sendWaterVolume(zone.estimatedWaterMl);
        Esp32BaseWeb::sendChunk("</td><td data-label='异常标记'><div class='record-flags'>");
        if (zone.flags == 0) {
            Esp32BaseWeb::sendChunk("<span class='muted'>无</span>");
        } else {
            if ((zone.flags & WateringRecordCodec::kZoneFlagLowFlow) != 0) {
                Esp32BaseWeb::sendChunk("<span class='tag warn'>低流量</span>");
            }
            if ((zone.flags & WateringRecordCodec::kZoneFlagHighFlow) != 0) {
                Esp32BaseWeb::sendChunk("<span class='tag danger'>高流量</span>");
            }
            if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
                Esp32BaseWeb::sendChunk("<span class='tag warn'>水量达到记录上限</span>");
            }
        }
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div></div><div class='actions'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>关闭</button></div></dialog>");
}

void sendRecordRow(const StoredWateringRecord& record, void* user) {
    RecordRowsContext* context = static_cast<RecordRowsContext*>(user);
    const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(record.payload);
    if (context) ++context->emitted;
    Esp32BaseWeb::sendChunk("<tr><td data-label='编号' class='record-id'>#");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("</td><td data-label='完成时间' class='record-time'>");
    sendRecordTime(record.timing);
    Esp32BaseWeb::sendChunk("</td><td data-label='来源'>");
    sendRecordSource(record.payload, context ? context->config : nullptr);
    Esp32BaseWeb::sendChunk("</td><td data-label='结果'><span class='tag ");
    Esp32BaseWeb::sendChunk(resultToneClass(record.payload.result));
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::sendChunk(resultName(record.payload.result));
    Esp32BaseWeb::sendChunk("</span></td><td data-label='实际浇水时间' class='record-number'>");
    sendDuration(totals.actualWateringSec);
    Esp32BaseWeb::sendChunk("</td><td data-label='历史估算水量' class='record-number'>");
    if (recordHasCappedEstimate(record.payload)) Esp32BaseWeb::sendChunk("至少 ");
    sendWaterVolume(totals.estimatedWaterMl);
    Esp32BaseWeb::sendChunk("</td><td data-label='操作' class='record-action'><button type='button' class='btnlink info compact' onclick=\"document.getElementById('record-detail-");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("').showModal()\">查看详情</button>");
    sendRecordDetailDialog(record, context ? context->config : nullptr, "record-detail-");
    Esp32BaseWeb::sendChunk("</td></tr>");
}

void csvRecord(const StoredWateringRecord& record, void*) {
    const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(record.payload);
    char line[192];
    std::snprintf(line,
                  sizeof(line),
                  "%lu,%lu,%s,%u,%s,%lu,%llu,%llu\r\n",
                  static_cast<unsigned long>(record.recordId),
                  static_cast<unsigned long>(record.timing.completedEpochSec),
                  sourceName(record.payload.source),
                  record.payload.planId,
                  resultName(record.payload.result),
                  static_cast<unsigned long>(record.timing.durationSec),
                  static_cast<unsigned long long>(totals.pulseCount),
                  static_cast<unsigned long long>(totals.estimatedWaterMl));
    Esp32BaseWeb::sendChunk(line);
}

struct EventFilter {
    uint8_t level = 0;
    int8_t category = -1;
    char query[64]{};
};

struct EventRowsContext {
    const EventFilter* filter;
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
                           const char* dialogId) {
    char title[96]{};
    char summary[160]{};
    IrrigationEvents::formatTitle(event, title, sizeof(title));
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
    if (eventCode == IrrigationEvents::EventCode::WateringStoppedAbnormally ||
        eventCode == IrrigationEvents::EventCode::FlowDeviation) {
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关数据</h3><div class='event-detail-grid event-detail-business'><div><b>水路</b><span>");
        sendUnsigned(event.objectId);
        Esp32BaseWeb::sendChunk("</span></div><div><b>检测脉冲</b><span>");
        sendUnsigned(event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1));
        Esp32BaseWeb::sendChunk(" 个</span></div><div><b>实际时间</b><span>");
        sendUnsigned(event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2));
        Esp32BaseWeb::sendChunk(" 秒</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::ClosedValveFlow) {
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关数据</h3><div class='event-detail-grid event-detail-business'><div><b>窗口脉冲</b><span>");
        sendUnsigned(event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1));
        Esp32BaseWeb::sendChunk(" 个</span></div><div><b>检测窗口</b><span>");
        sendUnsigned(event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2));
        Esp32BaseWeb::sendChunk(" 秒</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::FlowCalibrationSaved) {
        const uint32_t coefficient = event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1);
        char coefficientText[24]{};
        std::snprintf(coefficientText, sizeof(coefficientText), "%lu.%02lu P/L",
                      static_cast<unsigned long>(coefficient / 100U),
                      static_cast<unsigned long>(coefficient % 100U));
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关数据</h3><div class='event-detail-grid event-detail-business'><div><b>稳态流量系数</b><span>");
        Esp32BaseWeb::sendChunk(coefficientText);
        Esp32BaseWeb::sendChunk("</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::ZoneFlowSaved) {
        char flow[20]{};
        IrrigationConfigRules::formatLitersPerMinute(
            event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1), flow, sizeof(flow));
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关数据</h3><div class='event-detail-grid event-detail-business'><div><b>水路</b><span>");
        sendUnsigned(event.objectId);
        Esp32BaseWeb::sendChunk("</span></div><div><b>基准流量</b><span>");
        if (event.value1 == 0) {
            Esp32BaseWeb::sendChunk("已清除");
        } else {
            Esp32BaseWeb::sendChunk(flow);
            Esp32BaseWeb::sendChunk(" L/min");
        }
        Esp32BaseWeb::sendChunk("</span></div></div></section>");
    } else if (eventCode == IrrigationEvents::EventCode::AutomaticPlanSkipped) {
        Esp32BaseWeb::sendChunk("<section class='event-detail-section'><h3>相关对象</h3><div class='event-detail-grid event-detail-business'><div><b>计划</b><span>");
        sendUnsigned(event.objectId);
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
    char title[96]{};
    char summary[160]{};
    char dialogId[28]{};
    IrrigationEvents::formatTitle(event, title, sizeof(title));
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
    sendEventDetailDialog(event, dialogId);
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
           Esp32BaseWeb::addApi("/irrigation/api/flow-history", flowHistoryApi) &&
           Esp32BaseWeb::addRoute("/irrigation/records.csv",
                                  Esp32BaseWeb::METHOD_GET,
                                  recordsCsv);
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
                if (Esp32BaseWeb::hasParam(name) && !uintParam(name, 0, 120, duration)) {
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
.home-eyebrow{display:block;margin-bottom:5px;color:var(--eb-muted);font-size:12px;font-weight:750}.home-hero h1{margin:0 0 6px;font-size:24px}.home-hero p{margin:0;color:var(--eb-muted)}.home-hero .btnlink{min-height:38px;padding:0 16px}
.home-hero-side{display:flex;align-items:center;justify-content:flex-end;gap:20px}.home-clock{width:170px}.home-clock-time{display:block;color:inherit;font-size:24px;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.1}.home-clock-date{display:block;margin-top:5px;color:var(--eb-muted);font-size:12px;font-weight:400;white-space:nowrap}.home-clock.pending .home-clock-time{color:var(--eb-warn);font-size:16px}.home-clock.pending .home-clock-date{white-space:normal}
.home-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin:12px 0}.home-card{display:flex;flex-direction:column;min-height:210px;padding:18px;border:1px solid var(--eb-line);border-radius:12px;background:#fff}.home-card-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:14px}.home-card-head h2{margin:0;font-size:17px}.home-main{font-size:22px;font-weight:760;line-height:1.3;overflow-wrap:anywhere}.home-sub{margin:6px 0 0;color:var(--eb-muted);overflow-wrap:anywhere}.home-facts{display:grid;gap:7px;margin:14px 0 0}.home-fact{display:grid;grid-template-columns:6.5em minmax(0,1fr);gap:8px;font-size:13px}.home-fact span:first-child{color:var(--eb-muted)}.home-card-actions{display:flex;align-items:center;gap:8px;margin-top:auto;padding-top:16px}.home-card-actions form{margin:0}.home-empty{color:var(--eb-muted);line-height:1.7}.home-note{margin-top:10px;padding:10px 12px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:13px}
.manual-modal{width:min(720px,calc(100vw - 28px));padding:20px!important}.manual-template{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:end;margin:14px 0;padding:12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft)}.manual-template label{margin:0}.manual-template select{width:100%;max-width:none;margin:5px 0 0}.manual-template-preview{display:none;grid-column:1/-1;flex-wrap:wrap;gap:6px;padding-top:2px}.manual-template-preview.visible{display:flex}.manual-template-preview span{padding:4px 8px;border:1px solid #d6e1e5;border-radius:999px;background:#fff;color:#526071;font-size:12px;white-space:nowrap}.manual-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px}.manual-zone{display:grid;grid-template-columns:minmax(0,1fr) 100px auto;gap:8px;align-items:center;padding:10px 11px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft)}.manual-zone label{margin:0;overflow-wrap:anywhere}.manual-zone input{width:100%;max-width:none;min-height:38px;margin:0}.manual-zone span{color:var(--eb-muted);font-weight:650}.manual-summary{margin-top:12px;padding:11px 13px;border:1px solid #cbdde5;border-radius:8px;background:var(--eb-info-soft)}.manual-summary b,.manual-summary span{display:block}.manual-summary span{margin-top:3px;color:var(--eb-muted);font-size:12px}.manual-modal .actions{margin-top:14px}
@media(max-width:760px){.home-hero,.home-grid{grid-template-columns:1fr}.home-hero{padding:16px}.home-hero h1{font-size:21px}.home-hero-side{align-items:stretch;flex-direction:column;gap:14px}.home-clock{width:100%}.home-hero .btnlink{width:100%}.home-card{min-height:0;padding:15px}.home-fact{grid-template-columns:1fr;gap:2px}.manual-modal{width:calc(100vw - 20px);padding:14px!important}.manual-template,.manual-grid{grid-template-columns:1fr}.manual-zone{grid-template-columns:minmax(0,1fr) 86px auto}.manual-modal .actions{display:grid;grid-template-columns:1fr 1fr}.manual-modal .actions button,.manual-modal .actions input{width:100%;margin:0}}
</style>)HTML");
    const AutomaticWateringState automatic = g_app->automaticWateringState();
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    const WateringScheduler::TimeState schedulerTime = g_app->schedulerTimeState();
    const bool timeTrusted = now.synced && schedulerTime == WateringScheduler::TimeState::Ready;
    const bool storageFault = g_app->recordStorageFault() || g_app->eventStorageFault() ||
                              g_app->schedulerStorageFault() || g_app->checkpointStorageFault();
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
    const char* heroTitle = "一切正常，当前没有浇水";
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
        heroTitle = "灌溉配置需要重新建立";
        heroDescription = "当前配置结构不兼容或文件无效，全部输出保持关闭。请先导出需要保留的记录，再到系统工具格式化文件系统并重新配置。";
        heroHref = "/esp32base";
        heroAction = "打开系统工具";
    } else if (g_app->unexpectedFlowAlarm()) {
        heroTone = " danger";
        heroTitle = "关阀后水流异常";
        heroDescription = "所有阀门关闭后仍检测到水流，请检查阀门和管路。";
        heroHref = "/irrigation/zones";
        heroAction = "查看水路设置";
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
    Esp32BaseWeb::sendChunk("<section class='home-hero");
    Esp32BaseWeb::sendChunk(heroTone);
    Esp32BaseWeb::sendChunk("'><div><span class='home-eyebrow'>");
    Esp32BaseWeb::writeHtmlEscaped(heroEyebrow);
    Esp32BaseWeb::sendChunk("</span><h1>");
    Esp32BaseWeb::writeHtmlEscaped(heroTitle);
    Esp32BaseWeb::sendChunk("</h1><p>");
    Esp32BaseWeb::writeHtmlEscaped(heroDescription);
    if (watering.active) {
        Esp32BaseWeb::sendChunk(" · ");
        Esp32BaseWeb::writeHtmlEscaped(wateringStateName(watering.state));
        Esp32BaseWeb::sendChunk(watering.flowEstablished ? " · 水流正常" : " · 正在等待水流");
    }
    Esp32BaseWeb::sendChunk("</p></div><div class='home-hero-side'><div id='home-clock' class='home-clock");
    if (!timeTrusted) Esp32BaseWeb::sendChunk(" pending");
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
    Esp32BaseWeb::sendChunk("</span></div>");
    if (heroHref) {
        Esp32BaseWeb::sendChunk("<a class='btnlink info' href='");
        Esp32BaseWeb::writeHtmlEscaped(heroHref);
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(heroAction);
        Esp32BaseWeb::sendChunk("</a>");
    } else if (config && hasEnabledZone) {
        Esp32BaseWeb::sendChunk("<button type='button' class='btnlink info' onclick=\"document.getElementById('manual-watering').showModal()\">");
        Esp32BaseWeb::writeHtmlEscaped(heroAction);
        Esp32BaseWeb::sendChunk("</button>");
    }
    Esp32BaseWeb::sendChunk("</div></section>");
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
            if (automatic.mode == AutomaticWateringMode::Enabled) {
                Esp32BaseWeb::sendChunk("<div class='home-main'>");
                Esp32BaseWeb::writeHtmlEscaped(value);
                Esp32BaseWeb::sendChunk("</div><p class='home-sub'>");
                Esp32BaseWeb::writeHtmlEscaped(secondary);
                Esp32BaseWeb::sendChunk("</p>");
            }
            Esp32BaseWeb::sendChunk("<div class='home-facts'><div class='home-fact'><span>");
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

    Esp32BaseWeb::sendChunk("<section class='home-card'><div class='home-card-head'><h2>最近一次浇水</h2><a class='btnlink compact secondary' href='/irrigation/records'>全部记录</a></div>");
    if (g_app->recordStorageFault()) {
        Esp32BaseWeb::sendChunk("<div class='home-empty'>浇水记录存储异常，暂时无法读取最近记录。</div>");
    } else if (!latest.found) {
        Esp32BaseWeb::sendChunk("<div class='home-empty'>还没有浇水记录。完成第一次实际出水后，这里会显示结果。</div>");
    } else {
        uint32_t completedEpoch = 0;
        const bool hasCompletedTime = Esp32BaseRecordStore::resolveCompletedEpoch(
            latest.record.timing, completedEpoch);
        const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(
            latest.record.payload);
        Esp32BaseWeb::sendChunk("<div class='home-main'>");
        if (hasCompletedTime && formatFriendlyDateTime(completedEpoch, now.epochSec, value, sizeof(value))) {
            Esp32BaseWeb::writeHtmlEscaped(value);
        } else {
            Esp32BaseWeb::sendChunk("最近完成的一次浇水");
        }
        Esp32BaseWeb::sendChunk("</div><p class='home-sub'>");
        Esp32BaseWeb::writeHtmlEscaped(resultName(latest.record.payload.result));
        Esp32BaseWeb::sendChunk(" · ");
        Esp32BaseWeb::writeHtmlEscaped(sourceName(latest.record.payload.source));
        const char* recordPlanName = planNameById(config, latest.record.payload.planId);
        if (recordPlanName) {
            Esp32BaseWeb::sendChunk(" · ");
            Esp32BaseWeb::writeHtmlEscaped(recordPlanName);
        }
        Esp32BaseWeb::sendChunk("</p><div class='home-facts'><div class='home-fact'><span>实际浇水</span><b>");
        formatElapsed(totals.actualWateringSec, value, sizeof(value));
        Esp32BaseWeb::writeHtmlEscaped(value);
        Esp32BaseWeb::sendChunk("</b></div><div class='home-fact'><span>估算用水</span><b>");
        if (totals.estimatedWaterMl >= 1000ULL) {
            std::snprintf(value, sizeof(value), "%llu.%01llu L",
                          static_cast<unsigned long long>(totals.estimatedWaterMl / 1000ULL),
                          static_cast<unsigned long long>((totals.estimatedWaterMl % 1000ULL) / 100ULL));
        } else {
            std::snprintf(value, sizeof(value), "%llu mL",
                          static_cast<unsigned long long>(totals.estimatedWaterMl));
        }
        Esp32BaseWeb::writeHtmlEscaped(value);
        Esp32BaseWeb::sendChunk("</b></div></div><div class='home-card-actions'><a class='btnlink secondary' href='/irrigation/records?id=");
        sendUnsigned(latest.record.recordId);
        Esp32BaseWeb::sendChunk("'>查看详情</a></div>");
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
        if (hasPlan) {
            Esp32BaseWeb::sendChunk("<div class='manual-template'><label>计划模板<select id='manual-template'><option value=''>选择一个计划</option>");
            for (const WateringPlan& plan : config->plans) {
                if (!plan.configured) continue;
                Esp32BaseWeb::sendChunk("<option value='"); sendUnsigned(plan.id);
                Esp32BaseWeb::sendChunk("' data-durations='");
                for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                    if (index != 0) Esp32BaseWeb::sendChunk(",");
                    sendUnsigned(plan.zoneDurationMinutes[index]);
                }
                uint16_t activeZoneCount = 0;
                uint32_t totalMinutes = 0;
                for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                    if (!config->zones[index].enabled || plan.zoneDurationMinutes[index] == 0) continue;
                    ++activeZoneCount;
                    totalMinutes += plan.zoneDurationMinutes[index];
                }
                Esp32BaseWeb::sendChunk("'>"); Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
                Esp32BaseWeb::sendChunk(" · "); sendUnsigned(activeZoneCount);
                Esp32BaseWeb::sendChunk(" 路 · 共 "); sendUnsigned(totalMinutes);
                Esp32BaseWeb::sendChunk(" 分钟");
                Esp32BaseWeb::sendChunk("</option>");
            }
            Esp32BaseWeb::sendChunk("</select></label><button id='manual-fill' type='button' class='secondary'>填入时长</button><div id='manual-template-preview' class='manual-template-preview' aria-live='polite'></div></div>");
        }
        Esp32BaseWeb::sendChunk("<div class='manual-grid'>");
        for (uint8_t index = 0; index < config->zones.size(); ++index) {
            if (!config->zones[index].enabled) continue;
            Esp32BaseWeb::sendChunk("<div class='manual-zone'><label>");
            Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
            Esp32BaseWeb::sendChunk("</label><input class='manual-duration' data-zone-index='"); sendUnsigned(index);
            Esp32BaseWeb::sendChunk("' type='number' min='0' max='120' name='zone"); sendUnsigned(index + 1U);
            Esp32BaseWeb::sendChunk("' value='0' inputmode='numeric'><span>分钟</span></div>");
        }
        Esp32BaseWeb::sendChunk("</div><div class='manual-summary'><b id='manual-summary'>尚未选择水路</b><span id='manual-template-note'>每条水路范围 0～120 分钟，0 表示本次不执行。</span></div><div class='actions'><button id='manual-clear' type='button' class='secondary'>全部清零</button><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button><input id='manual-submit' type='submit' value='确认并开始浇水' disabled></div></form></dialog>");
    } else if (config) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "还没有启用水路", "请先到水路页面启用实际安装的水路。");
        Esp32BaseWeb::sendChunk("<div class='actions'><a class='btnlink info' href='/irrigation/zones'>前往水路设置</a></div>");
    }
    Esp32BaseWeb::sendChunk(R"HTML(<script>(function(){
var clock=document.getElementById('home-clock'),clockTime=document.getElementById('home-clock-time'),clockDate=document.getElementById('home-clock-date');if(clock&&clock.dataset.epoch){var clockBase=Number(clock.dataset.epoch),clockStarted=performance.now();function clockPad(v){return String(v).padStart(2,'0')}function updateClock(){var epoch=clockBase+Math.floor((performance.now()-clockStarted)/1000),d=new Date((epoch+28800)*1000);if(clockTime)clockTime.textContent=clockPad(d.getUTCHours())+':'+clockPad(d.getUTCMinutes())+':'+clockPad(d.getUTCSeconds());if(clockDate)clockDate.textContent=d.getUTCFullYear()+'年'+(d.getUTCMonth()+1)+'月'+d.getUTCDate()+'日'}updateClock();setInterval(updateClock,1000)}
var inputs=Array.prototype.slice.call(document.querySelectorAll('.manual-duration')),submit=document.getElementById('manual-submit'),summary=document.getElementById('manual-summary'),note=document.getElementById('manual-template-note'),preview=document.getElementById('manual-template-preview'),count=0,total=0;
function update(){count=0;total=0;inputs.forEach(function(input){var value=Math.max(0,Math.min(120,Number(input.value)||0));if(value>0){count++;total+=value}});if(summary)summary.textContent=count?('已选择 '+count+' 条水路 · 合计 '+total+' 分钟'):'尚未选择水路';if(submit)submit.disabled=count===0}
inputs.forEach(function(input){input.addEventListener('input',update)});
var select=document.getElementById('manual-template');function selectedOption(){return select&&select.options[select.selectedIndex]}function renderTemplatePreview(){if(!preview)return;preview.textContent='';var option=selectedOption();if(!option||!option.value){preview.classList.remove('visible');return}var values=(option.dataset.durations||'').split(',');inputs.forEach(function(input){var item=document.createElement('span'),label=input.closest('.manual-zone').querySelector('label');item.textContent=(label?label.textContent:'水路')+' '+(values[Number(input.dataset.zoneIndex)]||0)+' 分钟';preview.appendChild(item)});preview.classList.add('visible')}if(select)select.addEventListener('change',renderTemplatePreview);
var fill=document.getElementById('manual-fill');if(fill)fill.addEventListener('click',function(){var option=selectedOption();if(!option||!option.value){alert('请先选择一个计划。');return}var values=(option.dataset.durations||'').split(',');inputs.forEach(function(input){input.value=values[Number(input.dataset.zoneIndex)]||0});if(note)note.textContent='已填入所选计划时长，可继续修改；本次修改不会保存到计划。';update()});
var clear=document.getElementById('manual-clear');if(clear)clear.addEventListener('click',function(){inputs.forEach(function(input){input.value=0});if(note)note.textContent='已全部清零。';update()});
window.submitManualWatering=function(form){update();if(!count){alert('请至少为一条水路设置大于 0 的时长。');return false}return confirm('确认手动浇水 '+count+' 条水路，合计 '+total+' 分钟？')&&once(form)};update();
function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(response){return response.json()}).then(function(status){if(status.active)location.reload();else setTimeout(poll,2000)}).catch(function(){setTimeout(poll,3000)})}setTimeout(poll,2000);
})();</script>)HTML");
    endPage();
}

void IrrigationWeb::activeTask() {
    if (!beginPage("首页", "查看当前任务的实时状态")) return;
    Esp32BaseWeb::sendChunk(R"HTML(<style>
.run-idle-hero{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:16px;border:1px solid #cfe1e5;border-radius:10px;background:linear-gradient(135deg,#f4faf7,#fff)}.run-idle-hero h3{margin:0 0 4px;font-size:19px}.run-idle-hero p{margin:0;color:var(--eb-muted)}
.run-live-head{display:flex;align-items:flex-start;justify-content:space-between;gap:18px;padding:17px;border:1px solid #cbdde5;border-radius:11px;background:linear-gradient(135deg,var(--eb-info-soft),#fff)}.run-live-head span{display:block;color:var(--eb-muted);font-size:12px;font-weight:700}.run-live-head h3{margin:3px 0 4px;font-size:22px}.run-live-head p{margin:0;color:var(--eb-muted)}.run-live-head form{flex:0 0 auto;margin:0}.run-live-head input{min-height:38px}
.run-live-metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:9px;margin-top:12px}.run-live-metric{padding:12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft)}.run-live-metric span{display:block;color:var(--eb-muted);font-size:12px}.run-live-metric b{display:block;margin-top:3px;font-size:18px;overflow-wrap:anywhere}
.run-live-grid{display:grid;grid-template-columns:minmax(260px,.8fr) minmax(0,1.4fr);gap:12px;margin-top:12px}.run-current,.run-chart-card{padding:15px;border:1px solid var(--eb-line);border-radius:10px;background:#fff}.run-section-label{display:block;margin-bottom:5px;color:var(--eb-muted);font-size:12px;font-weight:700}.run-current-head{display:flex;align-items:center;justify-content:space-between;gap:10px}.run-current-head h3{margin:0;font-size:19px}.run-current-head .tag{flex:0 0 auto}.run-progress{height:10px;margin:13px 0 8px;border-radius:999px;background:var(--eb-line-soft);overflow:hidden}.run-progress span{display:block;height:100%;width:0;border-radius:inherit;background:var(--eb-primary);transition:width .25s ease}.run-current-detail{display:flex;justify-content:space-between;gap:10px;color:var(--eb-muted);font-size:13px}.run-flow-facts{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:13px}.run-flow-fact{padding:9px 10px;border-radius:8px;background:var(--eb-soft)}.run-flow-fact span{display:block;color:var(--eb-muted);font-size:11px}.run-flow-fact b{display:block;margin-top:2px;font-size:14px}.run-chart-card{min-width:0}.run-chart-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:8px}.run-chart-head h3{margin:0;font-size:16px}.run-chart-head span{color:var(--eb-muted);font-size:12px}.run-chart-wrap{position:relative;min-height:190px}.run-chart-wrap canvas{display:block;width:100%;height:190px}.run-chart-empty{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:var(--eb-muted);font-size:13px;pointer-events:none}
.run-steps{display:grid;gap:8px;margin-top:12px}.run-step{display:grid;grid-template-columns:34px minmax(0,1fr) auto;gap:10px;align-items:center;padding:10px 12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:#fff}.run-step-icon{display:flex;align-items:center;justify-content:center;width:28px;height:28px;border-radius:50%;background:var(--eb-soft);color:var(--eb-muted);font-size:12px;font-weight:750}.run-step.current{border-color:#9fc8d0;background:var(--eb-primary-soft)}.run-step.current .run-step-icon{background:var(--eb-primary);color:#fff}.run-step.complete .run-step-icon{background:var(--eb-ok);color:#fff}.run-step-main b,.run-step-main small{display:block}.run-step-main small,.run-step-detail{color:var(--eb-muted);font-size:12px}.run-step-detail{text-align:right}
@media(max-width:900px){.run-live-metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.run-live-grid{grid-template-columns:1fr}}
@media(max-width:760px){.run-live-head{align-items:stretch;flex-direction:column}.run-live-head form input{width:100%}.run-live-metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.run-live-grid{grid-template-columns:1fr}.run-step{grid-template-columns:30px minmax(0,1fr)}.run-step-detail{grid-column:2;text-align:left}}
@media(max-width:420px){.run-live-metrics,.run-flow-facts{grid-template-columns:1fr}}
</style>)HTML");
    const IrrigationConfig* config = g_app->configuration();
    const WateringStatus status = g_app->wateringStatus();
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
        Esp32BaseWeb::sendChunk("<script>(function(){function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active)location.reload();else setTimeout(poll,2000)}).catch(function(){setTimeout(poll,3000)})}setTimeout(poll,2000)})();</script>");
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
        }
        const char* activeZoneName = "—";
        if (config && status.activeZoneId >= 1 && status.activeZoneId <= config->zones.size()) {
            activeZoneName = config->zones[status.activeZoneId - 1U].name.data();
        }
        const uint32_t currentTargetSec = status.currentStepIndex < status.stepCount
                                              ? status.zones[status.currentStepIndex].plannedDurationSec
                                              : 0U;
        const uint32_t progress = currentTargetSec == 0
                                      ? 0U
                                      : (status.currentZoneElapsedSec >= currentTargetSec
                                             ? 100U
                                             : status.currentZoneElapsedSec * 100U / currentTargetSec);
        Esp32BaseWeb::beginPanel("当前运行");
        Esp32BaseWeb::sendChunk("<div id='run-live' data-generation='"); sendUnsigned(status.flowHistoryGeneration);
        Esp32BaseWeb::sendChunk("' data-serial='"); sendUnsigned(status.flowSampleSerial);
        Esp32BaseWeb::sendChunk("'><div class='run-live-head'><div><span>"); Esp32BaseWeb::writeHtmlEscaped(taskSource);
        Esp32BaseWeb::sendChunk("</span><h3>"); Esp32BaseWeb::writeHtmlEscaped(taskName);
        Esp32BaseWeb::sendChunk("</h3><p id='run-state'>"); Esp32BaseWeb::writeHtmlEscaped(wateringStateName(status.state));
        Esp32BaseWeb::sendChunk(status.flowEstablished ? " · 水流已建立" : " · 等待水流建立");
        Esp32BaseWeb::sendChunk("</p></div><form method='post' action='/irrigation' onsubmit=\"return confirm('确认停止当前整次浇水任务？')&&once(this)\"><input type='hidden' name='action' value='stop'><input class='danger' type='submit' value='停止当前任务'></form></div>");
        Esp32BaseWeb::sendChunk("<div class='run-live-metrics'><div class='run-live-metric'><span>整次已运行</span><b id='run-elapsed'>"); sendDuration(status.elapsedSec);
        Esp32BaseWeb::sendChunk("</b></div><div class='run-live-metric'><span>预计剩余浇水</span><b id='run-remaining'>"); sendDuration(status.plannedRemainingSec);
        Esp32BaseWeb::sendChunk("</b></div><div class='run-live-metric'><span>累计估算水量</span><b id='run-water'>"); sendLiters(status.totalEstimatedWaterMl);
        Esp32BaseWeb::sendChunk("</b></div><div class='run-live-metric'><span>执行进度</span><b id='run-step-count'>第 "); sendUnsigned(status.currentStepIndex + 1U); Esp32BaseWeb::sendChunk(" / "); sendUnsigned(status.stepCount);
        Esp32BaseWeb::sendChunk(" 条水路</b></div></div><div class='run-live-grid'><div class='run-current'><span class='run-section-label'>当前水路</span><div class='run-current-head'><h3 id='run-current-zone'>"); Esp32BaseWeb::writeHtmlEscaped(activeZoneName);
        Esp32BaseWeb::sendChunk("</h3><span id='run-flow-state' class='tag "); Esp32BaseWeb::sendChunk(status.flowEstablished ? "ok'>水流正常" : "warn'>等待水流");
        Esp32BaseWeb::sendChunk("</span></div><div class='run-progress'><span id='run-current-progress' style='width:"); sendUnsigned(progress);
        Esp32BaseWeb::sendChunk("%'></span></div><div class='run-current-detail'><span id='run-current-elapsed'>"); sendDuration(status.currentZoneElapsedSec);
        Esp32BaseWeb::sendChunk(" / "); sendDuration(currentTargetSec);
        Esp32BaseWeb::sendChunk("</span><span id='run-current-remaining'>剩余 "); sendDuration(status.currentZoneRemainingSec);
        Esp32BaseWeb::sendChunk("</span></div><div class='run-flow-facts'><div class='run-flow-fact'><span>当前流量</span><b id='run-flow'>");
        char flowText[20]{}; IrrigationConfigRules::formatLitersPerMinute(status.currentFlowMlPerMinute, flowText, sizeof(flowText)); Esp32BaseWeb::writeHtmlEscaped(flowText);
        Esp32BaseWeb::sendChunk(" L/min</b></div><div class='run-flow-fact'><span>基准流量</span><b id='run-expected-flow'>");
        if (status.expectedFlowMlPerMinute == 0) Esp32BaseWeb::sendChunk("未设置");
        else { char expected[20]{}; IrrigationConfigRules::formatLitersPerMinute(status.expectedFlowMlPerMinute, expected, sizeof(expected)); Esp32BaseWeb::writeHtmlEscaped(expected); Esp32BaseWeb::sendChunk(" L/min"); }
        Esp32BaseWeb::sendChunk("</b></div><div class='run-flow-fact'><span>当前水路脉冲</span><b id='run-pulses'>"); sendUnsigned(status.pulseCount);
        Esp32BaseWeb::sendChunk("</b></div><div class='run-flow-fact'><span>当前水路估算水量</span><b id='run-zone-water'>");
        if (status.currentStepIndex < status.stepCount) sendLiters(status.zones[status.currentStepIndex].estimatedWaterMl); else Esp32BaseWeb::sendChunk("0.000 L");
        Esp32BaseWeb::sendChunk("</b></div></div></div><div class='run-chart-card'><div class='run-chart-head'><div><span class='run-section-label'>当前水路</span><h3>最近 10 分钟流量</h3></div><span>每 5 秒采样</span></div><div class='run-chart-wrap'><canvas id='run-flow-chart' height='190'></canvas><div id='run-chart-empty' class='run-chart-empty'>正在等待流量数据</div></div></div></div><div class='run-steps'>");
        for (uint8_t index = 0; index < status.stepCount; ++index) {
            const ZoneWateringSummary& zone = status.zones[index];
            const char* stepClass = index < status.currentStepIndex ? " complete" : (index == status.currentStepIndex ? " current" : "");
            Esp32BaseWeb::sendChunk("<div class='run-step"); Esp32BaseWeb::sendChunk(stepClass); Esp32BaseWeb::sendChunk("' data-step-index='"); sendUnsigned(index);
            Esp32BaseWeb::sendChunk("' data-zone-id='"); sendUnsigned(zone.zoneId); Esp32BaseWeb::sendChunk("'><span class='run-step-icon'>");
            if (index < status.currentStepIndex) Esp32BaseWeb::sendChunk("&#10003;"); else sendUnsigned(index + 1U);
            Esp32BaseWeb::sendChunk("</span><div class='run-step-main'><b class='run-step-name'>");
            if (config && zone.zoneId >= 1 && zone.zoneId <= config->zones.size()) Esp32BaseWeb::writeHtmlEscaped(config->zones[zone.zoneId - 1U].name.data()); else { Esp32BaseWeb::sendChunk("水路 "); sendUnsigned(zone.zoneId); }
            Esp32BaseWeb::sendChunk("</b><small>计划 "); sendDuration(zone.plannedDurationSec); Esp32BaseWeb::sendChunk("</small></div><span class='run-step-detail'>");
            if (index < status.currentStepIndex) { Esp32BaseWeb::sendChunk("实际 "); sendDuration(zone.actualWateringSec); Esp32BaseWeb::sendChunk(" · "); sendLiters(zone.estimatedWaterMl); }
            else if (index == status.currentStepIndex) { Esp32BaseWeb::sendChunk("正在执行 · 剩余 "); sendDuration(status.currentZoneRemainingSec); }
            else Esp32BaseWeb::sendChunk("等待执行");
            Esp32BaseWeb::sendChunk("</span></div>");
        }
        Esp32BaseWeb::sendChunk("</div><p class='muted' style='margin:10px 0 0'>预计剩余时间只计算计划浇水时长，不包含等待水流和设备启停延时。</p></div>");
        Esp32BaseWeb::endPanel();
    }
    Esp32BaseWeb::sendChunk("<script>(function(){var live=document.getElementById('run-live'),initialActive=!!live;function duration(v){v=Math.max(0,Number(v)||0);if(v<60)return v+' 秒';if(v<3600)return Math.floor(v/60)+' 分 '+(v%60)+' 秒';return Math.floor(v/3600)+' 小时 '+Math.floor((v%3600)/60)+' 分'}function liters(v){return((Number(v)||0)/1000).toFixed(3)+' L'}function flow(v){return((Number(v)||0)/1000).toFixed(3)+' L/min'}function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}var inputs=document.querySelectorAll('.run-custom-duration'),submit=document.getElementById('run-custom-submit');function updateCustom(){var count=0,total=0;inputs.forEach(function(i){var v=Math.max(0,Math.min(120,Number(i.value)||0));if(v>0){count++;total+=v}});set('run-custom-summary',count?('已选择 '+count+' 条水路 · 合计 '+total+' 分钟'):'尚未选择水路');if(submit)submit.disabled=count===0}inputs.forEach(function(i){i.addEventListener('input',updateCustom)});window.runCustomSubmit=function(form){updateCustom();if(!submit||submit.disabled){alert('请至少为一条水路设置大于 0 的时长。');return false}return confirm('确认按当前自定义时长立即开始浇水？')&&once(form)};updateCustom();if(!initialActive){function idlePoll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(s.active)location.reload();else setTimeout(idlePoll,2000)}).catch(function(){setTimeout(idlePoll,3000)})}setTimeout(idlePoll,2000);return}var canvas=document.getElementById('run-flow-chart'),empty=document.getElementById('run-chart-empty'),samples=[],generation=Number(live.dataset.generation||0),serial=Number(live.dataset.serial||0);function draw(){if(!canvas)return;var rect=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1,w=Math.max(280,rect.width),h=190;canvas.width=w*dpr;canvas.height=h*dpr;var c=canvas.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);c.strokeStyle='#d8e2e7';c.lineWidth=1;c.beginPath();c.moveTo(42,12);c.lineTo(42,h-28);c.lineTo(w-10,h-28);c.stroke();if(samples.length===0){if(empty)empty.style.display='flex';return}if(empty)empty.style.display='none';var max=Math.max.apply(null,samples.concat([1000]))*1.12;c.fillStyle='#667085';c.font='11px sans-serif';c.fillText((max/1000).toFixed(2)+' L/min',2,14);c.fillText('10分钟前',42,h-8);c.fillText('现在',w-34,h-8);c.strokeStyle='#117b8b';c.lineWidth=2;c.beginPath();samples.forEach(function(v,i){var x=42+(w-52)*(samples.length===1?1:i/(samples.length-1)),y=12+(h-40)*(1-v/max);if(i===0)c.moveTo(x,y);else c.lineTo(x,y)});c.stroke()}function loadHistory(){fetch('/irrigation/api/flow-history',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(h){samples=Array.isArray(h.samples)?h.samples.slice(-120):[];generation=Number(h.generation||0);serial=Number(h.latestSerial||0);draw()}).catch(function(){setTimeout(loadHistory,2000)})}function update(s){set('run-elapsed',duration(s.elapsedSec));set('run-remaining',duration(s.plannedRemainingSec));set('run-water',liters(s.totalEstimatedWaterMl));set('run-step-count','第 '+(Number(s.currentStepIndex)+1)+' / '+s.stepCount+' 条水路');var states=['空闲','区域启动中','等待水流','正在浇水','区域停止中'];set('run-state',(states[s.state]||'未知')+(s.flowEstablished?' · 水流已建立':' · 等待水流建立'));set('run-current-elapsed',duration(s.currentZoneElapsedSec)+' / '+duration((s.zones[s.currentStepIndex]||{}).plannedDurationSec));set('run-current-remaining','剩余 '+duration(s.currentZoneRemainingSec));set('run-flow',flow(s.currentFlowMlPerMinute));set('run-expected-flow',s.expectedFlowMlPerMinute?flow(s.expectedFlowMlPerMinute):'未设置');set('run-pulses',s.pulseCount);set('run-zone-water',liters((s.zones[s.currentStepIndex]||{}).estimatedWaterMl));var current=document.querySelector('[data-step-index=\"'+s.currentStepIndex+'\"]');if(current){var name=current.querySelector('.run-step-name');set('run-current-zone',name?name.textContent:'水路 '+s.zoneId)}var target=Number((s.zones[s.currentStepIndex]||{}).plannedDurationSec)||0,percent=target?Math.min(100,Math.round(Number(s.currentZoneElapsedSec)*100/target)):0,bar=document.getElementById('run-current-progress');if(bar)bar.style.width=percent+'%';var flowState=document.getElementById('run-flow-state');if(flowState){flowState.textContent=s.flowEstablished?'水流正常':'等待水流';flowState.className='tag '+(s.flowEstablished?'ok':'warn')}document.querySelectorAll('.run-step').forEach(function(row){var i=Number(row.dataset.stepIndex),z=s.zones[i]||{},icon=row.querySelector('.run-step-icon'),detail=row.querySelector('.run-step-detail');row.classList.toggle('complete',i<s.currentStepIndex);row.classList.toggle('current',i===s.currentStepIndex);if(icon)icon.textContent=i<s.currentStepIndex?'✓':String(i+1);if(detail){if(i<s.currentStepIndex)detail.textContent='实际 '+duration(z.actualWateringSec)+' · '+liters(z.estimatedWaterMl);else if(i===s.currentStepIndex)detail.textContent='正在执行 · 剩余 '+duration(s.currentZoneRemainingSec);else detail.textContent='等待执行'}});var nextGeneration=Number(s.flowHistoryGeneration||0),nextSerial=Number(s.flowSampleSerial||0);if(nextGeneration!==generation){generation=nextGeneration;loadHistory()}else if(nextSerial!==serial){if(nextSerial===serial+1){samples.push(Number(s.currentFlowMlPerMinute)||0);if(samples.length>120)samples.shift();serial=nextSerial;draw()}else loadHistory()}}function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active){location.reload();return}update(s);setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}window.addEventListener('resize',draw);loadHistory();setTimeout(poll,1000)})();</script>");
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
        ".plan-auto{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:14px;border:1px solid #cfe1e5;border-radius:10px;background:var(--eb-soft)}"
        ".plan-auto h3{margin:0 0 4px;font-size:17px}.plan-auto p{margin:0;color:var(--eb-muted)}.plan-auto-actions{display:flex;gap:8px;flex:0 0 auto}.plan-auto-actions form{margin:0}"
        ".plan-pause-modal{width:min(620px,calc(100vw - 28px))}.plan-pause-options{display:grid;gap:10px;margin-top:14px}.plan-pause-option{padding:14px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft)}.plan-pause-option h3{margin:0 0 3px;font-size:15px}.plan-pause-option>small{display:block;margin-bottom:12px;color:var(--eb-muted)}.plan-pause-fields{display:grid;gap:12px}.plan-pause-field{margin:0}.plan-pause-field input{width:100%;max-width:none;margin:5px 0 0}.plan-pause-shortcuts{display:flex;flex-wrap:wrap;gap:7px;margin-top:7px}.plan-pause-shortcuts button{min-height:32px;padding:5px 10px}.plan-pause-shortcuts button.selected{border-color:var(--eb-primary);background:var(--eb-primary-soft);color:var(--eb-primary)}.plan-pause-unavailable{margin:0;padding:9px 11px;border-radius:7px;background:var(--eb-warn-soft);color:var(--eb-warn);font-size:13px}.plan-pause-submit{display:flex;justify-content:flex-end;margin-top:12px}.plan-pause-indefinite{display:flex;align-items:center;justify-content:space-between;gap:14px}.plan-pause-indefinite h3{margin-bottom:3px}.plan-pause-indefinite p{margin:0}.plan-pause-indefinite form{margin:0;flex:0 0 auto}"
        ".plan-toolbar .btnlink{min-height:36px}"
        ".plan-list{display:grid;gap:12px}"
        ".plan-card{padding:16px;border:1px solid #cfe1e5;border-radius:10px;background:linear-gradient(135deg,#f6fbfc 0,#fff 58%)}"
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
        char automaticDetail[96]{};
        if (automatic.mode == AutomaticWateringMode::Enabled) {
            std::snprintf(automaticDetail, sizeof(automaticDetail), "已启用的计划会在设定时间自动执行。");
        } else if (automatic.mode == AutomaticWateringMode::PausedIndefinitely) {
            std::snprintf(automaticDetail, sizeof(automaticDetail), "已暂停，等待手动恢复；手动浇水不受影响。");
        } else if (!formatFullDateTime(automatic.resumeAtEpoch,
                                       automaticDetail,
                                       sizeof(automaticDetail))) {
            std::snprintf(automaticDetail, sizeof(automaticDetail), "已定时暂停，设备时间就绪后自动恢复。");
        }
        Esp32BaseWeb::beginPanel("自动浇水");
        Esp32BaseWeb::sendChunk("<div class='plan-auto'><div><h3><span class='tag ");
        Esp32BaseWeb::sendChunk(automatic.mode == AutomaticWateringMode::Enabled
                                    ? "ok'>正常运行"
                                    : "warn'>已暂停");
        Esp32BaseWeb::sendChunk("</span></h3><p>");
        if (automatic.mode == AutomaticWateringMode::PausedUntil && automaticDetail[0] != '\0') {
            Esp32BaseWeb::sendChunk("自动浇水将在 ");
        }
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
            Esp32BaseWeb::sendChunk("<article class='plan-card'><div class='plan-card-head'><div class='plan-card-title'><div class='plan-card-title-row'><h3>");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("</h3><span class='tag ");
            Esp32BaseWeb::sendChunk(plan.scheduleEnabled ? "ok'>自动执行" : "'>不自动执行");
            Esp32BaseWeb::sendChunk("</span></div><small>计划 "); sendUnsigned(plan.id);
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
                Esp32BaseWeb::sendChunk("' min='0' max='120' value='"); sendUnsigned(plan.zoneDurationMinutes[index]);
                Esp32BaseWeb::sendChunk("'><small>单位：分钟，范围 0～120。</small></p>");
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
        redirectResult("/irrigation/zones", actionIs("save") && saveZoneFromRequest());
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
        "@media(max-width:760px){"
        ".zone-meter{align-items:stretch;flex-direction:column;gap:10px}.zone-meter .btnlink{width:100%}"
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
            if (zone.baselinePulseRateX100 != 0 &&
                FlowMonitor::pulseRateToFlowMlPerMinute(
                    zone.baselinePulseRateX100,
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
            if (zone.baselinePulseRateX100 == 0) Esp32BaseWeb::sendChunk("<span class='muted'>未学习</span>");
            else if (value[0] == '\0') Esp32BaseWeb::sendChunk("<span class='muted'>超出显示范围</span>");
            else { Esp32BaseWeb::writeHtmlEscaped(value); Esp32BaseWeb::sendChunk(" L/min（已学习）"); }
            Esp32BaseWeb::sendChunk("</td><td><div class='fsactions'><button type='button' class='btnlink info compact' onclick=\"document.getElementById('zone-"); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("').showModal()\">修改</button>");
            if (zone.enabled) { Esp32BaseWeb::sendChunk("<a class='btnlink ok compact' href='/irrigation/zones/learning?zone="); sendUnsigned(zone.id); Esp32BaseWeb::sendChunk("'>学习基准流量</a>"); }
            Esp32BaseWeb::sendChunk("</div></td></tr>");
        }
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::beginPanel("流量计维护");
        Esp32BaseWeb::sendChunk("<div class='zone-meter'><div class='zone-meter-main'><p class='zone-meter-label'>稳态流量系数</p><div class='zone-meter-value'><span class='zone-meter-number'>");
        Esp32BaseWeb::writeHtmlEscaped(coefficient);
        Esp32BaseWeb::sendChunk("</span><span class='zone-meter-unit'>P/L</span></div></div><a class='btnlink ok compact' href='/irrigation/zones/flow-calibration'>流量计校准</a></div>");
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
            success = uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId) &&
                      g_app->startFlowCalibration(static_cast<uint8_t>(zoneId), 10) ==
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
        ".cal-current{display:grid;grid-template-columns:repeat(3,minmax(0,1fr)) auto;gap:0;align-items:center;border:1px solid var(--eb-line-soft);border-radius:9px;background:#fff;overflow:hidden}"
        ".cal-current-fact{min-width:0;padding:10px 13px;border-right:1px solid var(--eb-line-soft)}.cal-current-label{display:block;color:var(--eb-muted);font-size:11px}.cal-current-value{display:block;margin-top:3px;color:var(--eb-text);font-size:17px;font-weight:400;line-height:1.35}.cal-current-unit{font-size:12px;color:var(--eb-muted)}.cal-current-action{padding:10px 13px}.cal-current-action button{white-space:nowrap}"
        ".cal-steps{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-bottom:14px}"
        ".cal-step{display:grid;grid-template-columns:28px minmax(0,1fr);gap:9px;align-items:start;padding:12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft);font-size:13px;line-height:1.55}"
        ".cal-step-num{display:flex;align-items:center;justify-content:center;width:26px;height:26px;border-radius:50%;background:var(--eb-primary-soft);color:var(--eb-primary);font-weight:600}"
        ".cal-action{padding:13px 14px;border:1px solid var(--eb-line-soft);border-radius:10px;background:#fff}"
        ".cal-start-form{display:grid;grid-template-columns:minmax(360px,1fr) minmax(280px,.8fr);gap:16px;align-items:stretch}.cal-start-picker label{display:block;margin-bottom:6px;font-weight:500}.cal-start-controls{display:flex;align-items:flex-start;gap:10px}.cal-start-controls select{flex:1;min-width:220px}.cal-start-controls .actions{flex:0 0 auto;margin:0}.cal-start-info{display:flex;flex-direction:column;justify-content:center;padding-left:16px;border-left:1px solid var(--eb-line-soft);color:var(--eb-muted);font-size:12px;line-height:1.55}.cal-start-info b{color:var(--eb-text);font-size:13px;font-weight:400}"
        ".cal-live-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:14px}.cal-live-head h3{margin:0 0 3px;font-size:17px}.cal-live-head p{margin:0;color:var(--eb-muted);font-size:13px}"
        ".cal-live .metrics{grid-template-columns:repeat(4,minmax(0,1fr))}.cal-live .metric b{font-weight:500}.cal-live .metric small{display:block;margin-top:4px;color:var(--eb-muted);font-size:11px;font-weight:400;line-height:1.4}.cal-live .actions{margin-top:12px}.cal-live-note{display:flex;flex-wrap:wrap;gap:8px 18px;margin-top:10px;padding:10px 12px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px;line-height:1.5}.cal-live-note b{color:var(--eb-text);font-weight:400}"
        ".cal-pending{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(280px,.75fr);gap:18px;align-items:start}.cal-pending-summary{padding:12px 13px;border-radius:9px;background:var(--eb-soft)}.cal-pending-summary h3{margin:0 0 3px;font-size:16px;font-weight:500}.cal-pending-route{margin:0 0 9px;color:var(--eb-muted);font-size:12px}.cal-phase-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.cal-phase{padding:8px 9px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.cal-phase-label{display:block;color:var(--eb-muted);font-size:11px}.cal-phase-value{display:block;margin-top:3px;color:var(--eb-text);font-size:13px;font-weight:400;line-height:1.4}.cal-phase-note{display:block;margin-top:2px;color:var(--eb-muted);font-size:11px;line-height:1.35}.cal-pending-total{margin:8px 0 0;color:var(--eb-muted);font-size:12px}.cal-pending-warning{color:var(--eb-danger)}.cal-pending label{font-weight:500}"
        ".cal-sample-toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px}.cal-counts{display:flex;flex-wrap:wrap;gap:7px}.cal-count{padding:5px 9px;border-radius:999px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px}.cal-count b{color:var(--eb-text);font-weight:500}.cal-count.ok{background:var(--eb-ok-soft);color:var(--eb-ok)}.cal-count.bad{background:var(--eb-danger-soft);color:var(--eb-danger)}"
        ".cal-sample-guide{margin:-2px 0 10px;color:var(--eb-muted);font-size:11px;line-height:1.5}.cal-sample-list{border:1px solid var(--eb-line-soft);border-radius:9px;overflow:hidden}.cal-sample-columns,.cal-sample{display:grid;grid-template-columns:minmax(95px,.65fr) minmax(135px,.9fr) minmax(160px,1.05fr) minmax(210px,1.35fr) minmax(200px,1.25fr) auto;gap:12px;align-items:center}.cal-sample-columns{padding:7px 11px;background:var(--eb-soft);color:var(--eb-muted);font-size:11px}.cal-sample{padding:10px 11px;background:#fff;border-top:1px solid var(--eb-line-soft)}.cal-sample:first-of-type{border-top:0}.cal-sample-cell{min-width:0}.cal-sample-main{display:block;color:var(--eb-text);font-size:13px;font-weight:400;line-height:1.4;overflow-wrap:anywhere}.cal-sample-sub{display:block;margin-top:2px;color:var(--eb-muted);font-size:11px;font-weight:400;line-height:1.4;overflow-wrap:anywhere}.cal-sample-sub.warn{color:var(--eb-danger)}.cal-sample-actions{display:flex;align-items:center;justify-content:flex-end;gap:6px;white-space:nowrap}.cal-sample-actions form{margin:0}.cal-sample-actions button,.cal-sample-actions input{min-height:30px;padding:5px 9px;font-size:12px}.cal-status{display:inline-flex;align-items:center;margin-left:5px;padding:2px 7px;border-radius:999px;font-size:11px;font-weight:400}.cal-status.ok{background:var(--eb-ok-soft);color:var(--eb-ok)}.cal-status.bad{background:var(--eb-danger-soft);color:var(--eb-danger)}"
        ".cal-empty{padding:20px;text-align:center;border:1px dashed #c9d6dc;border-radius:9px;background:var(--eb-soft)}"
        ".cal-empty-title{margin:0;color:#344054;font-size:15px;font-weight:500}"
        ".cal-empty-text{margin:5px 0 0;color:var(--eb-muted);font-size:13px}"
        ".cal-result{padding:12px 13px;border:1px solid var(--eb-line-soft);border-radius:9px;background:#fff}.cal-result-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px}.cal-result-head-left{display:flex;align-items:center;gap:8px}.cal-result-head p{margin:0;color:var(--eb-muted);font-size:12px}.cal-result-head form{margin:0}.cal-result-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));border:1px solid var(--eb-line-soft);border-radius:8px;overflow:hidden}.cal-result-fact{min-width:0;padding:9px 10px;border-right:1px solid var(--eb-line-soft);background:var(--eb-soft)}.cal-result-fact:last-child{border-right:0}.cal-result-fact span{display:block;color:var(--eb-muted);font-size:11px}.cal-result-fact b{display:block;margin-top:3px;color:var(--eb-text);font-size:15px;font-weight:400}.cal-result-meta{display:flex;flex-wrap:wrap;gap:5px 16px;margin-top:8px;color:var(--eb-muted);font-size:11px;line-height:1.5}.cal-result-note{margin:8px 0 0;color:var(--eb-muted);font-size:11px;line-height:1.5}"
        ".cal-sample-toolbar form{margin:0}.cal-sample-toolbar input{min-height:30px;padding:5px 10px;font-size:12px}"
        ".cal-edit{width:min(460px,calc(100vw - 28px))}.cal-edit h2{margin-bottom:4px}.cal-edit>p{margin-top:0}.cal-parameters{width:min(640px,calc(100vw - 28px))}.cal-parameter-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px 14px;margin-top:14px}.cal-parameter-grid .field{min-width:0}.cal-parameter-grid label{font-weight:500}.cal-parameter-grid input{width:100%;margin:0}.cal-parameter-grid small{display:block;margin-top:4px;color:var(--eb-muted);font-size:12px;line-height:1.4}"
        "@media(max-width:1150px){.cal-sample-columns{display:none}.cal-sample{grid-template-columns:repeat(2,minmax(0,1fr)) auto}.cal-sample-actions{grid-column:3;grid-row:1/span 3}}"
        "@media(max-width:900px){.cal-start-form{grid-template-columns:1fr}.cal-start-controls select{width:100%;min-width:0}.cal-start-info{padding:10px 0 0;border-left:0;border-top:1px solid var(--eb-line-soft)}.cal-phase-grid{grid-template-columns:1fr}.cal-result-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.cal-result-fact:nth-child(2){border-right:0}.cal-result-fact:nth-child(-n+2){border-bottom:1px solid var(--eb-line-soft)}}"
        "@media(max-width:760px){"
        ".cal-steps{grid-template-columns:1fr}"
        ".cal-live .metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.cal-pending{grid-template-columns:1fr}.cal-sample-toolbar{align-items:flex-start}.cal-start-controls{align-items:stretch}.cal-sample{grid-template-columns:1fr auto}.cal-sample-actions{grid-column:2;grid-row:1/span 5}"
        "}"
        "@media(max-width:560px){.cal-current{grid-template-columns:1fr auto}.cal-current-fact{border-right:0;border-bottom:1px solid var(--eb-line-soft)!important}.cal-current-action{grid-column:2;grid-row:1/span 3}.cal-result-head{align-items:flex-start}.cal-result-grid{grid-template-columns:1fr}.cal-result-fact{border-right:0;border-bottom:1px solid var(--eb-line-soft)!important}.cal-result-fact:last-child{border-bottom:0!important}.cal-sample{grid-template-columns:1fr}.cal-sample-actions{grid-column:1;grid-row:auto;justify-content:flex-start}.cal-parameter-grid{grid-template-columns:1fr}}"
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
    Esp32BaseWeb::sendChunk("<div class='cal-current'><div class='cal-current-fact'><span class='cal-current-label'>启动脉冲</span><span class='cal-current-value'>");
    sendUnsigned(config->flowMeter.calibrationStartupPulseCount); Esp32BaseWeb::sendChunk(" <span class='cal-current-unit'>个</span>");
    Esp32BaseWeb::sendChunk("</span></div><div class='cal-current-fact'><span class='cal-current-label'>估算启动水量</span><span class='cal-current-value'>");
    sendUnsigned(config->flowMeter.calibrationStartupWaterMl); Esp32BaseWeb::sendChunk(" <span class='cal-current-unit'>mL</span>");
    Esp32BaseWeb::sendChunk("</span></div><div class='cal-current-fact'><span class='cal-current-label'>稳态流量系数</span><span class='cal-current-value'>"); Esp32BaseWeb::writeHtmlEscaped(coefficient); Esp32BaseWeb::sendChunk(" <span class='cal-current-unit'>P/L</span></span></div><div class='cal-current-action'>");
    if (!managementLocked) Esp32BaseWeb::sendChunk("<button class='secondary' type='button' onclick=\"document.getElementById('cal-parameters').showModal()\">修改参数</button>");
    Esp32BaseWeb::sendChunk("</div></div>");
    Esp32BaseWeb::endPanel();
    if (!managementLocked) {
        Esp32BaseWeb::sendChunk("<dialog id='cal-parameters' class='panel eb-modal cal-edit cal-parameters' data-eb-light-dismiss='1'><h2>修改校准参数</h2><p class='muted'>三项参数统一保存；稳态流量系数参与流量和水量换算。</p><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='parameters'><div class='cal-parameter-grid'><p class='field'><label>启动脉冲</label><input type='number' name='startup_pulses' min='0' max='10000000' step='1' required value='"); sendUnsigned(config->flowMeter.calibrationStartupPulseCount); Esp32BaseWeb::sendChunk("'><small>单位：个；0 表示未设置。</small></p><p class='field'><label>估算启动水量</label><input type='number' name='startup_water_ml' min='0' max='1000000' step='1' required value='"); sendUnsigned(config->flowMeter.calibrationStartupWaterMl); Esp32BaseWeb::sendChunk("'><small>单位：mL；0 表示未设置。</small></p><p class='field'><label>稳态流量系数</label><input type='number' name='coefficient' min='0.01' max='100000.00' step='0.01' required value='"); Esp32BaseWeb::writeHtmlEscaped(coefficient); Esp32BaseWeb::sendChunk("'><small>单位：P/L。</small></p></div><div class='actions'><button class='secondary' type='button' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='保存参数'></div></form></dialog>");
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
        Esp32BaseWeb::sendChunk("<div id='cal-live' class='cal-live'><div class='cal-live-head'><div><h3>"); Esp32BaseWeb::writeHtmlEscaped(zoneName); Esp32BaseWeb::sendChunk("</h3><p id='cal-state'>"); Esp32BaseWeb::writeHtmlEscaped(wateringStateName(status.state)); Esp32BaseWeb::sendChunk("</p></div><span class='tag warn'>正在采样</span></div><div class='metrics'><div class='metric'><b id='cal-elapsed'>"); sendDuration(status.elapsedSec); Esp32BaseWeb::sendChunk("</b><span>已运行</span></div><div class='metric'><b id='cal-pulses'>"); sendUnsigned(status.pulseCount); Esp32BaseWeb::sendChunk("</b><span>完整脉冲</span></div><div class='metric'><b id='cal-steady-state'>");
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
        Esp32BaseWeb::sendChunk("<div class='cal-pending'><div class='cal-pending-summary'><h3>本次采样已停止</h3><p class='cal-pending-route'>采样水路："); Esp32BaseWeb::writeHtmlEscaped(pendingZoneName); Esp32BaseWeb::sendChunk("</p><div class='cal-phase-grid'><div class='cal-phase'><span class='cal-phase-label'>启动阶段</span><span class='cal-phase-value'>"); sendMilliseconds(pending->steadyStartedMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(pending->startupPulseCount); Esp32BaseWeb::sendChunk(" 脉冲</span><span class='cal-phase-note'>从开阀到进入稳态</span></div><div class='cal-phase'><span class='cal-phase-label'>稳态阶段</span><span class='cal-phase-value'>"); sendMilliseconds(pending->steadyDurationMs); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(pending->steadyPulseCount); Esp32BaseWeb::sendChunk(" 脉冲 · "); Esp32BaseWeb::writeHtmlEscaped(pendingSteadyRate); Esp32BaseWeb::sendChunk(" P/s</span><span class='cal-phase-note'>从进入稳态到停止命令</span></div></div><p class='cal-pending-total'>完整采样："); sendDuration(calibration.pendingElapsedSec()); Esp32BaseWeb::sendChunk(" · "); sendUnsigned(calibration.pendingPulseCount()); Esp32BaseWeb::sendChunk(" 脉冲"); if (pending->stopPulseCount != 0) { Esp32BaseWeb::sendChunk("<br><span class='cal-pending-warning'>停止后仍检测到 "); sendUnsigned(pending->stopPulseCount); Esp32BaseWeb::sendChunk(" 个脉冲，估算启动水量可能包含少量关闭尾水。</span>"); } Esp32BaseWeb::sendChunk("</p></div><div><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='measurement'><div class='fieldgrid'><p class='field full'><label>本次实测总水量</label><input type='number' name='measured_ml' min='1000' max='1000000' step='1' inputmode='numeric' required autofocus><small>填写启动和稳态期间实际接到的全部水量，单位 mL。</small></p></div><div class='actions'><input type='submit' value='保存有效样本'></div></form><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return confirm(&quot;确认本次接水无效？该样本会保留，但不参与计算。&quot;)&&once(this)'><input type='hidden' name='action' value='invalid'><div class='actions'><input class='secondary' type='submit' value='本次接水无效'></div></form></div></div>");
    } else if (calibration.sampleCount() < FlowCalibrationService::kMaximumSamples) {
        bool hasEligibleZone = false;
        for (const ZoneConfig& zone : config->zones) {
            hasEligibleZone = hasEligibleZone || zone.enabled;
        }
        if (hasEligibleZone) {
            Esp32BaseWeb::sendChunk("<form class='cal-start-form' method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认开始新样本？所选水路会立即出水。')&&once(this)\"><input type='hidden' name='action' value='start'><div class='cal-start-picker'><label>校准水路</label><div class='cal-start-controls'><select name='zone_id'>");
            for (const ZoneConfig& zone : config->zones) if (zone.enabled) { Esp32BaseWeb::sendChunk("<option value='"); sendUnsigned(zone.id); Esp32BaseWeb::sendChunk("'>"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data()); Esp32BaseWeb::sendChunk("</option>"); }
            Esp32BaseWeb::sendChunk("</select><div class='actions'><input type='submit' value='开始新样本'></div></div></div><div class='cal-start-info'><b>已保存 "); sendUnsigned(calibration.sampleCount()); Esp32BaseWeb::sendChunk(" / 10 个样本，还可增加 "); sendUnsigned(FlowCalibrationService::kMaximumSamples - calibration.sampleCount()); Esp32BaseWeb::sendChunk(" 个</b><span>每个样本可重新选择水路。稳态判定：每个窗口 "); sendUnsigned(config->calibrationStability.windowSec); Esp32BaseWeb::sendChunk(" 秒，需连续 "); sendUnsigned(config->calibrationStability.requiredWindows); Esp32BaseWeb::sendChunk(" 个，窗口速率波动不超过 "); sendUnsigned(config->calibrationStability.allowedVariationPercent); Esp32BaseWeb::sendChunk("%。</span></div></form>");
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
                Esp32BaseWeb::sendChunk("<span class='cal-sample-main'>不参与计算</span><span class='cal-sample-sub'>"); Esp32BaseWeb::writeHtmlEscaped(sample->stopReason == WateringStopReason::Completed ? "达到 10 分钟上限" : stopReasonName(sample->stopReason)); Esp32BaseWeb::sendChunk("</span>");
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
    if (active) Esp32BaseWeb::sendChunk("<script>(function(){function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}function duration(v){v=Math.max(0,Number(v)||0);if(v<60)return v+' 秒';return Math.floor(v/60)+' 分 '+(v%60)+' 秒'}function millis(v){v=Math.max(0,Number(v)||0);return (v/1000).toFixed(1)+' 秒'}function rate(p,m){m=Number(m)||0;return m>0?(Number(p||0)*1000/m).toFixed(2):'—'}function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active||s.purpose!==1){location.reload();return}var states=['空闲','区域启动中','等待水流','正在采样','正在停止'];set('cal-state',states[s.state]||'状态未知');set('cal-elapsed',duration(s.elapsedSec));set('cal-remaining',duration(s.currentZoneRemainingSec));set('cal-pulses',s.pulseCount);var z=s.zones&&s.zones[s.currentStepIndex];if(z){var detected=!!z.calibrationSteadyDetected,flow=!!s.flowEstablished,full=Number(z.calibrationCollectedWindows||0)>=Number(z.calibrationRequiredWindows||0);set('cal-steady-state',detected?'已确认':(!flow?'等待水流':(full?'波动偏大':'已采集 '+z.calibrationCollectedWindows+' / '+z.calibrationRequiredWindows+' 个窗口')));set('cal-steady-help',detected?'后续波动仍会继续监测':(!flow?'检测到脉冲后开始统计':(full?'最近 '+z.calibrationRequiredWindows+' 个窗口尚未满足波动要求，继续识别':'收满 '+z.calibrationRequiredWindows+' 个窗口后比较速率波动')));set('cal-startup-phase',detected?millis(z.calibrationSteadyStartedMs)+' · '+z.calibrationStartupPulses+' 脉冲':'等待稳态确认');set('cal-steady-phase',detected?millis(z.calibrationSteadyDurationMs)+' · '+z.calibrationSteadyPulses+' 脉冲 · '+rate(z.calibrationSteadyPulses,z.calibrationSteadyDurationMs)+' P/s':'尚未开始');set('cal-rate',(Number(z.calibrationLatestPulseRateX100||0)/100).toFixed(2)+' P/s')}setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}setTimeout(poll,1000)})();</script>");
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
    FlowMonitor::pulseRateToFlowMlPerMinute(
        zone.baselinePulseRateX100,
        config->flowMeter.pulsesPerLiterX100,
        learnedFlowMlPerMinute);
    char learned[20]{};
    IrrigationConfigRules::formatLitersPerMinute(
        learnedFlowMlPerMinute, learned, sizeof(learned));
    char learnedWithUnit[32]{};
    std::snprintf(learnedWithUnit, sizeof(learnedWithUnit), "%s L/min", learned);
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.learning-panel{padding-bottom:16px}.learning-context{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0;margin:-2px 0 14px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft)}.learning-context>div{min-width:0;padding:10px 12px;border-right:1px solid var(--eb-line-soft)}.learning-context>div:last-child{border-right:0}.learning-label{display:block;color:var(--eb-muted);font-size:11px;font-weight:400}.learning-context-value{display:block;margin-top:2px;font-size:14px;font-weight:400;overflow-wrap:anywhere}.learning-context-help{display:block;margin-top:2px;color:var(--eb-muted);font-size:11px;font-weight:400}
.learning-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:0 0 10px}.learning-head h3{margin:0;font-size:15px;font-weight:500}.learning-head .tag{font-weight:400}.learning-metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.learning-metric{min-width:0;padding:10px 11px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.learning-value{display:block;margin-top:3px;font-size:18px;font-weight:500;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.learning-rule{margin:10px 0 0;padding:9px 11px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:12px;font-weight:400;line-height:1.55}.learning-rule span{color:var(--eb-text);font-weight:400}
.learning-debug{margin-top:14px}.learning-debug h3{margin:0 0 2px;font-size:14px;font-weight:500}.learning-debug>p{margin:0 0 7px;color:var(--eb-muted);font-size:11px}.learning-table{width:100%;border-collapse:collapse;font-size:12px;font-variant-numeric:tabular-nums}.learning-table th,.learning-table td{padding:7px 8px;border-bottom:1px solid var(--eb-line-soft);text-align:right;font-weight:400;white-space:nowrap}.learning-table th{color:var(--eb-muted);font-size:11px}.learning-table th:first-child,.learning-table td:first-child{text-align:left}.learning-table tbody tr:last-child td{border-bottom:0}.learning-table .learning-empty{text-align:center;color:var(--eb-muted);padding:12px}.learning-summary{display:flex;flex-wrap:wrap;gap:5px 18px;margin:8px 0 0;color:var(--eb-muted);font-size:11px}.learning-summary span{font-weight:400}.learning-result{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-bottom:10px}.learning-result>div{padding:11px;border:1px solid var(--eb-line-soft);border-radius:8px}.learning-result-value{display:block;margin-top:3px;font-size:18px;font-weight:500;font-variant-numeric:tabular-nums}.learning-actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}.learning-actions form,.learning-actions .actions{margin:0}.learning-panel input,.learning-panel .btnlink{font-weight:500}
@media(max-width:760px){.learning-context{grid-template-columns:1fr 1fr}.learning-context>div{border-bottom:1px solid var(--eb-line-soft)}.learning-context>div:nth-child(2){border-right:0}.learning-context>div:last-child{grid-column:1/-1;border-bottom:0}.learning-metrics{grid-template-columns:repeat(2,minmax(0,1fr))}.learning-table{display:block}.learning-table thead{display:none}.learning-table tbody{display:grid;gap:7px}.learning-table tr{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));padding:7px 9px;border:1px solid var(--eb-line-soft);border-radius:7px}.learning-table td{display:grid;grid-template-columns:6.8em minmax(0,1fr);gap:6px;padding:3px 0;border:0;text-align:right}.learning-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:11px;text-align:left}.learning-table td:first-child{text-align:right}.learning-table .learning-empty{display:block;grid-column:1/-1;text-align:center}.learning-table .learning-empty::before{display:none}}
@media(max-width:420px){.learning-context,.learning-metrics,.learning-result{grid-template-columns:1fr}.learning-context>div{border-right:0}.learning-context>div:last-child{grid-column:auto}.learning-value,.learning-result-value{font-size:17px}.learning-table tr{grid-template-columns:1fr}}
</style>)HTML");
    Esp32BaseWeb::beginPanel("基准流量学习");
    Esp32BaseWeb::sendChunk("<div class='learning-panel'><div class='learning-context'><div><span class='learning-label'>水路</span><span class='learning-context-value'>");
    Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
    Esp32BaseWeb::sendChunk("</span></div><div><span class='learning-label'>当前基准流量</span><span class='learning-context-value'>");
    Esp32BaseWeb::sendChunk(zone.baselinePulseRateX100 == 0 ? "未学习" : learnedWithUnit);
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
        if (status.learningSampleCount < 5) {
            Esp32BaseWeb::sendChunk("采集中 ");
            sendUnsigned(status.learningSampleCount);
            Esp32BaseWeb::sendChunk("/5");
        } else {
            Esp32BaseWeb::sendChunk("波动偏大");
        }
        Esp32BaseWeb::sendChunk("</span></div></div><p class='learning-rule' id='learn-rule'>");
        if (status.learningSampleCount == 0) {
            Esp32BaseWeb::sendChunk("等待首个 5 秒有效窗口；每个窗口必须检测到脉冲。");
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
            Esp32BaseWeb::sendChunk("最近 ");
            sendUnsigned(status.learningSampleCount);
            Esp32BaseWeb::sendChunk(" 个窗口的原始脉冲速率为 <span>");
            Esp32BaseWeb::writeHtmlEscaped(minimumRate);
            Esp32BaseWeb::sendChunk("～");
            Esp32BaseWeb::writeHtmlEscaped(maximumRate);
            Esp32BaseWeb::sendChunk(" P/s</span>，速率跨度 <span>");
            Esp32BaseWeb::writeHtmlEscaped(spreadRate);
            Esp32BaseWeb::sendChunk(" P/s</span>；收满 5 个窗口后，跨度不超过 <span>");
            Esp32BaseWeb::writeHtmlEscaped(allowedRate);
            Esp32BaseWeb::sendChunk(" P/s</span> 即完成。允许跨度取平均速率的 10% 与一个脉冲的窗口计数误差中的较大值。");
        }
        Esp32BaseWeb::sendChunk("</p>");
    } else if (g_app->pendingLearnedZoneId() == zoneId) {
        char suggestion[20]{};
        const bool validSuggestion = IrrigationConfigRules::formatLitersPerMinute(
            g_app->pendingLearnedFlowMlPerMinute(), suggestion, sizeof(suggestion));
        if (validSuggestion) {
            char baselineRate[20]{};
            formatSignedHundredths(
                g_app->pendingLearnedBaselinePulseRateX100(),
                baselineRate, sizeof(baselineRate));
            Esp32BaseWeb::sendChunk("<div class='learning-head'><h3>学习结果</h3><span class='tag ok'>已稳定</span></div><div class='learning-result'><div><span class='learning-label'>建议基准流量</span><span class='learning-result-value'>");
            Esp32BaseWeb::writeHtmlEscaped(suggestion);
            Esp32BaseWeb::sendChunk(" L/min</span></div><div><span class='learning-label'>原始脉冲基准</span><span class='learning-result-value'>");
            Esp32BaseWeb::writeHtmlEscaped(baselineRate);
            Esp32BaseWeb::sendChunk(" P/s</span></div></div><p class='learning-rule'>结果按最近 5 个稳定窗口的总脉冲数 ÷ 总实际时长计算；流量计系数只用于换算显示流量。</p><div class='learning-actions'><form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='zone_id' value='");
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
        Esp32BaseWeb::sendChunk("<p class='muted'>系统每 5 秒统计一次原始脉冲速率；最近 5 个窗口波动不超过 10%，并容忍一个脉冲的计数误差时自动完成。</p><form method='post' action='/irrigation/zones/learning' onsubmit=\"return confirm('确认开始学习基准流量？开始后这条水路会立即出水。')&&once(this)\"><input type='hidden' name='action' value='start'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><div class='actions'><input type='submit' value='"); Esp32BaseWeb::sendChunk(zone.baselinePulseRateX100 == 0 ? "开始学习" : "重新学习"); Esp32BaseWeb::sendChunk("'></div></form>");
        if (zone.baselinePulseRateX100 != 0) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/learning' onsubmit=\"return confirm('确认清除这条水路的基准流量？清除后将停用该水路的高低流量报警。')&&once(this)\"><input type='hidden' name='action' value='clear'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision); Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='清除基准'></div></form>");
        }
    }
    const bool showLearningWindows =
        (active || g_app->pendingLearnedZoneId() == zoneId) &&
        status.learningSampleCount != 0;
    if (active || showLearningWindows) {
        Esp32BaseWeb::sendChunk("<div class='learning-debug'><h3>最近窗口明细</h3><p>按采集时间从早到晚排列；换算流量使用当前流量计系数。</p><table class='learning-table'><thead><tr><th>窗口</th><th>脉冲数</th><th>实际时长</th><th>脉冲速率</th><th>换算流量</th></tr></thead><tbody id='learn-window-body'><tr id='learn-window-empty'");
        if (status.learningSampleCount != 0) {
            Esp32BaseWeb::sendChunk(" style='display:none'");
        }
        Esp32BaseWeb::sendChunk("><td class='learning-empty' colspan='5'>等待第一个完整窗口</td></tr>");
        for (uint8_t index = 0; index < 5; ++index) {
            const bool populated = index < status.learningSampleCount;
            Esp32BaseWeb::sendChunk("<tr id='learn-window-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'");
            if (!populated) Esp32BaseWeb::sendChunk(" style='display:none'");
            Esp32BaseWeb::sendChunk("><td data-label='窗口' id='learn-window-index-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            sendUnsigned(index + 1U);
            Esp32BaseWeb::sendChunk("</td><td data-label='脉冲数' id='learn-window-pulses-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) sendUnsigned(status.learningWindows[index].pulseCount);
            Esp32BaseWeb::sendChunk("</td><td data-label='实际时长' id='learn-window-duration-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) sendMilliseconds(status.learningWindows[index].windowMs);
            Esp32BaseWeb::sendChunk("</td><td data-label='脉冲速率' id='learn-window-rate-");
            sendUnsigned(index);
            Esp32BaseWeb::sendChunk("'>");
            if (populated) {
                char rate[20]{};
                formatSignedHundredths(status.learningWindows[index].pulseRateX100,
                                       rate, sizeof(rate));
                Esp32BaseWeb::writeHtmlEscaped(rate);
                Esp32BaseWeb::sendChunk(" P/s");
            }
            Esp32BaseWeb::sendChunk("</td><td data-label='换算流量' id='learn-window-flow-");
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
            Esp32BaseWeb::sendChunk("</td></tr>");
        }
        uint32_t totalPulses = 0;
        uint32_t totalWindowMs = 0;
        for (uint8_t index = 0; index < status.learningSampleCount; ++index) {
            totalPulses += status.learningWindows[index].pulseCount;
            totalWindowMs += status.learningWindows[index].windowMs;
        }
        char coefficient[20]{};
        IrrigationConfigRules::formatPulsesPerLiter(
            config->flowMeter.pulsesPerLiterX100,
            coefficient, sizeof(coefficient));
        Esp32BaseWeb::sendChunk("</tbody></table><p class='learning-summary'><span>最近窗口合计：<span id='learn-window-total-pulses'>");
        sendUnsigned(totalPulses);
        Esp32BaseWeb::sendChunk("</span> 个脉冲 / <span id='learn-window-total-duration'>");
        sendMilliseconds(totalWindowMs);
        const uint32_t sessionPulseCount =
            active ? status.pulseCount : status.zones[0].pulseCount;
        Esp32BaseWeb::sendChunk("</span></span><span>本次累计脉冲：<span id='learn-total-pulses'>");
        sendUnsigned(sessionPulseCount);
        Esp32BaseWeb::sendChunk("</span></span><span>当前流量计系数：");
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
    Esp32BaseWeb::sendChunk("<p><a class='btnlink secondary' href='/irrigation/zones'>返回水路设置</a></p>");
    if (active) Esp32BaseWeb::sendChunk("<script>(function(){function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}function liters(v){return (Math.max(0,Number(v)||0)/1000).toFixed(3)+' L/min'}function rate(v){return (Math.max(0,Number(v)||0)/100).toFixed(2)+' P/s'}function seconds(v){return (Math.max(0,Number(v)||0)/1000).toFixed(1)+' 秒'}function duration(v){v=Math.max(0,Number(v)||0);return v<60?v+' 秒':Math.floor(v/60)+' 分 '+v%60+' 秒'}function updateWindows(s){var rows=s.learningWindows||[],tp=0,tm=0,empty=document.getElementById('learn-window-empty');if(empty)empty.style.display=rows.length?'none':'';for(var i=0;i<5;i++){var row=document.getElementById('learn-window-'+i),w=rows[i];if(!row)continue;row.style.display=w?'':'none';if(!w)continue;set('learn-window-index-'+i,i+1);set('learn-window-pulses-'+i,w.pulseCount);set('learn-window-duration-'+i,seconds(w.windowMs));set('learn-window-rate-'+i,rate(w.pulseRateX100));set('learn-window-flow-'+i,liters(w.flowMlPerMinute));tp+=Number(w.pulseCount)||0;tm+=Number(w.windowMs)||0}set('learn-window-total-pulses',tp);set('learn-window-total-duration',seconds(tm))}function updateRule(s){var n=Number(s.learningSampleCount)||0;if(!n)return '等待首个 5 秒有效窗口；每个窗口必须检测到脉冲。';var min=Number(s.learningMinimumPulseRateX100)||0,max=Number(s.learningMaximumPulseRateX100)||0,allow=Number(s.learningAllowedPulseRateSpreadX100)||0;return '最近 '+n+' 个窗口的原始脉冲速率为 '+rate(min)+'～'+rate(max)+'，速率跨度 '+rate(max-min)+'；收满 5 个窗口后，跨度不超过 '+rate(allow)+' 即完成。允许跨度取平均速率的 10% 与一个脉冲的窗口计数误差中的较大值。'}function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active||s.purpose!==2){location.reload();return}set('learn-elapsed',duration(s.elapsedSec));set('learn-current',liters(s.currentFlowMlPerMinute));set('learn-average',liters(s.learningAverageMlPerMinute));set('learn-state',Number(s.learningSampleCount)<5?'采集中 '+s.learningSampleCount+'/5':'波动偏大');set('learn-rule',updateRule(s));set('learn-total-pulses',s.pulseCount);updateWindows(s);setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}setTimeout(poll,1000)})();</script>");
    endPage();
}

void IrrigationWeb::records() {
    if (!beginPage("浇水记录", "最新记录优先")) return;
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.record-toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 0 10px}.record-toolbar p{margin:0;color:var(--eb-muted);font-size:13px}.record-table{width:100%;min-width:800px;border-collapse:collapse;font-size:13px}.record-table th,.record-table td{padding:9px 8px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:middle}.record-table th{color:var(--eb-muted);font-weight:650;white-space:nowrap}.record-table tbody tr:last-child td{border-bottom:0}.record-table tbody tr:hover{background:var(--eb-soft)}.record-id,.record-number{white-space:nowrap;font-variant-numeric:tabular-nums}.record-time{min-width:12em;white-space:nowrap}.record-action{width:1%;white-space:nowrap}.record-empty-cell{padding:24px 16px!important;background:var(--eb-soft);text-align:center!important}.record-empty-cell b{display:block;margin-bottom:4px;font-size:15px}.record-empty-cell span{color:var(--eb-muted)}
.record-detail-dialog{width:min(920px,calc(100vw - 28px))}.record-detail-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:12px}.record-detail-head h2{margin:1px 0 0;font-size:20px}.record-detail-head>div>span{font-size:12px}.record-detail-close{min-height:30px}.record-detail-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px 12px;padding:12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft)}.record-detail-grid>div{min-width:0}.record-detail-grid b{display:block;color:var(--eb-muted);font-size:11px;font-weight:650}.record-detail-grid span{display:block;margin-top:2px;overflow-wrap:anywhere}.record-detail-grid .tag{display:inline-flex}.record-detail-section{margin-top:16px}.record-detail-section>h3{margin-bottom:2px}.record-detail-section>p{margin-bottom:8px;color:var(--eb-muted);font-size:12px}.record-zone-table{width:100%;min-width:820px;border-collapse:collapse;font-size:12px}.record-zone-table th,.record-zone-table td{padding:7px 6px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:top}.record-zone-table th{color:var(--eb-muted);font-weight:650;white-space:nowrap}.record-zone-table tr:last-child td{border-bottom:0}.record-zone-table td>b,.record-zone-table td>small{display:block}.record-zone-table td>small{color:var(--eb-muted)}.record-flags{display:flex;flex-wrap:wrap;gap:4px;min-width:9em}
@media(max-width:760px){.record-toolbar{align-items:flex-start}.record-toolbar p{display:none}.record-table{display:block;min-width:0}.record-table thead{display:none}.record-table tbody{display:grid;gap:9px}.record-table tr{display:grid;padding:11px;border:1px solid var(--eb-line);border-radius:8px;background:#fff}.record-table tbody tr:hover{background:#fff}.record-table td{display:grid;grid-template-columns:7.2em minmax(0,1fr);gap:8px;padding:4px 0;border:0;white-space:normal}.record-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:12px;font-weight:650}.record-table .record-action{display:flex;justify-content:flex-end;width:auto;padding-top:9px;border-top:1px solid var(--eb-line-soft);margin-top:5px}.record-table .record-action::before{display:none}.record-table .record-action>.btnlink{width:100%;min-height:34px}.record-table .record-empty-row{display:block;padding:0}.record-table .record-empty-cell{display:block;padding:22px 12px!important}.record-table .record-empty-cell::before{display:none}.record-detail-dialog{width:calc(100vw - 20px);padding:12px!important}.record-detail-grid{grid-template-columns:repeat(2,minmax(0,1fr));padding:10px}.record-zone-table{display:block;min-width:0}.record-zone-table thead{display:none}.record-zone-table tbody{display:grid;gap:8px}.record-zone-table tr{display:grid;padding:9px;border:1px solid var(--eb-line);border-radius:7px}.record-zone-table td{display:grid;grid-template-columns:7.5em minmax(0,1fr);gap:7px;padding:3px 0;border:0}.record-zone-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:11px;font-weight:650}.record-zone-table td>b,.record-zone-table td>small{display:inline}.record-zone-table td>small{margin-left:4px}.record-flags{min-width:0}.record-detail-dialog>.actions button{width:100%}}
@media(max-width:420px){.record-detail-grid{grid-template-columns:1fr}.record-detail-head h2{font-size:18px}}
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
    Esp32BaseWeb::sendChunk("</p>");
    if (statusReady && status.recordCount != 0) {
        Esp32BaseWeb::sendChunk("<a class='btnlink' href='/irrigation/records.csv'>导出 CSV</a>");
    } else {
        Esp32BaseWeb::sendChunk("<span class='btnlink disabled' aria-disabled='true'>导出 CSV</span>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    if (!statusReady) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                 "浇水记录暂时无法读取",
                                 "请在系统状态中检查业务记录存储。");
    } else {
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='record-table'><thead><tr><th>编号</th><th>完成时间</th><th>来源</th><th>结果</th><th>实际浇水时间</th><th>历史估算水量</th><th>操作</th></tr></thead><tbody>");
        RecordRowsContext context{g_app->configuration(), 0};
        bool readOk = true;
        if (status.recordCount == 0) {
            Esp32BaseWeb::sendChunk("<tr class='record-empty-row'><td class='record-empty-cell' colspan='7'><b>还没有浇水记录</b><span>完成第一次实际出水后，记录会显示在这张表格中。</span></td></tr>");
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
.event-filter{display:flex;flex-wrap:wrap;align-items:flex-end;gap:10px 12px;margin-bottom:14px}.event-filter label{display:grid;gap:4px;margin:0;color:var(--eb-muted);font-size:12px}.event-filter select{width:180px;max-width:none;min-height:34px;margin:0}.event-filter-actions{display:flex;align-items:center;gap:8px}.event-filter-actions input,.event-filter-actions .btnlink{min-height:34px;margin:0}.event-table{width:100%;min-width:900px;border-collapse:collapse;table-layout:auto;font-size:13px}.event-table th,.event-table td{padding:11px 10px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:top}.event-table th{padding-top:9px;padding-bottom:9px;color:var(--eb-muted);font-weight:650;white-space:nowrap;background:var(--eb-soft)}.event-table tbody tr:last-child td{border-bottom:0}.event-table tbody tr:hover{background:#fbfcfd}.event-time{width:1%;min-width:10.5em;white-space:nowrap;font-variant-numeric:tabular-nums}.event-level{width:1%;white-space:nowrap}.event-category{width:1%;min-width:8em;white-space:nowrap}.event-title{min-width:15em;line-height:1.5;font-weight:400}.event-summary{min-width:20em;color:var(--eb-muted);line-height:1.5}.event-action{width:1%;white-space:nowrap;text-align:right!important}.event-empty{padding:26px!important;text-align:center!important;color:var(--eb-muted)}.event-detail{width:min(780px,calc(100vw - 28px));text-align:left;white-space:normal}.event-detail-head{display:flex;align-items:flex-start;justify-content:space-between;gap:16px}.event-detail-heading{flex:1;min-width:0}.event-detail-head h2{margin:7px 0 0;font-size:20px;font-weight:500}.event-detail-close{flex:0 0 auto;min-height:32px}.event-detail-summary{margin:16px 0;padding:12px 14px;border-radius:8px;background:var(--eb-soft);font-size:14px;line-height:1.55}.event-detail-section{margin-top:16px}.event-detail-section h3{margin-bottom:7px;color:#344054;font-size:13px;font-weight:500}.event-detail-grid{display:grid;gap:10px 18px;padding:12px 14px;border:1px solid var(--eb-line-soft);border-radius:8px;background:#fff}.event-detail-overview{grid-template-columns:repeat(3,minmax(0,1fr))}.event-detail-business{grid-template-columns:repeat(auto-fit,minmax(180px,1fr))}.event-detail-technical-grid{grid-template-columns:repeat(3,minmax(0,1fr));background:var(--eb-soft)}.event-detail-grid>div{min-width:0}.event-detail-grid b{display:block;color:var(--eb-muted);font-size:11px;font-weight:400}.event-detail-grid span{display:block;margin-top:3px;overflow-wrap:anywhere}.event-detail-grid span.tag{display:inline-flex}.event-time-value{white-space:nowrap;font-variant-numeric:tabular-nums}.event-technical{margin-top:18px;padding-top:13px;border-top:1px solid var(--eb-line)}.event-technical summary{cursor:pointer;color:var(--eb-muted);font-size:13px;font-weight:400}.event-technical[open] summary{margin-bottom:10px}
@media(max-width:760px){.event-filter{display:grid;grid-template-columns:1fr 1fr}.event-filter label,.event-filter select{width:100%}.event-filter-actions{grid-column:1/-1}.event-filter-actions input,.event-filter-actions .btnlink{flex:1}.event-table{display:block;min-width:0}.event-table thead{display:none}.event-table tbody{display:grid;gap:9px}.event-table tr{display:grid;padding:11px;border:1px solid var(--eb-line);border-radius:8px;background:#fff}.event-table tbody tr:hover{background:#fff}.event-table td{display:grid;grid-template-columns:6em minmax(0,1fr);gap:8px;width:auto;min-width:0;padding:4px 0;border:0;text-align:left!important;white-space:normal}.event-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:12px;font-weight:500}.event-table .event-time{white-space:nowrap}.event-table .event-action{display:flex;justify-content:flex-end;padding-top:9px;border-top:1px solid var(--eb-line-soft);margin-top:5px}.event-table .event-action::before{display:none}.event-table .event-action>.btnlink{width:100%;min-height:34px}.event-empty{display:block!important}.event-empty::before{display:none}.event-detail{width:calc(100vw - 20px);padding:12px!important}.event-detail-overview,.event-detail-technical-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
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

    EventRowsContext context{&filter, (page - 1U) * 20U, 20U, 0, 0};
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
    Esp32BaseWeb::sendChunk(",\"learningSampleCount\":"); sendUnsigned(status.learningSampleCount);
    Esp32BaseWeb::sendChunk(",\"learningWindows\":[");
    for (uint8_t index = 0; index < status.learningSampleCount; ++index) {
        if (index != 0) Esp32BaseWeb::sendChunk(",");
        const WateringStatus::LearningWindowSample& window =
            status.learningWindows[index];
        Esp32BaseWeb::sendChunk("{\"pulseCount\":"); sendUnsigned(window.pulseCount);
        Esp32BaseWeb::sendChunk(",\"windowMs\":"); sendUnsigned(window.windowMs);
        Esp32BaseWeb::sendChunk(",\"pulseRateX100\":"); sendUnsigned(window.pulseRateX100);
        Esp32BaseWeb::sendChunk(",\"flowMlPerMinute\":"); sendUnsigned(window.flowMlPerMinute);
        Esp32BaseWeb::sendChunk("}");
    }
    Esp32BaseWeb::sendChunk("]");
    Esp32BaseWeb::sendChunk(",\"automaticMode\":"); sendUnsigned(static_cast<uint32_t>(g_app->automaticWateringState().mode));
    Esp32BaseWeb::sendChunk(",\"unexpectedFlowAlarm\":"); Esp32BaseWeb::sendChunk(g_app->unexpectedFlowAlarm() ? "true" : "false");
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
        Esp32BaseWeb::sendChunk(",\"actualWateringSec\":"); sendUnsigned(zone.actualWateringSec);
        Esp32BaseWeb::sendChunk(",\"pulseCount\":"); sendUnsigned(zone.pulseCount);
        Esp32BaseWeb::sendChunk(",\"estimatedWaterMl\":"); sendUnsigned(zone.estimatedWaterMl);
        Esp32BaseWeb::sendChunk(",\"lowFlowDetected\":"); Esp32BaseWeb::sendChunk(zone.lowFlowDetected ? "true" : "false");
        Esp32BaseWeb::sendChunk(",\"highFlowDetected\":"); Esp32BaseWeb::sendChunk(zone.highFlowDetected ? "true" : "false");
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

void IrrigationWeb::recordsCsv() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (g_app->wateringStatus().active) {
        Esp32BaseWeb::sendText(409, "watering_active");
        return;
    }
    Esp32BaseRecordStore::StoreStatus status{};
    if (!g_app->readWateringRecordStoreStatus(status) ||
        !Esp32BaseWeb::beginCsv(200, "watering-records.csv")) {
        Esp32BaseWeb::sendText(500, "record_store_unavailable");
        return;
    }
    Esp32BaseWeb::sendChunk("record_id,completed_epoch,source,plan_id,result,duration_sec,total_pulses,estimated_water_ml\r\n");
    for (uint32_t offset = 0; offset < status.recordCount; offset += 50U) {
        if (!g_app->readLatestWateringRecords(offset, 50, csvRecord, nullptr)) break;
    }
    Esp32BaseWeb::endResponse();
}
