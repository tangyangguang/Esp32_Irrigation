#include "web/IrrigationWeb.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Version.h"
#include "domain/BusinessEventLog.h"
#include "domain/FlowCalibration.h"
#include "domain/FlowMeter.h"
#include "domain/MaintenanceService.h"
#include "domain/SafetyManager.h"
#include "domain/ZoneManager.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/ScheduleSkipStore.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"
#include "storage/ZoneErrorStore.h"

namespace {

void writeUInt(uint32_t value) {
    char text[16];
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(text);
}

void writeInt(int32_t value) {
    char text[16];
    snprintf(text, sizeof(text), "%ld", static_cast<long>(value));
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

bool rejectFactoryResetPending() {
    if (!MaintenanceService::factoryResetPending()) {
        return false;
    }
    sendError(409, "factory_reset_pending");
    return true;
}

bool checkBusinessPost(const char* context, bool allowFactoryResetPending = false) {
    if (!Esp32BaseWeb::checkPostAllowed(context)) {
        return false;
    }
    if (!allowFactoryResetPending && rejectFactoryResetPending()) {
        return false;
    }
    return true;
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

bool readCheckbox(const char* name) {
    bool value = false;
    return Esp32BaseWeb::hasParam(name) && readBool(name, &value) && value;
}

bool readZoneId(uint8_t* zoneId) {
    return readU8("zoneId", zoneId) && Irrigation::validZoneId(*zoneId);
}

const char* zoneErrorLabel(Irrigation::ZoneErrorCode code);
const char* taskTypeLabel(Irrigation::TaskType type);

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

void redirectOrOk(const char* path = "/index") {
    if (wantsRedirect() && !Esp32BaseWeb::isAjaxRequest()) {
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
    Esp32BaseWeb::sendChunk(",\"errorCode\":\"");
    Esp32BaseWeb::writeJsonEscaped(zoneErrorLabel(status.errorCode));
    Esp32BaseWeb::sendChunk("\"");
    Esp32BaseWeb::sendChunk(",\"taskType\":\"");
    Esp32BaseWeb::writeJsonEscaped(Irrigation::taskTypeName(status.taskType));
    Esp32BaseWeb::sendChunk("\",\"taskLabel\":\"");
    Esp32BaseWeb::writeJsonEscaped(status.busy ? taskTypeLabel(status.taskType) : "-");
    Esp32BaseWeb::sendChunk("\"");
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
    Esp32BaseWeb::sendChunk(",\"flowMlPerMin\":");
    writeUInt(status.flowMlPerMin);
    Esp32BaseWeb::sendChunk(",\"flowRateReady\":");
    writeBool(status.flowRateReady);
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
        used += snprintf(out + used, len - used, "%lu小时", static_cast<unsigned long>(hours));
    }
    if (used < len && minutes > 0) {
        used += snprintf(out + used, len - used, "%lu分钟", static_cast<unsigned long>(minutes));
    }
    if (used < len && (secs > 0 || used == 0)) {
        (void)snprintf(out + used, len - used, "%lu秒", static_cast<unsigned long>(secs));
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

void writeDurationMsHumanCompact(uint32_t ms) {
    writeDurationHuman((ms + 999UL) / 1000UL);
}

void writeLitersFromMilliliters(uint32_t milliliters) {
    char text[24];
    snprintf(text, sizeof(text), "%lu.%03lu L",
             static_cast<unsigned long>(milliliters / 1000UL),
             static_cast<unsigned long>(milliliters % 1000UL));
    Esp32BaseWeb::sendChunk(text);
}

void writeFlowRate(uint32_t mlPerMin) {
    char text[28];
    snprintf(text, sizeof(text), "%lu.%03lu L/min",
             static_cast<unsigned long>(mlPerMin / 1000UL),
             static_cast<unsigned long>(mlPerMin % 1000UL));
    Esp32BaseWeb::sendChunk(text);
}

uint32_t averageFlowMlPerMin(const RecordStore::WateringRecord& record) {
    if (record.endedUptimeMs <= record.startedUptimeMs) {
        return 0;
    }
    const uint32_t durationMs = record.endedUptimeMs - record.startedUptimeMs;
    return static_cast<uint32_t>((static_cast<uint64_t>(record.estimatedMilliliters) * 60000ULL) / durationMs);
}

void writeAverageFlowRate(const RecordStore::WateringRecord& record) {
    if (record.endedUptimeMs <= record.startedUptimeMs) {
        Esp32BaseWeb::sendChunk("-");
        return;
    }
    writeFlowRate(averageFlowMlPerMin(record));
}

void writeRecordFlowAt(uint32_t elapsedSec) {
    char text[16];
    snprintf(text, sizeof(text), "%02lu:%02lu",
             static_cast<unsigned long>(elapsedSec / 60UL),
             static_cast<unsigned long>(elapsedSec % 60UL));
    Esp32BaseWeb::sendChunk(text);
}

void writeRecordPeakFlow(uint32_t mlPerMin, uint32_t firstAtSec, bool valid) {
    if (!valid) {
        Esp32BaseWeb::sendChunk("-");
        return;
    }
    writeFlowRate(mlPerMin);
    Esp32BaseWeb::sendChunk(" @ ");
    writeRecordFlowAt(firstAtSec);
}

const char* calibrationStateLabel(FlowCalibration::State state) {
    switch (state) {
        case FlowCalibration::State::CAPTURING: return "采集中";
        case FlowCalibration::State::WAITING_ACTUAL: return "等待输入水量";
        case FlowCalibration::State::IDLE:
        default: return "空闲";
    }
}

static constexpr uint32_t calibrationWindowMs = 2000UL;
static constexpr uint32_t calibrationWindowStepMs = 200UL;

void writePermilleAsPercent(uint16_t permille) {
    writeUInt(permille / 10U);
    Esp32BaseWeb::sendChunk(".");
    writeUInt(permille % 10U);
    Esp32BaseWeb::sendChunk("%");
}

const char* calibrationRecommendationAdvice(const FlowCalibration::Recommendation& rec) {
    if (rec.sampleCount <= 1) {
        return "单条样本只能生成单点估计，无法交叉验证误差；建议补采不同水量样本。";
    }
    if (rec.sampleCount == 2) {
        if (rec.volumeSpanPermille < 200 || rec.pulseSpanPermille < 200) {
            return "两条样本水量或脉冲跨度偏小，建议补采一条明显不同水量的样本。";
        }
        if (rec.averageErrorPermille <= 50 && rec.maxErrorPermille <= 80) {
            return "两条样本拟合误差较小，可试用；建议补采一条不同水量样本做验证。";
        }
        return "两条样本拟合误差偏大，建议重采或检查接水量、流量稳定性。";
    }
    if (rec.averageErrorPermille <= 50 && rec.maxErrorPermille <= 80) {
        return "样本内误差较小，建议可应用。";
    }
    if (rec.averageErrorPermille <= 100 && rec.maxErrorPermille <= 150) {
        return "样本内误差可接受，可应用但建议复核。";
    }
    return "样本误差偏大，不建议应用；建议重采或检查接水量、流量稳定性。";
}

bool readFlowParameters(Irrigation::FlowParameters* out) {
    if (!out) {
        return false;
    }
    Irrigation::FlowParameters params = {};
    if (!readU16("startupPulseLimit", &params.startupPulseLimit) ||
        !readU16("startupEstimatedMl", &params.startupEstimatedMl) ||
        !readU16("stablePulsePerLiter", &params.stablePulsePerLiter)) {
        return false;
    }
    params = ZoneConfigStore::normalizeFlowParameters(params);
    if (!ZoneConfigStore::validateFlowParameters(params)) {
        return false;
    }
    *out = params;
    return true;
}

void writeFlowParameterLine(const Irrigation::FlowParameters& params) {
    Esp32BaseWeb::sendChunk("<div class='calibration-param-line'><span class='param'><span>启动脉冲</span><span class='value'>");
    writeUInt(params.startupPulseLimit);
    Esp32BaseWeb::sendChunk("</span></span><span class='param'><span>启动水量</span><span class='value'>");
    writeUInt(params.startupEstimatedMl);
    Esp32BaseWeb::sendChunk(" ml</span></span><span class='param'><span>稳定脉冲</span><span class='value'>");
    writeUInt(params.stablePulsePerLiter);
    Esp32BaseWeb::sendChunk(" P/L</span></span></div>");
}

void writeFlowParameterInputs(const Irrigation::FlowParameters& params) {
    Esp32BaseWeb::sendChunk("<p class='field short'><label>启动阶段脉冲</label><input name='startupPulseLimit' type='number' min='0' max='10000' value='");
    writeUInt(params.startupPulseLimit);
    Esp32BaseWeb::sendChunk("' required></p><p class='field short'><label>启动阶段水量 ml</label><input name='startupEstimatedMl' type='number' min='0' max='10000' value='");
    writeUInt(params.startupEstimatedMl);
    Esp32BaseWeb::sendChunk("' required></p><p class='field short'><label>稳定脉冲 P/L</label><input name='stablePulsePerLiter' type='number' min='1' max='10000' value='");
    writeUInt(params.stablePulsePerLiter);
    Esp32BaseWeb::sendChunk("' required></p>");
}

void writeFlowParameterLifecyclePanel() {
    Esp32BaseWeb::beginPanel("流量参数");
    Esp32BaseWeb::sendChunk("<div class='calibration-zone-grid'>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
        const bool busy = ZoneManager::isZoneBusy(zoneId);
        const bool candidateMatches = zone.candidateFlow.exists && ZoneConfigStore::flowParametersEqual(zone.flow, zone.candidateFlow.params);
        Esp32BaseWeb::sendChunk("<section class='calibration-zone-card'><div class='calibration-zone-head'><div><h3>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</h3><span class='tag");
        Esp32BaseWeb::sendChunk(zone.enabled ? " ok" : "");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(zone.enabled ? "启用" : "停用");
        Esp32BaseWeb::sendChunk("</span>");
        if (busy) {
            Esp32BaseWeb::sendChunk(" <span class='tag warn'>运行中</span>");
        }
        Esp32BaseWeb::sendChunk("</div></div><div class='calibration-param-grid'>");

        Esp32BaseWeb::sendChunk("<div class='calibration-param-card current'><div class='calibration-param-head'><h4>当前参数</h4><span class='tag ok'>正在使用</span></div>");
        writeFlowParameterLine(zone.flow);
        Esp32BaseWeb::sendChunk("</div>");

        Esp32BaseWeb::sendChunk("<div class='calibration-param-card candidate'><div class='calibration-param-head'><h4>候选参数</h4>");
        if (zone.candidateFlow.exists && !candidateMatches) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/calibration/apply' onsubmit=\"return confirm('确认将候选参数设为当前参数？当前参数会保存为上一套。')&&once(this)\">");
            writeOnePostHidden("source", "web_page");
            writeHiddenU32("zoneId", zoneId);
            Esp32BaseWeb::sendChunk("<input class='btnlink compact ok'");
            if (busy) {
                Esp32BaseWeb::sendChunk(" disabled");
            }
            Esp32BaseWeb::sendChunk(" type='submit' value='设为当前'></form>");
        } else if (zone.candidateFlow.exists) {
            Esp32BaseWeb::sendChunk("<span class='tag'>与当前一致</span>");
        }
        Esp32BaseWeb::sendChunk("</div>");
        if (zone.candidateFlow.exists) {
            writeFlowParameterLine(zone.candidateFlow.params);
            Esp32BaseWeb::sendChunk("<p class='calibration-param-note'>");
            Esp32BaseWeb::sendChunk(candidateMatches ? "候选参数与当前参数一致。" : "候选参数已保存，尚未设为当前。");
            Esp32BaseWeb::sendChunk("</p>");
        } else {
            Esp32BaseWeb::sendChunk("<p class='calibration-param-note'>暂无候选参数。</p>");
        }
        Esp32BaseWeb::sendChunk("<div class='actions'><button type='button' class='btnlink compact secondary' onclick='calibrationCandidateOpen(");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk(")'>");
        Esp32BaseWeb::sendChunk(zone.candidateFlow.exists ? "编辑候选" : "创建候选");
        Esp32BaseWeb::sendChunk("</button></div></div>");

        Esp32BaseWeb::sendChunk("<div class='calibration-param-card previous'><div class='calibration-param-head'><h4>上一套参数</h4>");
        if (zone.previousFlowExists && !ZoneConfigStore::flowParametersEqual(zone.flow, zone.previousFlow)) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/calibration/previous/restore' onsubmit=\"return confirm('确认将上一套参数设为当前参数？当前参数会与上一套互换。')&&once(this)\">");
            writeOnePostHidden("source", "web_page");
            writeHiddenU32("zoneId", zoneId);
            Esp32BaseWeb::sendChunk("<input class='btnlink compact secondary'");
            if (busy) {
                Esp32BaseWeb::sendChunk(" disabled");
            }
            Esp32BaseWeb::sendChunk(" type='submit' value='设为当前'></form>");
        }
        Esp32BaseWeb::sendChunk("</div>");
        if (zone.previousFlowExists) {
            writeFlowParameterLine(zone.previousFlow);
            if (ZoneConfigStore::flowParametersEqual(zone.flow, zone.previousFlow)) {
                Esp32BaseWeb::sendChunk("<p class='calibration-param-note'>上一套参数与当前参数一致。</p>");
            }
        } else {
            Esp32BaseWeb::sendChunk("<p class='calibration-param-note'>暂无上一套参数。</p>");
        }
        Esp32BaseWeb::sendChunk("</div></div></section>");

        const Irrigation::FlowParameters formParams = zone.candidateFlow.exists ? zone.candidateFlow.params : zone.flow;
        Esp32BaseWeb::sendChunk("<dialog id='calibrationCandidateDialog");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("' class='calibration-dialog calibration-candidate-dialog'><form class='editform' method='post' action='/api/v1/calibration/candidate' onsubmit=\"return confirm('确认保存候选参数？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
        Esp32BaseWeb::sendChunk("<h3>");
        Esp32BaseWeb::sendChunk(zone.candidateFlow.exists ? "编辑候选参数" : "创建候选参数");
        Esp32BaseWeb::sendChunk("</h3><p class='calibration-param-note'>候选参数只是待确认的参数值，不会改变当前运行估算。</p><div class='calibration-copy-fill'><label>从其他水路填入</label><div class='calibration-fill-row'><select id='calibrationFillSource");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        for (uint8_t fillZoneId = 1; fillZoneId <= Irrigation::MaxZones; ++fillZoneId) {
            if (fillZoneId == zoneId) {
                continue;
            }
            const Irrigation::FlowParameters& src = ZoneManager::config(fillZoneId).flow;
            Esp32BaseWeb::sendChunk("<option value='");
            writeUInt(fillZoneId);
            Esp32BaseWeb::sendChunk("' data-startup-pulse='");
            writeUInt(src.startupPulseLimit);
            Esp32BaseWeb::sendChunk("' data-startup-ml='");
            writeUInt(src.startupEstimatedMl);
            Esp32BaseWeb::sendChunk("' data-stable-pulse='");
            writeUInt(src.stablePulsePerLiter);
            Esp32BaseWeb::sendChunk("'>");
            Esp32BaseWeb::writeHtmlEscaped(ZoneManager::config(fillZoneId).name);
            Esp32BaseWeb::sendChunk("</option>");
        }
        Esp32BaseWeb::sendChunk("</select><button type='button' class='btnlink compact secondary' onclick='calibrationCandidateFill(");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk(")'>填入表单</button></div></div><div class='fieldgrid'>");
        writeFlowParameterInputs(formParams);
        Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='submit' value='保存为候选'><button class='btnlink secondary' type='button' onclick='calibrationCandidateClose(");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk(")'>取消</button></div></form></dialog>");
    }
    Esp32BaseWeb::sendChunk("</div><p class='calibration-param-note'>当前参数正在用于运行估算；候选参数只保存最终参数值，可手工输入、从其他水路填入后保存，或由校准样本生成。设为当前前会自动保存当前参数为上一套参数。</p>"
                            "<script>"
                            "function calibrationCandidateDialog(id){return document.getElementById('calibrationCandidateDialog'+id);}"
                            "function calibrationCandidateOpen(id){var d=calibrationCandidateDialog(id);if(!d)return;if(d.showModal)d.showModal();else d.setAttribute('open','open');}"
                            "function calibrationCandidateClose(id){var d=calibrationCandidateDialog(id);if(d)d.close?d.close():d.removeAttribute('open');}"
                            "function calibrationCandidateFill(id){var s=document.getElementById('calibrationFillSource'+id),d=calibrationCandidateDialog(id);if(!s||!d||!s.selectedOptions.length)return;var o=s.selectedOptions[0];d.querySelector('[name=startupPulseLimit]').value=o.dataset.startupPulse||0;d.querySelector('[name=startupEstimatedMl]').value=o.dataset.startupMl||0;d.querySelector('[name=stablePulsePerLiter]').value=o.dataset.stablePulse||450;}"
                            "</script>");
    Esp32BaseWeb::endPanel();
}

void writeSvgPoint(uint32_t x, uint32_t y) {
    char text[32];
    snprintf(text, sizeof(text), "%lu,%lu ", static_cast<unsigned long>(x), static_cast<unsigned long>(y));
    Esp32BaseWeb::sendChunk(text);
}

void writeChartTimeLabel(uint32_t ms) {
    if (ms < 10000UL && (ms % 1000UL) != 0) {
        writeUInt(ms / 1000UL);
        Esp32BaseWeb::sendChunk(".");
        writeUInt((ms % 1000UL) / 100UL);
        Esp32BaseWeb::sendChunk("秒");
        return;
    }
    writeUInt((ms + 500UL) / 1000UL);
    Esp32BaseWeb::sendChunk("秒");
}

void writeChartAxisTitle(const char* xTitle,
                         const char* yTitle,
                         uint32_t left,
                         uint32_t top,
                         uint32_t width,
                         uint32_t height,
                         uint32_t svgHeight) {
    Esp32BaseWeb::sendChunk("<text class='chart-axis-title' x='");
    writeUInt(left + width / 2UL);
    Esp32BaseWeb::sendChunk("' y='");
    writeUInt(svgHeight - 10UL);
    Esp32BaseWeb::sendChunk("' text-anchor='middle'>");
    Esp32BaseWeb::writeHtmlEscaped(xTitle);
    Esp32BaseWeb::sendChunk("</text><text class='chart-axis-title' x='");
    writeUInt(14);
    Esp32BaseWeb::sendChunk("' y='");
    writeUInt(top + height / 2UL);
    Esp32BaseWeb::sendChunk("' text-anchor='middle' transform='rotate(-90 14 ");
    writeUInt(top + height / 2UL);
    Esp32BaseWeb::sendChunk(")'>");
    Esp32BaseWeb::writeHtmlEscaped(yTitle);
    Esp32BaseWeb::sendChunk("</text>");
}

void writeChartTicks(uint32_t durationMs,
                     uint32_t maxValue,
                     uint32_t left,
                     uint32_t top,
                     uint32_t width,
                     uint32_t height) {
    static constexpr uint8_t xSegments = 6;
    static constexpr uint8_t ySegments = 5;
    for (uint8_t i = 0; i <= xSegments; ++i) {
        const uint32_t x = left + static_cast<uint32_t>((static_cast<uint64_t>(width) * i) / xSegments);
        const uint32_t labelMs = static_cast<uint32_t>((static_cast<uint64_t>(durationMs) * i) / xSegments);
        Esp32BaseWeb::sendChunk("<line class='chart-grid' x1='");
        writeUInt(x);
        Esp32BaseWeb::sendChunk("' y1='");
        writeUInt(top);
        Esp32BaseWeb::sendChunk("' x2='");
        writeUInt(x);
        Esp32BaseWeb::sendChunk("' y2='");
        writeUInt(top + height);
        Esp32BaseWeb::sendChunk("'></line><text class='chart-tick' x='");
        writeUInt(x);
        Esp32BaseWeb::sendChunk("' y='");
        writeUInt(top + height + 18UL);
        Esp32BaseWeb::sendChunk("' text-anchor='middle'>");
        writeChartTimeLabel(labelMs);
        Esp32BaseWeb::sendChunk("</text>");
    }
    for (uint8_t i = 0; i <= ySegments; ++i) {
        const uint32_t y = top + height - static_cast<uint32_t>((static_cast<uint64_t>(height) * i) / ySegments);
        const uint32_t value = static_cast<uint32_t>((static_cast<uint64_t>(maxValue) * i) / ySegments);
        Esp32BaseWeb::sendChunk("<line class='chart-grid' x1='");
        writeUInt(left);
        Esp32BaseWeb::sendChunk("' y1='");
        writeUInt(y);
        Esp32BaseWeb::sendChunk("' x2='");
        writeUInt(left + width);
        Esp32BaseWeb::sendChunk("' y2='");
        writeUInt(y);
        Esp32BaseWeb::sendChunk("'></line><text class='chart-tick' x='");
        writeUInt(left - 10UL);
        Esp32BaseWeb::sendChunk("' y='");
        writeUInt(y + 4UL);
        Esp32BaseWeb::sendChunk("' text-anchor='end'>");
        writeUInt(value);
        Esp32BaseWeb::sendChunk("</text>");
    }
}

void writeStableMarker(uint32_t stableStartMs, uint32_t durationMs, uint32_t left, uint32_t top, uint32_t width, uint32_t height) {
    if (stableStartMs == 0 || durationMs == 0 || stableStartMs > durationMs) {
        return;
    }
    const uint32_t x = left + static_cast<uint32_t>((static_cast<uint64_t>(stableStartMs) * width) / durationMs);
    Esp32BaseWeb::sendChunk("<rect x='");
    writeUInt(left);
    Esp32BaseWeb::sendChunk("' y='");
    writeUInt(top);
    Esp32BaseWeb::sendChunk("' width='");
    writeUInt(x > left ? x - left : 0);
    Esp32BaseWeb::sendChunk("' height='");
    writeUInt(height);
    Esp32BaseWeb::sendChunk("' class='chart-startup'></rect><line x1='");
    writeUInt(x);
    Esp32BaseWeb::sendChunk("' y1='");
    writeUInt(top);
    Esp32BaseWeb::sendChunk("' x2='");
    writeUInt(x);
    Esp32BaseWeb::sendChunk("' y2='");
    writeUInt(top + height);
    Esp32BaseWeb::sendChunk("' class='chart-stable-marker'></line>");
}

void writeChartFrame(uint32_t left, uint32_t top, uint32_t width, uint32_t height) {
    Esp32BaseWeb::sendChunk("<line x1='");
    writeUInt(left);
    Esp32BaseWeb::sendChunk("' y1='");
    writeUInt(top + height);
    Esp32BaseWeb::sendChunk("' x2='");
    writeUInt(left + width);
    Esp32BaseWeb::sendChunk("' y2='");
    writeUInt(top + height);
    Esp32BaseWeb::sendChunk("' class='chart-axis'></line><line x1='");
    writeUInt(left);
    Esp32BaseWeb::sendChunk("' y1='");
    writeUInt(top);
    Esp32BaseWeb::sendChunk("' x2='");
    writeUInt(left);
    Esp32BaseWeb::sendChunk("' y2='");
    writeUInt(top + height);
    Esp32BaseWeb::sendChunk("' class='chart-axis'></line>");
}

void writeCumulativePulseChart(uint8_t sampleIndex, const FlowCalibration::Sample& sample) {
    static constexpr uint32_t left = 62;
    static constexpr uint32_t top = 22;
    static constexpr uint32_t width = 832;
    static constexpr uint32_t height = 128;
    static constexpr uint32_t svgHeight = 200;
    const uint32_t durationMs = sample.durationMs == 0 ? 1UL : sample.durationMs;
    const uint32_t maxPulse = sample.detailCapturedPulses == 0 ? 1UL : sample.detailCapturedPulses;
    Esp32BaseWeb::sendChunk("<div class='calibration-chart'><div class='calibration-chart-head'><h3>累计脉冲</h3><span>原始明细脉冲随时间累计</span></div><svg viewBox='0 0 940 200' role='img' aria-label='累计脉冲图表'>");
    writeStableMarker(sample.stableStartMs, durationMs, left, top, width, height);
    writeChartTicks(durationMs, maxPulse, left, top, width, height);
    writeChartFrame(left, top, width, height);
    Esp32BaseWeb::sendChunk("<polyline class='chart-line cumulative' fill='none' points='");
    uint32_t timeMs = 0;
    for (uint16_t i = 0; i < sample.detailCapturedPulses; ++i) {
        timeMs += FlowCalibration::samplePulseDelta(sampleIndex, i);
        if (timeMs > durationMs) {
            timeMs = durationMs;
        }
        const uint32_t x = left + static_cast<uint32_t>((static_cast<uint64_t>(timeMs) * width) / durationMs);
        const uint32_t y = top + height - static_cast<uint32_t>((static_cast<uint64_t>(i + 1UL) * height) / maxPulse);
        writeSvgPoint(x, y);
    }
    Esp32BaseWeb::sendChunk("'></polyline>");
    writeChartAxisTitle("时间 (秒)", "累计脉冲", left, top, width, height, svgHeight);
    Esp32BaseWeb::sendChunk("</svg></div>");
}

void writeWindowPulseChart(uint8_t sampleIndex, const FlowCalibration::Sample& sample) {
    static constexpr uint32_t left = 62;
    static constexpr uint32_t top = 22;
    static constexpr uint32_t width = 832;
    static constexpr uint32_t height = 128;
    static constexpr uint32_t svgHeight = 200;
    const uint32_t durationMs = sample.durationMs == 0 ? 1UL : sample.durationMs;
    uint16_t maxWindow = 1;
    for (uint16_t i = 0; i < sample.windowPulseCount; ++i) {
        const uint16_t value = FlowCalibration::sampleWindowPulse(sampleIndex, i);
        if (value > maxWindow) {
            maxWindow = value;
        }
    }
    Esp32BaseWeb::sendChunk("<div class='calibration-chart'><div class='calibration-chart-head'><h3>2 秒滑动窗口脉冲</h3><span>窗口 ");
    writeUInt(calibrationWindowMs / 1000UL);
    Esp32BaseWeb::sendChunk(" 秒 / 步进 ");
    writeUInt(calibrationWindowStepMs);
    Esp32BaseWeb::sendChunk(" ms</span></div><svg viewBox='0 0 940 200' role='img' aria-label='滑动窗口脉冲图表'>");
    writeStableMarker(sample.stableStartMs, durationMs, left, top, width, height);
    writeChartTicks(durationMs, maxWindow, left, top, width, height);
    writeChartFrame(left, top, width, height);
    Esp32BaseWeb::sendChunk("<polyline class='chart-line window' fill='none' points='");
    for (uint16_t i = 0; i < sample.windowPulseCount; ++i) {
        uint32_t timeMs = static_cast<uint32_t>(i + 1UL) * calibrationWindowStepMs;
        if (timeMs > durationMs) {
            timeMs = durationMs;
        }
        const uint16_t value = FlowCalibration::sampleWindowPulse(sampleIndex, i);
        const uint32_t x = left + static_cast<uint32_t>((static_cast<uint64_t>(timeMs) * width) / durationMs);
        const uint32_t y = top + height - static_cast<uint32_t>((static_cast<uint64_t>(value) * height) / maxWindow);
        writeSvgPoint(x, y);
    }
    Esp32BaseWeb::sendChunk("'></polyline>");
    writeChartAxisTitle("时间 (秒)", "窗口脉冲", left, top, width, height, svgHeight);
    Esp32BaseWeb::sendChunk("</svg></div>");
}

uint8_t countPlansForDate(uint32_t ymd) {
    uint8_t count = 0;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
        if (!zone.enabled) {
            continue;
        }
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(zoneId, slot);
            if (plan.exists && plan.enabled && PlanStore::shouldRunOnDate(plan, ymd)) {
                ++count;
            }
        }
    }
    return count;
}

void writeShortDateTimeHuman(uint32_t epoch) {
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
    snprintf(text, sizeof(text), "%02d-%02d %02d:%02d:%02d",
             local.tm_mon + 1,
             local.tm_mday,
             local.tm_hour,
             local.tm_min,
             local.tm_sec);
    Esp32BaseWeb::sendChunk(text);
}

void writeEventTimeHuman(const Esp32BaseAppEventRecord& event) {
    uint32_t epoch = event.epochSec;
#if ESP32BASE_ENABLE_NTP
    if (epoch == 0) {
        (void)Esp32BaseNtp::resolveCurrentBootEvent(event.bootId, event.uptimeSec, &epoch);
    }
#endif
    if (epoch != 0) {
        writeShortDateTimeHuman(epoch);
        return;
    }
    Esp32BaseWeb::sendChunk("上电 ");
    writeUInt(event.bootId);
    Esp32BaseWeb::sendChunk(" 次 + ");
    writeUInt(event.uptimeSec);
    Esp32BaseWeb::sendChunk(" 秒");
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

const char* zoneErrorLabel(Irrigation::ZoneErrorCode code) {
    switch (code) {
        case Irrigation::ZoneErrorCode::FLOW_START_TIMEOUT: return "启动后未检测到水流";
        case Irrigation::ZoneErrorCode::FLOW_NO_PULSE_TIMEOUT: return "浇水中断流";
        case Irrigation::ZoneErrorCode::LEAK_DETECTED: return "待机时检测到异常水流";
        case Irrigation::ZoneErrorCode::CONFIG_INVALID: return "配置无效";
        case Irrigation::ZoneErrorCode::NONE:
        default: return "-";
    }
}

const char* zoneErrorClearConfirm(Irrigation::ZoneErrorCode code) {
    switch (code) {
        case Irrigation::ZoneErrorCode::FLOW_START_TIMEOUT: return "确认现场已处理“启动后未检测到水流”并清除异常？";
        case Irrigation::ZoneErrorCode::FLOW_NO_PULSE_TIMEOUT: return "确认现场已处理“浇水中断流”并清除异常？";
        case Irrigation::ZoneErrorCode::LEAK_DETECTED: return "确认现场已处理“待机时检测到异常水流”并清除异常？";
        case Irrigation::ZoneErrorCode::CONFIG_INVALID: return "确认已修正配置并清除异常？";
        case Irrigation::ZoneErrorCode::NONE:
        default: return "确认现场已处理并清除异常？";
    }
}

const char* zoneErrorDescription(Irrigation::ZoneErrorCode code) {
    switch (code) {
        case Irrigation::ZoneErrorCode::FLOW_START_TIMEOUT:
            return "系统已经打开这一路阀门，但在启动等待时间内没有收到流量计脉冲。常见原因是水源未打开、阀门或管路堵塞、流量计接线异常。";
        case Irrigation::ZoneErrorCode::FLOW_NO_PULSE_TIMEOUT:
            return "浇水已经开始后，系统连续一段时间没有收到新的流量脉冲。常见原因是中途断水、水压不足、管路被关闭或流量计信号异常。";
        case Irrigation::ZoneErrorCode::LEAK_DETECTED:
            return "所有水路应处于关闭状态时，系统仍检测到流量脉冲。请检查阀门是否关严、管路是否漏水、流量计输入是否受干扰。";
        case Irrigation::ZoneErrorCode::CONFIG_INVALID:
            return "这一路当前配置不满足启动或计划执行要求。请检查水路启用状态、浇水时长上限和流量检测参数。";
        case Irrigation::ZoneErrorCode::NONE:
        default:
            return "当前没有可显示的异常详情。";
    }
}

const char* zoneErrorAdvice(Irrigation::ZoneErrorCode code) {
    switch (code) {
        case Irrigation::ZoneErrorCode::FLOW_START_TIMEOUT:
        case Irrigation::ZoneErrorCode::FLOW_NO_PULSE_TIMEOUT:
            return "确认水源、阀门、管路和流量计都正常后，再点击清除异常。清除前系统会阻止这一路继续自动或手动浇水。";
        case Irrigation::ZoneErrorCode::LEAK_DETECTED:
            return "请先到现场确认没有持续出水或漏水风险，再清除异常。实验阶段可在系统配置中临时关闭漏水检测。";
        case Irrigation::ZoneErrorCode::CONFIG_INVALID:
            return "请先修正配置，再清除异常。";
        case Irrigation::ZoneErrorCode::NONE:
        default:
            return "如果异常仍然反复出现，请查看事件记录和现场接线。";
    }
}

const char* zoneErrorSourceLabel(Irrigation::StopSource source) {
    switch (source) {
        case Irrigation::StopSource::WEB_PAGE: return "网页操作";
        case Irrigation::StopSource::HTTP_API: return "外部 API";
        case Irrigation::StopSource::LOCAL_BUTTON: return "本地按键";
        case Irrigation::StopSource::SCHEDULER: return "计划调度";
        case Irrigation::StopSource::DURATION_REACHED: return "到达目标时长";
        case Irrigation::StopSource::FLOW_MONITOR: return "流量监控";
        case Irrigation::StopSource::LEAK_MONITOR: return "漏水监控";
        case Irrigation::StopSource::CONFIG_CHANGE: return "配置变更";
        case Irrigation::StopSource::FACTORY_RESET: return "恢复出厂保护";
        case Irrigation::StopSource::UNKNOWN:
        default: return "未知来源";
    }
}

const char* zoneErrorResultLabel(Irrigation::TaskResult result) {
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
        default: return "无运行任务";
    }
}

void writeZoneErrorTime(const ZoneErrorStore::ZoneError& error) {
    if (error.occurredEpoch != 0) {
        writeShortDateTimeHuman(error.occurredEpoch);
        return;
    }
    if (error.occurredUptimeMs != 0) {
        Esp32BaseWeb::sendChunk("上电后 ");
        writeDurationMsHumanCompact(error.occurredUptimeMs);
        return;
    }
    Esp32BaseWeb::sendChunk("时间未记录");
}

void writeZoneErrorDialog(uint8_t zoneId, const Irrigation::ZoneConfig& config, const ZoneErrorStore::ZoneError& error) {
    Esp32BaseWeb::sendChunk("<dialog id='irrFaultDialog");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("' class='panel eb-modal irr-fault' data-eb-light-dismiss><h2>");
    Esp32BaseWeb::writeHtmlEscaped(config.name);
    Esp32BaseWeb::sendChunk("异常</h2><table class='kv'><tbody><tr><th>原因</th><td>");
    Esp32BaseWeb::writeHtmlEscaped(zoneErrorLabel(error.errorCode));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>发生时间</th><td>");
    writeZoneErrorTime(error);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>触发来源</th><td>");
    Esp32BaseWeb::writeHtmlEscaped(zoneErrorSourceLabel(error.source));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>任务结果</th><td>");
    Esp32BaseWeb::writeHtmlEscaped(zoneErrorResultLabel(error.result));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>说明</th><td>");
    Esp32BaseWeb::writeHtmlEscaped(zoneErrorDescription(error.errorCode));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>处理建议</th><td>");
    Esp32BaseWeb::writeHtmlEscaped(zoneErrorAdvice(error.errorCode));
    Esp32BaseWeb::sendChunk("</td></tr></tbody></table><div class='actions'><input class='btnlink secondary' type='button' value='关闭' onclick='irrFaultClose(this)'></div></dialog>");
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

void sendMetricCard(const char* label, const char* value, const char* help, const char* tone = nullptr, const char* id = nullptr) {
    Esp32BaseWeb::sendChunk("<div class='metric");
    if (tone && tone[0]) {
        Esp32BaseWeb::sendChunk(" ");
        Esp32BaseWeb::writeHtmlEscaped(tone);
    }
    Esp32BaseWeb::sendChunk("'");
    if (id && id[0]) {
        Esp32BaseWeb::sendChunk(" id='");
        Esp32BaseWeb::writeHtmlEscaped(id);
        Esp32BaseWeb::sendChunk("'");
    }
    if (tone && strcmp(tone, "danger") == 0) {
        Esp32BaseWeb::sendChunk(" style='border-color:#efc0ba;background:var(--eb-danger-soft);color:var(--eb-danger)'");
    }
    Esp32BaseWeb::sendChunk("><b>");
    Esp32BaseWeb::writeHtmlEscaped(value ? value : "");
    Esp32BaseWeb::sendChunk("</b><span>");
    Esp32BaseWeb::writeHtmlEscaped(label ? label : "");
    if (help && help[0]) {
        Esp32BaseWeb::sendChunk(" · ");
        Esp32BaseWeb::writeHtmlEscaped(help);
    }
    Esp32BaseWeb::sendChunk("</span></div>");
}

const char* appEventLevelLabel(uint8_t level) {
    switch (level) {
        case Esp32BaseAppEventLog::LEVEL_INFO: return "信息";
        case Esp32BaseAppEventLog::LEVEL_WARN: return "注意";
        case Esp32BaseAppEventLog::LEVEL_ERROR: return "故障";
        default: return "未知";
    }
}

const char* appEventSourceLabel(const char* source) {
    if (strcmp(source, "web") == 0) return "网页";
    if (strcmp(source, "api") == 0) return "API";
    if (strcmp(source, "button") == 0) return "本地按键";
    if (strcmp(source, "schedule") == 0) return "计划";
    if (strcmp(source, "monitor") == 0) return "监控";
    if (strcmp(source, "runtime") == 0) return "运行保护";
    return source;
}

const char* appEventTypeLabel(const char* type) {
    if (strcmp(type, "schedule_skipped") == 0) return "计划跳过";
    if (strcmp(type, "schedule_unskipped") == 0) return "取消跳过";
    if (strcmp(type, "flow_fault") == 0) return "流量异常";
    if (strcmp(type, "leak_detected") == 0) return "漏水检测";
    if (strcmp(type, "zone_locked") == 0) return "水路锁定";
    if (strcmp(type, "alert_cleared") == 0) return "告警清除";
    if (strcmp(type, "safety_stop") == 0) return "安全停止";
    if (strcmp(type, "factory_reset") == 0) return "恢复出厂";
    return type;
}

uint8_t zoneIdFromEventObject(const char* object) {
    if (!object || strncmp(object, "zone:", 5) != 0) {
        return 0;
    }
    const unsigned long raw = strtoul(object + 5, nullptr, 10);
    return raw <= 255UL ? static_cast<uint8_t>(raw) : 0;
}

uint8_t eventZoneId(const Esp32BaseAppEventRecord& event) {
    const uint8_t direct = zoneIdFromEventObject(event.object);
    if (Irrigation::validZoneId(direct)) {
        return direct;
    }
    if (strcmp(event.type, "schedule_skipped") == 0 &&
        strcmp(event.source, "schedule") == 0 &&
        (event.valueMask & Esp32BaseAppEventLog::VALUE1) &&
        event.value1 >= 1 &&
        event.value1 <= Irrigation::MaxZones) {
        return static_cast<uint8_t>(event.value1);
    }
    return 0;
}

void writeEventZoneName(uint8_t zoneId) {
    if (Irrigation::validZoneId(zoneId)) {
        Esp32BaseWeb::writeHtmlEscaped(ZoneManager::config(zoneId).name);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
}

void writeEventDetailValue(const char* label, int32_t value, const char* unit = nullptr) {
    Esp32BaseWeb::sendChunk("；");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk(" ");
    char text[16];
    snprintf(text, sizeof(text), "%ld", static_cast<long>(value));
    Esp32BaseWeb::writeHtmlEscaped(text);
    if (unit && unit[0]) {
        Esp32BaseWeb::sendChunk(" ");
        Esp32BaseWeb::writeHtmlEscaped(unit);
    }
}

void writeEventDetail(const Esp32BaseAppEventRecord& event) {
    if (strcmp(event.type, "schedule_skipped") == 0) {
        if (strcmp(event.source, "schedule") == 0 && event.code <= static_cast<uint16_t>(Irrigation::PlanObservationStatus::MISSED)) {
            Esp32BaseWeb::writeHtmlEscaped(observationStatusLabel(static_cast<Irrigation::PlanObservationStatus>(event.code)));
            if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
                Esp32BaseWeb::sendChunk("；水路 ");
                writeEventZoneName(static_cast<uint8_t>(event.value1));
            }
            if (event.valueMask & Esp32BaseAppEventLog::VALUE2) {
                writeEventDetailValue("计划时长", event.value2, "秒");
            }
        } else {
            Esp32BaseWeb::sendChunk("已设置单次跳过");
            if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
                writeEventDetailValue("日期", event.value1, nullptr);
            }
        }
        return;
    }
    if (strcmp(event.type, "schedule_unskipped") == 0) {
        Esp32BaseWeb::sendChunk("已取消单次跳过");
        if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
            writeEventDetailValue("日期", event.value1, nullptr);
        }
        return;
    }
    if (strcmp(event.type, "flow_fault") == 0 || strcmp(event.type, "safety_stop") == 0) {
        Esp32BaseWeb::writeHtmlEscaped(taskResultLabel(static_cast<Irrigation::TaskResult>(event.code)));
        if (strcmp(event.type, "flow_fault") == 0) {
            if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
                writeEventDetailValue("目标", event.value1, "秒");
            }
            if (event.valueMask & Esp32BaseAppEventLog::VALUE2) {
                writeEventDetailValue("实际脉冲", event.value2, "个");
            }
            if (event.valueMask & Esp32BaseAppEventLog::VALUE3) {
                Esp32BaseWeb::sendChunk(event.value3 ? "；已锁定水路" : "；仅记录，未锁定");
            }
        }
        return;
    }
    if (strcmp(event.type, "leak_detected") == 0) {
        Esp32BaseWeb::sendChunk("待机状态检测到异常流量");
        if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
            writeEventDetailValue("实际脉冲", event.value1, "个");
        }
        if (event.valueMask & Esp32BaseAppEventLog::VALUE2) {
            writeEventDetailValue("阈值", event.value2, "个");
        }
        if (event.valueMask & Esp32BaseAppEventLog::VALUE3) {
            writeEventDetailValue("窗口", event.value3, "秒");
        }
        return;
    }
    if (strcmp(event.type, "zone_locked") == 0) {
        Esp32BaseWeb::sendChunk("水路进入异常锁定，需要人工清除");
        if (event.code <= static_cast<uint16_t>(Irrigation::ZoneErrorCode::CONFIG_INVALID)) {
            Esp32BaseWeb::sendChunk("；原因 ");
            Esp32BaseWeb::writeHtmlEscaped(zoneErrorLabel(static_cast<Irrigation::ZoneErrorCode>(event.code)));
        }
        if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
            Esp32BaseWeb::sendChunk("；任务结果 ");
            Esp32BaseWeb::writeHtmlEscaped(taskResultLabel(static_cast<Irrigation::TaskResult>(event.value1)));
        }
        return;
    }
    if (strcmp(event.type, "alert_cleared") == 0) {
        Esp32BaseWeb::sendChunk(strcmp(event.reason, "all_zones") == 0 ? "全部告警已清除" : "水路告警已清除");
        return;
    }
    if (strcmp(event.type, "factory_reset") == 0) {
        if (strcmp(event.reason, "requested") == 0) {
            Esp32BaseWeb::sendChunk("已请求恢复出厂");
        } else if (strcmp(event.reason, "executed") == 0) {
            Esp32BaseWeb::sendChunk("恢复出厂已执行");
        } else {
            Esp32BaseWeb::sendChunk("恢复出厂执行失败");
        }
        if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
            Esp32BaseWeb::sendChunk(event.value1 ? "；包含记录清空" : "；保留业务记录");
        }
        return;
    }
    Esp32BaseWeb::writeHtmlEscaped(event.text);
}

