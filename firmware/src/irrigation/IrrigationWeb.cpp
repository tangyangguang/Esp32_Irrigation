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
        return g_app->saveConfiguration(next, revision);
    }
    if (!actionIs("save")) {
        return false;
    }
    char name[kObjectNameCapacity]{};
    if (!getParam("name", name, sizeof(name))) {
        return false;
    }
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
    return g_app->saveConfiguration(next, revision);
}

bool saveZoneFromRequest() {
    const IrrigationConfig* current = g_app->configuration();
    uint32_t zoneId = 0, revision = 0;
    char name[kObjectNameCapacity]{};
    char learnedFlow[24]{};
    if (!current || !uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId) ||
        !uintParam("revision", 1, UINT32_MAX, revision) ||
        !getParam("name", name, sizeof(name)) ||
        !getParam("learned_flow", learnedFlow, sizeof(learnedFlow))) {
        return false;
    }
    IrrigationConfig next = *current;
    ZoneConfig& zone = next.zones[zoneId - 1U];
    zone.enabled = Esp32BaseWeb::hasParam("enabled");
    std::snprintf(zone.name.data(), zone.name.size(), "%s", name);
    if (!IrrigationConfigRules::parseLitersPerMinute(
            learnedFlow, zone.learnedFlowMlPerMinute)) {
        return false;
    }
    return g_app->saveConfiguration(next, revision);
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
        case WateringSource::ManualZones: return "手动区域";
        case WateringSource::ManualPlan: return "手动计划";
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

}  // namespace

bool IrrigationWeb::registerRoutes(IrrigationApp& app) {
    g_app = &app;
    Esp32BaseWeb::setDeviceName("智能浇水");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP);
    Esp32BaseWeb::setHomePath("/irrigation");
    return Esp32BaseWeb::addPage("/irrigation", "总览", overview) &&
           Esp32BaseWeb::addPage("/irrigation/run", "浇水", run) &&
           Esp32BaseWeb::addPage("/irrigation/plans", "计划", plans) &&
           Esp32BaseWeb::addPage("/irrigation/zones", "水路设置", zones) &&
           Esp32BaseWeb::addPage("/irrigation/records", "浇水记录", records) &&
           Esp32BaseWeb::addPage("/irrigation/settings", "系统参数", settings) &&
           Esp32BaseWeb::addRoute("/irrigation", Esp32BaseWeb::METHOD_POST, overview) &&
           Esp32BaseWeb::addRoute("/irrigation/run", Esp32BaseWeb::METHOD_POST, run) &&
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
           Esp32BaseWeb::addRoute("/irrigation/records.csv",
                                  Esp32BaseWeb::METHOD_GET,
                                  recordsCsv);
}

