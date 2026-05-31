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

void writeHeadExtra() {
    Esp32BaseWeb::sendChunk("<style>"
                            ".zg{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}"
                            ".zc{border:1px solid var(--border);border-radius:8px;padding:10px;background:var(--panel)}"
                            ".zt{display:flex;align-items:center;justify-content:space-between;gap:8px}"
                            ".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}"
                            ".planrow{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;border-bottom:1px solid var(--border);padding:8px 0}"
                            ".planmeta{color:var(--muted);font-size:.9em}"
                            "</style>"
                            "<script>function once(f){if(f.dataset.busy)return false;f.dataset.busy=1;var b=f.querySelector('[type=submit]');if(b)b.disabled=true;return true;}</script>");
}

void pageHeader(const char* title) {
    Esp32BaseWeb::sendHeader(title);
}

void handleOverviewPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    pageHeader("灌溉总览");
    Esp32BaseWeb::sendPageTitle("灌溉总览", "固定 4 路 Zone，按水路独立运行。");
    Esp32BaseWeb::beginPanel("当前状态");
    Esp32BaseWeb::sendChunk("<div class='zg'>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        const Irrigation::ZoneConfig& config = ZoneManager::config(zoneId);
        Esp32BaseWeb::sendChunk("<div class='zc'><div class='zt'><strong>");
        Esp32BaseWeb::writeHtmlEscaped(config.name);
        Esp32BaseWeb::sendChunk("</strong><span class='tag");
        Esp32BaseWeb::sendChunk(uiToneForState(status.state));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(Irrigation::zoneStateName(status.state));
        Esp32BaseWeb::sendChunk("</span></div><p>目标 ");
        writeDuration(status.targetSec);
        Esp32BaseWeb::sendChunk("，剩余 ");
        writeDuration(status.remainingSec);
        Esp32BaseWeb::sendChunk("</p><p>脉冲 ");
        writeUInt(status.pulses);
        Esp32BaseWeb::sendChunk("，估算 ");
        writeUInt(status.estimatedMilliliters);
        Esp32BaseWeb::sendChunk(" ml</p><div class='actions'>");
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/start' onsubmit=\"return confirm('确认启动该水路？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
        writeHiddenU32("durationSec", SystemConfigStore::current().manualDefaultDurationSec);
        Esp32BaseWeb::sendChunk("<button type='submit'");
        Esp32BaseWeb::sendChunk((status.enabled && !status.busy && !status.errorActive && !ZoneManager::leakAlertActive()) ? "" : " disabled");
        Esp32BaseWeb::sendChunk(">启动</button></form>");
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/stop' onsubmit=\"return confirm('确认停止该水路？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
        Esp32BaseWeb::sendChunk("<button type='submit'");
        Esp32BaseWeb::sendChunk(status.busy ? "" : " disabled");
        Esp32BaseWeb::sendChunk(">停止</button></form>");
        if (status.errorActive) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/zone/clear-error' onsubmit=\"return confirm('确认现场已处理并清除异常？')&&once(this)\">");
            writeOnePostHidden("source", "web_page");
            writeHiddenU32("zoneId", zoneId);
            Esp32BaseWeb::sendChunk("<button type='submit'>清除异常</button></form>");
        }
        Esp32BaseWeb::sendChunk("</div></div>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><form method='post' action='/api/v1/zones/stop-all' onsubmit=\"return confirm('确认停止全部水路？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    Esp32BaseWeb::sendChunk("<button class='danger' type='submit'>全部停止</button></form></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void writePlanRow(const Irrigation::PlanDefinition& plan) {
    Esp32BaseWeb::sendChunk("<div class='planrow'><div><strong>");
    Esp32BaseWeb::writeHtmlEscaped(plan.name);
    Esp32BaseWeb::sendChunk("</strong><div class='planmeta'>");
    writeTime(plan.timeHour, plan.timeMinute);
    Esp32BaseWeb::sendChunk(" · ");
    writeDuration(plan.durationSec);
    Esp32BaseWeb::sendChunk(" · ");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk(" 天循环 mask=");
    writeUInt(plan.cycleMask);
    Esp32BaseWeb::sendChunk("</div></div><div class='actions'><a class='btnlink info' href='/irrigation/plan?planId=");
    writeUInt(plan.planId);
    Esp32BaseWeb::sendChunk("'>编辑</a><form method='post' action='");
    Esp32BaseWeb::sendChunk(plan.enabled ? "/api/v1/plan/disable" : "/api/v1/plan/enable");
    Esp32BaseWeb::sendChunk("' onsubmit=\"return confirm('确认切换计划状态？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    writeHiddenU32("planId", plan.planId);
    Esp32BaseWeb::sendChunk("<button type='submit'>");
    Esp32BaseWeb::sendChunk(plan.enabled ? "停用" : "启用");
    Esp32BaseWeb::sendChunk("</button></form><form method='post' action='/api/v1/plan/delete' onsubmit=\"return confirm('确认删除该计划？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    writeHiddenU32("planId", plan.planId);
    Esp32BaseWeb::sendChunk("<button class='danger' type='submit'>删除</button></form></div></div>");
}

void handlePlansPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    pageHeader("计划");
    Esp32BaseWeb::sendPageTitle("计划", "每个 Zone 最多 6 条计划。");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        Esp32BaseWeb::beginPanel(ZoneManager::config(zoneId).name);
        Esp32BaseWeb::sendChunk("<div class='actions'><form method='post' action='/api/v1/plan/create' onsubmit=\"return confirm('确认为该水路新增计划？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
        Esp32BaseWeb::sendChunk("<button type='submit'");
        Esp32BaseWeb::sendChunk(PlanStore::countForZone(zoneId) < Irrigation::MaxPlansPerZone ? "" : " disabled");
        Esp32BaseWeb::sendChunk(">新增计划</button></form></div>");
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(zoneId, slot);
            if (plan.exists) {
                writePlanRow(plan);
            }
        }
        if (PlanStore::countForZone(zoneId) == 0) {
            Esp32BaseWeb::sendChunk("<p class='muted'>暂无计划</p>");
        }
        Esp32BaseWeb::endPanel();
    }
    Esp32BaseWeb::sendFooter();
}

void handlePlanEditPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint32_t planId = 0;
    Irrigation::PlanDefinition plan = {};
    if (!readU32("planId", &planId) || !PlanStore::getById(planId, &plan)) {
        pageHeader("计划编辑");
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "计划不存在", "请返回计划页重新选择。");
        Esp32BaseWeb::sendFooter();
        return;
    }
    pageHeader("计划编辑");
    Esp32BaseWeb::beginPanel(plan.name);
    Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/plan/update' onsubmit=\"return confirm('确认保存计划？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    writeHiddenU32("planId", plan.planId);
    Esp32BaseWeb::sendChunk("<div class='fieldgrid'>"
                            "<p class='field med'><label>名称</label><input name='name' maxlength='31' value='");
    Esp32BaseWeb::writeHtmlEscaped(plan.name);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>状态</label><select name='enabled'><option value='0'");
    Esp32BaseWeb::sendChunk(plan.enabled ? "" : " selected");
    Esp32BaseWeb::sendChunk(">停用</option><option value='1'");
    Esp32BaseWeb::sendChunk(plan.enabled ? " selected" : "");
    Esp32BaseWeb::sendChunk(">启用</option></select></p><p class='field short'><label>时间</label><input name='time' type='time' value='");
    writeTime(plan.timeHour, plan.timeMinute);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>时长秒</label><input name='durationSec' type='number' min='1' max='");
    writeUInt(SystemConfigStore::current().maxWateringDurationSec);
    Esp32BaseWeb::sendChunk("' value='");
    writeUInt(plan.durationSec);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>循环天数</label><input name='cycleDays' type='number' min='1' max='30' value='");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>循环 mask</label><input name='cycleMask' type='number' min='1' value='");
    writeUInt(plan.cycleMask);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>起始日期</label><input name='cycleStartYmd' type='date' value='");
    writeYmdInput(plan.cycleStartYmd);
    Esp32BaseWeb::sendChunk("'></p></div><div class='actions'><button type='submit'>保存</button><a class='btnlink' href='/irrigation/plans'>返回</a></div></form>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleSettingsPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    pageHeader("设置");
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    Esp32BaseWeb::beginPanel("系统配置");
    Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/config' onsubmit=\"return confirm('确认保存系统配置？')&&once(this)\">");
    writeOnePostHidden("source", "web_page");
    Esp32BaseWeb::sendChunk("<div class='fieldgrid'><p class='field short'><label>最大出水秒</label><input name='maxWateringDurationSec' type='number' min='60' max='86400' value='");
    writeUInt(system.maxWateringDurationSec);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>调度宽限秒</label><input name='scheduleGraceSec' type='number' min='1' max='60' value='");
    writeUInt(system.scheduleGraceSec);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>手动默认秒</label><input name='manualDefaultDurationSec' type='number' min='1' value='");
    writeUInt(system.manualDefaultDurationSec);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>漏水窗口秒</label><input name='idleLeakWindowSec' type='number' min='1' max='300' value='");
    writeUInt(system.idleLeakWindowSec);
    Esp32BaseWeb::sendChunk("'></p><p class='field short'><label>漏水脉冲</label><input name='idleLeakPulseThreshold' type='number' min='1' max='1000' value='");
    writeUInt(system.idleLeakPulseThreshold);
    Esp32BaseWeb::sendChunk("'></p>");
    for (uint8_t i = 0; i < 6; ++i) {
        Esp32BaseWeb::sendChunk("<p class='field short'><label>预设 ");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk("</label><input name='preset");
        writeUInt(i);
        Esp32BaseWeb::sendChunk("' type='number' min='1' value='");
        writeUInt(system.durationPresets[i]);
        Esp32BaseWeb::sendChunk("'></p>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><button type='submit'>保存系统配置</button></div></form>");
    Esp32BaseWeb::endPanel();

    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
        Esp32BaseWeb::beginPanel(zone.name);
        Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/zone/config' onsubmit=\"return confirm('确认保存水路配置？')&&once(this)\">");
        writeOnePostHidden("source", "web_page");
        writeHiddenU32("zoneId", zoneId);
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
        Esp32BaseWeb::sendChunk(">是</option></select></p></div><div class='actions'><button type='submit'>保存水路配置</button></div></form>");
        Esp32BaseWeb::endPanel();
    }
    Esp32BaseWeb::sendFooter();
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

void handleDataPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    pageHeader("记录");
    Esp32BaseWeb::beginPanel("近期记录");
    Esp32BaseWeb::sendInfoRowCompactLink("浇水记录", "最近 50 条浇水记录，包含计划快照和配置快照。", nullptr, "/api/v1/records", "打开");
    Esp32BaseWeb::sendInfoRowCompactLink("事件记录", "最近 50 条系统和业务事件。", nullptr, "/api/v1/events", "打开");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
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
            redirectOrOk("/irrigation/settings");
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
    if (!readZoneId(&zoneId) || !readU32("durationSec", &durationSec)) {
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
        redirectOrOk("/irrigation/settings");
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
    if (!readZoneId(&zoneId) || !PlanStore::create(zoneId, &plan)) {
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
    if (Esp32BaseWeb::hasParam("name") && !Esp32BaseWeb::getParam("name", plan.name, sizeof(plan.name))) sendError(400, "invalid_name");
    else if (Esp32BaseWeb::hasParam("enabled") && !readBool("enabled", &plan.enabled)) sendError(400, "invalid_enabled");
    else if (Esp32BaseWeb::hasParam("time") && !readTimeParam("time", &plan.timeHour, &plan.timeMinute)) sendError(400, "invalid_time");
    else if (Esp32BaseWeb::hasParam("durationSec") && !readU32("durationSec", &plan.durationSec)) sendError(400, "invalid_duration");
    else if (Esp32BaseWeb::hasParam("cycleDays") && !readU8("cycleDays", &plan.cycleDays)) sendError(400, "invalid_cycle_days");
    else if (Esp32BaseWeb::hasParam("cycleMask") && !readU32("cycleMask", &plan.cycleMask)) sendError(400, "invalid_cycle_mask");
    else if (Esp32BaseWeb::hasParam("cycleStartYmd") && !readYmd("cycleStartYmd", &plan.cycleStartYmd)) sendError(400, "invalid_cycle_start");
    else if (plan.durationSec > SystemConfigStore::current().maxWateringDurationSec) sendError(400, "duration_exceeds_system_limit");
    else if (!PlanStore::set(planId, plan)) sendError(400, "plan_save_failed");
    else redirectOrOk("/irrigation/plans");
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
    Esp32BaseWeb::setHeadExtraCallback(writeHeadExtra);
    const bool overviewOk = Esp32BaseWeb::addPage("/irrigation", "灌溉", handleOverviewPage);
    const bool plansOk = Esp32BaseWeb::addPage("/irrigation/plans", "计划", handlePlansPage);
    const bool settingsOk = Esp32BaseWeb::addPage("/irrigation/settings", "设置", handleSettingsPage);
    const bool dataOk = Esp32BaseWeb::addPage("/irrigation/data", "记录", handleDataPage);
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
    ESP32BASE_LOG_I("irrigation.web", "routes overview=%s plans=%s settings=%s data=%s planEdit=%s status=%s config=%s zoneStart=%s zoneStop=%s allStop=%s zoneConfig=%s clearError=%s plansApi=%s planCreate=%s planUpdate=%s planDelete=%s planEnable=%s planDisable=%s skip=%s unskip=%s records=%s events=%s factory=%s firmware=%s",
                    overviewOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    settingsOk ? "ok" : "fail",
                    dataOk ? "ok" : "fail",
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
