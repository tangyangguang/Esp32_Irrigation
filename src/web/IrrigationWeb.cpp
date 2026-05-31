#include "web/IrrigationWeb.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Version.h"
#include "domain/MaintenanceService.h"
#include "domain/SafetyManager.h"
#include "domain/ZoneManager.h"
#include "storage/EventStore.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/ScheduleSkipStore.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"

namespace {

void writeUInt(uint32_t value) {
    char text[16];
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(text);
}

void writeBool(bool value) {
    Esp32BaseWeb::sendChunk(value ? "true" : "false");
}

void beginJson(int code = 200) {
    (void)Esp32BaseWeb::beginResponse(code, "application/json", nullptr);
}

void endJson() {
    Esp32BaseWeb::endResponse();
}

void sendError(int code, const char* error) {
    beginJson(code);
    Esp32BaseWeb::sendChunk("{\"ok\":false,\"error\":\"");
    Esp32BaseWeb::writeJsonEscaped(error ? error : "error");
    Esp32BaseWeb::sendChunk("\"}");
    endJson();
}

void sendOk() {
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

void sendMethodNotAllowed(const char* allowed) {
    beginJson(405);
    Esp32BaseWeb::sendChunk("{\"ok\":false,\"error\":\"method_not_allowed\",\"allowed\":\"");
    Esp32BaseWeb::writeJsonEscaped(allowed);
    Esp32BaseWeb::sendChunk("\",\"method\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32BaseWeb::currentMethodName());
    Esp32BaseWeb::sendChunk("\"}");
    endJson();
}

bool readU32(const char* name, uint32_t* value) {
    char text[24] = "";
    if (!value || !Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    char* end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

bool readU16(const char* name, uint16_t* value) {
    uint32_t raw = 0;
    if (!value || !readU32(name, &raw) || raw > 65535UL) {
        return false;
    }
    *value = static_cast<uint16_t>(raw);
    return true;
}

bool readU8(const char* name, uint8_t* value) {
    uint32_t raw = 0;
    if (!value || !readU32(name, &raw) || raw > 255UL) {
        return false;
    }
    *value = static_cast<uint8_t>(raw);
    return true;
}

bool readBool(const char* name, bool* value) {
    char text[8] = "";
    if (!value || !Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0 || strcmp(text, "on") == 0) {
        *value = true;
        return true;
    }
    if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0) {
        *value = false;
        return true;
    }
    return false;
}

bool readZoneId(uint8_t* zoneId) {
    return readU8("zoneId", zoneId) && Irrigation::validZoneId(*zoneId);
}

bool readYmd(const char* name, uint32_t* ymd) {
    char text[16] = "";
    if (!ymd || !Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    if (strlen(text) == 10 && text[4] == '-' && text[7] == '-') {
        for (uint8_t i = 0; i < 10; ++i) {
            if ((i == 4 || i == 7) ? text[i] != '-' : (text[i] < '0' || text[i] > '9')) {
                return false;
            }
        }
        *ymd = static_cast<uint32_t>((text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0')) * 10000UL +
               static_cast<uint32_t>((text[5] - '0') * 10 + (text[6] - '0')) * 100UL +
               static_cast<uint32_t>((text[8] - '0') * 10 + (text[9] - '0'));
        return true;
    }
    return readU32(name, ymd);
}

bool readTimeParam(const char* name, uint8_t* hour, uint8_t* minute) {
    char text[8] = "";
    if (!hour || !minute || !Esp32BaseWeb::getParam(name, text, sizeof(text)) || strlen(text) != 5 || text[2] != ':') {
        return false;
    }
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '9' || text[4] < '0' || text[4] > '9') {
        return false;
    }
    const uint8_t h = static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
    const uint8_t m = static_cast<uint8_t>((text[3] - '0') * 10 + (text[4] - '0'));
    if (h > 23 || m > 59) {
        return false;
    }
    *hour = h;
    *minute = m;
    return true;
}

Irrigation::StartSource startSourceFromRequest() {
    char source[16] = "";
    return Esp32BaseWeb::getParam("source", source, sizeof(source)) && strcmp(source, "web_page") == 0
        ? Irrigation::StartSource::WEB_PAGE
        : Irrigation::StartSource::HTTP_API;
}

Irrigation::StopSource stopSourceFromRequest() {
    char source[16] = "";
    return Esp32BaseWeb::getParam("source", source, sizeof(source)) && strcmp(source, "web_page") == 0
        ? Irrigation::StopSource::WEB_PAGE
        : Irrigation::StopSource::HTTP_API;
}

bool wantsRedirect() {
    char source[16] = "";
    return Esp32BaseWeb::getParam("source", source, sizeof(source)) && strcmp(source, "web_page") == 0;
}

void redirectOrOk(const char* path = "/irrigation") {
    if (wantsRedirect()) {
        Esp32BaseWeb::redirectSeeOther(path);
        return;
    }
    sendOk();
}

uint32_t currentYmd() {
#if ESP32BASE_ENABLE_NTP
    if (!Esp32BaseNtp::isTimeSynced()) {
        return PlanStore::DefaultCycleStartYmd;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    tm local = {};
    if (localtime_r(&now, &local) == nullptr) {
        return PlanStore::DefaultCycleStartYmd;
    }
    return static_cast<uint32_t>(local.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(local.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(local.tm_mday);
#else
    return PlanStore::DefaultCycleStartYmd;
#endif
}

void writeDuration(uint32_t seconds) {
    writeUInt(seconds / 60UL);
    Esp32BaseWeb::sendChunk(" 分钟");
}

void writeTime(uint8_t hour, uint8_t minute) {
    char text[8];
    snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(hour), static_cast<unsigned>(minute));
    Esp32BaseWeb::sendChunk(text);
}

void writeYmdInput(uint32_t ymd) {
    char text[12];
    snprintf(text, sizeof(text), "%04lu-%02lu-%02lu",
             static_cast<unsigned long>(ymd / 10000UL),
             static_cast<unsigned long>((ymd / 100UL) % 100UL),
             static_cast<unsigned long>(ymd % 100UL));
    Esp32BaseWeb::sendChunk(text);
}

const char* uiToneForState(Irrigation::ZoneState state) {
    switch (state) {
        case Irrigation::ZoneState::RUNNING:
        case Irrigation::ZoneState::STARTING: return " warn";
        case Irrigation::ZoneState::ERROR: return " danger";
        case Irrigation::ZoneState::IDLE: return " ok";
        case Irrigation::ZoneState::DISABLED:
        default: return "";
    }
}

void writeStatusJson(const Irrigation::ZoneStatus& status) {
    Esp32BaseWeb::sendChunk("{\"zoneId\":");
    writeUInt(status.zoneId);
    Esp32BaseWeb::sendChunk(",\"state\":\"");
    Esp32BaseWeb::writeJsonEscaped(Irrigation::zoneStateName(status.state));
    Esp32BaseWeb::sendChunk("\",\"enabled\":");
    writeBool(status.enabled);
    Esp32BaseWeb::sendChunk(",\"busy\":");
    writeBool(status.busy);
    Esp32BaseWeb::sendChunk(",\"errorActive\":");
    writeBool(status.errorActive);
    Esp32BaseWeb::sendChunk(",\"targetSec\":");
    writeUInt(status.targetSec);
    Esp32BaseWeb::sendChunk(",\"elapsedSec\":");
    writeUInt(status.elapsedSec);
    Esp32BaseWeb::sendChunk(",\"remainingSec\":");
    writeUInt(status.remainingSec);
    Esp32BaseWeb::sendChunk(",\"pulses\":");
    writeUInt(status.pulses);
    Esp32BaseWeb::sendChunk(",\"estimatedMl\":");
    writeUInt(status.estimatedMilliliters);
    Esp32BaseWeb::sendChunk(",\"flowRatePerMinuteX1000\":");
    writeUInt(status.flowRatePerMinuteX1000);
    Esp32BaseWeb::sendChunk(",\"planId\":");
    writeUInt(status.planId);
    Esp32BaseWeb::sendChunk("}");
}

void writePlanJson(const Irrigation::PlanDefinition& plan) {
    Esp32BaseWeb::sendChunk("{\"exists\":");
    writeBool(plan.exists);
    Esp32BaseWeb::sendChunk(",\"planId\":");
    writeUInt(plan.planId);
    Esp32BaseWeb::sendChunk(",\"zoneId\":");
    writeUInt(plan.zoneId);
    Esp32BaseWeb::sendChunk(",\"slotIndex\":");
    writeUInt(plan.slotIndex);
    Esp32BaseWeb::sendChunk(",\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(plan.name);
    Esp32BaseWeb::sendChunk("\",\"enabled\":");
    writeBool(plan.enabled);
    Esp32BaseWeb::sendChunk(",\"time\":\"");
    writeTime(plan.timeHour, plan.timeMinute);
    Esp32BaseWeb::sendChunk("\",\"durationSec\":");
    writeUInt(plan.durationSec);
    Esp32BaseWeb::sendChunk(",\"cycleDays\":");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk(",\"cycleMask\":");
    writeUInt(plan.cycleMask);
    Esp32BaseWeb::sendChunk(",\"cycleStartYmd\":");
    writeUInt(plan.cycleStartYmd);
    Esp32BaseWeb::sendChunk("}");
}

void writeOnePostHidden(const char* name, const char* value) {
    Esp32BaseWeb::sendChunk("<input type='hidden' name='");
    Esp32BaseWeb::writeHtmlEscaped(name);
    Esp32BaseWeb::sendChunk("' value='");
    Esp32BaseWeb::writeHtmlEscaped(value);
    Esp32BaseWeb::sendChunk("'>");
}

void writeHiddenU32(const char* name, uint32_t value) {
    char text[16];
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    writeOnePostHidden(name, text);
}

void pageHeader(const char* title) {
    Esp32BaseWeb::sendHeader(title);
}

void pageFooter() {
    Esp32BaseWeb::sendFooter();
}

uint32_t durationMinutesForUi(uint32_t seconds) {
    return seconds == 0 ? 0 : ((seconds + 59UL) / 60UL);
}

void writeDurationMinutes(uint32_t seconds) {
    writeUInt(durationMinutesForUi(seconds));
    Esp32BaseWeb::sendChunk(" 分钟");
}

void writeCycleSummary(const Irrigation::PlanDefinition& plan) {
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk(" 天循环：");
    bool first = true;
    for (uint8_t i = 0; i < plan.cycleDays; ++i) {
        if ((plan.cycleMask & (1UL << i)) == 0) {
            continue;
        }
        if (!first) {
            Esp32BaseWeb::sendChunk("、");
        }
        first = false;
        Esp32BaseWeb::sendChunk("第 ");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk(" 天");
    }
}

void formatDurationHuman(uint32_t seconds, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    const uint32_t hours = seconds / 3600UL;
    const uint32_t minutes = (seconds % 3600UL) / 60UL;
    const uint32_t secs = seconds % 60UL;
    size_t used = 0;
    out[0] = '\0';
    if (hours > 0) {
        used += snprintf(out + used, len - used, "%lu 小时", static_cast<unsigned long>(hours));
    }
    if (used < len && (minutes > 0 || (used == 0 && secs == 0))) {
        used += snprintf(out + used, len - used, "%s%lu 分钟", used > 0 ? " " : "", static_cast<unsigned long>(minutes));
    }
    if (used < len && (secs > 0 || used == 0)) {
        (void)snprintf(out + used, len - used, "%s%lu 秒", used > 0 ? " " : "", static_cast<unsigned long>(secs));
    }
}

void writeDurationHuman(uint32_t seconds) {
    char text[32];
    formatDurationHuman(seconds, text, sizeof(text));
    Esp32BaseWeb::sendChunk(text);
}

void writeDurationMsHuman(uint32_t ms) {
    writeDurationHuman((ms + 999UL) / 1000UL);
}

void writeDateTimeHuman(uint32_t epoch) {
    if (epoch == 0) {
        Esp32BaseWeb::sendChunk("时间未同步");
        return;
    }
    const time_t value = static_cast<time_t>(epoch);
    tm local = {};
    if (localtime_r(&value, &local) == nullptr) {
        Esp32BaseWeb::sendChunk("时间无效");
        return;
    }
    char text[24];
    snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
             local.tm_year + 1900,
             local.tm_mon + 1,
             local.tm_mday,
             local.tm_hour,
             local.tm_min,
             local.tm_sec);
    Esp32BaseWeb::sendChunk(text);
}

const char* zoneStateLabel(Irrigation::ZoneState state) {
    switch (state) {
        case Irrigation::ZoneState::DISABLED: return "已禁用";
        case Irrigation::ZoneState::IDLE: return "待机";
        case Irrigation::ZoneState::STARTING: return "启动中";
        case Irrigation::ZoneState::RUNNING: return "浇水中";
        case Irrigation::ZoneState::ERROR: return "异常";
        default: return "未知";
    }
}

const char* taskTypeLabel(Irrigation::TaskType type) {
    switch (type) {
        case Irrigation::TaskType::PLAN: return "计划浇水";
        case Irrigation::TaskType::MANUAL:
        default: return "手动浇水";
    }
}

const char* taskResultLabel(Irrigation::TaskResult result) {
    switch (result) {
        case Irrigation::TaskResult::COMPLETED: return "正常完成";
        case Irrigation::TaskResult::USER_STOPPED: return "手动停止";
        case Irrigation::TaskResult::FLOW_START_TIMEOUT: return "启动后未检测到水流";
        case Irrigation::TaskResult::FLOW_NO_PULSE_TIMEOUT: return "浇水中断流";
        case Irrigation::TaskResult::LEAK_PROTECTED: return "漏水保护停止";
        case Irrigation::TaskResult::FACTORY_RESET_PROTECTED: return "恢复出厂保护停止";
        case Irrigation::TaskResult::CONFIG_INVALID: return "配置无效";
        case Irrigation::TaskResult::REJECTED: return "启动被拒绝";
        case Irrigation::TaskResult::NONE:
        default: return "未完成";
    }
}

const char* eventTypeLabel(Irrigation::EventType type) {
    switch (type) {
        case Irrigation::EventType::BOOT: return "系统启动";
        case Irrigation::EventType::ZONE_CONFIG_CHANGED: return "水路配置变更";
        case Irrigation::EventType::SYSTEM_CONFIG_CHANGED: return "系统配置变更";
        case Irrigation::EventType::PLAN_CHANGED: return "计划变更";
        case Irrigation::EventType::PLAN_OBSERVED: return "计划调度";
        case Irrigation::EventType::WATER_START: return "开始浇水";
        case Irrigation::EventType::WATER_FINISH: return "浇水完成";
        case Irrigation::EventType::WATER_ERROR: return "浇水异常";
        case Irrigation::EventType::LEAK_ALERT: return "漏水告警";
        case Irrigation::EventType::ALERT_CLEARED: return "告警清除";
        case Irrigation::EventType::FACTORY_RESET_REQUESTED: return "请求恢复出厂";
        case Irrigation::EventType::FACTORY_RESET_EXECUTED: return "已恢复出厂";
        case Irrigation::EventType::WIFI_STATUS_CHANGED: return "网络状态";
        case Irrigation::EventType::OTA_STATUS_CHANGED: return "固件升级";
        default: return "未知事件";
    }
}

const char* eventSourceLabel(Irrigation::EventSource source) {
    switch (source) {
        case Irrigation::EventSource::BUTTON: return "本地按键";
        case Irrigation::EventSource::WEB: return "网页";
        case Irrigation::EventSource::PLAN: return "计划";
        case Irrigation::EventSource::SYSTEM:
        default: return "系统";
    }
}

const char* observationStatusLabel(Irrigation::PlanObservationStatus status) {
    switch (status) {
        case Irrigation::PlanObservationStatus::STARTED: return "计划已启动";
        case Irrigation::PlanObservationStatus::SKIPPED_CALENDAR: return "当天已设置跳过";
        case Irrigation::PlanObservationStatus::SKIPPED_DISABLED: return "计划或水路已停用";
        case Irrigation::PlanObservationStatus::SKIPPED_BUSY: return "水路正在执行其他任务";
        case Irrigation::PlanObservationStatus::SKIPPED_ERROR: return "水路处于异常状态";
        case Irrigation::PlanObservationStatus::SKIPPED_LEAK: return "因漏水告警跳过";
        case Irrigation::PlanObservationStatus::SKIPPED_RESET: return "恢复出厂中跳过";
        case Irrigation::PlanObservationStatus::SKIPPED_CYCLE: return "非本轮执行日";
        case Irrigation::PlanObservationStatus::SKIPPED_CONFIG_INVALID: return "配置无效，未执行";
        case Irrigation::PlanObservationStatus::REJECTED: return "启动被拒绝";
        case Irrigation::PlanObservationStatus::MISSED: return "系统发现计划已错过";
        case Irrigation::PlanObservationStatus::NOT_EVALUATED:
        default: return "未评估";
    }
}

void writeEventDetail(const EventStore::Event& event) {
    const Irrigation::EventType type = static_cast<Irrigation::EventType>(event.type);
    switch (type) {
        case Irrigation::EventType::BOOT:
            Esp32BaseWeb::sendChunk("设备启动完成");
            return;
        case Irrigation::EventType::ZONE_CONFIG_CHANGED:
            Esp32BaseWeb::sendChunk("水路配置已保存");
            return;
        case Irrigation::EventType::SYSTEM_CONFIG_CHANGED:
            Esp32BaseWeb::sendChunk("系统配置已保存");
            return;
        case Irrigation::EventType::PLAN_CHANGED:
            Esp32BaseWeb::sendChunk(event.code == 1 ? "计划已创建" : (event.code == 2 ? "计划已删除" : "计划已保存"));
            return;
        case Irrigation::EventType::PLAN_OBSERVED:
            Esp32BaseWeb::writeHtmlEscaped(observationStatusLabel(static_cast<Irrigation::PlanObservationStatus>(event.code)));
            return;
        case Irrigation::EventType::WATER_START:
            Esp32BaseWeb::sendChunk("目标时长 ");
            writeDurationHuman(static_cast<uint32_t>(event.value1 > 0 ? event.value1 : 0));
            return;
        case Irrigation::EventType::WATER_FINISH:
        case Irrigation::EventType::WATER_ERROR:
            Esp32BaseWeb::writeHtmlEscaped(taskResultLabel(static_cast<Irrigation::TaskResult>(event.code)));
            return;
        case Irrigation::EventType::LEAK_ALERT:
            Esp32BaseWeb::sendChunk("待机状态检测到异常流量");
            return;
        case Irrigation::EventType::ALERT_CLEARED:
            Esp32BaseWeb::sendChunk(event.zoneId == 0 ? "全部告警已清除" : "水路告警已清除");
            return;
        case Irrigation::EventType::WIFI_STATUS_CHANGED:
            if (strcmp(event.text, "CONNECTED") == 0) {
                Esp32BaseWeb::sendChunk("网络已连接");
            } else if (strcmp(event.text, "CONNECTING") == 0) {
                Esp32BaseWeb::sendChunk("网络连接中");
            } else {
                Esp32BaseWeb::writeHtmlEscaped(event.text);
            }
            return;
        case Irrigation::EventType::FACTORY_RESET_REQUESTED:
            Esp32BaseWeb::sendChunk("已请求恢复出厂设置");
            return;
        case Irrigation::EventType::FACTORY_RESET_EXECUTED:
            Esp32BaseWeb::sendChunk("恢复出厂设置已执行");
            return;
        case Irrigation::EventType::OTA_STATUS_CHANGED:
            Esp32BaseWeb::writeHtmlEscaped(event.text);
            return;
        default:
            Esp32BaseWeb::writeHtmlEscaped(event.text);
            return;
    }
}

void readPaging(uint16_t total, uint16_t defaultPerPage, uint32_t* page, uint32_t* perPage, uint16_t* offset) {
    uint32_t rawPage = 1;
    uint32_t rawPer = defaultPerPage;
    (void)readU32("page", &rawPage);
    (void)readU32("per", &rawPer);
    if (rawPer != 10 && rawPer != 20 && rawPer != 50) {
        rawPer = defaultPerPage;
    }
    const uint32_t totalPages = total == 0 ? 1 : ((static_cast<uint32_t>(total) + rawPer - 1UL) / rawPer);
    if (rawPage < 1) {
        rawPage = 1;
    } else if (rawPage > totalPages) {
        rawPage = totalPages;
    }
    *page = rawPage;
    *perPage = rawPer;
    *offset = static_cast<uint16_t>((rawPage - 1UL) * rawPer);
}

void handleOverviewPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    pageHeader("灌溉总览");
    Esp32BaseWeb::sendPageTitle("灌溉总览", "固定 4 路 Zone，按水路独立运行。");

    uint8_t enabledCount = 0;
    uint8_t runningCount = 0;
    uint8_t errorCount = 0;
    uint8_t startableCount = 0;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        if (!status.enabled) {
            continue;
        }
        ++enabledCount;
        if (status.busy) {
            ++runningCount;
        }
        if (status.errorActive) {
            ++errorCount;
        }
        if (!status.busy && !status.errorActive && !ZoneManager::leakAlertActive()) {
            ++startableCount;
        }
    }

    char enabledText[16];
    char runningText[16];
    char startableText[16];
    char safetyText[24];
    char limitText[32];
    snprintf(enabledText, sizeof(enabledText), "%u / %u", static_cast<unsigned>(enabledCount), static_cast<unsigned>(Irrigation::MaxZones));
    snprintf(runningText, sizeof(runningText), "%u", static_cast<unsigned>(runningCount));
    snprintf(startableText, sizeof(startableText), "%u", static_cast<unsigned>(startableCount));
    if (ZoneManager::leakAlertActive()) {
        strlcpy(safetyText, "漏水告警", sizeof(safetyText));
    } else if (errorCount > 0) {
        snprintf(safetyText, sizeof(safetyText), "%u 路异常", static_cast<unsigned>(errorCount));
    } else {
        strlcpy(safetyText, "正常", sizeof(safetyText));
    }
    formatDurationHuman(SystemConfigStore::current().maxWateringDurationSec, limitText, sizeof(limitText));

    Esp32BaseWeb::beginPanel("关键指标");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("系统安全", safetyText, ZoneManager::leakAlertActive() ? "需要现场确认" : "无全局漏水告警");
    Esp32BaseWeb::sendMetric("启用水路", enabledText, "首页仅展示启用水路");
    Esp32BaseWeb::sendMetric("正在浇水", runningText, runningCount > 0 ? "可在水路状态中停止" : "当前无执行任务");
    Esp32BaseWeb::sendMetric("可手动启动", startableText, "显示在手动浇水启动区");
    Esp32BaseWeb::sendMetric("单次上限", limitText, "手动和计划共同受限");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("水路状态");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>水路</th><th>状态</th><th>任务</th><th>目标时长</th><th>剩余时间</th><th>脉冲</th><th>估算水量</th><th>操作</th></tr></thead><tbody>");
    bool wroteStatus = false;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        if (!status.enabled) {
            continue;
        }
        const Irrigation::ZoneConfig& config = ZoneManager::config(zoneId);
        wroteStatus = true;
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(config.name);
        Esp32BaseWeb::sendChunk("</td><td><span class='tag");
        Esp32BaseWeb::sendChunk(uiToneForState(status.state));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(zoneStateLabel(status.state));
        Esp32BaseWeb::sendChunk("</span></td><td>");
        Esp32BaseWeb::writeHtmlEscaped(status.busy ? taskTypeLabel(status.taskType) : "-");
        Esp32BaseWeb::sendChunk("</td><td>");
        writeDurationHuman(status.targetSec);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeDurationHuman(status.remainingSec);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(status.pulses);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(status.estimatedMilliliters);
        Esp32BaseWeb::sendChunk(" ml</td><td><div class='fsactions'>");
        if (status.busy) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/stop' onsubmit=\"return confirm('确认停止该水路？')&&once(this)\">");
            writeOnePostHidden("source", "web_page");
            writeHiddenU32("zoneId", zoneId);
            Esp32BaseWeb::sendChunk("<input class='fsaction' type='submit' value='停止'></form>");
        }
        if (status.errorActive) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/clear-error' onsubmit=\"return confirm('确认现场已处理并清除异常？')&&once(this)\">");
            writeOnePostHidden("source", "web_page");
            writeHiddenU32("zoneId", zoneId);
            Esp32BaseWeb::sendChunk("<input class='fsaction' type='submit' value='清除异常'></form>");
        }
        if (!status.busy && !status.errorActive) {
            Esp32BaseWeb::sendChunk("-");
        }
        Esp32BaseWeb::sendChunk("</div></td></tr>");
    }
    if (!wroteStatus) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='8'>暂无启用水路</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><div class='actions'><form method='post' action='/api/v1/zones/stop-all' onsubmit=\"return confirm('确认停止全部水路？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    Esp32BaseWeb::sendChunk("<input class='danger' type='submit' value='全部停止'");
    Esp32BaseWeb::sendChunk(runningCount > 0 ? "" : " disabled");
    Esp32BaseWeb::sendChunk("></form></div>");
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("手动浇水启动");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>水路</th><th>建议时长</th><th>操作</th></tr></thead><tbody>");
    bool wroteManual = false;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        if (!status.enabled || status.busy || status.errorActive || ZoneManager::leakAlertActive()) {
            continue;
        }
        const Irrigation::ZoneConfig& config = ZoneManager::config(zoneId);
        wroteManual = true;
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(config.name);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeDurationHuman(SystemConfigStore::current().manualDefaultDurationSec);
        Esp32BaseWeb::sendChunk("</td><td><div class='fsactions'>");
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/start' onsubmit=\"return confirm('确认手动启动该水路？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
        Esp32BaseWeb::sendChunk("<input type='number' name='durationMin' min='1' max='");
        writeUInt(SystemConfigStore::current().maxWateringDurationSec / 60UL);
        Esp32BaseWeb::sendChunk("' value='");
        writeUInt(durationMinutesForUi(SystemConfigStore::current().manualDefaultDurationSec));
        Esp32BaseWeb::sendChunk("'><input class='fsaction' type='submit' value='启动'></form></div></td></tr>");
    }
    if (!wroteManual) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='3'>当前没有可手动启动的水路</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    pageFooter();
}

void writePlanRow(const Irrigation::PlanDefinition& plan) {
    Esp32BaseWeb::sendChunk("<tr><td>");
    Esp32BaseWeb::writeHtmlEscaped(plan.name);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::sendChunk(plan.enabled ? "启用" : "停用");
    Esp32BaseWeb::sendChunk("</td><td>");
    writeTime(plan.timeHour, plan.timeMinute);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeDurationMinutes(plan.durationSec);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeCycleSummary(plan);
    Esp32BaseWeb::sendChunk("</td><td><div class='fsactions'><a class='btnlink compact' href='/irrigation/plan?planId=");
    writeUInt(plan.planId);
    Esp32BaseWeb::sendChunk("'>编辑</a><form method='post' action='");
    Esp32BaseWeb::sendChunk(plan.enabled ? "/api/v1/plan/disable" : "/api/v1/plan/enable");
    Esp32BaseWeb::sendChunk("' onsubmit=\"return confirm('确认切换计划状态？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    writeHiddenU32("planId", plan.planId);
    Esp32BaseWeb::sendChunk("<input class='fsaction' type='submit' value='");
    Esp32BaseWeb::sendChunk(plan.enabled ? "停用" : "启用");
    Esp32BaseWeb::sendChunk("'></form><form method='post' action='/api/v1/plan/delete' onsubmit=\"return confirm('确认删除该计划？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    writeHiddenU32("planId", plan.planId);
    Esp32BaseWeb::sendChunk("<input class='fsdelete danger' type='submit' value='删除'></form></div></td></tr>");
}

void handlePlansPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    pageHeader("计划");
    Esp32BaseWeb::sendPageTitle("计划", "每个 Zone 最多 6 条计划。");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        Esp32BaseWeb::beginPanel(ZoneManager::config(zoneId).name);
        Esp32BaseWeb::sendChunk("<div class='actions'>");
        if (PlanStore::countForZone(zoneId) < Irrigation::MaxPlansPerZone) {
            Esp32BaseWeb::sendChunk("<a class='btnlink compact' href='/irrigation/plan?zoneId=");
            writeUInt(zoneId);
            Esp32BaseWeb::sendChunk("'>新增计划</a>");
        } else {
            Esp32BaseWeb::sendChunk("<span class='tag'>计划已满</span>");
        }
        Esp32BaseWeb::sendChunk("</div><div class='tablewrap'><table class='part'><thead><tr><th>名称</th><th>状态</th><th>时间</th><th>时长</th><th>循环</th><th>操作</th></tr></thead><tbody>");
        bool wrote = false;
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(zoneId, slot);
            if (plan.exists) {
                writePlanRow(plan);
                wrote = true;
            }
        }
        if (!wrote) {
            Esp32BaseWeb::sendChunk("<tr><td colspan='6'>暂无计划</td></tr>");
        }
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
        Esp32BaseWeb::endPanel();
    }
    pageFooter();
}

Irrigation::PlanDefinition makeDraftPlan(uint8_t zoneId) {
    Irrigation::PlanDefinition plan = {};
    plan.exists = true;
    plan.zoneId = zoneId;
    strlcpy(plan.name, "新计划", sizeof(plan.name));
    plan.enabled = false;
    plan.timeHour = 7;
    plan.timeMinute = 0;
    plan.durationSec = SystemConfigStore::current().manualDefaultDurationSec;
    plan.cycleDays = 1;
    plan.cycleMask = 1;
    plan.cycleStartYmd = currentYmd();
    return plan;
}

bool applyPlanForm(Irrigation::PlanDefinition& plan, const char** error) {
    uint32_t value = 0;
    if (Esp32BaseWeb::hasParam("name") && !Esp32BaseWeb::getParam("name", plan.name, sizeof(plan.name))) {
        *error = "invalid_name";
        return false;
    }
    if (Esp32BaseWeb::hasParam("enabled") && !readBool("enabled", &plan.enabled)) {
        *error = "invalid_enabled";
        return false;
    }
    if (Esp32BaseWeb::hasParam("time") && !readTimeParam("time", &plan.timeHour, &plan.timeMinute)) {
        *error = "invalid_time";
        return false;
    }
    if (Esp32BaseWeb::hasParam("durationMin")) {
        if (!readU32("durationMin", &value) || value == 0 || value > (SystemConfigStore::current().maxWateringDurationSec / 60UL)) {
            *error = "invalid_duration";
            return false;
        }
        plan.durationSec = value * 60UL;
    } else if (Esp32BaseWeb::hasParam("durationSec") && !readU32("durationSec", &plan.durationSec)) {
        *error = "invalid_duration";
        return false;
    }
    if (Esp32BaseWeb::hasParam("cycleDays") && !readU8("cycleDays", &plan.cycleDays)) {
        *error = "invalid_cycle_days";
        return false;
    }
    if (Esp32BaseWeb::hasParam("cycleMask") && !readU32("cycleMask", &plan.cycleMask)) {
        *error = "invalid_cycle_mask";
        return false;
    }
    if (Esp32BaseWeb::hasParam("cycleStartYmd") && !readYmd("cycleStartYmd", &plan.cycleStartYmd)) {
        *error = "invalid_cycle_start";
        return false;
    }
    if (plan.durationSec > SystemConfigStore::current().maxWateringDurationSec) {
        *error = "duration_exceeds_system_limit";
        return false;
    }
    return true;
}

void writePlanForm(const Irrigation::PlanDefinition& plan, bool creating) {
    Esp32BaseWeb::sendChunk("<form class='editform planform' method='post' action='");
    Esp32BaseWeb::sendChunk(creating ? "/api/v1/plan/create" : "/api/v1/plan/update");
    Esp32BaseWeb::sendChunk("' onsubmit=\"return irrPlanPrepare(this)&&confirm('确认保存计划？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    if (creating) {
        writeHiddenU32("zoneId", plan.zoneId);
    } else {
        writeHiddenU32("planId", plan.planId);
    }
    Esp32BaseWeb::sendChunk("<input type='hidden' name='cycleMask' value='");
    writeUInt(plan.cycleMask);
    Esp32BaseWeb::sendChunk("'><input type='hidden' name='durationSec' value='");
    writeUInt(plan.durationSec);
    Esp32BaseWeb::sendChunk("'><div class='fieldgrid'>"
                            "<p class='field med'><label>名称</label><input name='name' maxlength='31' value='");
    Esp32BaseWeb::writeHtmlEscaped(plan.name);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>状态</label><select name='enabled'><option value='0'");
    Esp32BaseWeb::sendChunk(plan.enabled ? "" : " selected");
    Esp32BaseWeb::sendChunk(">停用</option><option value='1'");
    Esp32BaseWeb::sendChunk(plan.enabled ? " selected" : "");
    Esp32BaseWeb::sendChunk(">启用</option></select></p><p class='field short'><label>开始时间</label><input name='time' type='time' value='");
    writeTime(plan.timeHour, plan.timeMinute);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>浇水时长（分钟）</label><input name='durationMin' type='number' min='1' max='");
    writeUInt(SystemConfigStore::current().maxWateringDurationSec / 60UL);
    Esp32BaseWeb::sendChunk("' value='");
    writeUInt(durationMinutesForUi(plan.durationSec));
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>循环天数</label><input name='cycleDays' type='number' min='1' max='30' value='");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk("' oninput='irrPlanCycle(this.form)'></p><p class='field short'><label>起始日期</label><input name='cycleStartYmd' type='date' value='");
    writeYmdInput(plan.cycleStartYmd);
    Esp32BaseWeb::sendChunk("'></p><div class='field full'><label>执行日</label><div class='radioopts'>");
    for (uint8_t i = 0; i < 30; ++i) {
        Esp32BaseWeb::sendChunk("<label data-cycle-day='");
        writeUInt(i);
        Esp32BaseWeb::sendChunk("'");
        if (i >= plan.cycleDays) {
            Esp32BaseWeb::sendChunk(" hidden");
        }
        Esp32BaseWeb::sendChunk("><input type='checkbox'");
        if ((plan.cycleMask & (1UL << i)) != 0) {
            Esp32BaseWeb::sendChunk(" checked");
        }
        Esp32BaseWeb::sendChunk(">第 ");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk(" 天</label>");
    }
    Esp32BaseWeb::sendChunk("</div></div></div><div class='actions'><input type='submit' value='保存'><a class='btnlink secondary' href='/irrigation/plans'>返回</a></div></form>"
                            "<script>"
                            "function irrPlanCycle(f){var n=parseInt(f.cycleDays.value||'0',10),ds=f.querySelectorAll('[data-cycle-day]');for(var i=0;i<ds.length;i++){ds[i].hidden=i>=n;}}"
                            "function irrPlanPrepare(f){var m=parseInt(f.durationMin.value||'0',10),n=parseInt(f.cycleDays.value||'0',10),mask=0,ds=f.querySelectorAll('[data-cycle-day]');if(!m||m<1){alert('浇水时长无效');return false;}f.durationSec.value=String(m*60);if(!n||n<1||n>30){alert('循环天数无效');return false;}for(var i=0;i<n&&i<ds.length;i++){if(ds[i].querySelector('input').checked)mask|=(1<<i);}if(!mask){alert('至少选择一个执行日');return false;}f.cycleMask.value=String(mask);return true;}"
                            "irrPlanCycle(document.currentScript.previousElementSibling);"
                            "</script>");
}

void handlePlanEditPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint32_t planId = 0;
    uint8_t zoneId = 0;
    Irrigation::PlanDefinition plan = {};
    const bool editing = Esp32BaseWeb::hasParam("planId");
    if (editing && (!readU32("planId", &planId) || !PlanStore::getById(planId, &plan))) {
        pageHeader("计划编辑");
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "计划不存在", "请返回计划页重新选择。");
        pageFooter();
        return;
    }
    if (!editing) {
        if (!readZoneId(&zoneId)) {
            pageHeader("新增计划");
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "水路不存在", "请返回计划页重新选择。");
            pageFooter();
            return;
        }
        plan = makeDraftPlan(zoneId);
    }
    pageHeader(editing ? "计划编辑" : "新增计划");
    Esp32BaseWeb::beginPanel(editing ? plan.name : ZoneManager::config(zoneId).name);
    writePlanForm(plan, !editing);
    Esp32BaseWeb::endPanel();
    pageFooter();
}

void handleSettingsPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint8_t editZoneId = 0;
    if (Esp32BaseWeb::hasParam("zoneId")) {
        if (!readZoneId(&editZoneId)) {
            pageHeader("水路管理");
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "水路不存在", "请返回水路管理页重新选择。");
            pageFooter();
            return;
        }
        const Irrigation::ZoneConfig& zone = ZoneManager::config(editZoneId);
        pageHeader("水路编辑");
        Esp32BaseWeb::sendPageTitle("水路编辑", "修改单个水路的名称、启用状态、流量校准和错误策略。");
        Esp32BaseWeb::beginPanel(zone.name);
        Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/zone/config' onsubmit=\"return confirm('确认保存水路配置？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", editZoneId);
        Esp32BaseWeb::sendChunk("<div class='fieldgrid'><p class='field med'><label>名称</label><input name='name' maxlength='31' value='");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>启用</label><select name='enabled'><option value='0'");
        Esp32BaseWeb::sendChunk(zone.enabled ? "" : " selected");
        Esp32BaseWeb::sendChunk(">停用</option><option value='1'");
        Esp32BaseWeb::sendChunk(zone.enabled ? " selected" : "");
        Esp32BaseWeb::sendChunk(">启用</option></select></p><p class='field short'><label>每升脉冲</label><input name='pulsePerLiter' type='number' min='1' max='10000' value='");
        writeUInt(zone.pulsePerLiter);
        Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>校准 x1000</label><input name='calibrationX1000' type='number' min='100' max='10000' value='");
        writeUInt(zone.calibrationX1000);
        Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>启动超时秒</label><input name='startTimeoutSec' type='number' min='1' max='300' value='");
        writeUInt(zone.startTimeoutSec);
        Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>无脉冲超时秒</label><input name='flowNoPulseTimeoutSec' type='number' min='1' max='300' value='");
        writeUInt(zone.flowNoPulseTimeoutSec);
        Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>抑制流量错误</label><select name='suppressError'><option value='0'");
        Esp32BaseWeb::sendChunk(zone.suppressError ? "" : " selected");
        Esp32BaseWeb::sendChunk(">否</option><option value='1'");
        Esp32BaseWeb::sendChunk(zone.suppressError ? " selected" : "");
        Esp32BaseWeb::sendChunk(">是</option></select></p></div><div class='actions'><input type='submit' value='保存水路配置'><a class='btnlink secondary' href='/irrigation/zones'>返回列表</a></div></form>");
        Esp32BaseWeb::endPanel();
        pageFooter();
        return;
    }

    pageHeader("水路管理");
    Esp32BaseWeb::sendPageTitle("水路管理", "查看 4 路水路状态；点击编辑进入单个水路配置。系统级参数在基础库 App Config 中设置。");
    Esp32BaseWeb::beginPanel("水路列表");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>水路</th><th>启用状态</th><th>阀门 GPIO</th><th>流量 GPIO</th><th>每升脉冲</th><th>校准</th><th>错误策略</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</td><td><span class='tag");
        Esp32BaseWeb::sendChunk(zone.enabled ? " ok" : "");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(zone.enabled ? "启用" : "停用");
        Esp32BaseWeb::sendChunk("</span></td><td>");
        writeUInt(zone.valvePin);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(zone.flowPin);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(zone.pulsePerLiter);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(zone.calibrationX1000);
        Esp32BaseWeb::sendChunk(" / 1000</td><td>");
        Esp32BaseWeb::sendChunk(zone.suppressError ? "只记录，不锁定异常" : "检测异常并锁定水路");
        Esp32BaseWeb::sendChunk("</td><td><a class='btnlink compact' href='/irrigation/zones?zoneId=");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>编辑</a></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    pageFooter();
}