void IrrigationWeb::overview() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_automatic_state")) return;
        bool success = false;
        if (actionIs("pause_indefinitely")) {
            success = g_app->pauseAutomaticWateringIndefinitely();
        } else if (actionIs("resume")) {
            success = g_app->resumeAutomaticWatering();
        } else if (actionIs("pause_hours")) {
            uint32_t hours = 0;
            uint32_t resumeAt = 0;
            const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
            success = uintParam("hours", 1, 8760, hours) && now.synced &&
                      IrrigationTime::resumeAfterHours(now.epochSec, hours, resumeAt) &&
                      g_app->pauseAutomaticWateringUntil(resumeAt);
        } else if (actionIs("pause_until")) {
            char localDateTime[20]{};
            uint32_t resumeAt = 0;
            success = getParam("resume_at", localDateTime, sizeof(localDateTime)) &&
                      IrrigationTime::parseLocalDateTimeUtc8(localDateTime, resumeAt) &&
                      g_app->pauseAutomaticWateringUntil(resumeAt);
        }
        redirectResult("/irrigation", success);
        return;
    }
    if (!Esp32BaseWeb::checkAuth()) return;
    Esp32BaseWeb::sendHeader("智能浇水");
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.home-hero{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:18px;align-items:center;padding:20px;border:1px solid var(--eb-line);border-radius:12px;background:linear-gradient(135deg,#f4faf7,#fff);margin:12px 0}
.home-hero.warn{background:linear-gradient(135deg,var(--eb-warn-soft),#fff);border-color:#efcf96}.home-hero.danger{background:linear-gradient(135deg,var(--eb-danger-soft),#fff);border-color:#efc0ba}.home-hero.info{background:linear-gradient(135deg,var(--eb-info-soft),#fff);border-color:#cbdde5}
.home-eyebrow{display:block;margin-bottom:5px;color:var(--eb-muted);font-size:12px;font-weight:750}.home-hero h1{margin:0 0 6px;font-size:24px}.home-hero p{margin:0;color:var(--eb-muted)}.home-hero .btnlink{min-height:38px;padding:0 16px}
.home-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin:12px 0}.home-card{display:flex;flex-direction:column;min-height:210px;padding:18px;border:1px solid var(--eb-line);border-radius:12px;background:#fff}.home-card-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:14px}.home-card-head h2{margin:0;font-size:17px}.home-main{font-size:22px;font-weight:760;line-height:1.3;overflow-wrap:anywhere}.home-sub{margin:6px 0 0;color:var(--eb-muted);overflow-wrap:anywhere}.home-facts{display:grid;gap:7px;margin:14px 0 0}.home-fact{display:grid;grid-template-columns:6.5em minmax(0,1fr);gap:8px;font-size:13px}.home-fact span:first-child{color:var(--eb-muted)}.home-card-actions{display:flex;align-items:center;gap:8px;margin-top:auto;padding-top:16px}.home-card-actions form{margin:0}.home-empty{color:var(--eb-muted);line-height:1.7}.home-note{margin-top:10px;padding:10px 12px;border-radius:8px;background:var(--eb-soft);color:var(--eb-muted);font-size:13px}
.pause-modal{width:min(580px,calc(100vw - 28px));padding:20px!important}.pause-modal>p{margin-top:-2px}.pause-preview{padding:14px 16px;border:1px solid #cbdde5;border-radius:10px;background:var(--eb-info-soft);margin:14px 0}.pause-preview b{display:block;font-size:19px}.pause-preview span{display:block;margin-top:4px;color:var(--eb-muted);font-size:13px}.pause-modes{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin:12px 0}.pause-mode{min-height:42px;white-space:normal}.pause-mode.selected{background:var(--eb-primary-soft);border-color:#9fc8d0;color:var(--eb-primary)}.pause-pane{padding:14px;border:1px solid var(--eb-line-soft);border-radius:10px;background:var(--eb-soft)}.pause-pane[hidden]{display:none}.pause-quick{display:flex;flex-wrap:wrap;gap:7px;margin:10px 0}.pause-quick button{min-height:34px;background:#fff;color:#344054;border:1px solid var(--eb-button-border)}.pause-quick button.selected{background:var(--eb-primary-soft);border-color:#9fc8d0;color:var(--eb-primary)}.pause-stepper{display:grid;grid-template-columns:44px minmax(0,1fr) 44px;gap:8px;align-items:center;max-width:320px}.pause-stepper button{min-height:42px;padding:0;font-size:20px}.pause-stepper input{width:100%;max-width:none;min-height:42px;margin:0;text-align:center;font-size:18px;font-weight:700}.pause-pane label{margin-bottom:7px}.pause-pane input[type=datetime-local]{width:100%;max-width:none;margin:0}.pause-help{display:block;margin-top:8px;color:var(--eb-muted)}.pause-modal .actions{margin-top:16px}.pause-modal .actions button,.pause-modal .actions input{min-height:40px}
@media(max-width:760px){.home-hero,.home-grid{grid-template-columns:1fr}.home-hero{padding:16px}.home-hero h1{font-size:21px}.home-hero .btnlink{width:100%}.home-card{min-height:0;padding:15px}.home-fact{grid-template-columns:1fr;gap:2px}.pause-modal{width:calc(100vw - 20px);padding:14px!important}.pause-modes{grid-template-columns:1fr}.pause-mode{min-height:38px}.pause-modal .actions{display:grid;grid-template-columns:1fr 1fr}.pause-modal .actions button,.pause-modal .actions input{width:100%;margin:0}}
</style>)HTML");
    const WateringStatus watering = g_app->wateringStatus();
    const AutomaticWateringState automatic = g_app->automaticWateringState();
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    const WateringScheduler::TimeState schedulerTime = g_app->schedulerTimeState();
    const bool timeTrusted = now.synced && schedulerTime == WateringScheduler::TimeState::Ready;
    const bool storageFault = g_app->recordStorageFault() || g_app->eventStorageFault() ||
                              g_app->schedulerStorageFault() || g_app->checkpointStorageFault();
    const IrrigationConfig* config = g_app->configuration();
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
    const char* heroHref = "/irrigation/run";
    const char* heroAction = "开始浇水";
    if (g_app->unexpectedFlowAlarm()) {
        heroTone = " danger";
        heroTitle = "检测到非计划水流";
        heroDescription = "所有水路关闭时仍检测到水流，请检查阀门和管路是否漏水。";
        heroHref = "/irrigation/zones";
        heroAction = "查看水路设置";
    } else if (watering.active) {
        heroTone = " info";
        const char* zoneName = nullptr;
        if (config && watering.activeZoneId >= 1 && watering.activeZoneId <= config->zones.size()) {
            zoneName = config->zones[watering.activeZoneId - 1U].name.data();
        }
        if (zoneName) {
            std::snprintf(value, sizeof(value), "正在浇水：%s", zoneName);
            heroTitle = value;
        } else {
            heroTitle = "正在浇水";
        }
        formatElapsed(watering.elapsedSec, secondary, sizeof(secondary));
        heroDescription = secondary;
        heroHref = "/irrigation/run";
        heroAction = "查看浇水";
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
    Esp32BaseWeb::sendChunk("</p></div><a class='btnlink info' href='");
    Esp32BaseWeb::writeHtmlEscaped(heroHref);
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::writeHtmlEscaped(heroAction);
    Esp32BaseWeb::sendChunk("</a></section>");
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
    Esp32BaseWeb::sendChunk("<div class='home-card-actions'>");
    if (automatic.mode == AutomaticWateringMode::Enabled) {
        Esp32BaseWeb::sendChunk("<button type='button' class='btnlink info' onclick=\"document.getElementById('pause-auto').showModal()\">暂停自动浇水</button>");
    } else {
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation' onsubmit='return once(this)'><input type='hidden' name='action' value='resume'><input type='submit' class='btnlink ok' value='提前恢复'></form>");
    }
    if (next.status == NextAutomaticWateringStatus::NoEnabledPlans) {
        Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/plans'>设置计划</a>");
    }
    Esp32BaseWeb::sendChunk("</div></section>");

    Esp32BaseWeb::sendChunk("<section class='home-card'><div class='home-card-head'><h2>最近一次浇水</h2><a class='btnlink compact secondary' href='/irrigation/records'>全部记录</a></div>");
    if (g_app->recordStorageFault()) {
        Esp32BaseWeb::sendChunk("<div class='home-empty'>浇水记录存储异常，暂时无法读取最近记录。</div>");
    } else if (!latest.found) {
        Esp32BaseWeb::sendChunk("<div class='home-empty'>还没有浇水记录。完成第一次实际出水后，这里会显示结果。</div><div class='home-card-actions'><a class='btnlink info' href='/irrigation/run'>开始浇水</a></div>");
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
    const uint32_t initialResumeEpoch = timeTrusted && now.epochSec <= UINT32_MAX - 86400U
                                            ? now.epochSec + 86400U
                                            : 1767196800UL;
    char inputDateTime[20]{};
    char minimumDateTime[20]{};
    formatInputDateTime(initialResumeEpoch, inputDateTime, sizeof(inputDateTime));
    formatInputDateTime(timeTrusted && now.epochSec <= UINT32_MAX - 60U ? now.epochSec + 60U
                                                                       : 1767196800UL,
                        minimumDateTime, sizeof(minimumDateTime));
    Esp32BaseWeb::sendChunk("<dialog id='pause-auto' class='panel eb-modal pause-modal' data-eb-light-dismiss='1'><h2>暂停自动浇水</h2><p class='muted'>暂停只影响之后的自动计划，不停止当前浇水，也不影响手动浇水。</p><form id='pause-form' method='post' action='/irrigation' data-now-epoch='");
    sendUnsigned(now.epochSec);
    Esp32BaseWeb::sendChunk("' data-time-trusted='");
    Esp32BaseWeb::sendChunk(timeTrusted ? "1" : "0");
    Esp32BaseWeb::sendChunk("'><input id='pause-action' type='hidden' name='action' value='pause_hours'><div class='pause-preview' aria-live='polite'><b id='pause-preview-title'>将暂停 24 小时</b><span id='pause-preview-detail'>正在计算恢复时间…</span></div>");
    if (!timeTrusted) {
        Esp32BaseWeb::sendChunk("<div class='notice'><b>设备时间尚未就绪</b><br>暂时只能选择暂停至手动恢复。</div>");
    }
    Esp32BaseWeb::sendChunk("<div class='pause-modes' role='group' aria-label='暂停方式'><button type='button' class='pause-mode secondary selected' data-pause-mode='duration'");
    if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
    Esp32BaseWeb::sendChunk(">按时长</button><button type='button' class='pause-mode secondary' data-pause-mode='until'");
    if (!timeTrusted) Esp32BaseWeb::sendChunk(" disabled");
    Esp32BaseWeb::sendChunk(">到指定时间</button><button type='button' class='pause-mode secondary' data-pause-mode='manual'>手动恢复</button></div><div id='pause-duration' class='pause-pane'><label for='pause-hours'>暂停多少小时</label><div class='pause-stepper'><button type='button' class='secondary' id='pause-minus' aria-label='减少一小时'>−</button><input id='pause-hours' type='number' name='hours' min='1' max='8760' value='24' inputmode='numeric'><button type='button' class='secondary' id='pause-plus' aria-label='增加一小时'>+</button></div><div class='pause-quick' aria-label='常用暂停时长'>");
    const uint16_t quickHours[] = {1, 6, 12, 24, 48};
    for (const uint16_t hours : quickHours) {
        Esp32BaseWeb::sendChunk("<button type='button' data-hours='");
        sendUnsigned(hours);
        Esp32BaseWeb::sendChunk("'");
        if (hours == 24U) Esp32BaseWeb::sendChunk(" class='selected'");
        Esp32BaseWeb::sendChunk(">+");
        sendUnsigned(hours);
        Esp32BaseWeb::sendChunk(" 小时</button>");
    }
    Esp32BaseWeb::sendChunk("</div><small class='pause-help'>可输入 1～8760 个整小时，使用 − / + 每次调整 1 小时。</small></div><div id='pause-until' class='pause-pane' hidden><label>常用恢复时间</label><div class='pause-quick'><button type='button' data-day='1' data-hour='6'>明早 6 点</button><button type='button' data-day='1' data-hour='8'>明早 8 点</button><button type='button' data-day='1' data-hour='20'>明晚 8 点</button><button type='button' data-day='2' data-hour='6'>后天早 6 点</button></div><label for='pause-resume-at'>自定义时间（UTC+8）</label><input id='pause-resume-at' type='datetime-local' name='resume_at' min='");
    Esp32BaseWeb::writeHtmlEscaped(minimumDateTime);
    Esp32BaseWeb::sendChunk("' max='2099-12-31T23:59' value='");
    Esp32BaseWeb::writeHtmlEscaped(inputDateTime);
    Esp32BaseWeb::sendChunk("'><small class='pause-help'>选择一个明确的恢复日期和时间。</small></div><div id='pause-manual' class='pause-pane' hidden><b>暂停至手动恢复</b><p class='muted'>自动计划会一直保持暂停，直到你回到首页点击“提前恢复”。</p></div><div class='actions'><button type='button' class='secondary' onclick='this.closest(\"dialog\").close()'>取消</button><input type='submit' value='确认暂停'></div></form></dialog>");
    Esp32BaseWeb::sendChunk(R"HTML(<script>(function(){
var form=document.getElementById('pause-form');if(!form)return;
var action=document.getElementById('pause-action'),hours=document.getElementById('pause-hours'),resume=document.getElementById('pause-resume-at');
var title=document.getElementById('pause-preview-title'),detail=document.getElementById('pause-preview-detail');
var panes={duration:document.getElementById('pause-duration'),until:document.getElementById('pause-until'),manual:document.getElementById('pause-manual')};
var mode='duration',serverNow=Number(form.dataset.nowEpoch||0),loadedAt=Date.now(),trusted=form.dataset.timeTrusted==='1',offset=28800;
function nowEpoch(){return serverNow+Math.floor((Date.now()-loadedAt)/1000)}
function pad(v){return String(v).padStart(2,'0')}
function localDate(epoch){return new Date((epoch+offset)*1000)}
function inputFromEpoch(epoch){var d=localDate(epoch);return d.getUTCFullYear()+'-'+pad(d.getUTCMonth()+1)+'-'+pad(d.getUTCDate())+'T'+pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())}
function epochFromInput(value){if(!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}$/.test(value))return 0;var p=value.match(/\d+/g).map(Number);return Math.floor(Date.UTC(p[0],p[1]-1,p[2],p[3],p[4])/1000)-offset}
function targetLabel(epoch){var d=localDate(epoch),n=nowEpoch(),day=Math.floor((epoch+offset)/86400),today=Math.floor((n+offset)/86400),prefix=day===today?'今天':day===today+1?'明天':day===today+2?'后天':(d.getUTCMonth()+1)+'月'+d.getUTCDate()+'日';return prefix+' '+pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())}
function fullLabel(epoch){var d=localDate(epoch),week=['周日','周一','周二','周三','周四','周五','周六'],left=Math.max(0,epoch-nowEpoch()),relative=left<3600?Math.ceil(left/60)+' 分钟后':left<86400?Math.ceil(left/3600)+' 小时后':Math.ceil(left/86400)+' 天后';return d.getUTCFullYear()+'年'+(d.getUTCMonth()+1)+'月'+d.getUTCDate()+'日 '+week[d.getUTCDay()]+' '+pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())+'（约 '+relative+'）'}
function clampHours(){var v=Math.round(Number(hours.value)||1);v=Math.max(1,Math.min(8760,v));hours.value=v;return v}
function update(){if(!trusted&&mode!=='manual')mode='manual';document.querySelectorAll('[data-pause-mode]').forEach(function(b){b.classList.toggle('selected',b.dataset.pauseMode===mode);b.setAttribute('aria-pressed',b.dataset.pauseMode===mode?'true':'false')});Object.keys(panes).forEach(function(k){panes[k].hidden=k!==mode});if(mode==='manual'){action.value='pause_indefinitely';title.textContent='暂停至手动恢复';detail.textContent='自动计划保持暂停，手动浇水仍可使用。';return}var epoch;if(mode==='duration'){var h=clampHours();action.value='pause_hours';epoch=nowEpoch()+h*3600;title.textContent='将暂停 '+h+' 小时'}else{action.value='pause_until';epoch=epochFromInput(resume.value);title.textContent=epoch?'将暂停到'+targetLabel(epoch):'请选择恢复时间'}detail.textContent=epoch?fullLabel(epoch):'恢复时间必须晚于现在。'}
function setMode(next){mode=next;update()}
document.querySelectorAll('[data-pause-mode]').forEach(function(b){b.addEventListener('click',function(){if(!b.disabled)setMode(b.dataset.pauseMode)})});
document.getElementById('pause-minus').addEventListener('click',function(){hours.value=clampHours()-1;update()});
document.getElementById('pause-plus').addEventListener('click',function(){hours.value=clampHours()+1;update()});hours.addEventListener('input',update);hours.addEventListener('change',update);
document.querySelectorAll('[data-hours]').forEach(function(b){b.addEventListener('click',function(){hours.value=b.dataset.hours;document.querySelectorAll('[data-hours]').forEach(function(x){x.classList.toggle('selected',x===b)});update()})});
document.querySelectorAll('[data-day][data-hour]').forEach(function(b){b.addEventListener('click',function(){var d=localDate(nowEpoch());d.setUTCDate(d.getUTCDate()+Number(b.dataset.day));d.setUTCHours(Number(b.dataset.hour),0,0,0);resume.value=inputFromEpoch(Math.floor(d.getTime()/1000)-offset);document.querySelectorAll('[data-day][data-hour]').forEach(function(x){x.classList.toggle('selected',x===b)});update()})});
resume.addEventListener('input',function(){document.querySelectorAll('[data-day][data-hour]').forEach(function(x){x.classList.remove('selected')});update()});
form.addEventListener('submit',function(e){if(mode==='until'&&epochFromInput(resume.value)<=nowEpoch()){e.preventDefault();alert('恢复时间必须晚于现在。');return}if(mode==='duration')clampHours();if(typeof once==='function'&&!once(form))e.preventDefault()});
if(!trusted)mode='manual';update();
})();</script>)HTML");
    endPage();
}

