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

bool parseLocalDateTime(const char* text, uint32_t& epochSec) {
    if (!text || std::strlen(text) != 16 || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':') {
        return false;
    }
    const uint8_t digitIndexes[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15};
    for (const uint8_t index : digitIndexes) {
        if (text[index] < '0' || text[index] > '9') return false;
    }
    const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                     (text[2] - '0') * 10 + text[3] - '0';
    const unsigned month = static_cast<unsigned>((text[5] - '0') * 10 + text[6] - '0');
    const unsigned day = static_cast<unsigned>((text[8] - '0') * 10 + text[9] - '0');
    const unsigned hour = static_cast<unsigned>((text[11] - '0') * 10 + text[12] - '0');
    const unsigned minute = static_cast<unsigned>((text[14] - '0') * 10 + text[15] - '0');
    if (year < 2026 || year > 2099 || month < 1 || month > 12 || hour > 23 || minute > 59) {
        return false;
    }
    static constexpr uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const unsigned maximumDay = month == 2 && leap ? 29U : daysPerMonth[month - 1U];
    if (day < 1 || day > maximumDay) return false;
    int adjustedYear = year - (month <= 2 ? 1 : 0);
    const int era = adjustedYear / 400;
    const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3U : month + 9U;
    const unsigned dayOfYear = (153U * adjustedMonth + 2U) / 5U + day - 1U;
    const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    const int64_t daysSinceEpoch = static_cast<int64_t>(era) * 146097LL + dayOfEra - 719468LL;
    const int64_t localEpoch = daysSinceEpoch * 86400LL + hour * 3600LL + minute * 60LL;
    const int64_t utcEpoch = localEpoch - 8LL * 3600LL;
    if (utcEpoch <= 0 || utcEpoch > UINT32_MAX) return false;
    epochSec = static_cast<uint32_t>(utcEpoch);
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
    if (!current || !uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId) ||
        !uintParam("revision", 1, UINT32_MAX, revision) ||
        !getParam("name", name, sizeof(name))) {
        return false;
    }
    IrrigationConfig next = *current;
    ZoneConfig& zone = next.zones[zoneId - 1U];
    zone.enabled = Esp32BaseWeb::hasParam("enabled");
    std::snprintf(zone.name.data(), zone.name.size(), "%s", name);
    return g_app->saveConfiguration(next, revision);
}