struct RecordJsonContext {
    bool first;
};

void writeRecordJson(const RecordStore::WateringRecord& record, void* user) {
    RecordJsonContext* ctx = static_cast<RecordJsonContext*>(user);
    if (!ctx->first) Esp32BaseWeb::sendChunk(",");
    ctx->first = false;
    Esp32BaseWeb::sendChunk("{\"recordId\":");
    writeUInt(record.recordId);
    Esp32BaseWeb::sendChunk(",\"zoneId\":");
    writeUInt(record.zoneId);
    Esp32BaseWeb::sendChunk(",\"taskType\":\"");
    Esp32BaseWeb::writeJsonEscaped(Irrigation::taskTypeName(static_cast<Irrigation::TaskType>(record.taskType)));
    Esp32BaseWeb::sendChunk("\",\"result\":\"");
    Esp32BaseWeb::writeJsonEscaped(Irrigation::taskResultName(static_cast<Irrigation::TaskResult>(record.result)));
    Esp32BaseWeb::sendChunk("\",\"planId\":");
    writeUInt(record.planId);
    Esp32BaseWeb::sendChunk(",\"planName\":\"");
    Esp32BaseWeb::writeJsonEscaped(record.planNameSnapshot);
    Esp32BaseWeb::sendChunk("\",\"targetSec\":");
    writeUInt(record.targetSec);
    Esp32BaseWeb::sendChunk(",\"startedEpoch\":");
    writeUInt(record.startedEpoch);
    Esp32BaseWeb::sendChunk(",\"endedEpoch\":");
    writeUInt(record.endedEpoch);
    Esp32BaseWeb::sendChunk(",\"estimatedMl\":");
    writeUInt(record.estimatedMilliliters);
    Esp32BaseWeb::sendChunk("}");
}