void IrrigationWeb::run() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_run")) return;
        bool success = false;
        if (actionIs("stop")) {
            success = g_app->stopWatering();
        } else if (actionIs("start_plan")) {
            uint32_t planId = 0;
            success = uintParam("plan_id", 1, kWateringPlanCount, planId) &&
                      g_app->startWateringPlan(static_cast<uint8_t>(planId)) ==
                          WateringStartResult::Started;
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
        redirectResult("/irrigation/run", success);
        return;
    }
    if (!beginPage("浇水", "选择已有计划，或按已启用水路设置时长")) return;
    Esp32BaseWeb::sendChunk(
        "<style>"
        ".run-status{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}"
        ".run-status-card{padding:12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft)}"
        ".run-status-card small{display:block;margin-bottom:3px;color:var(--eb-muted)}"
        ".run-status-card b{display:block;font-size:17px;overflow-wrap:anywhere}"
        ".run-plan{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:14px;align-items:center;padding:11px 0;border-bottom:1px solid var(--eb-line-soft)}"
        ".run-plan:last-child{border-bottom:0}"
        ".run-plan small{display:block;margin-top:3px;color:var(--eb-muted)}"
        ".run-plan form{margin:0}"
        ".run-zone-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}"
        ".run-zone{padding:12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft)}"
        ".run-zone label{margin-bottom:7px}"
        ".run-duration{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center}"
        ".run-duration input{width:100%;max-width:none;min-height:36px;margin:0}"
        ".run-duration span{color:var(--eb-muted);font-weight:650}"
        ".run-zone small{display:block;margin-top:6px;color:var(--eb-muted)}"
        ".run-actions{justify-content:space-between;padding-top:4px}"
        ".run-actions small{color:var(--eb-muted)}"
        "@media(max-width:760px){"
        ".run-status{grid-template-columns:1fr}"
        ".run-plan{grid-template-columns:1fr}"
        ".run-plan form input{width:100%}"
        ".run-zone-grid{grid-template-columns:1fr}"
        ".run-actions{align-items:stretch}"
        ".run-actions input{width:100%}"
        "}"
        "</style>");
    const IrrigationConfig* config = g_app->configuration();
    const WateringStatus status = g_app->wateringStatus();
    Esp32BaseWeb::beginPanel("当前状态");
    const char* activeZoneName = "—";
    if (config && status.activeZoneId >= 1 && status.activeZoneId <= config->zones.size()) {
        activeZoneName = config->zones[status.activeZoneId - 1U].name.data();
    }
    Esp32BaseWeb::sendChunk("<div class='run-status'><div class='run-status-card'><small>运行状态</small><b>");
    Esp32BaseWeb::writeHtmlEscaped(wateringStateName(status.state));
    Esp32BaseWeb::sendChunk("</b></div><div class='run-status-card'><small>当前水路</small><b>");
    Esp32BaseWeb::writeHtmlEscaped(activeZoneName);
    Esp32BaseWeb::sendChunk("</b></div><div class='run-status-card'><small>水流状态</small><b>");
    Esp32BaseWeb::writeHtmlEscaped(status.active ? (status.flowEstablished ? "已检测到水流" : "等待水流") : "—");
    Esp32BaseWeb::sendChunk("</b></div></div>");
    if (status.active) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run' onsubmit=\"return confirm('确认停止当前整次浇水任务？')&&once(this)\"><input type='hidden' name='action' value='stop'><div class='actions'><input class='danger' type='submit' value='停止当前任务'></div></form>");
    } else {
        Esp32BaseWeb::sendChunk("<p class='muted' style='margin-top:10px'>当前没有正在执行的浇水任务。</p>");
    }
    Esp32BaseWeb::endPanel();
    if (config) {
        Esp32BaseWeb::beginPanel("按计划手动浇水");
        bool hasPlan = false;
        for (const WateringPlan& plan : config->plans) {
            if (!plan.configured) continue;
            hasPlan = true;
            uint16_t totalMinutes = 0;
            uint8_t zoneCount = 0;
            for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                if (!config->zones[index].enabled || plan.zoneDurationMinutes[index] == 0) continue;
                totalMinutes = static_cast<uint16_t>(totalMinutes + plan.zoneDurationMinutes[index]);
                ++zoneCount;
            }
            char summary[64];
            std::snprintf(summary, sizeof(summary), "%u 条水路 · 合计 %u 分钟",
                          zoneCount, totalMinutes);
            Esp32BaseWeb::sendChunk("<div class='run-plan'><div><b>");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("</b><small>");
            Esp32BaseWeb::writeHtmlEscaped(summary);
            Esp32BaseWeb::sendChunk("；自动执行开关不影响手动启动</small></div><form method='post' action='/irrigation/run' onsubmit='return once(this)'><input type='hidden' name='action' value='start_plan'><input type='hidden' name='plan_id' value='");
            sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("'><input type='submit' value='按此计划启动'></form></div>");
        }
        if (!hasPlan) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "尚未配置浇水计划", "新增计划后可在这里一键启动整套浇水流程。");
            Esp32BaseWeb::sendChunk("<div class='actions'><a class='btnlink info' href='/irrigation/plans'>前往新增计划</a></div>");
        }
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::beginPanel("按水路手动浇水");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run' onsubmit='return once(this)'><input type='hidden' name='action' value='start_zones'><div class='run-zone-grid'>");
        for (uint8_t index = 0; index < config->zones.size(); ++index) {
            if (!config->zones[index].enabled) continue;
            Esp32BaseWeb::sendChunk("<div class='run-zone'><label>");
            Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
            Esp32BaseWeb::sendChunk("</label><div class='run-duration'><input type='number' min='0' max='120' name='zone"); sendUnsigned(index + 1U);
            Esp32BaseWeb::sendChunk("' value='0' inputmode='numeric'><span>分钟</span></div><small>0 表示本次不浇，范围 0～120 分钟。</small></div>");
        }
        Esp32BaseWeb::sendChunk("</div><div class='actions run-actions'><small>至少为一条水路设置大于 0 的时长。</small><input type='submit' value='开始浇水'></div></form>");
        Esp32BaseWeb::endPanel();
    }
    endPage();
}