bool saveSettingsFromRequest() {
    const IrrigationConfig* current = g_app->configuration();
    uint32_t revision = 0;
    char coefficientText[24]{};
    if (!current || !uintParam("revision", 1, UINT32_MAX, revision) ||
        !getParam("flow_coefficient", coefficientText, sizeof(coefficientText))) {
        return false;
    }
    IrrigationConfig next = *current;
    uint32_t value = 0;
#define READ_FIELD(name, minimum, maximum, target) \
    do { \
        if (!uintParam(name, minimum, maximum, value)) return false; \
        target = static_cast<decltype(target)>(value); \
    } while (false)
    READ_FIELD("pull_in_ms", 100, 10000, next.valveDrive.pullInTimeMs);
    READ_FIELD("pwm_hz", 1000, 25000, next.valveDrive.pwmFrequencyHz);
    READ_FIELD("hold_duty", 1, 100, next.valveDrive.holdDutyPercent);
    next.pump.enabled = Esp32BaseWeb::hasParam("pump_enabled");
    READ_FIELD("pump_start_ms", 0, 60000, next.pump.startDelayMs);
    READ_FIELD("pump_stop_ms", 0, 10000, next.pump.stopToValveCloseDelayMs);
    if (!IrrigationConfigRules::parsePulsesPerLiter(
            coefficientText, next.flowMeter.pulsesPerLiterX100)) return false;
    READ_FIELD("flow_start_sec", 1, 120, next.flowProtection.flowStartTimeoutSec);
    READ_FIELD("no_flow_sec", 1, 60, next.flowProtection.noFlowTimeoutSec);
    READ_FIELD("unexpected_delay_sec", 0, 300, next.flowProtection.unexpectedFlowDelaySec);
    READ_FIELD("unexpected_window_sec", 1, 300, next.flowProtection.unexpectedFlowWindowSec);
    READ_FIELD("unexpected_pulses", 1, 65535, next.flowProtection.unexpectedFlowPulseCount);
    READ_FIELD("deviation_confirm_sec", 1, 300, next.flowProtection.flowDeviationConfirmSec);
    READ_FIELD("low_percent", 1, 99, next.flowProtection.lowFlowPercent);
    READ_FIELD("high_percent", 101, 1000, next.flowProtection.highFlowPercent);
    READ_FIELD("rollback_minutes", 1, 60, next.timeSafety.rtcRollbackThresholdMinutes);
    READ_FIELD("checkpoint_hours", 0, 168, next.timeSafety.aliveCheckpointHours);
    uint32_t action = 0;
    if (!uintParam("low_action", 0, 1, action)) return false;
    next.flowProtection.lowFlowAction = static_cast<FlowAlertAction>(action);
    if (!uintParam("high_action", 0, 1, action)) return false;
    next.flowProtection.highFlowAction = static_cast<FlowAlertAction>(action);
#undef READ_FIELD
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

const char* automaticModeName(AutomaticWateringMode mode) {
    switch (mode) {
        case AutomaticWateringMode::Enabled: return "正常运行";
        case AutomaticWateringMode::PausedIndefinitely: return "暂停至手动恢复";
        case AutomaticWateringMode::PausedUntil: return "定时暂停";
    }
    return "未知";
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

struct RecordRowsContext {
    const IrrigationConfig* config;
};

void sendRecordRow(const StoredWateringRecord& record, void* user) {
    const RecordRowsContext* context = static_cast<const RecordRowsContext*>(user);
    const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(record.payload);
    Esp32BaseWeb::sendChunk("<tr><td><a href='/irrigation/records?id=");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("'>");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("</a></td><td>");
    sendRecordTime(record.timing);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::sendChunk(sourceName(record.payload.source));
    if (record.payload.planId != 0 && context && context->config) {
        Esp32BaseWeb::sendChunk(" · ");
        const WateringPlan& plan = context->config->plans[record.payload.planId - 1U];
        if (plan.configured) {
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
        } else {
            Esp32BaseWeb::sendChunk("计划 ");
            sendUnsigned(record.payload.planId);
            Esp32BaseWeb::sendChunk("（已删除）");
        }
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::sendChunk(resultName(record.payload.result));
    Esp32BaseWeb::sendChunk("</td><td>");
    sendUnsigned(totals.actualWateringSec);
    Esp32BaseWeb::sendChunk(" 秒</td><td>");
    sendUnsigned(static_cast<uint32_t>(totals.estimatedWaterMl / 1000ULL));
    Esp32BaseWeb::sendChunk(" L</td></tr>");
}

void sendRecordDetail(uint32_t id) {
    StoredWateringRecord record{};
    if (g_app->readWateringRecordById(id, record) !=
        Esp32BaseRecordStore::RecordReadResult::Found) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "找不到该浇水记录");
        return;
    }
    const WateringRecordTotals totals = WateringRecordCodec::calculateTotals(record.payload);
    Esp32BaseWeb::beginPanel("记录详情");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='kv'><tr><th>编号</th><td>");
    sendUnsigned(record.recordId);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>完成时间</th><td>");
    sendRecordTime(record.timing);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>来源</th><td>");
    Esp32BaseWeb::sendChunk(sourceName(record.payload.source));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>结果</th><td>");
    Esp32BaseWeb::sendChunk(resultName(record.payload.result));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>整次历时</th><td>");
    sendUnsigned(record.timing.durationSec);
    Esp32BaseWeb::sendChunk(" 秒</td></tr><tr><th>总脉冲</th><td>");
    char totalText[32];
    std::snprintf(totalText, sizeof(totalText), "%llu",
                  static_cast<unsigned long long>(totals.pulseCount));
    Esp32BaseWeb::sendChunk(totalText);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>历史估算总水量</th><td>");
    std::snprintf(totalText, sizeof(totalText), "%llu mL",
                  static_cast<unsigned long long>(totals.estimatedWaterMl));
    Esp32BaseWeb::sendChunk(totalText);
    Esp32BaseWeb::sendChunk("</td></tr></table></div><h3>区域明细</h3><div class='tablewrap'><table><tr><th>区域</th><th>结果</th><th>计划秒</th><th>实际秒</th><th>脉冲</th><th>估算 mL</th></tr>");
    for (uint8_t index = 0; index < record.payload.zones.size(); ++index) {
        const ZoneWateringRecord& zone = record.payload.zones[index];
        if (zone.plannedDurationSec == 0) {
            continue;
        }
        Esp32BaseWeb::sendChunk("<tr><td>");
        sendUnsigned(index + 1U);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(zone.result == ZoneWateringResult::Completed ? "完成" :
                                zone.result == ZoneWateringResult::Stopped ? "停止" :
                                zone.result == ZoneWateringResult::Failed ? "失败" : "未开始");
        Esp32BaseWeb::sendChunk("</td><td>"); sendUnsigned(zone.plannedDurationSec);
        Esp32BaseWeb::sendChunk("</td><td>"); sendUnsigned(zone.actualWateringSec);
        Esp32BaseWeb::sendChunk("</td><td>"); sendUnsigned(zone.pulseCount);
        Esp32BaseWeb::sendChunk("</td><td>"); sendUnsigned(zone.estimatedWaterMl);
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table></div>");
    Esp32BaseWeb::endPanel();
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
    Esp32BaseWeb::setDeviceName("智能浇水控制器");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP);
    Esp32BaseWeb::setHomePath("/irrigation");
    return Esp32BaseWeb::addPage("/irrigation", "总览", overview) &&
           Esp32BaseWeb::addPage("/irrigation/run", "运行", run) &&
           Esp32BaseWeb::addPage("/irrigation/plans", "计划", plans) &&
           Esp32BaseWeb::addPage("/irrigation/zones", "区域", zones) &&
           Esp32BaseWeb::addPage("/irrigation/records", "浇水记录", records) &&
           Esp32BaseWeb::addPage("/irrigation/settings", "浇水设置", settings) &&
           Esp32BaseWeb::addRoute("/irrigation", Esp32BaseWeb::METHOD_POST, overview) &&
           Esp32BaseWeb::addRoute("/irrigation/run", Esp32BaseWeb::METHOD_POST, run) &&
           Esp32BaseWeb::addRoute("/irrigation/plans", Esp32BaseWeb::METHOD_POST, plans) &&
           Esp32BaseWeb::addRoute("/irrigation/zones", Esp32BaseWeb::METHOD_POST, zones) &&
           Esp32BaseWeb::addRoute("/irrigation/records", Esp32BaseWeb::METHOD_POST, records) &&
           Esp32BaseWeb::addRoute("/irrigation/settings", Esp32BaseWeb::METHOD_POST, settings) &&
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
            const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
            success = uintParam("hours", 1, 8760, hours) && now.synced &&
                      g_app->pauseAutomaticWateringUntil(now.epochSec + hours * 3600U);
        } else if (actionIs("pause_until")) {
            char localDateTime[20]{};
            uint32_t resumeAt = 0;
            success = getParam("resume_at", localDateTime, sizeof(localDateTime)) &&
                      parseLocalDateTime(localDateTime, resumeAt) &&
                      g_app->pauseAutomaticWateringUntil(resumeAt);
        }
        redirectResult("/irrigation", success);
        return;
    }
    if (!beginPage("总览", "设备、自动浇水和存储状态")) return;
    const WateringStatus watering = g_app->wateringStatus();
    const AutomaticWateringState automatic = g_app->automaticWateringState();
    Esp32BaseRecordStore::StoreStatus recordsStatus{};
    g_app->readWateringRecordStoreStatus(recordsStatus);
    char value[48];
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("运行状态", wateringStateName(watering.state));
    Esp32BaseWeb::sendMetric("自动浇水", automaticModeName(automatic.mode));
    std::snprintf(value, sizeof(value), "%lu / %lu",
                  static_cast<unsigned long>(recordsStatus.recordCount),
                  static_cast<unsigned long>(recordsStatus.capacity));
    Esp32BaseWeb::sendMetric("浇水记录", value);
    Esp32BaseWeb::sendMetric("非计划流量", g_app->unexpectedFlowAlarm() ? "报警" : "正常");
    if (g_app->lastKnownAliveEpoch() != 0 &&
        Esp32BaseTime::formatEpoch(g_app->lastKnownAliveEpoch(), value, sizeof(value),
                                   "%Y-%m-%d %H:%M")) {
        Esp32BaseWeb::sendMetric("上次在线检查点", value, "用于估算可能断电范围，不是精确断电时间");
    } else {
        Esp32BaseWeb::sendMetric("上次在线检查点", "尚无", "达到配置间隔且没有其它业务活动后才写入");
    }
    Esp32BaseWeb::endMetricGrid();
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
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
    if (g_app->recordStorageFault() || g_app->eventStorageFault() ||
        g_app->schedulerStorageFault() || g_app->checkpointStorageFault()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "持久存储存在故障");
    }
    Esp32BaseWeb::beginPanel("自动浇水总控");
    Esp32BaseWeb::sendInfoRowCompactForm("暂停自动浇水", "保持暂停，直到手动恢复", automaticModeName(automatic.mode),
                                         "/irrigation", "无限期暂停", "action", "pause_indefinitely", Esp32BaseWeb::UI_WARN);
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation' onsubmit='return once(this)'><input type='hidden' name='action' value='pause_hours'><label>暂停小时数 <input type='number' name='hours' min='1' max='8760' value='24' required></label> <input type='submit' value='定时暂停'></form>");
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation' onsubmit='return once(this)'><input type='hidden' name='action' value='pause_until'><label>暂停到（UTC+8）<input type='datetime-local' name='resume_at' min='2026-01-01T00:00' max='2099-12-31T23:59' required></label> <input type='submit' value='暂停到指定时刻'></form>");
    Esp32BaseWeb::sendInfoRowCompactForm("恢复自动浇水", "只影响之后的自动计划", automaticModeName(automatic.mode),
                                         "/irrigation", "立即恢复", "action", "resume", Esp32BaseWeb::UI_OK);
    Esp32BaseWeb::endPanel();
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
    if (!beginPage("运行", "手动浇水和当前执行状态")) return;
    const IrrigationConfig* config = g_app->configuration();
    const WateringStatus status = g_app->wateringStatus();
    Esp32BaseWeb::beginPanel("当前状态");
    char value[32];
    std::snprintf(value, sizeof(value), "%u", status.activeZoneId);
    Esp32BaseWeb::sendInfoRowCompact("状态", "", wateringStateName(status.state));
    Esp32BaseWeb::sendInfoRowCompact("当前区域", "0 表示没有活动区域", value);
    if (status.active) {
        Esp32BaseWeb::sendInfoRowCompactForm("停止当前任务", "停止整次任务", "正在运行",
                                             "/irrigation/run", "停止", "action", "stop", Esp32BaseWeb::UI_DANGER);
    }
    Esp32BaseWeb::endPanel();
    if (config) {
        Esp32BaseWeb::beginPanel("按计划手动浇水");
        for (const WateringPlan& plan : config->plans) {
            if (!plan.configured) continue;
            Esp32BaseWeb::sendChunk("<div class='urow'><div><b>");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("</b><small>计划自动执行开关不影响此入口</small></div><form method='post' action='/irrigation/run' onsubmit='return once(this)'><input type='hidden' name='action' value='start_plan'><input type='hidden' name='plan_id' value='");
            sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("'><input type='submit' value='启动'></form></div>");
        }
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::beginPanel("按区域手动浇水");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run' onsubmit='return once(this)'><input type='hidden' name='action' value='start_zones'><div class='fieldgrid'>");
        for (uint8_t index = 0; index < config->zones.size(); ++index) {
            Esp32BaseWeb::sendChunk("<label>"); Esp32BaseWeb::writeHtmlEscaped(config->zones[index].name.data());
            Esp32BaseWeb::sendChunk("（分钟）<input type='number' min='0' max='120' name='zone"); sendUnsigned(index + 1U);
            Esp32BaseWeb::sendChunk("' value='0' ");
            if (!config->zones[index].enabled) Esp32BaseWeb::sendChunk("disabled");
            Esp32BaseWeb::sendChunk("></label>");
        }
        Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='submit' value='开始浇水'></div></form>");
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
    if (!beginPage("计划", "自动计划配置和手动运行入口")) return;
    const IrrigationConfig* config = g_app->configuration();
    if (config) {
        for (const WateringPlan& plan : config->plans) {
            Esp32BaseWeb::beginPanel(plan.configured ? plan.name.data() : "未配置计划");
            char value[48];
            std::snprintf(value, sizeof(value), "计划 %u · %s", plan.id,
                          plan.scheduleEnabled ? "自动执行" : "不自动执行");
            Esp32BaseWeb::sendInfoRowCompact("状态", "", plan.configured ? value : "未配置");
            if (plan.configured) {
                for (uint8_t index = 0; index < plan.startMinutes.size(); ++index) {
                    if (plan.startMinutes[index] == kUnusedStartMinute) continue;
                    std::snprintf(value, sizeof(value), "%02u:%02u",
                                  plan.startMinutes[index] / 60U,
                                  plan.startMinutes[index] % 60U);
                    Esp32BaseWeb::sendInfoRowCompact("启动时间", "", value);
                }
            }
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='plan_id' value='");
            sendUnsigned(plan.id);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='");
            sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><div class='fieldgrid'><label>名称<input name='name' maxlength='63' required value='");
            Esp32BaseWeb::writeHtmlEscaped(plan.name.data());
            Esp32BaseWeb::sendChunk("'></label><label><input type='checkbox' name='schedule_enabled' value='1' ");
            if (plan.scheduleEnabled) Esp32BaseWeb::sendChunk("checked");
            Esp32BaseWeb::sendChunk("> 自动执行</label>");
            for (uint8_t index = 0; index < plan.startMinutes.size(); ++index) {
                Esp32BaseWeb::sendChunk("<label>启动时间 "); sendUnsigned(index + 1U);
                Esp32BaseWeb::sendChunk("<input type='time' name='time"); sendUnsigned(index + 1U);
                Esp32BaseWeb::sendChunk("' value='");
                if (plan.startMinutes[index] != kUnusedStartMinute) {
                    char time[8]; std::snprintf(time, sizeof(time), "%02u:%02u",
                                                plan.startMinutes[index] / 60U,
                                                plan.startMinutes[index] % 60U);
                    Esp32BaseWeb::sendChunk(time);
                }
                Esp32BaseWeb::sendChunk("'></label>");
            }
            for (uint8_t index = 0; index < plan.zoneDurationMinutes.size(); ++index) {
                Esp32BaseWeb::sendChunk("<label>区域 "); sendUnsigned(index + 1U);
                Esp32BaseWeb::sendChunk("（分钟）<input type='number' name='zone"); sendUnsigned(index + 1U);
                Esp32BaseWeb::sendChunk("' min='0' max='120' value='"); sendUnsigned(plan.zoneDurationMinutes[index]);
                Esp32BaseWeb::sendChunk("'></label>");
            }
            Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='submit' value='保存计划'></div></form>");
            if (plan.configured) {
                Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans' onsubmit=\"return confirm('确认删除该计划？')&&once(this)\"><input type='hidden' name='action' value='delete'><input type='hidden' name='plan_id' value='");
                sendUnsigned(plan.id);
                Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision);
                Esp32BaseWeb::sendChunk("'><input class='danger' type='submit' value='删除计划'></form>");
            }
            Esp32BaseWeb::endPanel();
        }
    }
    endPage();
}