struct EventJsonContext {
    bool first;
};

void writeEventJson(const EventStore::Event& event, void* user) {
    EventJsonContext* ctx = static_cast<EventJsonContext*>(user);
    if (!ctx->first) Esp32BaseWeb::sendChunk(",");
    ctx->first = false;
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(event.id);
    Esp32BaseWeb::sendChunk(",\"uptimeMs\":");
    writeUInt(event.uptimeMs);
    Esp32BaseWeb::sendChunk(",\"epoch\":");
    writeUInt(event.epoch);
    Esp32BaseWeb::sendChunk(",\"type\":\"");
    Esp32BaseWeb::writeJsonEscaped(EventStore::typeName(static_cast<Irrigation::EventType>(event.type)));
    Esp32BaseWeb::sendChunk("\",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(EventStore::sourceName(static_cast<Irrigation::EventSource>(event.source)));
    Esp32BaseWeb::sendChunk("\",\"zoneId\":");
    writeUInt(event.zoneId);
    Esp32BaseWeb::sendChunk(",\"code\":");
    writeUInt(event.code);
    Esp32BaseWeb::sendChunk(",\"text\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.text);
    Esp32BaseWeb::sendChunk("\"}");
}

struct RecordTableContext {
    bool wrote;
};

void writeRecordRow(const RecordStore::WateringRecord& record, void* user) {
    RecordTableContext* ctx = static_cast<RecordTableContext*>(user);
    ctx->wrote = true;
    Esp32BaseWeb::sendChunk("<tr><td>");
    writeUInt(record.recordId);
    Esp32BaseWeb::sendChunk("</td><td>");
    if (Irrigation::validZoneId(record.zoneId)) {
        Esp32BaseWeb::writeHtmlEscaped(ZoneManager::config(record.zoneId).name);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(taskTypeLabel(static_cast<Irrigation::TaskType>(record.taskType)));
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(taskResultLabel(static_cast<Irrigation::TaskResult>(record.result)));
    Esp32BaseWeb::sendChunk("</td><td>");
    if (record.planId == Irrigation::NoPlanId) {
        Esp32BaseWeb::sendChunk("-");
    } else {
        writeUInt(record.planId);
        Esp32BaseWeb::sendChunk(" ");
        Esp32BaseWeb::writeHtmlEscaped(record.planNameSnapshot);
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    writeDurationHuman(record.targetSec);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeDateTimeHuman(record.startedEpoch);
    Esp32BaseWeb::sendChunk("</td><td>");
    if (record.endedUptimeMs >= record.startedUptimeMs) {
        writeDurationMsHuman(record.endedUptimeMs - record.startedUptimeMs);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUInt(record.estimatedMilliliters);
    Esp32BaseWeb::sendChunk(" ml</td></tr>");
}

struct EventTableContext {
    bool wrote;
};

void writeEventRow(const EventStore::Event& event, void* user) {
    EventTableContext* ctx = static_cast<EventTableContext*>(user);
    ctx->wrote = true;
    Esp32BaseWeb::sendChunk("<tr><td>");
    writeUInt(event.id);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeDurationMsHuman(event.uptimeMs);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeDateTimeHuman(event.epoch);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(eventTypeLabel(static_cast<Irrigation::EventType>(event.type)));
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(eventSourceLabel(static_cast<Irrigation::EventSource>(event.source)));
    Esp32BaseWeb::sendChunk("</td><td>");
    if (Irrigation::validZoneId(event.zoneId)) {
        Esp32BaseWeb::writeHtmlEscaped(ZoneManager::config(event.zoneId).name);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    writeEventDetail(event);
    Esp32BaseWeb::sendChunk("</td></tr>");
}

void handleRecordsPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint32_t page = 1;
    uint32_t perPage = 20;
    uint16_t offset = 0;
    readPaging(RecordStore::count(), 20, &page, &perPage, &offset);
    pageHeader("浇水记录");
    Esp32BaseWeb::sendPageTitle("浇水记录", "按时间倒序展示浇水执行记录。");
    Esp32BaseWeb::beginPanel("列表");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>ID</th><th>水路</th><th>类型</th><th>结果</th><th>计划</th><th>目标时长</th><th>开始时间</th><th>运行时间</th><th>估算水量</th></tr></thead><tbody>");
    RecordTableContext ctx = {false};
    (void)RecordStore::readLatest(offset, static_cast<uint16_t>(perPage), writeRecordRow, &ctx);
    if (!ctx.wrote) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='9'>暂无记录</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::sendPagination({"/irrigation/records", nullptr, page, perPage, RecordStore::count()});
    Esp32BaseWeb::endPanel();
    pageFooter();
}

void handleEventsPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint32_t page = 1;
    uint32_t perPage = 20;
    uint16_t offset = 0;
    readPaging(EventStore::count(), 20, &page, &perPage, &offset);
    pageHeader("事件记录");
    Esp32BaseWeb::sendPageTitle("事件记录", "按时间倒序展示系统和业务事件。");
    Esp32BaseWeb::beginPanel("列表");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>ID</th><th>运行时间</th><th>实际时间</th><th>类型</th><th>来源</th><th>水路</th><th>说明</th></tr></thead><tbody>");
    EventTableContext ctx = {false};
    (void)EventStore::readLatest(offset, static_cast<uint16_t>(perPage), writeEventRow, &ctx);
    if (!ctx.wrote) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>暂无事件</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::sendPagination({"/irrigation/events", nullptr, page, perPage, EventStore::count()});
    Esp32BaseWeb::endPanel();
    pageFooter();
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"timeSynced\":");
#if ESP32BASE_ENABLE_NTP
    writeBool(Esp32BaseNtp::isTimeSynced());
#else
    writeBool(false);
#endif
    Esp32BaseWeb::sendChunk(",\"leakAlertActive\":");
    writeBool(ZoneManager::leakAlertActive());
    Esp32BaseWeb::sendChunk(",\"zones\":[");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        if (zoneId > 1) Esp32BaseWeb::sendChunk(",");
        writeStatusJson(ZoneManager::status(zoneId));
    }
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

void handleConfigApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        const Irrigation::SystemConfig& system = SystemConfigStore::current();
        beginJson(200);
        Esp32BaseWeb::sendChunk("{\"ok\":true,\"system\":{\"maxWateringDurationSec\":");
        writeUInt(system.maxWateringDurationSec);
        Esp32BaseWeb::sendChunk(",\"scheduleGraceSec\":");
        writeUInt(system.scheduleGraceSec);
        Esp32BaseWeb::sendChunk(",\"manualDefaultDurationSec\":");
        writeUInt(system.manualDefaultDurationSec);
        Esp32BaseWeb::sendChunk(",\"idleLeakWindowSec\":");
        writeUInt(system.idleLeakWindowSec);
        Esp32BaseWeb::sendChunk(",\"idleLeakPulseThreshold\":");
        writeUInt(system.idleLeakPulseThreshold);
        Esp32BaseWeb::sendChunk(",\"durationPresets\":[");
        for (uint8_t i = 0; i < 6; ++i) {
            if (i) Esp32BaseWeb::sendChunk(",");
            writeUInt(system.durationPresets[i]);
        }
        Esp32BaseWeb::sendChunk("]}}");
        endJson();
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("GET,POST");
        return;
    }
    Irrigation::SystemConfig system = SystemConfigStore::current();
    if (Esp32BaseWeb::hasParam("maxWateringDurationSec") && !readU32("maxWateringDurationSec", &system.maxWateringDurationSec)) sendError(400, "invalid_max_duration");
    else if (Esp32BaseWeb::hasParam("scheduleGraceSec") && !readU16("scheduleGraceSec", &system.scheduleGraceSec)) sendError(400, "invalid_schedule_grace");
    else if (Esp32BaseWeb::hasParam("manualDefaultDurationSec") && !readU32("manualDefaultDurationSec", &system.manualDefaultDurationSec)) sendError(400, "invalid_manual_default");
    else if (Esp32BaseWeb::hasParam("idleLeakWindowSec") && !readU16("idleLeakWindowSec", &system.idleLeakWindowSec)) sendError(400, "invalid_leak_window");
    else if (Esp32BaseWeb::hasParam("idleLeakPulseThreshold") && !readU16("idleLeakPulseThreshold", &system.idleLeakPulseThreshold)) sendError(400, "invalid_leak_threshold");
    else {
        bool parsed = true;
        for (uint8_t i = 0; i < 6; ++i) {
            char key[12];
            snprintf(key, sizeof(key), "preset%u", static_cast<unsigned>(i));
            if (Esp32BaseWeb::hasParam(key) && !readU32(key, &system.durationPresets[i])) {
                parsed = false;
            }
        }
        if (!parsed) {
            sendError(400, "invalid_preset");
        } else if (!SystemConfigStore::set(system)) {
            sendError(400, "invalid_system_config");
        } else {
            redirectOrOk("/esp32base/app-config");
        }
    }
}

void handleZoneStartApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint8_t zoneId = 0;
    uint32_t durationSec = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_start_request");
        return;
    }
    if (Esp32BaseWeb::hasParam("durationMin")) {
        uint32_t durationMin = 0;
        if (!readU32("durationMin", &durationMin) || durationMin == 0 || durationMin > (SystemConfigStore::current().maxWateringDurationSec / 60UL)) {
            sendError(400, "invalid_start_duration");
            return;
        }
        durationSec = durationMin * 60UL;
    } else if (!readU32("durationSec", &durationSec)) {
        sendError(400, "invalid_start_request");
        return;
    }
    if (!ZoneManager::startManual(zoneId, durationSec, startSourceFromRequest())) {
        sendError(409, "zone_start_rejected");
        return;
    }
    redirectOrOk();
}

void handleZoneStopApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    (void)ZoneManager::stopZone(zoneId, stopSourceFromRequest());
    redirectOrOk();
}

void handleZonesStopAllApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    (void)ZoneManager::stopAll(stopSourceFromRequest());
    redirectOrOk();
}

void handleZoneConfigApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    Irrigation::ZoneConfig zone = ZoneConfigStore::get(zoneId);
    if (Esp32BaseWeb::hasParam("name") && !Esp32BaseWeb::getParam("name", zone.name, sizeof(zone.name))) sendError(400, "invalid_name");
    else if (Esp32BaseWeb::hasParam("enabled") && !readBool("enabled", &zone.enabled)) sendError(400, "invalid_enabled");
    else if (Esp32BaseWeb::hasParam("pulsePerLiter") && !readU16("pulsePerLiter", &zone.pulsePerLiter)) sendError(400, "invalid_pulse_per_liter");
    else if (Esp32BaseWeb::hasParam("calibrationX1000") && !readU16("calibrationX1000", &zone.calibrationX1000)) sendError(400, "invalid_calibration");
    else if (Esp32BaseWeb::hasParam("startTimeoutSec") && !readU16("startTimeoutSec", &zone.startTimeoutSec)) sendError(400, "invalid_start_timeout");
    else if (Esp32BaseWeb::hasParam("flowNoPulseTimeoutSec") && !readU16("flowNoPulseTimeoutSec", &zone.flowNoPulseTimeoutSec)) sendError(400, "invalid_no_pulse_timeout");
    else if (Esp32BaseWeb::hasParam("suppressError") && !readBool("suppressError", &zone.suppressError)) sendError(400, "invalid_suppress_error");
    else if (!ZoneConfigStore::set(zoneId, zone)) sendError(400, "invalid_zone_config");
    else {
        (void)ZoneManager::reloadZone(zoneId);
        redirectOrOk("/irrigation/zones");
    }
}

void handleZoneClearErrorApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    if (!ZoneManager::clearError(zoneId)) {
        sendError(400, "clear_error_failed");
        return;
    }
    redirectOrOk();
}

void handlePlansApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    uint8_t zoneId = 0;
    const bool filter = Esp32BaseWeb::hasParam("zoneId");
    if (filter && !readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"plans\":[");
    bool first = true;
    for (uint8_t z = 1; z <= Irrigation::MaxZones; ++z) {
        if (filter && z != zoneId) continue;
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(z, slot);
            if (!plan.exists) continue;
            if (!first) Esp32BaseWeb::sendChunk(",");
            first = false;
            writePlanJson(plan);
        }
    }
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

void handlePlanCreateApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint8_t zoneId = 0;
    Irrigation::PlanDefinition plan = {};
    const char* error = nullptr;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    plan = makeDraftPlan(zoneId);
    if (!applyPlanForm(plan, &error)) {
        sendError(400, error);
        return;
    }
    if (!PlanStore::create(zoneId, plan, &plan)) {
        sendError(400, "plan_create_failed");
        return;
    }
    if (wantsRedirect()) {
        char path[48];
        snprintf(path, sizeof(path), "/irrigation/plan?planId=%lu", static_cast<unsigned long>(plan.planId));
        Esp32BaseWeb::redirectSeeOther(path);
        return;
    }
    beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"plan\":");
    writePlanJson(plan);
    Esp32BaseWeb::sendChunk("}");
    endJson();
}