void IrrigationWeb::plans() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_plan_save")) return;
        redirectResult("/irrigation/plans", savePlanFromRequest());
        return;
    }
    if (!beginPage("浇水计划", "查看已有计划，点击编辑后再修改详细内容")) return;
    Esp32BaseWeb::sendChunk(
        "<style>"
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
        ".plan-modal{width:calc(100vw - 20px)}"
        ".plan-basic,.plan-times,.plan-zones{grid-template-columns:1fr}"
        ".plan-group{padding:12px}"
        "}"
        "</style>");
    const IrrigationConfig* config = g_app->configuration();
    if (config) {
        int firstAvailable = -1;
        bool anyConfigured = false;
        Esp32BaseWeb::beginPanel("已有计划");
        for (const WateringPlan& plan : config->plans) {
            if (!plan.configured) { if (firstAvailable < 0) firstAvailable = plan.id - 1; continue; }
            anyConfigured = true;
            uint8_t zoneCount = 0, timeCount = 0;
            for (uint8_t i = 0; i < plan.zoneDurationMinutes.size(); ++i)
                if (config->zones[i].enabled && plan.zoneDurationMinutes[i] != 0) ++zoneCount;
            for (uint16_t minute : plan.startMinutes) if (minute != kUnusedStartMinute) ++timeCount;
            char summary[72];
            std::snprintf(summary, sizeof(summary), "%s · %u 条水路 · %u 个时间",
                          plan.scheduleEnabled ? "自动执行" : "仅手动", zoneCount, timeCount);
            Esp32BaseWeb::sendChunk("<div class='urow'><div><b>");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("</b><small>"); Esp32BaseWeb::writeHtmlEscaped(summary);
            Esp32BaseWeb::sendChunk("</small></div><div class='uactions'><span class='uvalue'>计划 "); sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("</span><button type='button' class='btnlink info' onclick=\"document.getElementById('plan-"); sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("').showModal()\">编辑</button></div></div>");
        }
        if (!anyConfigured) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "尚无浇水计划", "新增后可用于手动浇水或定时自动执行。");
        if (firstAvailable >= 0) {
            Esp32BaseWeb::sendChunk("<div class='actions'><button type='button' onclick=\"document.getElementById('plan-"); sendUnsigned(firstAvailable + 1U);
            Esp32BaseWeb::sendChunk("').showModal()\">新增计划</button></div>");
        }
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
            Esp32BaseWeb::sendChunk("> 自动执行</label><small>仅控制定时执行，不影响手动选择该计划。</small></div></div></section><section class='plan-group'><h3>自动执行时间</h3><small>每天最多执行 4 次；留空表示不使用。点击“清除”可删除已经设置的时间。</small><div class='plan-times'>");
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
    if (!beginPage("水路设置", "启用实际安装的水路，并设置名称和基准流量")) return;
    Esp32BaseWeb::sendChunk(
        "<style>"
        ".zone-meter{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:20px;align-items:center;padding:16px;border:1px solid #cfe1e5;border-radius:10px;background:linear-gradient(135deg,#f2f9fa,#fff)}"
        ".zone-meter-label{margin:0 0 5px;color:var(--eb-muted);font-size:13px}"
        ".zone-meter-value{display:flex;align-items:baseline;gap:7px;color:var(--eb-primary);line-height:1}"
        ".zone-meter-number{font-size:32px;font-weight:500;letter-spacing:-.02em}"
        ".zone-meter-unit{font-size:14px;color:var(--eb-muted)}"
        ".zone-meter-help{margin:8px 0 0;color:var(--eb-muted);font-size:13px}"
        ".zone-meter-actions{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:8px}"
        ".zone-table th{font-weight:600}.zone-table td{font-weight:400}"
        ".zone-table .tag{font-weight:500}"
        ".zone-table .btnlink{font-weight:500}"
        "@media(max-width:760px){"
        ".zone-meter{grid-template-columns:1fr;padding:14px;gap:14px}"
        ".zone-meter-actions{justify-content:flex-start}"
        ".zone-meter-number{font-size:28px}"
        "}"
        "</style>");
    const IrrigationConfig* config = g_app->configuration();
    if (config) {
        char coefficient[20]{};
        IrrigationConfigRules::formatPulsesPerLiter(
            config->flowMeter.pulsesPerLiterX100, coefficient, sizeof(coefficient));
        Esp32BaseWeb::beginPanel("流量计参数");
        Esp32BaseWeb::sendChunk("<div class='zone-meter'><div><p class='zone-meter-label'>当前流量系数</p><div class='zone-meter-value'><span class='zone-meter-number'>");
        Esp32BaseWeb::writeHtmlEscaped(coefficient);
        Esp32BaseWeb::sendChunk("</span><span class='zone-meter-unit'>pulse/L</span></div><p class='zone-meter-help'>用于把流量计脉冲换算为实时流量和累计水量。</p></div><div class='zone-meter-actions'><a class='btnlink ok' href='/irrigation/zones/flow-calibration'>流量计校准</a></div></div>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::beginPanel("水路列表");
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part zone-table'><thead><tr><th>水路</th><th>名称</th><th>启用状态</th><th>基准流量</th><th>操作</th></tr></thead><tbody>");
        for (const ZoneConfig& zone : config->zones) {
            char value[20]{};
            if (zone.learnedFlowMlPerMinute != 0) {
                IrrigationConfigRules::formatLitersPerMinute(
                    zone.learnedFlowMlPerMinute, value, sizeof(value));
            }
            Esp32BaseWeb::sendChunk("<tr><td>水路 "); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("</td><td>"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
            Esp32BaseWeb::sendChunk("</td><td><span class='tag ");
            Esp32BaseWeb::sendChunk(zone.enabled ? "ok'>已启用" : "'>未启用");
            Esp32BaseWeb::sendChunk("</span></td><td>");
            if (zone.learnedFlowMlPerMinute == 0) Esp32BaseWeb::sendChunk("<span class='muted'>未设置</span>");
            else { Esp32BaseWeb::writeHtmlEscaped(value); Esp32BaseWeb::sendChunk(" L/min"); }
            Esp32BaseWeb::sendChunk("</td><td><div class='fsactions'><button type='button' class='btnlink info compact' onclick=\"document.getElementById('zone-"); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("').showModal()\">修改</button>");
            if (zone.enabled) { Esp32BaseWeb::sendChunk("<a class='btnlink ok compact' href='/irrigation/zones/learning?zone="); sendUnsigned(zone.id); Esp32BaseWeb::sendChunk("'>学习基准流量</a>"); }
            Esp32BaseWeb::sendChunk("</div></td></tr>");
        }
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
        Esp32BaseWeb::endPanel();
        for (const ZoneConfig& zone : config->zones) {
            char learnedFlow[20]{};
            IrrigationConfigRules::formatLitersPerMinute(
                zone.learnedFlowMlPerMinute, learnedFlow, sizeof(learnedFlow));
            Esp32BaseWeb::sendChunk("<dialog id='zone-"); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("' class='panel eb-modal' data-eb-light-dismiss='1'><h2>修改水路 "); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("</h2><form method='post' action='/irrigation/zones' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='zone_id' value='"); sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><div class='fieldgrid'><p class='field full'><label>水路名称</label><input name='name' maxlength='63' required value='"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
            Esp32BaseWeb::sendChunk("'><small>用于计划、运行和记录页面显示。</small></p><p class='field full'><label>基准流量</label><input type='number' name='learned_flow' min='0' max='100' step='0.001' required value='");
            Esp32BaseWeb::writeHtmlEscaped(learnedFlow);
            Esp32BaseWeb::sendChunk("'><small>单位：L/min，范围 0～100.000；0 表示未设置。</small></p><p class='field full'><label><input type='checkbox' name='enabled' value='1' "); if (zone.enabled) Esp32BaseWeb::sendChunk("checked");
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
        } else if (actionIs("apply")) {
            success = g_app->applyFlowCalibrationResult();
        } else if (actionIs("clear")) {
            g_app->resetFlowCalibration();
            success = !g_app->wateringStatus().active;
        }
        redirectResult("/irrigation/zones/flow-calibration", success);
        return;
    }
    if (!beginPage("流量计校准", "通过不同接水量消除启动阶段影响，计算稳态脉冲/L")) return;
    Esp32BaseWeb::sendChunk(
        "<style>"
        ".cal-current{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:16px;align-items:center;padding:16px;border:1px solid #cfe1e5;border-radius:10px;background:linear-gradient(135deg,#f2f9fa,#fff)}"
        ".cal-current-label{margin:0 0 5px;color:var(--eb-muted);font-size:13px}"
        ".cal-current-value{display:flex;align-items:baseline;gap:7px;color:var(--eb-primary);line-height:1}"
        ".cal-current-number{font-size:32px;font-weight:500;letter-spacing:-.02em}"
        ".cal-current-unit{font-size:14px;color:var(--eb-muted)}"
        ".cal-current-note{max-width:360px;color:var(--eb-muted);font-size:13px;line-height:1.6}"
        ".cal-steps{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin-bottom:16px}"
        ".cal-step{display:grid;grid-template-columns:28px minmax(0,1fr);gap:9px;align-items:start;padding:12px;border:1px solid var(--eb-line-soft);border-radius:9px;background:var(--eb-soft);font-size:13px;line-height:1.55}"
        ".cal-step-num{display:flex;align-items:center;justify-content:center;width:26px;height:26px;border-radius:50%;background:var(--eb-primary-soft);color:var(--eb-primary);font-weight:600}"
        ".cal-action{padding:14px;border:1px solid var(--eb-line-soft);border-radius:9px;background:#fff}"
        ".cal-action .fieldgrid{margin:0}.cal-action .field{margin:0}"
        ".cal-action label{font-weight:500}"
        ".cal-action .actions{margin-bottom:0}"
        ".cal-live .metric b{font-weight:500}"
        ".cal-samples th{font-weight:600}.cal-samples td{font-weight:400}"
        ".cal-empty{padding:20px;text-align:center;border:1px dashed #c9d6dc;border-radius:9px;background:var(--eb-soft)}"
        ".cal-empty-title{margin:0;color:#344054;font-size:15px;font-weight:500}"
        ".cal-empty-text{margin:5px 0 0;color:var(--eb-muted);font-size:13px}"
        ".cal-result{margin-top:14px;padding:15px;border:1px solid #bfe0d4;border-radius:10px;background:var(--eb-ok-soft)}"
        ".cal-result-label{margin:0 0 5px;color:var(--eb-ok);font-size:13px}"
        ".cal-result-value{font-size:28px;font-weight:500;color:var(--eb-ok);line-height:1.15}"
        ".cal-result-detail{margin:7px 0 0;color:#526071;font-size:13px}"
        "@media(max-width:760px){"
        ".cal-current,.cal-steps{grid-template-columns:1fr}"
        ".cal-current{padding:14px}.cal-current-number{font-size:28px}"
        "}"
        "</style>");
    const IrrigationConfig* config = g_app->configuration();
    if (!config) { endPage(); return; }
    const WateringStatus status = g_app->wateringStatus();
    const FlowCalibrationService& calibration = g_app->flowCalibration();
    const bool active = status.active && status.purpose == WateringPurpose::FlowCalibration;
    char coefficient[20]{};
    IrrigationConfigRules::formatPulsesPerLiter(
        config->flowMeter.pulsesPerLiterX100, coefficient, sizeof(coefficient));
    Esp32BaseWeb::beginPanel("当前流量系数");
    Esp32BaseWeb::sendChunk("<div class='cal-current'><div><p class='cal-current-label'>设备当前使用</p><div class='cal-current-value'><span class='cal-current-number'>");
    Esp32BaseWeb::writeHtmlEscaped(coefficient);
    Esp32BaseWeb::sendChunk("</span><span class='cal-current-unit'>pulse/L</span></div></div><div class='cal-current-note'>校准完成前不会修改这个参数。只有确认使用校准结果后，系统才保存新的流量系数。</div></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::beginPanel("开始校准");
    Esp32BaseWeb::sendChunk("<div class='cal-steps'><div class='cal-step'><span class='cal-step-num'>1</span><span>选择一条已启用水路，每次都从完全停止状态重新开始接水。</span></div><div class='cal-step'><span class='cal-step-num'>2</span><span>至少采集两次不同水量；建议相差 500 mL 以上，达到 1 L 更可靠。</span></div><div class='cal-step'><span class='cal-step-num'>3</span><span>查看拟合质量，确认采用后才保存；全部样本只保存在内存。</span></div></div><div class='cal-action'>");
    if (status.active && !active) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "设备正在执行其它任务", "请等待当前任务结束后再校准。");
    } else if (active) {
        Esp32BaseWeb::sendChunk("<div id='cal-live' class='cal-live' data-was-active='1'><div class='metrics'><div class='metric'><b id='cal-elapsed'>"); sendUnsigned(status.elapsedSec); Esp32BaseWeb::sendChunk(" s</b><span>已运行</span></div><div class='metric'><b id='cal-pulses'>"); sendUnsigned(status.pulseCount); Esp32BaseWeb::sendChunk("</b><span>累计脉冲</span></div></div><form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='stop'><div class='actions'><input class='danger' type='submit' value='停止本次采样'></div></form></div>");
    } else if (calibration.hasPendingMeasurement()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "请输入本次实测水量", "按量杯读数填写，至少 1000 mL。");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/flow-calibration' onsubmit='return once(this)'><input type='hidden' name='action' value='measurement'><div class='fieldgrid'><p class='field med'><label>实测水量</label><input type='number' name='measured_ml' min='1000' max='1000000' step='1' required><small>单位：mL，例如 1.5 L 填写 1500；范围 1000～1000000 mL。</small></p></div><div class='actions'><input type='submit' value='保存样本'></div></form>");
    } else if (calibration.sampleCount() < FlowCalibrationService::kMaximumSamples) {
        bool hasEligibleZone = false;
        for (const ZoneConfig& zone : config->zones) {
            hasEligibleZone = hasEligibleZone ||
                              (zone.enabled &&
                               (calibration.zoneId() == 0 ||
                                calibration.zoneId() == zone.id));
        }
        if (hasEligibleZone) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认开始流量计校准？开始后所选水路会立即出水。')&&once(this)\"><input type='hidden' name='action' value='start'><div class='fieldgrid'><p class='field med'><label>校准水路</label><select name='zone_id'>");
            for (const ZoneConfig& zone : config->zones) if (zone.enabled && (calibration.zoneId() == 0 || calibration.zoneId() == zone.id)) { Esp32BaseWeb::sendChunk("<option value='"); sendUnsigned(zone.id); Esp32BaseWeb::sendChunk("'>"); Esp32BaseWeb::writeHtmlEscaped(zone.name.data()); Esp32BaseWeb::sendChunk("</option>"); }
            Esp32BaseWeb::sendChunk("</select><small>一次校准会话固定使用同一条水路，单次最长 10 分钟。</small></p></div><div class='actions'><input type='submit' value='开始新样本'></div></form>");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN,
                                     "没有可用于校准的水路",
                                     calibration.zoneId() == 0
                                         ? "请先启用至少一条水路。"
                                         : "本次会话使用的水路已停用；请清空会话或重新启用该水路。");
        }
    } else {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "已达到 5 个样本", "可以应用结果，或清空后重新校准。");
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("校准样本");
    if (calibration.sampleCount() == 0) {
        Esp32BaseWeb::sendChunk("<div class='cal-empty'><p class='cal-empty-title'>尚无校准样本</p><p class='cal-empty-text'>完成第一次接水并填写实测水量后，样本会显示在这里。</p></div>");
    } else {
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part cal-samples'><thead><tr><th>样本</th><th>实测水量</th><th>完整脉冲数</th></tr></thead><tbody>");
        for (uint8_t index = 0; index < calibration.sampleCount(); ++index) {
            const FlowCalibrationService::Sample* sample = calibration.sample(index);
            Esp32BaseWeb::sendChunk("<tr><td>"); sendUnsigned(index + 1U); Esp32BaseWeb::sendChunk("</td><td>"); sendUnsigned(sample->measuredWaterMl); Esp32BaseWeb::sendChunk(" mL</td><td>"); sendUnsigned(sample->pulseCount); Esp32BaseWeb::sendChunk("</td></tr>");
        }
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
        if (!calibration.resultReady()) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "暂不能生成稳态系数", "请增加不同水量的有效样本。");
        } else {
            char result[20]{};
            IrrigationConfigRules::formatPulsesPerLiter(
                calibration.combinedPulsesPerLiterX100(), result, sizeof(result));
            char detail[96]{};
            std::snprintf(detail, sizeof(detail), "水量跨度 %lu mL · 最大拟合残差 %u.%02u%%",
                          static_cast<unsigned long>(calibration.volumeSpanMl()),
                          calibration.maximumResidualPercentX100() / 100U,
                          calibration.maximumResidualPercentX100() % 100U);
            Esp32BaseWeb::sendChunk("<div class='cal-result'><p class='cal-result-label'>拟合得到的稳态流量系数</p><div class='cal-result-value'>");
            Esp32BaseWeb::writeHtmlEscaped(result);
            Esp32BaseWeb::sendChunk(" <span class='cal-current-unit'>pulse/L</span></div><p class='cal-result-detail'>");
            Esp32BaseWeb::writeHtmlEscaped(detail);
            Esp32BaseWeb::sendChunk("</p></div>");
            const uint8_t flags = calibration.qualityFlags();
            if (calibration.volumeSpanMl() >= 1000U) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "水量跨度达到 1 L", "这组样本对量杯读数误差更不敏感。");
            if (flags & FlowCalibrationService::kQualityOnlyTwoSamples) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "当前只有两个样本", "结果可用，建议增加第三个样本检查一致性。");
            if (flags & FlowCalibrationService::kQualitySmallVolumeSpan) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "水量差小于 500 mL", "量杯误差容易被放大，建议增加差距更大的样本。");
            if (flags & FlowCalibrationService::kQualityNonMonotonic) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "样本趋势不完全一致", "建议检查接水读数或补充样本。");
            if (flags & FlowCalibrationService::kQualityResidualHigh) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "拟合残差较大", "建议重新采样后再应用。");
            if (config->flowMeter.pulsesPerLiterX100 == calibration.combinedPulsesPerLiterX100()) {
                Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "当前已使用此结果");
            } else {
                Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认使用这组稳态流量系数？')&&once(this)\"><input type='hidden' name='action' value='apply'><div class='actions'><input type='submit' value='使用校准结果'></div></form>");
            }
        }
        if (!active) Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/flow-calibration' onsubmit=\"return confirm('确认清空本次内存样本？')&&once(this)\"><input type='hidden' name='action' value='clear'><div class='actions'><input class='danger' type='submit' value='清空本次校准'></div></form>");
    }
    Esp32BaseWeb::endPanel();
    if (active) Esp32BaseWeb::sendChunk("<script>(function(){function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active||s.purpose!==1){location.reload();return;}var e=document.getElementById('cal-elapsed'),p=document.getElementById('cal-pulses');if(e)e.textContent=s.elapsedSec+' s';if(p)p.textContent=s.pulseCount;setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}setTimeout(poll,1000)})();</script>");
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
    char learned[20]{};
    IrrigationConfigRules::formatLitersPerMinute(zone.learnedFlowMlPerMinute, learned, sizeof(learned));
    char learnedWithUnit[32]{};
    std::snprintf(learnedWithUnit, sizeof(learnedWithUnit), "%s L/min", learned);
    Esp32BaseWeb::beginPanel("学习水路");
    Esp32BaseWeb::sendInfoRowCompact("水路名称", "", zone.name.data());
    Esp32BaseWeb::sendInfoRowCompact("当前基准流量", zone.learnedFlowMlPerMinute == 0 ? "尚未设置" : "", zone.learnedFlowMlPerMinute == 0 ? "未设置" : learnedWithUnit);
    Esp32BaseWeb::sendInfoRowCompact("安全上限", "达到上限仍不稳定时自动停止", "10 分钟");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::beginPanel("学习状态");
    if (!zone.enabled) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "这条水路尚未启用", "请先返回水路设置启用。");
    } else if (status.active && !active) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "设备正在执行其它任务");
    } else if (active) {
        Esp32BaseWeb::sendChunk("<div class='metrics'><div class='metric'><b id='learn-elapsed'>"); sendUnsigned(status.elapsedSec); Esp32BaseWeb::sendChunk(" s</b><span>已运行</span></div><div class='metric'><b id='learn-current'>"); sendUnsigned(status.currentFlowMlPerMinute); Esp32BaseWeb::sendChunk(" mL/min</b><span>最近 5 秒流量</span></div><div class='metric'><b id='learn-average'>"); sendUnsigned(status.learningAverageMlPerMinute); Esp32BaseWeb::sendChunk(" mL/min</b><span>窗口平均值</span></div><div class='metric'><b id='learn-range'>"); sendUnsigned(status.learningMinimumMlPerMinute); Esp32BaseWeb::sendChunk("–"); sendUnsigned(status.learningMaximumMlPerMinute); Esp32BaseWeb::sendChunk("</b><span>窗口范围</span></div></div><p class='muted'>有效窗口：<b id='learn-count'>"); sendUnsigned(status.learningSampleCount); Esp32BaseWeb::sendChunk("/5</b></p><form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='stop'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='停止学习'></div></form>");
    } else if (g_app->pendingLearnedZoneId() == zoneId) {
        char suggestion[20]{};
        const bool validSuggestion = IrrigationConfigRules::formatLitersPerMinute(
            g_app->pendingLearnedFlowMlPerMinute(), suggestion, sizeof(suggestion));
        if (validSuggestion) {
            char suggestionWithUnit[32]{};
            std::snprintf(suggestionWithUnit, sizeof(suggestionWithUnit), "%s L/min", suggestion);
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "学习完成", suggestionWithUnit);
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision); Esp32BaseWeb::sendChunk("'><div class='actions'><input type='submit' value='保存为基准流量'></div></form>");
        } else {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                     "学习结果超出有效范围",
                                     "基准流量必须在 0～100.000 L/min，请检查流量系数后重新学习。");
        }
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones/learning' onsubmit='return once(this)'><input type='hidden' name='action' value='discard'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><div class='actions'><input class='danger' type='submit' value='放弃结果'></div></form>");
    } else if (g_app->pendingLearnedZoneId() != 0) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN,
                                 "另一条水路有待确认的学习结果",
                                 "请先返回对应水路保存或放弃该结果。");
    } else {
        if (status.purpose == WateringPurpose::ZoneFlowLearning && status.lastZoneId == zoneId && status.lastStopReason == WateringStopReason::LearningTimeout) Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "学习超时", "10 分钟内没有取得稳定流量，请检查供水和管路。");
        Esp32BaseWeb::sendChunk("<p class='muted'>系统每 5 秒计算一次流量，最近 5 个窗口波动不超过平均值的 10%时自动完成。</p><form method='post' action='/irrigation/zones/learning' onsubmit=\"return confirm('确认开始学习基准流量？开始后这条水路会立即出水。')&&once(this)\"><input type='hidden' name='action' value='start'><input type='hidden' name='zone_id' value='"); sendUnsigned(zoneId); Esp32BaseWeb::sendChunk("'><div class='actions'><input type='submit' value='开始学习'></div></form>");
    }
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendChunk("<p><a class='btnlink secondary' href='/irrigation/zones'>返回水路设置</a></p>");
    if (active) Esp32BaseWeb::sendChunk("<script>(function(){function set(id,v){var e=document.getElementById(id);if(e)e.textContent=v}function poll(){fetch('/irrigation/api/status',{cache:'no-store',credentials:'same-origin'}).then(function(r){return r.json()}).then(function(s){if(!s.active||s.purpose!==2){location.reload();return;}set('learn-elapsed',s.elapsedSec+' s');set('learn-current',s.currentFlowMlPerMinute+' mL/min');set('learn-average',s.learningAverageMlPerMinute+' mL/min');set('learn-range',s.learningMinimumMlPerMinute+'–'+s.learningMaximumMlPerMinute);set('learn-count',s.learningSampleCount+'/5');setTimeout(poll,1000)}).catch(function(){setTimeout(poll,2000)})}setTimeout(poll,1000)})();</script>");
    endPage();
}