void writeManualStartDialog() {
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    const uint32_t maxMin = system.maxWateringDurationSec / 60UL;
    const uint32_t defaultMin = durationMinutesForUi(system.manualDefaultDurationSec);

    Esp32BaseWeb::sendChunk("<style>"
                            ".irrmanual{max-width:520px}.irrmanual h2{margin-bottom:10px}.irrmanual-summary{display:grid;grid-template-columns:88px minmax(0,1fr);gap:6px 12px;border:1px solid var(--eb-line-soft);background:var(--eb-soft);border-radius:8px;padding:9px 10px;margin:0 0 12px}.irrmanual-summary b{color:var(--eb-muted);font-weight:650}.irrmanual-summary span{min-width:0}.irrmanual .durationrow{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:10px;align-items:end;margin-bottom:10px}.irrmanual .durationrow input{width:9ch;max-width:9ch;margin:0}.irrmanual-presets{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;margin-top:6px}.irrmanual-presets .btnlink{width:100%;min-height:32px}.irrmanual-note{font-size:12px;color:var(--eb-muted);line-height:1.45;margin:8px 0 0}@media(max-width:560px){.irrmanual .durationrow{grid-template-columns:1fr}.irrmanual-presets{grid-template-columns:repeat(2,minmax(0,1fr))}}"
                            "</style><dialog id='irrManualDialog' class='panel eb-modal irrmanual'><form id='irrManualForm' class='editform' method='post' action='/api/v1/zone/start' data-default-min='");
    writeUInt(defaultMin);
    Esp32BaseWeb::sendChunk("' onsubmit=\"return irrManualSubmit(this)&&confirm('确认启动手动浇水？')&&irrManualSend(this,once(this))\"><h2>启动手动浇水</h2>");
    writeOnePostHidden("source", "web_page");
    Esp32BaseWeb::sendChunk("<input type='hidden' name='zoneId' value=''><div class='irrmanual-summary'><b>水路</b><span id='irrManualZoneName'></span><b>安全确认</b><span>远程启动前请确认现场安全，阀门和管路可以正常出水。</span></div><div class='durationrow'><p class='field'><label>浇水分钟数</label><input id='irrManualDuration' name='durationMin' type='number' min='1' max='");
    writeUInt(maxMin);
    Esp32BaseWeb::sendChunk("' value='");
    writeUInt(defaultMin);
    Esp32BaseWeb::sendChunk("'><small>默认 ");
    writeUInt(defaultMin);
    Esp32BaseWeb::sendChunk(" 分钟，最大 ");
    writeUInt(maxMin);
    Esp32BaseWeb::sendChunk(" 分钟。</small></p><div><label>快捷时长</label><div class='irrmanual-presets'>");
    for (uint8_t i = 0; i < 6; ++i) {
        const uint32_t presetMin = durationMinutesForUi(system.durationPresets[i]);
        Esp32BaseWeb::sendChunk("<button class='btnlink compact' type='button' data-min='");
        writeUInt(presetMin);
        Esp32BaseWeb::sendChunk("' onclick='irrManualPreset(this)'>");
        writeUInt(presetMin);
        Esp32BaseWeb::sendChunk(" 分钟</button>");
    }
    Esp32BaseWeb::sendChunk("</div><p class='irrmanual-note'>点选快捷时长会自动填入，确认启动前仍可手动修改。</p></div></div><div class='actions'><input type='submit' value='确认启动'><input class='btnlink secondary' type='button' value='取消' onclick='irrManualCancel()'></div></form></dialog>"
                            "<script>"
                            "function irrManualOpen(b){var d=document.getElementById('irrManualDialog'),f=document.getElementById('irrManualForm');f.elements['zoneId'].value=b.dataset.zoneId;f.elements['durationMin'].value=f.dataset.defaultMin;document.getElementById('irrManualZoneName').textContent=b.dataset.zoneName;if(d.showModal)d.showModal();else d.setAttribute('open','open');}"
                            "function irrManualPreset(b){document.getElementById('irrManualDuration').value=b.dataset.min;}"
                            "function irrManualCancel(){var d=document.getElementById('irrManualDialog');if(d.close)d.close();else d.removeAttribute('open');}"
                            "function irrManualSubmit(f){var m=parseInt(f.elements['durationMin'].value||'0',10),max=parseInt(f.elements['durationMin'].max||'0',10);if(!m||m<1||m>max){alert('请输入 1 到 '+max+' 分钟');return false;}return true;}"
                            "function irrManualReset(f){delete f.dataset.busy;var b=f.querySelector('[type=submit]');if(b)b.disabled=false;}"
                            "function irrManualSend(f,ready){if(!ready)return false;if(!window.fetch||!window.URLSearchParams||!window.FormData)return true;fetch(f.action,{method:'POST',headers:{'X-Esp32Base-Ajax':'1',Accept:'application/json','Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f)).toString(),credentials:'same-origin'}).then(function(r){return r.json().catch(function(){return {ok:false,error:'bad_response'};});}).then(function(j){if(!j||!j.ok){alert('启动失败：'+((j&&j.error)||'请求失败'));irrManualReset(f);return;}irrManualCancel();irrManualReset(f);if(window.irrOverviewPollNow)irrOverviewPollNow();}).catch(function(){alert('启动失败：网络请求失败');irrManualReset(f);});return false;}"
                            "</script>");
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
    Esp32BaseWeb::sendPageTitle("灌溉总览", "查看今天是否还有计划、当前哪些水路在浇水；需要临时补水时，可在水路状态中启动。");
    if (Esp32BaseWeb::hasParam("alert_cleared")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "异常已清除", "系统已记录本次清除操作。");
    }
    if (ZoneConfigStore::schemaResetDetected()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "水路配置已重置", "检测到已保存的水路配置格式不匹配，已使用当前固件默认配置。请检查水路启用、名称和流量参数。");
    }
    if (PlanStore::schemaResetDetected()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "计划配置已重置", "检测到已保存的计划配置格式不匹配，相关计划已使用当前固件默认状态。请重新检查计划。");
    }

    uint8_t enabledCount = 0;
    uint8_t runningCount = 0;
    uint8_t errorCount = 0;
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
    }
    const uint8_t todayPlanCount = countPlansForDate(currentYmd());

    char runningText[16];
    char errorText[16];
    char safetyText[24];
    char todayPlanText[16];
    snprintf(runningText, sizeof(runningText), "%u / %u", static_cast<unsigned>(runningCount), static_cast<unsigned>(enabledCount));
    snprintf(errorText, sizeof(errorText), "%u", static_cast<unsigned>(errorCount));
    snprintf(todayPlanText, sizeof(todayPlanText), "%u", static_cast<unsigned>(todayPlanCount));
    if (ZoneManager::leakAlertActive()) {
        strlcpy(safetyText, "漏水告警", sizeof(safetyText));
    } else if (errorCount > 0) {
        snprintf(safetyText, sizeof(safetyText), "%u 路异常", static_cast<unsigned>(errorCount));
    } else {
        strlcpy(safetyText, "正常", sizeof(safetyText));
    }

    Esp32BaseWeb::beginPanel("关键指标");
    Esp32BaseWeb::beginMetricGrid();
    sendMetricCard("系统状态", safetyText, ZoneManager::leakAlertActive() ? "请先到现场确认是否漏水" : "可以按计划或手动浇水", ZoneManager::leakAlertActive() ? "danger" : nullptr, "irrMetricSafety");
    sendMetricCard("正在浇水", runningText, "正在执行 / 已启用水路", nullptr, "irrMetricRunning");
    sendMetricCard("今日计划", todayPlanText, todayPlanCount > 0 ? "今天会按计划自动检查" : "今天没有启用的自动计划", nullptr, "irrMetricTodayPlans");
    sendMetricCard("异常提醒", errorText, errorCount > 0 ? "处理后可在水路状态中清除" : "没有需要处理的水路异常", errorCount > 0 ? "danger" : nullptr, "irrMetricErrors");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("水路状态");
    Esp32BaseWeb::sendChunk("<style>.irr-flow-chart-row td{padding-top:0}.irr-flow-chart-box{display:none;border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft);padding:8px 10px;margin:0 0 6px}.irr-flow-chart-box.active{display:block}.irr-flow-chart-head{display:flex;justify-content:space-between;gap:8px;color:var(--eb-muted);font-size:12px;margin-bottom:4px}.irr-flow-chart{width:100%;height:54px;display:block;background:#fff;border:1px solid var(--eb-line-soft);border-radius:6px}</style>");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>水路</th><th>状态</th><th>任务</th><th>目标时长</th><th>剩余时间</th><th>流速</th><th>估算水量</th><th>操作</th></tr></thead><tbody>");
    bool wroteStatus = false;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        if (!status.enabled) {
            continue;
        }
        const Irrigation::ZoneConfig& config = ZoneManager::config(zoneId);
        wroteStatus = true;
        Esp32BaseWeb::sendChunk("<tr data-irr-zone-row='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("' data-state='");
        Esp32BaseWeb::writeHtmlEscaped(Irrigation::zoneStateName(status.state));
        Esp32BaseWeb::sendChunk("' data-busy='");
        Esp32BaseWeb::sendChunk(status.busy ? "1" : "0");
        Esp32BaseWeb::sendChunk("' data-error='");
        Esp32BaseWeb::sendChunk(status.errorActive ? "1" : "0");
        Esp32BaseWeb::sendChunk("' data-zone-name='");
        Esp32BaseWeb::writeHtmlEscaped(config.name);
        Esp32BaseWeb::sendChunk("'><td>");
        Esp32BaseWeb::writeHtmlEscaped(config.name);
        Esp32BaseWeb::sendChunk("</td><td data-irr-state='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        if (status.errorActive) {
            Esp32BaseWeb::sendChunk("<button type='button' class='tag danger' style='border:0;cursor:pointer' onclick='irrFaultOpen(\"irrFaultDialog");
            writeUInt(zoneId);
            Esp32BaseWeb::sendChunk("\")'>");
            Esp32BaseWeb::writeHtmlEscaped(zoneStateLabel(status.state));
            Esp32BaseWeb::sendChunk("</button>");
        } else {
            Esp32BaseWeb::sendChunk("<span class='tag");
            Esp32BaseWeb::sendChunk(uiToneForState(status.state));
            Esp32BaseWeb::sendChunk("'>");
            Esp32BaseWeb::writeHtmlEscaped(zoneStateLabel(status.state));
            Esp32BaseWeb::sendChunk("</span>");
        }
        Esp32BaseWeb::sendChunk("</td><td data-irr-task='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(status.busy ? taskTypeLabel(status.taskType) : "-");
        Esp32BaseWeb::sendChunk("</td><td data-irr-target='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        writeDurationHuman(status.targetSec);
        Esp32BaseWeb::sendChunk("</td><td data-irr-remaining='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        writeDurationHuman(status.remainingSec);
        Esp32BaseWeb::sendChunk("</td><td data-irr-flow='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        if (status.busy && status.flowRateReady) {
            writeFlowRate(status.flowMlPerMin);
        } else {
            Esp32BaseWeb::sendChunk("-");
        }
        Esp32BaseWeb::sendChunk("</td><td data-irr-ml='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        writeLitersFromMilliliters(status.estimatedMilliliters);
        Esp32BaseWeb::sendChunk("</td><td data-irr-actions='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'><div class='fsactions'>");
        if (status.busy) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/stop' onsubmit=\"return confirm('确认停止该水路？')&&once(this)\">");
            writeOnePostHidden("source", "web_page");
            writeHiddenU32("zoneId", zoneId);
            Esp32BaseWeb::sendChunk("<input class='fsaction' type='submit' value='停止'></form>");
        }
        if (status.errorActive) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/clear-error' onsubmit=\"return confirm('");
            Esp32BaseWeb::writeHtmlEscaped(zoneErrorClearConfirm(status.errorCode));
            Esp32BaseWeb::sendChunk("')&&once(this)\">");
            writeOnePostHidden("source", "web_page");
            writeHiddenU32("zoneId", zoneId);
            Esp32BaseWeb::sendChunk("<input class='fsaction' type='submit' value='清除异常'></form>");
        }
        if (!status.busy && !status.errorActive) {
            if (ZoneManager::leakAlertActive()) {
                Esp32BaseWeb::sendChunk("<span class='muted'>漏水告警中</span>");
            } else {
                Esp32BaseWeb::sendChunk("<button class='btnlink compact ok' type='button' data-zone-id='");
                writeUInt(zoneId);
                Esp32BaseWeb::sendChunk("' data-zone-name='");
                Esp32BaseWeb::writeHtmlEscaped(config.name);
                Esp32BaseWeb::sendChunk("' onclick='irrManualOpen(this)'>启动</button>");
            }
        }
        Esp32BaseWeb::sendChunk("</div></td></tr><tr class='irr-flow-chart-row' data-irr-flow-row='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'><td colspan='8'><div class='irr-flow-chart-box");
        Esp32BaseWeb::sendChunk(status.busy ? " active" : "");
        Esp32BaseWeb::sendChunk("' id='irrFlowChartBox");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'><div class='irr-flow-chart-head'><span>近期流速</span><span data-irr-flow-chart-label='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>");
        writeUInt(SystemConfigStore::current().flowChartHistoryMin);
        Esp32BaseWeb::sendChunk(" 分钟</span></div><svg class='irr-flow-chart' id='irrFlowChart");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("' viewBox='0 0 300 54' preserveAspectRatio='none' role='img' aria-label='近期流速图表'></svg></div></td></tr>");
    }
    if (!wroteStatus) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='8'>暂无启用水路</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><div class='actions'><form method='post' action='/api/v1/zones/stop-all' onsubmit=\"return confirm('确认停止全部水路？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    Esp32BaseWeb::sendChunk("<input id='irrStopAll' class='danger' type='submit' value='全部停止'");
    Esp32BaseWeb::sendChunk(runningCount > 0 ? "" : " disabled");
    Esp32BaseWeb::sendChunk("></form></div>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        if (status.enabled && status.errorActive) {
            writeZoneErrorDialog(zoneId, ZoneManager::config(zoneId), ZoneErrorStore::get(zoneId));
        }
    }
    Esp32BaseWeb::sendChunk("<script>"
                            "function irrFaultOpen(id){var d=document.getElementById(id);if(!d)return;if(d.showModal)d.showModal();else d.setAttribute('open','open');}"
                            "function irrFaultClose(b){var d=b&&b.closest?b.closest('dialog'):null;if(d&&d.close)d.close();else if(d)d.removeAttribute('open');}"
                            "function irrOverviewDuration(s){s=parseInt(s||0,10)||0;var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),r=s%60,t='';if(h)t+=h+'小时';if(m)t+=m+'分钟';if(r||!t)t+=r+'秒';return t;}"
                            "function irrOverviewLiters(ml){ml=parseInt(ml||0,10)||0;return Math.floor(ml/1000)+'.'+('00'+(ml%1000)).slice(-3)+' L';}"
                            "function irrOverviewFlow(v,ready,busy){v=parseInt(v||0,10)||0;if(!busy||!ready)return '-';return Math.floor(v/1000)+'.'+('00'+(v%1000)).slice(-3)+' L/min';}"
                            "function irrOverviewEscape(s){return String(s||'').replace(/[&<>\"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c];});}"
                            "function irrOverviewStateLabel(s){return {disabled:'已禁用',idle:'待机',starting:'启动中',running:'浇水中',error:'异常'}[s]||'未知';}"
                            "function irrOverviewStateTone(s){return s==='running'||s==='starting'?' warn':(s==='error'?' danger':(s==='idle'?' ok':''));}"
                            "function irrOverviewSetMetric(id,value){var e=document.querySelector('#'+id+' b');if(e)e.textContent=value;}"
                            "function irrOverviewRefreshMs(data){if(document.hidden)return 60000;return data&&data.zones&&data.zones.some(function(z){return z.busy;})?1000:30000;}"
                            "function irrOverviewInitialFast(){return !!document.querySelector('[data-irr-zone-row][data-busy=\"1\"]');}"
                            "function irrOverviewRenderState(z){var e=document.querySelector('[data-irr-state=\"'+z.zoneId+'\"]');if(e)e.innerHTML='<span class=\"tag'+irrOverviewStateTone(z.state)+'\">'+irrOverviewEscape(irrOverviewStateLabel(z.state))+'</span>';}"
                            "function irrOverviewRenderActions(row,z,leak){var e=document.querySelector('[data-irr-actions=\"'+z.zoneId+'\"]');if(!e)return;var name=irrOverviewEscape(row.dataset.zoneName||('水路 '+z.zoneId));if(z.busy){e.innerHTML='<div class=\"fsactions\"><form method=\"post\" action=\"/api/v1/zone/stop\" onsubmit=\"return confirm(\\'确认停止该水路？\\')&&once(this)\"><input type=\"hidden\" name=\"source\" value=\"web_page\"><input type=\"hidden\" name=\"zoneId\" value=\"'+z.zoneId+'\"><input class=\"fsaction\" type=\"submit\" value=\"停止\"></form></div>';return;}e.innerHTML=leak?'<div class=\"fsactions\"><span class=\"muted\">漏水告警中</span></div>':'<div class=\"fsactions\"><button class=\"btnlink compact ok\" type=\"button\" data-zone-id=\"'+z.zoneId+'\" data-zone-name=\"'+name+'\" onclick=\"irrManualOpen(this)\">启动</button></div>';}"
                            "function irrFlowChartDraw(id,points){var svg=document.getElementById('irrFlowChart'+id);if(!svg)return;points=points||[];var max=0;for(var i=0;i<points.length;i++)max=Math.max(max,parseInt(points[i]||0,10)||0);svg.innerHTML='';if(points.length<2||max<=0){return;}var w=300,h=54,d='';for(var j=0;j<points.length;j++){var x=points.length===1?0:(j*(w-2)/(points.length-1)+1),y=h-3-((parseInt(points[j]||0,10)||0)*(h-8)/max);d+=(j?'L':'M')+x.toFixed(1)+' '+y.toFixed(1);}svg.innerHTML='<path d=\"'+d+'\" fill=\"none\" stroke=\"#0f766e\" stroke-width=\"2\" vector-effect=\"non-scaling-stroke\"/><line x1=\"0\" y1=\"'+(h-3)+'\" x2=\"300\" y2=\"'+(h-3)+'\" stroke=\"#e2e8f0\" vector-effect=\"non-scaling-stroke\"/>';}"
                            "function irrFlowChart(id,busy){var box=document.getElementById('irrFlowChartBox'+id);if(box)box.classList.toggle('active',!!busy);if(!busy)return;fetch('/api/v1/flow/history?zoneId='+id,{headers:{Accept:'application/json'},credentials:'same-origin'}).then(function(r){return r.ok?r.json():null;}).then(function(j){if(!j||!j.ok)return;irrFlowChartDraw(id,j.flowHistory||[]);var label=document.querySelector('[data-irr-flow-chart-label=\"'+id+'\"]');if(label)label.textContent=String(j.historyMin||'')+' 分钟';}).catch(function(){});}"
                            "function irrOverviewApplyStatus(data){if(!data||!data.ok||!data.zones)return;var enabled=0,running=0,errors=0,structural=false;data.zones.forEach(function(z){if(!z.enabled)return;enabled++;if(z.busy)running++;if(z.errorActive)errors++;var row=document.querySelector('[data-irr-zone-row=\"'+z.zoneId+'\"]');if(!row){structural=true;return;}var busy=z.busy?'1':'0',err=z.errorActive?'1':'0';if(row.dataset.error!==err){structural=true;return;}row.dataset.state=z.state;row.dataset.busy=busy;row.dataset.error=err;irrOverviewRenderState(z);var e=document.querySelector('[data-irr-task=\"'+z.zoneId+'\"]');if(e)e.textContent=z.busy?(z.taskLabel||'浇水中'):'-';e=document.querySelector('[data-irr-target=\"'+z.zoneId+'\"]');if(e)e.textContent=irrOverviewDuration(z.targetSec);e=document.querySelector('[data-irr-remaining=\"'+z.zoneId+'\"]');if(e)e.textContent=irrOverviewDuration(z.remainingSec);e=document.querySelector('[data-irr-flow=\"'+z.zoneId+'\"]');if(e)e.textContent=irrOverviewFlow(z.flowMlPerMin,z.flowRateReady,z.busy);e=document.querySelector('[data-irr-ml=\"'+z.zoneId+'\"]');if(e)e.textContent=irrOverviewLiters(z.estimatedMl);irrFlowChart(z.zoneId,z.busy);irrOverviewRenderActions(row,z,data.leakAlertActive);});if(enabled!==document.querySelectorAll('[data-irr-zone-row]').length)structural=true;if(structural){location.reload();return;}irrOverviewSetMetric('irrMetricRunning',running+' / '+enabled);irrOverviewSetMetric('irrMetricErrors',String(errors));irrOverviewSetMetric('irrMetricSafety',data.leakAlertActive?'漏水告警':(errors>0?errors+' 路异常':'正常'));var stop=document.getElementById('irrStopAll');if(stop)stop.disabled=running<=0;}"
                            "var irrOverviewTimer=0;function irrOverviewSchedule(ms){if(irrOverviewTimer)clearTimeout(irrOverviewTimer);irrOverviewTimer=setTimeout(irrOverviewPoll,ms);}"
                            "function irrOverviewPoll(){fetch('/api/v1/status',{headers:{Accept:'application/json'},credentials:'same-origin'}).then(function(r){return r.ok?r.json():null;}).then(function(j){irrOverviewApplyStatus(j);irrOverviewSchedule(irrOverviewRefreshMs(j));}).catch(function(){irrOverviewSchedule(45000);});}"
                            "function irrOverviewPollNow(){irrOverviewSchedule(0);}"
                            "irrOverviewSchedule(irrOverviewInitialFast()?1000:30000);"
                            "</script>");
    writeManualStartDialog();
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
    bool wroteZone = false;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
        if (!zone.enabled) {
            continue;
        }
        wroteZone = true;
        Esp32BaseWeb::beginPanel(zone.name);
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
    if (!wroteZone) {
        Esp32BaseWeb::beginPanel("计划");
        Esp32BaseWeb::sendChunk("<p class='muted'>暂无启用水路。请先到水路管理中启用需要使用的水路。</p>");
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
    if (Esp32BaseWeb::hasParam("enabledPresent")) {
        plan.enabled = readCheckbox("enabled");
    } else if (Esp32BaseWeb::hasParam("enabled") && !readBool("enabled", &plan.enabled)) {
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
    if (plan.cycleDays < 1 || plan.cycleDays > 30) {
        *error = "invalid_cycle_days";
        return false;
    }
    if (Esp32BaseWeb::hasParam("cycleMask") && !readU32("cycleMask", &plan.cycleMask)) {
        *error = "invalid_cycle_mask";
        return false;
    }
    const uint32_t validMask = (1UL << plan.cycleDays) - 1UL;
    plan.cycleMask &= validMask;
    if (plan.cycleMask == 0) {
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
    Esp32BaseWeb::sendChunk("<style>"
                            ".planform{max-width:940px}.planform-section{grid-column:1/-1;margin:2px 0 0;padding-top:4px;border-top:1px solid var(--eb-line-soft)}.planform-section:first-of-type{border-top:0;padding-top:0}.planform-section b{display:block;margin-bottom:2px}.planform-section small{display:block;color:var(--eb-muted);font-size:12px;line-height:1.45}.planform-cycle{grid-column:1/-1;display:grid;grid-template-columns:minmax(132px,168px) minmax(180px,220px) minmax(0,1fr);gap:12px 16px;align-items:start;border:1px solid var(--eb-line-soft);background:var(--eb-soft);border-radius:8px;padding:10px 12px}.planform-cycle .field{min-width:0}.planform-cycle .field input{width:100%;box-sizing:border-box}.cycleday-block{min-width:0}.cycleday-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(78px,1fr));gap:7px;margin-top:4px}.cycleday-grid label{display:flex;align-items:center;gap:6px;min-height:32px;margin:0;padding:4px 8px;border:1px solid var(--eb-line);border-radius:7px;background:var(--eb-surface);font-weight:650;white-space:nowrap}.cycleday-grid input{margin:0}.planform .hintline{margin-top:6px;color:var(--eb-muted);font-size:12px;line-height:1.45}@media(max-width:760px){.planform-cycle{grid-template-columns:1fr}.cycleday-grid{grid-template-columns:repeat(auto-fit,minmax(88px,1fr))}}"
                            "</style>");
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
    Esp32BaseWeb::sendChunk("'><input type='hidden' name='enabledPresent' value='1'><div class='fieldgrid'>"
                            "<p class='planform-section'><b>基本信息</b><small>用于识别这条计划，并控制它是否参与自动执行。</small></p>"
                            "<p class='field long'><label>名称</label><input name='name' maxlength='31' value='");
    Esp32BaseWeb::writeHtmlEscaped(plan.name);
    Esp32BaseWeb::sendChunk("'><small>会显示在计划列表和浇水记录里。</small></p><p class='field med'><label><input type='checkbox' name='enabled' value='1'");
    Esp32BaseWeb::sendChunk(plan.enabled ? " checked" : "");
    Esp32BaseWeb::sendChunk("> 启用计划</label><small>勾选后，到点才会自动检查是否执行。</small></p>"
                            "<p class='planform-section'><b>执行设置</b><small>设置每天检查计划的时间，以及单次浇水时长。</small></p>"
                            "<p class='field med'><label>开始时间</label><input name='time' type='time' value='");
    writeTime(plan.timeHour, plan.timeMinute);
    Esp32BaseWeb::sendChunk("'><small>到达该时间后检查当天是否是执行日。</small></p><p class='field med'><label>浇水时长（分钟）</label><input name='durationMin' type='number' min='1' max='");
    writeUInt(SystemConfigStore::current().maxWateringDurationSec / 60UL);
    Esp32BaseWeb::sendChunk("' value='");
    writeUInt(durationMinutesForUi(plan.durationSec));
    Esp32BaseWeb::sendChunk("'><small>受系统单次最长时间约束。</small></p>"
                            "<p class='planform-section'><b>循环设置</b><small>循环天数决定下面出现多少个执行日；例如 5 天循环只显示第 1 到第 5 天。</small></p>"
                            "<div class='planform-cycle'><p class='field'><label>循环天数</label><input name='cycleDays' type='number' min='1' max='30' value='");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk("' oninput='irrPlanRenderDays(this.form)' onchange='irrPlanRenderDays(this.form)'><small>范围 1 到 30 天。</small></p><p class='field'><label>起始日期</label><input name='cycleStartYmd' type='date' value='");
    writeYmdInput(plan.cycleStartYmd);
    Esp32BaseWeb::sendChunk("'><small>用于计算今天处于循环中的第几天。</small></p><div class='cycleday-block'><label>执行日</label><div class='cycleday-grid' aria-label='执行日'></div><p class='hintline'>只勾选需要浇水的循环日，未勾选的日子会自动跳过。</p></div></div></div><div class='actions'><input type='submit' value='保存'><a class='btnlink secondary' href='/irrigation/plans'>返回</a></div></form>"
                            "<script>"
                            "function irrPlanClampDays(f){var n=parseInt(f.cycleDays.value||'1',10);if(!n)n=1;n=Math.min(30,Math.max(1,n));f.cycleDays.value=String(n);return n;}"
                            "function irrPlanMaskLimit(n){return (1<<n)-1;}"
                            "function irrPlanCurrentMask(f){var mask=0,boxes=f.querySelectorAll('.cycleday-grid input');for(var i=0;i<boxes.length;i++){if(boxes[i].checked)mask|=(1<<parseInt(boxes[i].value,10));}return mask;}"
                            "function irrPlanSyncMask(f){var mask=irrPlanCurrentMask(f);f.cycleMask.value=String(mask);return mask;}"
                            "function irrPlanRenderDays(f){var n=irrPlanClampDays(f),grid=f.querySelector('.cycleday-grid'),stored=parseInt(f.cycleMask.value||'0',10)||0,mask=irrPlanCurrentMask(f)||stored||1;mask&=irrPlanMaskLimit(n);if(mask===0)mask=1;var frag=document.createDocumentFragment();for(var i=0;i<n;i++){var label=document.createElement('label'),input=document.createElement('input');input.type='checkbox';input.value=String(i);input.checked=!!(mask&(1<<i));input.onchange=function(){irrPlanSyncMask(f);};label.appendChild(input);label.appendChild(document.createTextNode('第 '+(i+1)+' 天'));frag.appendChild(label);}grid.replaceChildren(frag);f.cycleMask.value=String(mask);}"
                            "function irrPlanPrepare(f){var m=parseInt(f.durationMin.value||'0',10),n=irrPlanClampDays(f),mask=irrPlanSyncMask(f);if(!m||m<1){alert('浇水时长无效');return false;}f.durationSec.value=String(m*60);if(!n||n<1||n>30){alert('循环天数无效');return false;}if(!mask){alert('至少选择一个执行日');return false;}return true;}"
                            "irrPlanRenderDays(document.currentScript.previousElementSibling);"
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
        Esp32BaseWeb::sendPageTitle("水路编辑", "设置水路名称、是否使用和异常处理方式。流量估算参数在流量校准页通过候选参数应用。");
        Esp32BaseWeb::beginPanel(zone.name);
        Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/zone/config' onsubmit=\"return confirm('确认保存水路配置？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", editZoneId);
        Esp32BaseWeb::sendChunk("<input type='hidden' name='enabledPresent' value='1'><input type='hidden' name='suppressErrorPresent' value='1'><div class='fieldgrid'><p class='field full'><b>基本信息</b><small>决定这一路是否出现在首页、计划和记录里。</small></p><p class='field med'><label>名称</label><input name='name' maxlength='31' value='");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("'><small>显示在首页、计划和记录中的水路名称。</small></p><p class='field med'><label><input type='checkbox' name='enabled' value='1'");
        Esp32BaseWeb::sendChunk(zone.enabled ? " checked" : "");
        Esp32BaseWeb::sendChunk("> 启用水路</label><small>未启用的水路不会出现在首页和计划业务页面，也不能启动浇水。</small></p><p class='field full'><b>硬件引脚</b><small>固定 4 路硬件定义，只用于安装核对和现场排查，不能在页面修改。</small></p><p class='field med'><label>阀门控制 GPIO</label><code>GPIO");
        writeUInt(zone.valvePin);
        Esp32BaseWeb::sendChunk("</code><small>控制这一路电磁阀或阀门驱动输出。</small></p><p class='field med'><label>流量计输入 GPIO</label><code>GPIO");
        writeUInt(zone.flowPin);
        Esp32BaseWeb::sendChunk("</code><small>连接这一路流量计脉冲信号；输入脚需要按硬件设计提供稳定上拉。</small></p><p class='field full'><b>当前流量估算参数</b><small>当前参数只读；候选参数可手工填写、从其他水路填入或由校准样本生成，再到流量校准页设为当前。</small></p><div class='field full'>");
        writeFlowParameterLine(zone.flow);
        Esp32BaseWeb::sendChunk("<div class='actions'><a class='btnlink secondary' href='/irrigation/calibration'>管理候选参数</a></div></div><p class='field full'><b>异常处理</b><small>用于判断开阀后是否真的有水流，以及水流中断时如何处理。</small></p><p class='field short'><label>启动超时秒</label><input name='startTimeoutSec' type='number' min='1' max='300' value='");
        writeUInt(zone.startTimeoutSec);
        Esp32BaseWeb::sendChunk("'><small>开阀后在该时间内没有检测到水流，判定启动异常。</small></p><p class='field short'><label>无脉冲超时秒</label><input name='flowNoPulseTimeoutSec' type='number' min='1' max='300' value='");
        writeUInt(zone.flowNoPulseTimeoutSec);
        Esp32BaseWeb::sendChunk("'><small>运行中超过该时间没有新脉冲，判定断流异常。</small></p><p class='field full'><label><input type='checkbox' name='suppressError' value='1'");
        Esp32BaseWeb::sendChunk(zone.suppressError ? " checked" : "");
        Esp32BaseWeb::sendChunk("> 流量异常只记录，不锁定水路</label><small>勾选后，系统仍会停止本次浇水并写入记录，但不会把这一路锁定为异常；取消勾选时，出现无水流或断流后需要人工清除异常。</small></p></div><div class='actions'><input type='submit' value='保存水路配置'><a class='btnlink secondary' href='/irrigation/zones'>返回列表</a></div></form>");
        Esp32BaseWeb::endPanel();
        pageFooter();
        return;
    }

    pageHeader("水路管理");
    Esp32BaseWeb::sendPageTitle("水路管理", "查看 4 路水路状态；点击编辑进入单个水路配置。系统级参数在基础库 App Config 中设置。");
    Esp32BaseWeb::beginPanel("水路列表");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>水路</th><th>启用状态</th><th>启动超时</th><th>无脉冲超时</th><th>流量估算</th><th>错误策略</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</td><td><span class='tag");
        Esp32BaseWeb::sendChunk(zone.enabled ? " ok" : "");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(zone.enabled ? "启用" : "停用");
        Esp32BaseWeb::sendChunk("</span></td><td>");
        writeUInt(zone.startTimeoutSec);
        Esp32BaseWeb::sendChunk(" 秒</td><td>");
        writeUInt(zone.flowNoPulseTimeoutSec);
        Esp32BaseWeb::sendChunk(" 秒</td><td>");
        writeUInt(zone.flow.startupPulseLimit);
        Esp32BaseWeb::sendChunk(" 脉冲启动 / ");
        writeUInt(zone.flow.startupEstimatedMl);
        Esp32BaseWeb::sendChunk(" ml · 稳定 ");
        writeUInt(zone.flow.stablePulsePerLiter);
        Esp32BaseWeb::sendChunk(" P/L</td><td>");
        Esp32BaseWeb::sendChunk(zone.suppressError ? "只记录，不锁定异常" : "检测异常并锁定水路");
        Esp32BaseWeb::sendChunk("</td><td><a class='btnlink compact' href='/irrigation/zones?zoneId=");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>编辑</a></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    pageFooter();
}

void handleCalibrationPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    pageHeader("流量校准");
    Esp32BaseWeb::sendPageTitle("流量校准", "按现场水压和管路条件采集样本，自动计算启动阶段和稳定阶段流量参数。");
    if (FlowCalibration::lastError()[0] != '\0') {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "校准提示", FlowCalibration::lastError());
    }
    Esp32BaseWeb::sendChunk("<style>"
                            ".calibration-metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin:8px 0 12px}"
                            ".calibration-metrics .metric b{font-weight:600}"
                            ".calibration-zone-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px}"
                            ".calibration-zone-card{border:1px solid var(--eb-line);border-radius:8px;padding:12px;background:var(--eb-surface)}"
                            ".calibration-zone-head{display:flex;justify-content:space-between;gap:10px;align-items:flex-start;margin-bottom:10px}"
                            ".calibration-zone-head h3{margin:0 0 6px;font-size:1rem;font-weight:600}"
                            ".calibration-param-grid{display:grid;grid-template-columns:1fr;gap:10px}"
                            ".calibration-param-card{border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft);padding:10px;min-width:0}"
                            ".calibration-param-card.current{background:#fff}"
                            ".calibration-param-head{display:flex;justify-content:space-between;gap:8px;align-items:center;margin-bottom:8px}"
                            ".calibration-param-head h4{margin:0;font-size:.92rem;font-weight:500;color:var(--eb-muted)}"
                            ".calibration-param-head form{margin:0}"
                            ".calibration-param-line{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px 10px;align-items:baseline}"
                            ".calibration-param-line .param{display:flex;gap:5px;align-items:baseline;min-width:0;color:var(--eb-muted);font-size:13px;white-space:nowrap}"
                            ".calibration-param-line .value{color:var(--eb-ink);font-weight:500}"
                            ".calibration-param-card.candidate,.calibration-param-card.previous{font-size:13px}"
                            ".calibration-param-card.candidate .calibration-param-line .param,.calibration-param-card.previous .calibration-param-line .param{font-size:12px}"
                            ".calibration-param-note{margin:8px 0 0;color:var(--eb-muted);font-size:12px;line-height:1.45}"
                            ".calibration-internal{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0;color:var(--eb-muted);font-size:.92rem}"
                            ".calibration-internal span{border:1px solid var(--eb-line-soft);border-radius:8px;padding:6px 9px;background:var(--eb-soft)}"
                            ".calibration-workflow{display:grid;grid-template-columns:repeat(3,minmax(180px,1fr));gap:12px;align-items:stretch}"
                            ".calibration-stage{border:1px solid var(--eb-line-soft);border-radius:8px;padding:12px;background:var(--eb-soft)}"
                            ".calibration-stage h3{margin:0 0 8px;font-size:.95rem;font-weight:500;color:var(--eb-muted)}"
                            ".calibration-stage .tag{margin-left:0}"
                            ".calibration-actions{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;align-items:end}"
                            ".calibration-actions form{margin:0}"
                            ".calibration-actions .field{margin:0}"
                            ".calibration-sample-top{display:flex;justify-content:flex-end;margin:-4px 0 10px}"
                            ".calibration-sample-actions{display:flex;flex-wrap:wrap;gap:8px;align-items:center}"
                            ".calibration-note{border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft);padding:10px 12px;margin:12px 0;color:var(--eb-muted)}"
                            ".calibration-note code{color:var(--eb-ink)}"
                            ".calibration-dialog{border:1px solid var(--eb-line);border-radius:8px;padding:16px;max-width:420px;width:calc(100% - 32px)}"
                            ".calibration-dialog::backdrop{background:rgba(15,23,42,.35)}"
                            ".calibration-candidate-dialog{max-width:560px}"
                            ".calibration-candidate-dialog .field.short{grid-column:span 4}"
                            ".calibration-copy-fill{border:1px solid var(--eb-line-soft);border-radius:8px;background:var(--eb-soft);padding:10px;margin:10px 0}"
                            ".calibration-copy-fill label{display:block;margin:0 0 6px;color:var(--eb-muted);font-size:13px}"
                            ".calibration-fill-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}"
                            ".calibration-fill-row select{width:auto;margin:0}"
                            "@media(max-width:720px){.calibration-workflow{grid-template-columns:1fr}.calibration-zone-grid{grid-template-columns:1fr}.calibration-param-line{grid-template-columns:1fr}.calibration-candidate-dialog .field.short{grid-column:1/-1}}"
                            "</style>");
    writeFlowParameterLifecyclePanel();

    Esp32BaseWeb::beginPanel("校准配置");
    Esp32BaseWeb::sendChunk("<div class='calibration-metrics'>");
    char text[32];
    snprintf(text, sizeof(text), "%u 条", static_cast<unsigned>(system.calibrationSampleTarget));
    sendMetricCard("样本容量", text, "只限制新增样本");
    snprintf(text, sizeof(text), "%u 分钟", static_cast<unsigned>(system.calibrationMaxCaptureMin));
    sendMetricCard("单次最长", text, "超时样本无效");
    snprintf(text, sizeof(text), "%u 秒", static_cast<unsigned>(system.calibrationDetailCaptureSec));
    sendMetricCard("明细采集", text, "原始脉冲时间差");
    snprintf(text, sizeof(text), "%u 个", static_cast<unsigned>(system.calibrationDetailPulseLimit));
    sendMetricCard("明细脉冲上限", text, "只限制明细");
    Esp32BaseWeb::sendChunk("</div><div class='calibration-internal'><span>稳定窗口 2000 ms</span><span>滑动步进 200 ms</span><span>连续窗口 5</span><span>波动阈值 10%</span><span>稳定扫描从 1000 ms 开始</span></div><div class='actions'><a class='btnlink secondary' href='/esp32base/app-config'>修改校准配置</a></div>");
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("采集样本");
    Esp32BaseWeb::sendChunk("<div class='calibration-sample-top'><form method='post' action='/api/v1/calibration/clear' onsubmit=\"return confirm('确认清空当前校准样本？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    Esp32BaseWeb::sendChunk("<input class='secondary' type='submit' value='清空样本'></form></div><div class='calibration-workflow'><div class='calibration-stage'><h3>状态</h3><span class='tag");
    Esp32BaseWeb::sendChunk(FlowCalibration::state() == FlowCalibration::State::IDLE ? " ok" : " warn");
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::writeHtmlEscaped(calibrationStateLabel(FlowCalibration::state()));
    Esp32BaseWeb::sendChunk("</span><p>");
    Esp32BaseWeb::sendChunk("已保存 ");
    writeUInt(FlowCalibration::sampleCount());
    Esp32BaseWeb::sendChunk(" 条，有效 ");
    writeUInt(FlowCalibration::validSampleCount());
    Esp32BaseWeb::sendChunk(" 条，容量 ");
    writeUInt(system.calibrationSampleTarget);
    Esp32BaseWeb::sendChunk(" 条</p></div><div class='calibration-stage'><h3>接水</h3>");
    if (FlowCalibration::state() == FlowCalibration::State::CAPTURING) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/calibration/stop' onsubmit=\"return confirm('确认停止校准出水？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        Esp32BaseWeb::sendChunk("<p>接到量杯目标水量后停止出水。</p><div class='actions'><input type='submit' value='停止并输入水量'></div></form>");
    } else if (FlowCalibration::state() == FlowCalibration::State::WAITING_ACTUAL) {
        Esp32BaseWeb::sendChunk("<p>已停止出水，请在右侧保存本次实测水量。</p>");
    } else if (FlowCalibration::sampleCount() >= system.calibrationSampleTarget) {
        Esp32BaseWeb::sendChunk("<p>当前样本数已达到配置容量；可先生成候选参数，或清空样本后重新采集。</p>");
    } else {
        Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/calibration/start' onsubmit=\"return confirm('确认开始校准出水？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        Esp32BaseWeb::sendChunk("<p>选择水路，准备量杯后开始接水。</p><p class='field short'><label>水路</label><select name='zoneId'>");
        for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
            const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
            Esp32BaseWeb::sendChunk("<option value='");
            writeUInt(zoneId);
            Esp32BaseWeb::sendChunk("'");
            if (!zone.enabled) {
                Esp32BaseWeb::sendChunk(" disabled");
            }
            Esp32BaseWeb::sendChunk(">");
            Esp32BaseWeb::writeHtmlEscaped(zone.name);
            Esp32BaseWeb::sendChunk(zone.enabled ? "" : "（停用）");
            Esp32BaseWeb::sendChunk("</option>");
        }
        Esp32BaseWeb::sendChunk("</select></p><div class='actions'><input type='submit' value='开始接水'></div></form>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='calibration-stage'><h3>保存样本</h3>");
    if (FlowCalibration::state() == FlowCalibration::State::WAITING_ACTUAL) {
        Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/calibration/sample' onsubmit=\"return confirm('确认保存本次校准样本？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        Esp32BaseWeb::sendChunk("<p>输入量杯实测水量后保存样本。</p><p class='field short'><label>实际水量 ml</label><input name='actualMl' type='number' min='1' max='100000' required></p><div class='actions'><input type='submit' value='保存样本'></div></form>");
    } else if (FlowCalibration::state() == FlowCalibration::State::CAPTURING) {
        Esp32BaseWeb::sendChunk("<p>停止出水后，这里会要求输入实际水量。</p>");
    } else {
        Esp32BaseWeb::sendChunk("<p>完成一次接水后，在这里输入实际水量并保存。</p>");
    }
    Esp32BaseWeb::sendChunk("</div></div>");
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("样本");
    Esp32BaseWeb::sendChunk("<dialog id='calibrationActualDialog' class='calibration-dialog'><form method='post' action='/api/v1/calibration/sample/update' onsubmit=\"return confirm('确认修改该样本实际水量？')&&once(this)\"><input type='hidden' name='source' value='web_page'><input id='calibrationActualIndex' type='hidden' name='sampleIndex'><h3>修改实际水量</h3><p class='field short'><label>实际水量 ml</label><input id='calibrationActualInput' name='actualMl' type='number' min='1' max='100000' required></p><div class='actions'><input type='submit' value='保存修改'><button class='btnlink secondary' type='button' onclick='document.getElementById(\"calibrationActualDialog\").close()'>取消</button></div></form></dialog>");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>#</th><th>水路</th><th>状态</th><th>实际水量</th><th>总脉冲</th><th>时长</th><th>稳定开始</th><th>启动脉冲</th><th>波动</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t i = 0; i < FlowCalibration::sampleCount(); ++i) {
        const FlowCalibration::Sample& sample = FlowCalibration::sample(i);
        Esp32BaseWeb::sendChunk("<tr><td>");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(sample.zoneId);
        Esp32BaseWeb::sendChunk("</td><td>");
        if (sample.valid) {
            Esp32BaseWeb::sendChunk(sample.stableDetected ? "有效" : "有效，未识别稳定点");
        } else {
            Esp32BaseWeb::writeHtmlEscaped(sample.invalidReason);
        }
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(sample.actualMl);
        Esp32BaseWeb::sendChunk(" ml</td><td>");
        writeUInt(sample.totalPulses);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeDurationMsHumanCompact(sample.durationMs);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeDurationMsHumanCompact(sample.stableStartMs);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(sample.startupPulseInSample);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(sample.rateVariationPermille);
        Esp32BaseWeb::sendChunk("‰</td><td><div class='calibration-sample-actions'><button class='btnlink compact secondary' type='button' onclick='calibrationEditActual(");
        writeUInt(i);
        Esp32BaseWeb::sendChunk(",");
        writeUInt(sample.actualMl);
        Esp32BaseWeb::sendChunk(")'>修改</button><a class='btnlink compact' href='/irrigation/calibration/sample?index=");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk("'>详情</a></div></td></tr>");
    }
    if (FlowCalibration::sampleCount() == 0) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='10'>暂无样本</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><div class='actions'><form method='post' action='/api/v1/calibration/compute' onsubmit=\"return confirm('确认生成候选参数？这会替换该水路当前候选参数。')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    Esp32BaseWeb::sendChunk("<input type='submit' value='生成候选参数'></form></div><script>function calibrationEditActual(i,ml){var d=document.getElementById('calibrationActualDialog');document.getElementById('calibrationActualIndex').value=String(i);document.getElementById('calibrationActualInput').value=String(ml||'');if(d.showModal)d.showModal();else d.setAttribute('open','open');}</script>");
    Esp32BaseWeb::endPanel();

    const FlowCalibration::Recommendation& rec = FlowCalibration::recommendation();
    if (rec.valid) {
        Esp32BaseWeb::beginPanel("候选参数诊断");
        Esp32BaseWeb::sendChunk("<div class='calibration-note'><b>运行估算公式</b><br><code>P</code> 为总脉冲，<code>S</code> 为启动阶段脉冲，<code>V0</code> 为启动阶段水量，<code>K</code> 为稳定脉冲 P/L。<br>未过启动阶段：<code>P <= S，水量 = P × V0 / S</code>。<br>超过启动阶段：<code>P > S，水量 = V0 + (P - S) × 1000 / K</code>。</div>");
        Esp32BaseWeb::sendChunk("<div class='calibration-note'><b>参数生成算法</b><br>系统从样本脉冲时间差还原时间线，用 2 秒窗口、200 ms 步进扫描流速，找到连续稳定的起点；稳定起点前的脉冲数作为启动阶段参考。多个样本取中位数附近搜索候选启动脉冲，并拟合启动阶段水量和稳定脉冲 P/L，选择平均误差和最大误差最小的一组。</div>");
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><tbody><tr><th>水路</th><td>");
        writeUInt(rec.zoneId);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>启动阶段脉冲</th><td>");
        writeUInt(rec.flow.startupPulseLimit);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>启动阶段水量</th><td>");
        writeUInt(rec.flow.startupEstimatedMl);
        Esp32BaseWeb::sendChunk(" ml</td></tr><tr><th>稳定脉冲 P/L</th><td>");
        writeUInt(rec.flow.stablePulsePerLiter);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>有效样本数</th><td>");
        writeUInt(rec.sampleCount);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>稳定点识别</th><td>");
        writeUInt(rec.stableDetectedCount);
        Esp32BaseWeb::sendChunk(" / ");
        writeUInt(rec.sampleCount);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>水量跨度</th><td>");
        writePermilleAsPercent(rec.volumeSpanPermille);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>脉冲跨度</th><td>");
        writePermilleAsPercent(rec.pulseSpanPermille);
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>样本内平均误差</th><td>");
        if (rec.sampleCount < 2) {
            Esp32BaseWeb::sendChunk("无法评估");
        } else {
            writePermilleAsPercent(rec.averageErrorPermille);
        }
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>样本内最大误差</th><td>");
        if (rec.sampleCount < 2) {
            Esp32BaseWeb::sendChunk("无法评估");
        } else {
            writePermilleAsPercent(rec.maxErrorPermille);
        }
        Esp32BaseWeb::sendChunk("</td></tr><tr><th>建议</th><td>");
        Esp32BaseWeb::writeHtmlEscaped(calibrationRecommendationAdvice(rec));
        Esp32BaseWeb::sendChunk("</td></tr></tbody></table></div>");
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>样本</th><th>实际</th><th>估算</th><th>误差</th></tr></thead><tbody>");
        for (uint8_t i = 0; i < rec.sampleCount; ++i) {
            Esp32BaseWeb::sendChunk("<tr><td>");
            writeUInt(rec.errors[i].sampleIndex + 1);
            Esp32BaseWeb::sendChunk("</td><td>");
            writeUInt(rec.errors[i].actualMl);
            Esp32BaseWeb::sendChunk(" ml</td><td>");
            writeUInt(rec.errors[i].estimatedMl);
            Esp32BaseWeb::sendChunk(" ml</td><td>");
            if (rec.errors[i].errorMl < 0) {
                Esp32BaseWeb::sendChunk("-");
                writeUInt(static_cast<uint32_t>(-rec.errors[i].errorMl));
            } else {
                writeUInt(static_cast<uint32_t>(rec.errors[i].errorMl));
            }
            Esp32BaseWeb::sendChunk(" ml / ");
            writeUInt(rec.errors[i].errorPermille);
            Esp32BaseWeb::sendChunk("‰</td></tr>");
        }
        Esp32BaseWeb::sendChunk("</tbody></table></div><p class='calibration-param-note'>以上诊断对应最近一次生成的候选参数。请在“流量参数”面板中确认并应用候选参数。</p>");
        Esp32BaseWeb::endPanel();
    }
    pageFooter();
}

void handleCalibrationSampleDetailPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint8_t displayIndex = 0;
    if (!readU8("index", &displayIndex) || displayIndex == 0 || displayIndex > FlowCalibration::sampleCount()) {
        pageHeader("样本详情");
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "样本不存在", "请选择一个有效的校准样本。");
        Esp32BaseWeb::sendChunk("<div class='actions'><a class='btnlink secondary' href='/irrigation/calibration'>返回流量校准</a></div>");
        pageFooter();
        return;
    }
    const uint8_t sampleIndex = static_cast<uint8_t>(displayIndex - 1);
    const FlowCalibration::Sample& sample = FlowCalibration::sample(sampleIndex);
    pageHeader("样本详情");
    Esp32BaseWeb::sendPageTitle("校准样本详情", "查看本次采集的脉冲明细、累计脉冲和滑动窗口流速变化。");
    Esp32BaseWeb::sendChunk("<style>"
                            ".calibration-detail-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin:8px 0 12px}"
                            ".calibration-detail-summary{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:8px;margin:10px 0 0}"
                            ".calibration-detail-summary span{border:1px solid var(--eb-border);border-radius:8px;background:var(--eb-surface-subtle);padding:8px 10px}"
                            ".calibration-detail-summary b{display:block;color:var(--eb-muted);font-size:.88rem;margin-bottom:2px}"
                            ".calibration-chart{margin:14px 0;border:1px solid var(--eb-border);border-radius:8px;padding:12px 14px 10px;background:linear-gradient(180deg,#fff,#f8fbfd);box-shadow:0 1px 2px rgba(15,23,42,.04)}"
                            ".calibration-chart-head{display:flex;justify-content:space-between;gap:12px;align-items:baseline;margin-bottom:8px}"
                            ".calibration-chart h3{margin:0;font-size:1rem}"
                            ".calibration-chart-head span{color:var(--eb-muted);font-size:.9rem}"
                            ".calibration-chart svg{width:100%;height:auto;max-height:240px;display:block;background:#fff;border:1px solid #e2e8f0;border-radius:6px}"
                            ".chart-grid{stroke:#e8eef5;stroke-width:1}"
                            ".chart-axis{stroke:#94a3b8;stroke-width:1.4}"
                            ".chart-tick{fill:#64748b;font-size:12px}"
                            ".chart-axis-title{fill:#0f172a;font-size:13px;font-weight:700}"
                            ".chart-startup{fill:#dff3f5;opacity:.78}"
                            ".chart-stable-marker{stroke:#0f7a86;stroke-width:2.2;stroke-dasharray:4 3}"
                            ".chart-line{stroke-width:2.6;stroke-linecap:round;stroke-linejoin:round}"
                            ".chart-line.cumulative{stroke:#0f7a86}"
                            ".chart-line.window{stroke:#2563ad}"
                            ".calibration-detail-note{color:var(--eb-muted);margin:8px 0}"
                            "@media(max-width:720px){.calibration-chart-head{display:block}.calibration-chart svg{max-height:none}}"
                            "</style>");
    Esp32BaseWeb::beginPanel("样本概览");
    Esp32BaseWeb::sendChunk("<div class='calibration-detail-grid'>");
    char text[40];
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(displayIndex));
    sendMetricCard("样本", text, "当前 RAM 样本");
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(sample.zoneId));
    sendMetricCard("水路", text, sample.valid ? "有效样本" : sample.invalidReason);
    snprintf(text, sizeof(text), "%lu ml", static_cast<unsigned long>(sample.actualMl));
    sendMetricCard("实际水量", text, "量杯实测输入");
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(sample.totalPulses));
    sendMetricCard("总脉冲", text, "完整采集总量");
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(sample.detailCapturedPulses));
    sendMetricCard("明细脉冲", text, sample.detailCaptureEndedReason);
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(sample.windowPulseCount));
    sendMetricCard("窗口点数", text, "200 ms 一点");
    Esp32BaseWeb::sendChunk("</div><div class='tablewrap'><table class='part'><tbody><tr><th>采集时长</th><td>");
    writeDurationMsHumanCompact(sample.durationMs);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>稳定开始</th><td>");
    writeDurationMsHumanCompact(sample.stableStartMs);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>启动脉冲</th><td>");
    writeUInt(sample.startupPulseInSample);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>稳定流速</th><td>");
    writeUInt(sample.stableRatePerMinuteX1000 / 1000UL);
    Esp32BaseWeb::sendChunk(" 脉冲/分钟</td></tr><tr><th>波动</th><td>");
    writeUInt(sample.rateVariationPermille);
    Esp32BaseWeb::sendChunk("‰</td></tr></tbody></table></div><div class='calibration-detail-summary'><span><b>横坐标</b>时间，按采集时长分成 6 段显示。</span><span><b>纵坐标</b>当前图表对应的脉冲数量，按 5 段显示。</span><span><b>稳定标记</b>浅色为启动阶段，虚线为稳定开始。</span></div><div class='actions'><a class='btnlink secondary' href='/irrigation/calibration'>返回流量校准</a></div>");
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::beginPanel("图表");
    if (sample.detailCapturedPulses == 0 && sample.windowPulseCount == 0) {
        Esp32BaseWeb::sendChunk("<p class='calibration-detail-note'>该样本没有可展示的图表明细。</p>");
    } else {
        Esp32BaseWeb::sendChunk("<p class='calibration-detail-note'>浅色区域表示稳定起点之前的启动阶段，竖线为识别出的稳定开始时间。</p>");
        if (sample.detailCapturedPulses > 0) {
            writeCumulativePulseChart(sampleIndex, sample);
        }
        if (sample.windowPulseCount > 0) {
            writeWindowPulseChart(sampleIndex, sample);
        }
    }
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
    Esp32BaseWeb::sendChunk(",\"averageFlowMlPerMin\":");
    writeUInt(averageFlowMlPerMin(record));
    Esp32BaseWeb::sendChunk(",\"flowStatsValid\":");
    writeBool(record.flowStatsValid);
    Esp32BaseWeb::sendChunk(",\"flowRateWindowSec\":");
    writeUInt(record.flowRateWindowSec);
    Esp32BaseWeb::sendChunk(",\"maxFlowMlPerMin\":");
    writeUInt(record.maxFlowMlPerMin);
    Esp32BaseWeb::sendChunk(",\"maxFlowFirstAtSec\":");
    writeUInt(record.maxFlowFirstAtSec);
    Esp32BaseWeb::sendChunk(",\"minFlowMlPerMin\":");
    writeUInt(record.minFlowMlPerMin);
    Esp32BaseWeb::sendChunk(",\"minFlowFirstAtSec\":");
    writeUInt(record.minFlowFirstAtSec);
    Esp32BaseWeb::sendChunk("}");
}