void IrrigationWeb::zones() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_zone_learning")) return;
        bool success = false;
        uint32_t zoneId = 0;
        if (actionIs("save")) {
            success = saveZoneFromRequest();
        } else if (actionIs("learn") && uintParam("zone_id", 1, BoardPins::kZoneCount, zoneId)) {
            success = g_app->startZoneFlowLearning(static_cast<uint8_t>(zoneId)) ==
                      WateringStartResult::Started;
        } else if (actionIs("save_learning")) {
            uint32_t revision = 0;
            success = uintParam("revision", 1, UINT32_MAX, revision) &&
                      g_app->saveLearnedZoneFlow(revision);
        } else if (actionIs("discard_learning")) {
            g_app->discardLearnedZoneFlow(); success = true;
        }
        redirectResult("/irrigation/zones", success);
        return;
    }
    if (!beginPage("区域", "区域状态和基准流量学习")) return;
    const IrrigationConfig* config = g_app->configuration();
    if (config) {
        for (const ZoneConfig& zone : config->zones) {
            Esp32BaseWeb::beginPanel(zone.name.data());
            char value[48];
            if (zone.learnedFlowMlPerMinute == 0) std::snprintf(value, sizeof(value), "未学习");
            else std::snprintf(value, sizeof(value), "%lu mL/min", static_cast<unsigned long>(zone.learnedFlowMlPerMinute));
            Esp32BaseWeb::sendInfoRowCompact("启用状态", "", zone.enabled ? "启用" : "停用");
            Esp32BaseWeb::sendInfoRowCompact("基准流量", "未学习时不启用低/高流量判断", value);
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones' onsubmit='return once(this)'><input type='hidden' name='action' value='save'><input type='hidden' name='zone_id' value='");
            sendUnsigned(zone.id);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='revision' value='"); sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><label>名称<input name='name' maxlength='63' required value='");
            Esp32BaseWeb::writeHtmlEscaped(zone.name.data());
            Esp32BaseWeb::sendChunk("'></label><label><input type='checkbox' name='enabled' value='1' ");
            if (zone.enabled) Esp32BaseWeb::sendChunk("checked");
            Esp32BaseWeb::sendChunk("> 启用</label><input type='submit' value='保存区域'></form>");
            if (zone.enabled) {
                Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones' onsubmit='return once(this)'><input type='hidden' name='action' value='learn'><input type='hidden' name='zone_id' value='");
                sendUnsigned(zone.id);
                Esp32BaseWeb::sendChunk("'><input type='submit' value='开始流量学习'></form>");
            }
            Esp32BaseWeb::endPanel();
        }
        if (g_app->pendingLearnedZoneId() != 0) {
            Esp32BaseWeb::beginPanel("待确认学习结果");
            char value[48]; std::snprintf(value, sizeof(value), "区域 %u：%lu mL/min",
                                          g_app->pendingLearnedZoneId(),
                                          static_cast<unsigned long>(g_app->pendingLearnedFlowMlPerMinute()));
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "学习完成", value);
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/zones'><input type='hidden' name='action' value='save_learning'><input type='hidden' name='revision' value='");
            sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><input type='submit' value='保存基准流量'></form>");
            Esp32BaseWeb::sendInfoRowCompactForm("放弃结果", "不修改区域配置", "", "/irrigation/zones", "放弃", "action", "discard_learning", Esp32BaseWeb::UI_WARN);
            Esp32BaseWeb::endPanel();
        }
    }
    endPage();
}