void IrrigationWeb::records() {
    if (!beginPage("浇水记录", "最新记录优先")) return;
    Esp32BaseWeb::sendChunk(
        R"HTML(<style>
.record-toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 0 10px}.record-toolbar p{margin:0;color:var(--eb-muted);font-size:13px}.record-table{width:100%;min-width:800px;border-collapse:collapse;font-size:13px}.record-table th,.record-table td{padding:9px 8px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:middle}.record-table th{color:var(--eb-muted);font-weight:650;white-space:nowrap}.record-table tbody tr:last-child td{border-bottom:0}.record-table tbody tr:hover{background:var(--eb-soft)}.record-id,.record-number{white-space:nowrap;font-variant-numeric:tabular-nums}.record-time{min-width:12em;white-space:nowrap}.record-action{width:1%;white-space:nowrap}.record-empty{padding:22px 16px;border:1px dashed var(--eb-line);border-radius:8px;background:var(--eb-soft);text-align:center}.record-empty b{display:block;margin-bottom:4px;font-size:15px}.record-empty span{color:var(--eb-muted)}
.record-detail-dialog{width:min(920px,calc(100vw - 28px))}.record-detail-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:12px}.record-detail-head h2{margin:1px 0 0;font-size:20px}.record-detail-head>div>span{font-size:12px}.record-detail-close{min-height:30px}.record-detail-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px 12px;padding:12px;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft)}.record-detail-grid>div{min-width:0}.record-detail-grid b{display:block;color:var(--eb-muted);font-size:11px;font-weight:650}.record-detail-grid span{display:block;margin-top:2px;overflow-wrap:anywhere}.record-detail-grid .tag{display:inline-flex}.record-detail-section{margin-top:16px}.record-detail-section>h3{margin-bottom:2px}.record-detail-section>p{margin-bottom:8px;color:var(--eb-muted);font-size:12px}.record-zone-table{width:100%;min-width:820px;border-collapse:collapse;font-size:12px}.record-zone-table th,.record-zone-table td{padding:7px 6px;border-bottom:1px solid var(--eb-line);text-align:left;vertical-align:top}.record-zone-table th{color:var(--eb-muted);font-weight:650;white-space:nowrap}.record-zone-table tr:last-child td{border-bottom:0}.record-zone-table td>b,.record-zone-table td>small{display:block}.record-zone-table td>small{color:var(--eb-muted)}.record-flags{display:flex;flex-wrap:wrap;gap:4px;min-width:9em}
@media(max-width:760px){.record-toolbar{align-items:flex-start}.record-toolbar p{display:none}.record-table{display:block;min-width:0}.record-table thead{display:none}.record-table tbody{display:grid;gap:9px}.record-table tr{display:grid;padding:11px;border:1px solid var(--eb-line);border-radius:8px;background:#fff}.record-table tbody tr:hover{background:#fff}.record-table td{display:grid;grid-template-columns:7.2em minmax(0,1fr);gap:8px;padding:4px 0;border:0;white-space:normal}.record-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:12px;font-weight:650}.record-table .record-action{display:flex;justify-content:flex-end;width:auto;padding-top:9px;border-top:1px solid var(--eb-line-soft);margin-top:5px}.record-table .record-action::before{display:none}.record-table .record-action>.btnlink{width:100%;min-height:34px}.record-detail-dialog{width:calc(100vw - 20px);padding:12px!important}.record-detail-grid{grid-template-columns:repeat(2,minmax(0,1fr));padding:10px}.record-zone-table{display:block;min-width:0}.record-zone-table thead{display:none}.record-zone-table tbody{display:grid;gap:8px}.record-zone-table tr{display:grid;padding:9px;border:1px solid var(--eb-line);border-radius:7px}.record-zone-table td{display:grid;grid-template-columns:7.5em minmax(0,1fr);gap:7px;padding:3px 0;border:0}.record-zone-table td::before{content:attr(data-label);color:var(--eb-muted);font-size:11px;font-weight:650}.record-zone-table td>b,.record-zone-table td>small{display:inline}.record-zone-table td>small{margin-left:4px}.record-flags{min-width:0}.record-detail-dialog>.actions button{width:100%}}
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
    } else if (status.recordCount == 0) {
        Esp32BaseWeb::sendChunk("<div class='record-empty'><b>还没有浇水记录</b><span>完成第一次实际出水后，这里会显示执行结果和各水路明细。</span></div>");
    } else {
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='record-table'><thead><tr><th>编号</th><th>完成时间</th><th>来源</th><th>结果</th><th>实际浇水时间</th><th>历史估算水量</th><th>操作</th></tr></thead><tbody>");
        RecordRowsContext context{g_app->configuration(), 0};
        const bool readOk = g_app->readLatestWateringRecords(
            (page - 1U) * perPage, perPage, sendRecordRow, &context);
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
        if (!readOk) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER,
                                     "本页记录读取失败",
                                     "请刷新页面；如果问题持续，请检查记录存储状态。");
        } else if (context.emitted == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO,
                                     "本页没有记录",
                                     "页码已经调整，请刷新后重试。");
        }
        Esp32BaseWeb::Pagination pagination{};
        pagination.path = "/irrigation/records";
        pagination.query = "";
        pagination.page = page;
        pagination.perPage = perPage;
        pagination.total = status.recordCount;
        Esp32BaseWeb::sendPagination(pagination);
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