bool updatePlanEnabled(bool enabled) {
    uint32_t planId = 0;
    Irrigation::PlanDefinition plan = {};
    if (!readU32("planId", &planId) || !PlanStore::getById(planId, &plan)) {
        sendError(400, "plan_not_found");
        return false;
    }
    plan.enabled = enabled;
    if (!PlanStore::set(planId, plan)) {
        sendError(400, "plan_save_failed");
        return false;
    }
    redirectOrOk("/irrigation/plans");
    return true;
}

void handlePlanEnableApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    (void)updatePlanEnabled(true);
}

void handlePlanDisableApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    (void)updatePlanEnabled(false);
}

void handlePlanDeleteApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint32_t planId = 0;
    if (!readU32("planId", &planId) || !PlanStore::remove(planId)) {
        sendError(400, "plan_delete_failed");
        return;
    }
    redirectOrOk("/irrigation/plans");
}

void handlePlanUpdateApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint32_t planId = 0;
    Irrigation::PlanDefinition plan = {};
    if (!readU32("planId", &planId) || !PlanStore::getById(planId, &plan)) {
        sendError(400, "plan_not_found");
        return;
    }
    const char* error = nullptr;
    if (!applyPlanForm(plan, &error)) {
        sendError(400, error);
    } else if (!PlanStore::set(planId, plan)) {
        sendError(400, "plan_save_failed");
    } else {
        redirectOrOk("/irrigation/plans");
    }
}