void IrrigationWeb::records() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_records")) return;
        const bool success = actionIs("clear") && paramIs("confirm", "1") &&
                             g_app->clearWateringRecords(true);
        redirectResult("/irrigation/records", success);
        return;
    }
    if (!beginPage("浇水记录", "最新记录优先")) return;
    uint32_t id = 0;
    char idText[16]{};
    if (getParam("id", idText, sizeof(idText)) && parseUint(idText, 1, UINT32_MAX, id)) {
        sendRecordDetail(id);
        endPage();
        return;
    }
    uint32_t page = 1, perPage = 15;
    char text[16]{};
    if (getParam("page", text, sizeof(text))) parseUint(text, 1, UINT32_MAX, page);
    if (getParam("per", text, sizeof(text))) parseUint(text, 10, 50, perPage);
    Esp32BaseRecordStore::StoreStatus status{};
    g_app->readWateringRecordStoreStatus(status);
    Esp32BaseWeb::beginPanel("历史记录");
    Esp32BaseWeb::sendChunk("<p><a class='btnlink' href='/irrigation/records.csv'>导出 CSV</a></p><div class='tablewrap'><table><tr><th>编号</th><th>完成时间</th><th>来源</th><th>结果</th><th>实际时间</th><th>估算水量</th></tr>");
    RecordRowsContext context{g_app->configuration()};
    g_app->readLatestWateringRecords((page - 1U) * perPage,
                                     perPage,
                                     sendRecordRow,
                                     &context);
    Esp32BaseWeb::sendChunk("</table></div>");
    Esp32BaseWeb::Pagination pagination{};
    pagination.path = "/irrigation/records";
    pagination.query = "";
    pagination.page = page;
    pagination.perPage = perPage;
    pagination.total = status.recordCount;
    Esp32BaseWeb::sendPagination(pagination);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::beginPanel("清空记录");
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/records' onsubmit=\"return confirm('确认清空全部浇水记录？此操作不能撤销。')&&once(this)\"><input type='hidden' name='action' value='clear'><input type='hidden' name='confirm' value='1'><input class='danger' type='submit' value='清空全部记录'></form>");
    Esp32BaseWeb::endPanel();
    endPage();
}