void IrrigationWeb::settings() {
    if (!Esp32BaseWeb::checkAuth()) return;
    Esp32BaseWeb::redirectSeeOther("/esp32base/app-config");
}

void IrrigationWeb::statusApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const WateringStatus status = g_app->wateringStatus();
    char json[768];
    std::snprintf(json,
                  sizeof(json),
                  "{\"ready\":%s,\"active\":%s,\"state\":%u,\"zoneId\":%u,\"lastZoneId\":%u,\"flowEstablished\":%s,\"purpose\":%u,\"elapsedSec\":%lu,\"pulseCount\":%lu,\"currentFlowMlPerMinute\":%lu,\"learningAverageMlPerMinute\":%lu,\"learningMinimumMlPerMinute\":%lu,\"learningMaximumMlPerMinute\":%lu,\"learningSampleCount\":%u,\"automaticMode\":%u,\"unexpectedFlowAlarm\":%s,\"recordStorageFault\":%s,\"eventStorageFault\":%s,\"schedulerStorageFault\":%s,\"checkpointStorageFault\":%s}",
                  g_app->businessReady() ? "true" : "false",
                  status.active ? "true" : "false",
                  static_cast<unsigned>(status.state),
                  status.activeZoneId,
                  status.lastZoneId,
                  status.flowEstablished ? "true" : "false",
                  static_cast<unsigned>(status.purpose),
                  static_cast<unsigned long>(status.elapsedSec),
                  static_cast<unsigned long>(status.pulseCount),
                  static_cast<unsigned long>(status.currentFlowMlPerMinute),
                  static_cast<unsigned long>(status.learningAverageMlPerMinute),
                  static_cast<unsigned long>(status.learningMinimumMlPerMinute),
                  static_cast<unsigned long>(status.learningMaximumMlPerMinute),
                  status.learningSampleCount,
                  static_cast<unsigned>(g_app->automaticWateringState().mode),
                  g_app->unexpectedFlowAlarm() ? "true" : "false",
                  g_app->recordStorageFault() ? "true" : "false",
                  g_app->eventStorageFault() ? "true" : "false",
                  g_app->schedulerStorageFault() ? "true" : "false",
                  g_app->checkpointStorageFault() ? "true" : "false");
    Esp32BaseWeb::sendJson(200, json);
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