void handleScheduleSkipApi(bool skip) {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint32_t planId = 0;
    uint32_t ymd = 0;
    uint8_t reasonRaw = 0;
    if (!readU32("planId", &planId) || !readYmd("ymd", &ymd)) {
        sendError(400, "invalid_skip_request");
        return;
    }
    if (!Esp32BaseWeb::hasParam("reason")) {
        reasonRaw = static_cast<uint8_t>(Irrigation::SkipReason::MANUAL);
    } else if (!readU8("reason", &reasonRaw) || reasonRaw > static_cast<uint8_t>(Irrigation::SkipReason::WEATHER)) {
        sendError(400, "invalid_skip_reason");
        return;
    }
    const bool ok = skip
        ? ScheduleSkipStore::skip(planId, ymd, static_cast<Irrigation::SkipReason>(reasonRaw))
        : ScheduleSkipStore::unskip(planId, ymd);
    if (!ok) {
        sendError(400, "skip_update_failed");
        return;
    }
    redirectOrOk("/irrigation/plans");
}

void handleScheduleSkipApi() {
    handleScheduleSkipApi(true);
}

void handleScheduleUnskipApi() {
    handleScheduleSkipApi(false);
}

void handleRecordsApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"count\":");
    writeUInt(RecordStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(RecordStore::capacity());
    Esp32BaseWeb::sendChunk(",\"records\":[");
    RecordJsonContext ctx = {true};
    (void)RecordStore::readLatest(0, 50, writeRecordJson, &ctx);
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

void handleEventsApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"count\":");
    writeUInt(EventStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(EventStore::capacity());
    Esp32BaseWeb::sendChunk(",\"events\":[");
    EventJsonContext ctx = {true};
    (void)EventStore::readLatest(0, 50, writeEventJson, &ctx);
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

void handleFactoryResetApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    bool clearRecords = false;
    if (Esp32BaseWeb::hasParam("clearRecords") && !readBool("clearRecords", &clearRecords)) {
        sendError(400, "invalid_clear_records");
        return;
    }
    if (!MaintenanceService::requestFactoryReset(clearRecords)) {
        sendError(409, "factory_reset_pending");
        return;
    }
    redirectOrOk("/irrigation");
}

}

namespace IrrigationWeb {

void begin() {
    const bool overviewOk = Esp32BaseWeb::addPage("/irrigation", "灌溉", handleOverviewPage);
    const bool plansOk = Esp32BaseWeb::addPage("/irrigation/plans", "计划", handlePlansPage);
    const bool settingsOk = Esp32BaseWeb::addPage("/irrigation/zones", "水路管理", handleSettingsPage);
    const bool recordsPageOk = Esp32BaseWeb::addPage("/irrigation/records", "浇水记录", handleRecordsPage);
    const bool eventsPageOk = Esp32BaseWeb::addPage("/irrigation/events", "事件记录", handleEventsPage);
    const bool planEditOk = Esp32BaseWeb::addRoute("/irrigation/plan", Esp32BaseWeb::METHOD_GET, handlePlanEditPage);
    const bool statusOk = Esp32BaseWeb::addRoute("/api/v1/status", Esp32BaseWeb::METHOD_GET, handleStatusApi);
    const bool configOk = Esp32BaseWeb::addApi("/api/v1/config", handleConfigApi);
    const bool zoneStartOk = Esp32BaseWeb::addRoute("/api/v1/zone/start", Esp32BaseWeb::METHOD_POST, handleZoneStartApi);
    const bool zoneStopOk = Esp32BaseWeb::addRoute("/api/v1/zone/stop", Esp32BaseWeb::METHOD_POST, handleZoneStopApi);
    const bool allStopOk = Esp32BaseWeb::addRoute("/api/v1/zones/stop-all", Esp32BaseWeb::METHOD_POST, handleZonesStopAllApi);
    const bool zoneConfigOk = Esp32BaseWeb::addRoute("/api/v1/zone/config", Esp32BaseWeb::METHOD_POST, handleZoneConfigApi);
    const bool clearErrorOk = Esp32BaseWeb::addRoute("/api/v1/zone/clear-error", Esp32BaseWeb::METHOD_POST, handleZoneClearErrorApi);
    const bool plansApiOk = Esp32BaseWeb::addRoute("/api/v1/plans", Esp32BaseWeb::METHOD_GET, handlePlansApi);
    const bool planCreateOk = Esp32BaseWeb::addRoute("/api/v1/plan/create", Esp32BaseWeb::METHOD_POST, handlePlanCreateApi);
    const bool planUpdateOk = Esp32BaseWeb::addRoute("/api/v1/plan/update", Esp32BaseWeb::METHOD_POST, handlePlanUpdateApi);
    const bool planDeleteOk = Esp32BaseWeb::addRoute("/api/v1/plan/delete", Esp32BaseWeb::METHOD_POST, handlePlanDeleteApi);
    const bool planEnableOk = Esp32BaseWeb::addRoute("/api/v1/plan/enable", Esp32BaseWeb::METHOD_POST, handlePlanEnableApi);
    const bool planDisableOk = Esp32BaseWeb::addRoute("/api/v1/plan/disable", Esp32BaseWeb::METHOD_POST, handlePlanDisableApi);
    const bool skipOk = Esp32BaseWeb::addRoute("/api/v1/schedule/skip", Esp32BaseWeb::METHOD_POST, handleScheduleSkipApi);
    const bool unskipOk = Esp32BaseWeb::addRoute("/api/v1/schedule/unskip", Esp32BaseWeb::METHOD_POST, handleScheduleUnskipApi);
    const bool recordsOk = Esp32BaseWeb::addRoute("/api/v1/records", Esp32BaseWeb::METHOD_GET, handleRecordsApi);
    const bool eventsOk = Esp32BaseWeb::addRoute("/api/v1/events", Esp32BaseWeb::METHOD_GET, handleEventsApi);
    const bool factoryOk = Esp32BaseWeb::addRoute("/api/v1/maintenance/factory-reset", Esp32BaseWeb::METHOD_POST, handleFactoryResetApi);
    ESP32BASE_LOG_I("irrigation.web", "routes overview=%s plans=%s zones=%s recordsPage=%s eventsPage=%s planEdit=%s status=%s config=%s zoneStart=%s zoneStop=%s allStop=%s zoneConfig=%s clearError=%s plansApi=%s planCreate=%s planUpdate=%s planDelete=%s planEnable=%s planDisable=%s skip=%s unskip=%s records=%s events=%s factory=%s firmware=%s",
                    overviewOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    settingsOk ? "ok" : "fail",
                    recordsPageOk ? "ok" : "fail",
                    eventsPageOk ? "ok" : "fail",
                    planEditOk ? "ok" : "fail",
                    statusOk ? "ok" : "fail",
                    configOk ? "ok" : "fail",
                    zoneStartOk ? "ok" : "fail",
                    zoneStopOk ? "ok" : "fail",
                    allStopOk ? "ok" : "fail",
                    zoneConfigOk ? "ok" : "fail",
                    clearErrorOk ? "ok" : "fail",
                    plansApiOk ? "ok" : "fail",
                    planCreateOk ? "ok" : "fail",
                    planUpdateOk ? "ok" : "fail",
                    planDeleteOk ? "ok" : "fail",
                    planEnableOk ? "ok" : "fail",
                    planDisableOk ? "ok" : "fail",
                    skipOk ? "ok" : "fail",
                    unskipOk ? "ok" : "fail",
                    recordsOk ? "ok" : "fail",
                    eventsOk ? "ok" : "fail",
                    factoryOk ? "ok" : "fail",
                    IrrigationVersion::FirmwareVersion);
}

}