struct EventJsonContext {
    bool first;
};

void writeEventJson(const Esp32BaseAppEventRecord& event, void* user) {
    EventJsonContext* ctx = static_cast<EventJsonContext*>(user);
    if (!ctx->first) Esp32BaseWeb::sendChunk(",");
    ctx->first = false;
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(event.id);
    Esp32BaseWeb::sendChunk(",\"uptimeSec\":");
    writeUInt(event.uptimeSec);
    Esp32BaseWeb::sendChunk(",\"epochSec\":");
    writeUInt(event.epochSec);
    Esp32BaseWeb::sendChunk(",\"level\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32BaseAppEventLog::levelName(static_cast<Esp32BaseAppEventLog::Level>(event.level)));
    Esp32BaseWeb::sendChunk("\",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.source);
    Esp32BaseWeb::sendChunk("\",\"type\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.type);
    Esp32BaseWeb::sendChunk("\",\"reason\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.reason);
    Esp32BaseWeb::sendChunk("\",\"object\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.object);
    Esp32BaseWeb::sendChunk("\",\"code\":");
    writeUInt(event.code);
    Esp32BaseWeb::sendChunk(",\"value1\":");
    writeInt(event.value1);
    Esp32BaseWeb::sendChunk(",\"value2\":");
    writeInt(event.value2);
    Esp32BaseWeb::sendChunk(",\"value3\":");
    writeInt(event.value3);
    Esp32BaseWeb::sendChunk(",\"valueMask\":");
    writeUInt(event.valueMask);
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
    writeShortDateTimeHuman(record.startedEpoch);
    Esp32BaseWeb::sendChunk("</td><td>");
    if (record.endedUptimeMs >= record.startedUptimeMs) {
        writeDurationMsHumanCompact(record.endedUptimeMs - record.startedUptimeMs);
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td><td>");
    writeLitersFromMilliliters(record.estimatedMilliliters);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeAverageFlowRate(record);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeRecordPeakFlow(record.maxFlowMlPerMin, record.maxFlowFirstAtSec, record.flowStatsValid);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeRecordPeakFlow(record.minFlowMlPerMin, record.minFlowFirstAtSec, record.flowStatsValid);
    Esp32BaseWeb::sendChunk("</td></tr>");
}

struct EventTableContext {
    bool wrote;
};

void writeEventRow(const Esp32BaseAppEventRecord& event, void* user) {
    EventTableContext* ctx = static_cast<EventTableContext*>(user);
    ctx->wrote = true;
    Esp32BaseWeb::sendChunk("<tr><td>");
    writeUInt(event.id);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(appEventLevelLabel(event.level));
    Esp32BaseWeb::sendChunk("</td><td>");
    writeEventTimeHuman(event);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(appEventTypeLabel(event.type));
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(appEventSourceLabel(event.source));
    Esp32BaseWeb::sendChunk("</td><td>");
    writeEventZoneName(eventZoneId(event));
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
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>ID</th><th>水路</th><th>类型</th><th>结果</th><th>计划</th><th>目标时长</th><th>开始时间</th><th>运行时间</th><th>估算水量</th><th>平均流速</th><th>最大流速</th><th>最小流速</th></tr></thead><tbody>");
    RecordTableContext ctx = {false};
    (void)RecordStore::readLatest(offset, static_cast<uint16_t>(perPage), writeRecordRow, &ctx);
    if (!ctx.wrote) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='12'>暂无记录</td></tr>");
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
    readPaging(Esp32BaseAppEventLog::count(), 20, &page, &perPage, &offset);
    pageHeader("事件记录");
    Esp32BaseWeb::sendPageTitle("事件记录", "按时间倒序展示灌溉业务重要事件；设备系统日志请到 Esp32Base 查看。");
    if (Esp32BaseAppEventLog::faulted()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "事件日志存储故障", Esp32BaseAppEventLog::lastError());
    } else if (!Esp32BaseAppEventLog::isReady()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "事件日志未就绪", Esp32BaseAppEventLog::lastError());
    }
    Esp32BaseWeb::beginPanel("列表");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>ID</th><th>等级</th><th>时间</th><th>类型</th><th>来源</th><th>水路</th><th>说明</th></tr></thead><tbody>");
    EventTableContext ctx = {false};
    (void)Esp32BaseAppEventLog::readLatest(offset, static_cast<uint16_t>(perPage), writeEventRow, &ctx);
    if (!ctx.wrote) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>暂无事件</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::sendPagination({"/irrigation/events", nullptr, page, perPage, Esp32BaseAppEventLog::count()});
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

void handleFlowHistoryApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    uint16_t points[FlowMeter::MaxFlowHistoryPoints] = {};
    const uint16_t count = FlowMeter::readFlowHistory(zoneId, points, FlowMeter::MaxFlowHistoryPoints);
    beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"zoneId\":");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk(",\"intervalSec\":");
    writeUInt(FlowMeter::flowChartIntervalSec());
    Esp32BaseWeb::sendChunk(",\"historyMin\":");
    writeUInt(FlowMeter::flowChartHistoryMin());
    Esp32BaseWeb::sendChunk(",\"flowHistory\":[");
    for (uint16_t i = 0; i < count; ++i) {
        if (i) Esp32BaseWeb::sendChunk(",");
        writeUInt(points[i]);
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
        Esp32BaseWeb::sendChunk(",\"idleLeakDetectionEnabled\":");
        writeBool(system.idleLeakDetectionEnabled);
        Esp32BaseWeb::sendChunk(",\"idleLeakWindowSec\":");
        writeUInt(system.idleLeakWindowSec);
        Esp32BaseWeb::sendChunk(",\"idleLeakPulseThreshold\":");
        writeUInt(system.idleLeakPulseThreshold);
        Esp32BaseWeb::sendChunk(",\"calibrationSampleTarget\":");
        writeUInt(system.calibrationSampleTarget);
        Esp32BaseWeb::sendChunk(",\"calibrationMaxCaptureMin\":");
        writeUInt(system.calibrationMaxCaptureMin);
        Esp32BaseWeb::sendChunk(",\"calibrationDetailCaptureSec\":");
        writeUInt(system.calibrationDetailCaptureSec);
        Esp32BaseWeb::sendChunk(",\"calibrationDetailPulseLimit\":");
        writeUInt(system.calibrationDetailPulseLimit);
        Esp32BaseWeb::sendChunk(",\"flowRateWindowSec\":");
        writeUInt(system.flowRateWindowSec);
        Esp32BaseWeb::sendChunk(",\"flowChartIntervalSec\":");
        writeUInt(system.flowChartIntervalSec);
        Esp32BaseWeb::sendChunk(",\"flowChartHistoryMin\":");
        writeUInt(system.flowChartHistoryMin);
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
    if (!checkBusinessPost("irrigation.config")) {
        return;
    }
    Irrigation::SystemConfig system = SystemConfigStore::current();
    if (Esp32BaseWeb::hasParam("maxWateringDurationSec") && !readU32("maxWateringDurationSec", &system.maxWateringDurationSec)) sendError(400, "invalid_max_duration");
    else if (Esp32BaseWeb::hasParam("scheduleGraceSec") && !readU16("scheduleGraceSec", &system.scheduleGraceSec)) sendError(400, "invalid_schedule_grace");
    else if (Esp32BaseWeb::hasParam("manualDefaultDurationSec") && !readU32("manualDefaultDurationSec", &system.manualDefaultDurationSec)) sendError(400, "invalid_manual_default");
    else if (Esp32BaseWeb::hasParam("idleLeakDetectionEnabled") && !readBool("idleLeakDetectionEnabled", &system.idleLeakDetectionEnabled)) sendError(400, "invalid_leak_enabled");
    else if (Esp32BaseWeb::hasParam("idleLeakWindowSec") && !readU16("idleLeakWindowSec", &system.idleLeakWindowSec)) sendError(400, "invalid_leak_window");
    else if (Esp32BaseWeb::hasParam("idleLeakPulseThreshold") && !readU16("idleLeakPulseThreshold", &system.idleLeakPulseThreshold)) sendError(400, "invalid_leak_threshold");
    else if (Esp32BaseWeb::hasParam("calibrationSampleTarget") && !readU8("calibrationSampleTarget", &system.calibrationSampleTarget)) sendError(400, "invalid_calibration_sample_target");
    else if (Esp32BaseWeb::hasParam("calibrationMaxCaptureMin") && !readU16("calibrationMaxCaptureMin", &system.calibrationMaxCaptureMin)) sendError(400, "invalid_calibration_max_capture");
    else if (Esp32BaseWeb::hasParam("calibrationDetailCaptureSec") && !readU16("calibrationDetailCaptureSec", &system.calibrationDetailCaptureSec)) sendError(400, "invalid_calibration_detail_capture");
    else if (Esp32BaseWeb::hasParam("calibrationDetailPulseLimit") && !readU16("calibrationDetailPulseLimit", &system.calibrationDetailPulseLimit)) sendError(400, "invalid_calibration_detail_pulses");
    else if (Esp32BaseWeb::hasParam("flowRateWindowSec") && !readU16("flowRateWindowSec", &system.flowRateWindowSec)) sendError(400, "invalid_flow_rate_window");
    else if (Esp32BaseWeb::hasParam("flowChartIntervalSec") && !readU16("flowChartIntervalSec", &system.flowChartIntervalSec)) sendError(400, "invalid_flow_chart_interval");
    else if (Esp32BaseWeb::hasParam("flowChartHistoryMin") && !readU16("flowChartHistoryMin", &system.flowChartHistoryMin)) sendError(400, "invalid_flow_chart_history");
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

void handleCalibrationStartApi() {
    if (!checkBusinessPost("irrigation.calibration.start")) {
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    if (!FlowCalibration::start(zoneId, SystemConfigStore::current())) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationStopApi() {
    if (!checkBusinessPost("irrigation.calibration.stop")) {
        return;
    }
    if (!FlowCalibration::stop()) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationSampleApi() {
    if (!checkBusinessPost("irrigation.calibration.sample")) {
        return;
    }
    uint32_t actualMl = 0;
    if (!readU32("actualMl", &actualMl) || actualMl == 0) {
        sendError(400, "invalid_actual_ml");
        return;
    }
    if (!FlowCalibration::submitActualMilliliters(actualMl)) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationSampleUpdateApi() {
    if (!checkBusinessPost("irrigation.calibration.sample_update")) {
        return;
    }
    uint8_t sampleIndex = 0;
    uint32_t actualMl = 0;
    if (!readU8("sampleIndex", &sampleIndex) ||
        !readU32("actualMl", &actualMl) ||
        actualMl == 0) {
        sendError(400, "invalid_sample_update");
        return;
    }
    if (!FlowCalibration::updateActualMilliliters(sampleIndex, actualMl)) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationComputeApi() {
    if (!checkBusinessPost("irrigation.calibration.compute")) {
        return;
    }
    FlowCalibration::Recommendation rec = {};
    if (!FlowCalibration::computeRecommendation(&rec)) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    if (!FlowCalibration::saveCandidate()) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationCandidateSaveApi() {
    if (!checkBusinessPost("irrigation.calibration.candidate_save")) {
        return;
    }
    uint8_t zoneId = 0;
    Irrigation::FlowParameters params = {};
    if (!readZoneId(&zoneId) || !readFlowParameters(&params)) {
        sendError(400, "invalid_candidate");
        return;
    }
    if (!ZoneConfigStore::saveCandidate(zoneId, params)) {
        sendError(400, "candidate_save_failed");
        return;
    }
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationApplyApi() {
    if (!checkBusinessPost("irrigation.calibration.apply")) {
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    if (ZoneManager::isZoneBusy(zoneId)) {
        sendError(409, "zone_busy");
        return;
    }
    const Irrigation::ZoneConfig& before = ZoneConfigStore::get(zoneId);
    if (!before.candidateFlow.exists) {
        sendError(409, "no_candidate");
        return;
    }
    if (ZoneConfigStore::flowParametersEqual(before.flow, before.candidateFlow.params)) {
        sendError(409, "candidate_unchanged");
        return;
    }
    Irrigation::FlowParameters oldParams = {};
    Irrigation::FlowParameters newParams = {};
    if (!ZoneConfigStore::applyCandidate(zoneId, &oldParams, &newParams)) {
        sendError(409, "candidate_apply_failed");
        return;
    }
    (void)ZoneManager::reloadZone(zoneId);
    BusinessEventLog::appendFlowCandidateApplied(zoneId, oldParams, newParams, wantsRedirect() ? "web" : "api");
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationPreviousRestoreApi() {
    if (!checkBusinessPost("irrigation.calibration.previous_restore")) {
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    if (ZoneManager::isZoneBusy(zoneId)) {
        sendError(409, "zone_busy");
        return;
    }
    Irrigation::FlowParameters oldParams = {};
    Irrigation::FlowParameters newParams = {};
    if (!ZoneConfigStore::restorePrevious(zoneId, &oldParams, &newParams)) {
        sendError(409, "previous_restore_failed");
        return;
    }
    (void)ZoneManager::reloadZone(zoneId);
    BusinessEventLog::appendFlowPreviousRestored(zoneId, oldParams, newParams, wantsRedirect() ? "web" : "api");
    redirectOrOk("/irrigation/calibration");
}

void handleCalibrationClearApi() {
    if (!checkBusinessPost("irrigation.calibration.clear")) {
        return;
    }
    (void)FlowCalibration::clear();
    redirectOrOk("/irrigation/calibration");
}

void handleZoneStartApi() {
    if (!checkBusinessPost("irrigation.zone.start")) {
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
    if (!checkBusinessPost("irrigation.zone.stop", true)) {
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
    if (!checkBusinessPost("irrigation.zones.stop_all", true)) {
        return;
    }
    (void)ZoneManager::stopAll(stopSourceFromRequest());
    redirectOrOk();
}

void handleZoneConfigApi() {
    if (!checkBusinessPost("irrigation.zone.config")) {
        return;
    }
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone");
        return;
    }
    Irrigation::ZoneConfig zone = ZoneConfigStore::get(zoneId);
    const char* error = nullptr;
    if (Esp32BaseWeb::hasParam("name") && !Esp32BaseWeb::getParam("name", zone.name, sizeof(zone.name))) {
        error = "invalid_name";
    } else if (Esp32BaseWeb::hasParam("enabledPresent")) {
        zone.enabled = readCheckbox("enabled");
    } else if (Esp32BaseWeb::hasParam("enabled") && !readBool("enabled", &zone.enabled)) {
        error = "invalid_enabled";
    }
    if (!error && (Esp32BaseWeb::hasParam("startupPulseLimit") ||
                   Esp32BaseWeb::hasParam("startupEstimatedMl") ||
                   Esp32BaseWeb::hasParam("stablePulsePerLiter"))) {
        error = "flow_params_read_only";
    }
    if (!error && Esp32BaseWeb::hasParam("startTimeoutSec") && !readU16("startTimeoutSec", &zone.startTimeoutSec)) {
        error = "invalid_start_timeout";
    }
    if (!error && Esp32BaseWeb::hasParam("flowNoPulseTimeoutSec") && !readU16("flowNoPulseTimeoutSec", &zone.flowNoPulseTimeoutSec)) {
        error = "invalid_no_pulse_timeout";
    }
    if (!error && Esp32BaseWeb::hasParam("suppressErrorPresent")) {
        zone.suppressError = readCheckbox("suppressError");
    } else if (!error && Esp32BaseWeb::hasParam("suppressError") && !readBool("suppressError", &zone.suppressError)) {
        error = "invalid_suppress_error";
    }
    if (error) {
        sendError(400, error);
        return;
    }
    if (!ZoneConfigStore::set(zoneId, zone)) {
        sendError(400, "invalid_zone_config");
        return;
    }
    (void)ZoneManager::reloadZone(zoneId);
    redirectOrOk("/irrigation/zones");
}

void handleZoneClearErrorApi() {
    if (!checkBusinessPost("irrigation.zone.clear_error")) {
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
    redirectOrOk("/index?alert_cleared=1");
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
    if (!checkBusinessPost("irrigation.plan.create")) {
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
    if (!checkBusinessPost("irrigation.plan.enable")) {
        return;
    }
    (void)updatePlanEnabled(true);
}

void handlePlanDisableApi() {
    if (!checkBusinessPost("irrigation.plan.disable")) {
        return;
    }
    (void)updatePlanEnabled(false);
}

void handlePlanDeleteApi() {
    if (!checkBusinessPost("irrigation.plan.delete")) {
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
    if (!checkBusinessPost("irrigation.plan.update")) {
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
    if (!checkBusinessPost(skip ? "irrigation.schedule.skip" : "irrigation.schedule.unskip")) {
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
    BusinessEventLog::appendScheduleSkipDecision(planId,
                                                 ymd,
                                                 static_cast<Irrigation::SkipReason>(reasonRaw),
                                                 skip,
                                                 wantsRedirect() ? "web" : "api");
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
    writeUInt(Esp32BaseAppEventLog::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(Esp32BaseAppEventLog::capacity());
    Esp32BaseWeb::sendChunk(",\"events\":[");
    EventJsonContext ctx = {true};
    (void)Esp32BaseAppEventLog::readLatest(0, 50, writeEventJson, &ctx);
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

}

namespace IrrigationWeb {

void begin() {
    const bool overviewOk = Esp32BaseWeb::addPage("/index", "首页", handleOverviewPage);
    const bool plansOk = Esp32BaseWeb::addPage("/irrigation/plans", "计划", handlePlansPage);
    const bool settingsOk = Esp32BaseWeb::addPage("/irrigation/zones", "水路管理", handleSettingsPage);
    const bool calibrationPageOk = Esp32BaseWeb::addPage("/irrigation/calibration", "流量校准", handleCalibrationPage);
    const bool calibrationSamplePageOk = Esp32BaseWeb::addRoute("/irrigation/calibration/sample", Esp32BaseWeb::METHOD_GET, handleCalibrationSampleDetailPage);
    const bool recordsPageOk = Esp32BaseWeb::addPage("/irrigation/records", "浇水记录", handleRecordsPage);
    const bool eventsPageOk = Esp32BaseWeb::addPage("/irrigation/events", "事件记录", handleEventsPage);
    const bool planEditOk = Esp32BaseWeb::addRoute("/irrigation/plan", Esp32BaseWeb::METHOD_GET, handlePlanEditPage);
    const bool statusOk = Esp32BaseWeb::addRoute("/api/v1/status", Esp32BaseWeb::METHOD_GET, handleStatusApi);
    const bool flowHistoryOk = Esp32BaseWeb::addRoute("/api/v1/flow/history", Esp32BaseWeb::METHOD_GET, handleFlowHistoryApi);
    const bool configOk = Esp32BaseWeb::addApi("/api/v1/config", handleConfigApi);
    const bool zoneStartOk = Esp32BaseWeb::addRoute("/api/v1/zone/start", Esp32BaseWeb::METHOD_POST, handleZoneStartApi);
    const bool zoneStopOk = Esp32BaseWeb::addRoute("/api/v1/zone/stop", Esp32BaseWeb::METHOD_POST, handleZoneStopApi);
    const bool allStopOk = Esp32BaseWeb::addRoute("/api/v1/zones/stop-all", Esp32BaseWeb::METHOD_POST, handleZonesStopAllApi);
    const bool zoneConfigOk = Esp32BaseWeb::addRoute("/api/v1/zone/config", Esp32BaseWeb::METHOD_POST, handleZoneConfigApi);
    const bool clearErrorOk = Esp32BaseWeb::addRoute("/api/v1/zone/clear-error", Esp32BaseWeb::METHOD_POST, handleZoneClearErrorApi);
    const bool calibrationStartOk = Esp32BaseWeb::addRoute("/api/v1/calibration/start", Esp32BaseWeb::METHOD_POST, handleCalibrationStartApi);
    const bool calibrationStopOk = Esp32BaseWeb::addRoute("/api/v1/calibration/stop", Esp32BaseWeb::METHOD_POST, handleCalibrationStopApi);
    const bool calibrationSampleOk = Esp32BaseWeb::addRoute("/api/v1/calibration/sample", Esp32BaseWeb::METHOD_POST, handleCalibrationSampleApi);
    const bool calibrationSampleUpdateOk = Esp32BaseWeb::addRoute("/api/v1/calibration/sample/update", Esp32BaseWeb::METHOD_POST, handleCalibrationSampleUpdateApi);
    const bool calibrationComputeOk = Esp32BaseWeb::addRoute("/api/v1/calibration/compute", Esp32BaseWeb::METHOD_POST, handleCalibrationComputeApi);
    const bool calibrationCandidateSaveOk = Esp32BaseWeb::addRoute("/api/v1/calibration/candidate", Esp32BaseWeb::METHOD_POST, handleCalibrationCandidateSaveApi);
    const bool calibrationApplyOk = Esp32BaseWeb::addRoute("/api/v1/calibration/apply", Esp32BaseWeb::METHOD_POST, handleCalibrationApplyApi);
    const bool calibrationRestoreOk = Esp32BaseWeb::addRoute("/api/v1/calibration/previous/restore", Esp32BaseWeb::METHOD_POST, handleCalibrationPreviousRestoreApi);
    const bool calibrationClearOk = Esp32BaseWeb::addRoute("/api/v1/calibration/clear", Esp32BaseWeb::METHOD_POST, handleCalibrationClearApi);
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
    ESP32BASE_LOG_I("irrigation.web", "routes overview=%s plans=%s zones=%s calibration=%s calSamplePage=%s recordsPage=%s eventsPage=%s planEdit=%s status=%s flowHistory=%s config=%s zoneStart=%s zoneStop=%s allStop=%s zoneConfig=%s clearError=%s calStart=%s calStop=%s calSample=%s calSampleUpdate=%s calCompute=%s calCandidateSave=%s calApply=%s calRestore=%s calClear=%s plansApi=%s planCreate=%s planUpdate=%s planDelete=%s planEnable=%s planDisable=%s skip=%s unskip=%s records=%s events=%s firmware=%s",
                    overviewOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    settingsOk ? "ok" : "fail",
                    calibrationPageOk ? "ok" : "fail",
                    calibrationSamplePageOk ? "ok" : "fail",
                    recordsPageOk ? "ok" : "fail",
                    eventsPageOk ? "ok" : "fail",
                    planEditOk ? "ok" : "fail",
                    statusOk ? "ok" : "fail",
                    flowHistoryOk ? "ok" : "fail",
                    configOk ? "ok" : "fail",
                    zoneStartOk ? "ok" : "fail",
                    zoneStopOk ? "ok" : "fail",
                    allStopOk ? "ok" : "fail",
                    zoneConfigOk ? "ok" : "fail",
                    clearErrorOk ? "ok" : "fail",
                    calibrationStartOk ? "ok" : "fail",
                    calibrationStopOk ? "ok" : "fail",
                    calibrationSampleOk ? "ok" : "fail",
                    calibrationSampleUpdateOk ? "ok" : "fail",
                    calibrationComputeOk ? "ok" : "fail",
                    calibrationCandidateSaveOk ? "ok" : "fail",
                    calibrationApplyOk ? "ok" : "fail",
                    calibrationRestoreOk ? "ok" : "fail",
                    calibrationClearOk ? "ok" : "fail",
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
                    IrrigationVersion::FirmwareVersion);
}

}