void IrrigationWeb::settings() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        if (!Esp32BaseWeb::checkPostAllowed("irrigation_calibration")) return;
        bool success = false;
        if (actionIs("save_settings")) {
            success = saveSettingsFromRequest();
        } else if (actionIs("start_calibration")) {
            uint32_t zone = 0, minutes = 0;
            success = uintParam("zone_id", 1, BoardPins::kZoneCount, zone) &&
                      uintParam("minutes", 1, 60, minutes) &&
                      g_app->startFlowCalibration(static_cast<uint8_t>(zone),
                                                  static_cast<uint16_t>(minutes)) ==
                          WateringStartResult::Started;
        } else if (actionIs("measurement")) {
            uint32_t measuredMl = 0;
            success = uintParam("measured_ml", 1000, UINT32_MAX, measuredMl) &&
                      g_app->submitFlowCalibrationMeasurement(measuredMl);
        } else if (actionIs("save_calibration")) {
            uint32_t revision = 0;
            success = uintParam("revision", 1, UINT32_MAX, revision) &&
                      g_app->saveFlowCalibration(revision);
        } else if (actionIs("reset_calibration")) {
            g_app->resetFlowCalibration(); success = true;
        }
        redirectResult("/irrigation/settings", success);
        return;
    }
    if (!beginPage("浇水设置", "流量、保护、泵和阀驱动参数")) return;
    const IrrigationConfig* config = g_app->configuration();
    if (config) {
        char value[64];
        Esp32BaseWeb::beginPanel("当前参数");
        std::snprintf(value, sizeof(value), "%lu.%02lu 脉冲/L",
                      static_cast<unsigned long>(config->flowMeter.pulsesPerLiterX100 / 100U),
                      static_cast<unsigned long>(config->flowMeter.pulsesPerLiterX100 % 100U));
        Esp32BaseWeb::sendInfoRowCompact("流量系数", "", value);
        std::snprintf(value, sizeof(value), "%lu Hz / %u%%",
                      static_cast<unsigned long>(config->valveDrive.pwmFrequencyHz),
                      config->valveDrive.holdDutyPercent);
        Esp32BaseWeb::sendInfoRowCompact("阀驱动", "先全功率吸合，再维持", value);
        std::snprintf(value, sizeof(value), "%u 秒 / %u 秒",
                      config->flowProtection.flowStartTimeoutSec,
                      config->flowProtection.noFlowTimeoutSec);
        Esp32BaseWeb::sendInfoRowCompact("无流量保护", "启动 / 运行中", value);
        Esp32BaseWeb::endPanel();

        Esp32BaseWeb::beginPanel("参数修改");
        char coefficient[24];
        IrrigationConfigRules::formatPulsesPerLiter(config->flowMeter.pulsesPerLiterX100,
                                                    coefficient,
                                                    sizeof(coefficient));
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/settings' onsubmit='return once(this)'><input type='hidden' name='action' value='save_settings'><input type='hidden' name='revision' value='");
        sendUnsigned(config->revision);
        Esp32BaseWeb::sendChunk("'><div class='fieldgrid'>");
#define SEND_NUMBER(label, name, minValue, maxValue, valueExpr) \
        do { \
            Esp32BaseWeb::sendChunk("<label>" label "<input type='number' name='" name "' min='" minValue "' max='" maxValue "' value='"); \
            sendUnsigned(valueExpr); \
            Esp32BaseWeb::sendChunk("' required></label>"); \
        } while (false)
        SEND_NUMBER("阀全功率吸合 ms", "pull_in_ms", "100", "10000", config->valveDrive.pullInTimeMs);
        SEND_NUMBER("PWM 频率 Hz", "pwm_hz", "1000", "25000", config->valveDrive.pwmFrequencyHz);
        SEND_NUMBER("维持占空比 %", "hold_duty", "1", "100", config->valveDrive.holdDutyPercent);
        Esp32BaseWeb::sendChunk("<label><input type='checkbox' name='pump_enabled' value='1' ");
        if (config->pump.enabled) Esp32BaseWeb::sendChunk("checked");
        Esp32BaseWeb::sendChunk("> 启用外部泵控制</label>");
        SEND_NUMBER("泵启动延时 ms", "pump_start_ms", "0", "60000", config->pump.startDelayMs);
        SEND_NUMBER("停泵后关阀延时 ms", "pump_stop_ms", "0", "10000", config->pump.stopToValveCloseDelayMs);
        Esp32BaseWeb::sendChunk("<label>流量系数（脉冲/L）<input name='flow_coefficient' value='"); Esp32BaseWeb::sendChunk(coefficient); Esp32BaseWeb::sendChunk("' required></label>");
        SEND_NUMBER("流量建立超时 秒", "flow_start_sec", "1", "120", config->flowProtection.flowStartTimeoutSec);
        SEND_NUMBER("运行无流量超时 秒", "no_flow_sec", "1", "60", config->flowProtection.noFlowTimeoutSec);
        SEND_NUMBER("关闭后检测延时 秒", "unexpected_delay_sec", "0", "300", config->flowProtection.unexpectedFlowDelaySec);
        SEND_NUMBER("非计划流量窗口 秒", "unexpected_window_sec", "1", "300", config->flowProtection.unexpectedFlowWindowSec);
        SEND_NUMBER("非计划流量脉冲阈值", "unexpected_pulses", "1", "65535", config->flowProtection.unexpectedFlowPulseCount);
        SEND_NUMBER("流量偏差确认 秒", "deviation_confirm_sec", "1", "300", config->flowProtection.flowDeviationConfirmSec);
        SEND_NUMBER("低流量阈值 %", "low_percent", "1", "99", config->flowProtection.lowFlowPercent);
        SEND_NUMBER("高流量阈值 %", "high_percent", "101", "1000", config->flowProtection.highFlowPercent);
        Esp32BaseWeb::sendChunk("<label>低流量动作<select name='low_action'><option value='0'"); if (config->flowProtection.lowFlowAction == FlowAlertAction::AlertOnly) Esp32BaseWeb::sendChunk(" selected"); Esp32BaseWeb::sendChunk(">只报警</option><option value='1'"); if (config->flowProtection.lowFlowAction == FlowAlertAction::StopWatering) Esp32BaseWeb::sendChunk(" selected"); Esp32BaseWeb::sendChunk(">停止浇水</option></select></label>");
        Esp32BaseWeb::sendChunk("<label>高流量动作<select name='high_action'><option value='0'"); if (config->flowProtection.highFlowAction == FlowAlertAction::AlertOnly) Esp32BaseWeb::sendChunk(" selected"); Esp32BaseWeb::sendChunk(">只报警</option><option value='1'"); if (config->flowProtection.highFlowAction == FlowAlertAction::StopWatering) Esp32BaseWeb::sendChunk(" selected"); Esp32BaseWeb::sendChunk(">停止浇水</option></select></label>");
        SEND_NUMBER("RTC 倒退阈值 分钟", "rollback_minutes", "1", "60", config->timeSafety.rtcRollbackThresholdMinutes);
        SEND_NUMBER("在线检查点 小时", "checkpoint_hours", "0", "168", config->timeSafety.aliveCheckpointHours);
#undef SEND_NUMBER
        Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='submit' value='保存全部参数'></div></form>");
        Esp32BaseWeb::endPanel();

        Esp32BaseWeb::beginPanel("流量计校准");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/settings'><input type='hidden' name='action' value='start_calibration'><label>区域 <input type='number' name='zone_id' min='1' max='6' value='1' required></label> <label>最长分钟 <input type='number' name='minutes' min='1' max='60' value='10' required></label> <input type='submit' value='开始校准出水'></form>");
        if (g_app->flowCalibration().hasPendingMeasurement()) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/settings'><input type='hidden' name='action' value='measurement'><label>实测水量 mL <input type='number' name='measured_ml' min='1000' required></label><input type='submit' value='加入样本'></form>");
        }
        if (g_app->flowCalibration().sampleCount() != 0) {
            std::snprintf(value, sizeof(value), "%lu.%02lu 脉冲/L · %u 个样本",
                          static_cast<unsigned long>(g_app->flowCalibration().combinedPulsesPerLiterX100() / 100U),
                          static_cast<unsigned long>(g_app->flowCalibration().combinedPulsesPerLiterX100() % 100U),
                          g_app->flowCalibration().sampleCount());
            Esp32BaseWeb::sendNotice(g_app->flowCalibration().samplesUnstable() ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK,
                                     "校准建议值", value);
            Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/settings'><input type='hidden' name='action' value='save_calibration'><input type='hidden' name='revision' value='");
            sendUnsigned(config->revision);
            Esp32BaseWeb::sendChunk("'><input type='submit' value='保存流量系数'></form>");
        }
        Esp32BaseWeb::sendInfoRowCompactForm("清除校准样本", "不修改已保存系数", "", "/irrigation/settings", "清除", "action", "reset_calibration", Esp32BaseWeb::UI_WARN);
        Esp32BaseWeb::endPanel();
    }
    endPage();
}

void IrrigationWeb::statusApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const WateringStatus status = g_app->wateringStatus();
    char json[400];
    std::snprintf(json,
                  sizeof(json),
                  "{\"ready\":%s,\"active\":%s,\"state\":%u,\"zoneId\":%u,\"flowEstablished\":%s,\"automaticMode\":%u,\"unexpectedFlowAlarm\":%s,\"recordStorageFault\":%s,\"eventStorageFault\":%s,\"schedulerStorageFault\":%s,\"checkpointStorageFault\":%s}",
                  g_app->businessReady() ? "true" : "false",
                  status.active ? "true" : "false",
                  static_cast<unsigned>(status.state),
                  status.activeZoneId,
                  status.flowEstablished ? "true" : "false",
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
