#include "web/IrrigationWeb.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Pins.h"
#include "Version.h"
#include "domain/FlowMeter.h"
#include "domain/LeakMonitor.h"
#include "domain/MaintenanceService.h"
#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/WateringSession.h"
#include "storage/EventStore.h"
#include "storage/PlanSkipStore.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace {

void writeBool(bool value) {
    Esp32BaseWeb::sendChunk(value ? "true" : "false");
}

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

void writeUIntText(uint32_t value) {
    writeUInt(value);
}

void beginJson(int code = 200) {
    (void)Esp32BaseWeb::beginResponse(code, "application/json", nullptr);
}

void endJson() {
    Esp32BaseWeb::endResponse();
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

bool readUIntParam(const char* name, uint16_t* value) {
    char text[16] = "";
    if (!Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    char* end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed < 0 || parsed > 65535) {
        return false;
    }
    *value = static_cast<uint16_t>(parsed);
    return true;
}

bool readU32Param(const char* name, uint32_t* value) {
    char text[16] = "";
    if (!Esp32BaseWeb::getParam(name, text, sizeof(text))) {
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

bool readBoolParam(const char* name, bool* value) {
    char text[8] = "";
    if (!Esp32BaseWeb::getParam(name, text, sizeof(text))) {
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

bool readModeParam(const char* name, SettingsStore::ExecutionMode* mode) {
    char text[20] = "";
    return Esp32BaseWeb::getParam(name, text, sizeof(text)) && SettingsStore::parseExecutionMode(text, mode);
}

bool readMinuteOfDayParam(const char* name, uint16_t* minuteOfDay) {
    char text[8] = "";
    if (!minuteOfDay || !Esp32BaseWeb::getParam(name, text, sizeof(text)) || strlen(text) != 5 || text[2] != ':') {
        return false;
    }
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '9' || text[4] < '0' || text[4] > '9') {
        return false;
    }
    const uint8_t hour = static_cast<uint8_t>((text[0] - '0') * 10 + (text[1] - '0'));
    const uint8_t minute = static_cast<uint8_t>((text[3] - '0') * 10 + (text[4] - '0'));
    if (hour > 23 || minute > 59) {
        return false;
    }
    *minuteOfDay = static_cast<uint16_t>(hour * 60U + minute);
    return true;
}

uint16_t minutesToSeconds(uint16_t minutes) {
    return minutes > 240 ? 0 : static_cast<uint16_t>(minutes * 60U);
}

bool readDurationSecondsParam(const char* name, uint16_t* seconds) {
    uint16_t minutes = 0;
    if (!seconds || !readUIntParam(name, &minutes) || minutes < 1 || minutes > 240) {
        return false;
    }
    *seconds = static_cast<uint16_t>(minutes * 60U);
    return true;
}

bool readPlanDurationSecondsParam(const char* name, uint16_t* seconds) {
    uint16_t minutes = 0;
    if (!seconds || !readUIntParam(name, &minutes) || minutes > 240) {
        return false;
    }
    *seconds = static_cast<uint16_t>(minutes * 60U);
    return true;
}

void writeMinutesFromSeconds(uint32_t seconds) {
    writeUInt(seconds / 60UL);
}

void writeFixed2FromX100(uint32_t value) {
    char text[20];
    snprintf(text, sizeof(text), "%lu.%02lu", static_cast<unsigned long>(value / 100UL), static_cast<unsigned long>(value % 100UL));
    Esp32BaseWeb::sendChunk(text);
}

void writeLitersFromMl(uint32_t ml) {
    char text[20];
    snprintf(text, sizeof(text), "%lu.%03lu L", static_cast<unsigned long>(ml / 1000UL), static_cast<unsigned long>(ml % 1000UL));
    Esp32BaseWeb::sendChunk(text);
}

uint8_t readLimitParam(uint8_t defaultLimit, uint8_t maxLimit) {
    uint16_t raw = defaultLimit;
    if (Esp32BaseWeb::hasParam("limit") && (!readUIntParam("limit", &raw) || raw == 0)) {
        return defaultLimit;
    }
    if (raw > maxLimit) {
        raw = maxLimit;
    }
    return static_cast<uint8_t>(raw);
}

const char* modeLabel(SettingsStore::ExecutionMode mode) {
    return mode == SettingsStore::MODE_SEQUENTIAL ? "顺序浇水" : "同时浇水";
}

const char* modeName(SettingsStore::ExecutionMode mode) {
    return SettingsStore::executionModeName(mode);
}

const char* roadStateLabel(WateringSession::RoadState state) {
    switch (state) {
        case WateringSession::ROAD_IDLE: return "关闭";
        case WateringSession::ROAD_PENDING: return "等待";
        case WateringSession::ROAD_RUNNING: return "浇水中";
        case WateringSession::ROAD_DONE: return "完成";
        case WateringSession::ROAD_STOPPED: return "已停止";
        case WateringSession::ROAD_ERROR: return "异常";
        default: return "未知";
    }
}

uint32_t makeYmd(const tm& value) {
    return static_cast<uint32_t>(value.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(value.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(value.tm_mday);
}

bool localDateFromOffset(int8_t offsetDays, tm* out) {
    if (!out || !Esp32BaseNtp::isTimeSynced()) {
        return false;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    const time_t target = now + static_cast<time_t>(offsetDays) * 86400L;
    return localtime_r(&target, out) != nullptr;
}

uint32_t currentYmd() {
    tm now = {};
    return localDateFromOffset(0, &now) ? makeYmd(now) : 0;
}

uint16_t currentMinuteOfDay() {
    tm now = {};
    if (!localDateFromOffset(0, &now)) {
        return 0;
    }
    return static_cast<uint16_t>(now.tm_hour * 60 + now.tm_min);
}

void writeYmd(uint32_t ymd) {
    char text[12];
    snprintf(text, sizeof(text), "%04lu-%02lu-%02lu",
             static_cast<unsigned long>(ymd / 10000UL),
             static_cast<unsigned long>((ymd / 100UL) % 100UL),
             static_cast<unsigned long>(ymd % 100UL));
    Esp32BaseWeb::sendChunk(text);
}

void writeMinuteOfDay(uint16_t minuteOfDay) {
    char text[8];
    snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(minuteOfDay / 60), static_cast<unsigned>(minuteOfDay % 60));
    Esp32BaseWeb::sendChunk(text);
}

void writeSelected(bool selected) {
    if (selected) {
        Esp32BaseWeb::sendChunk(" selected");
    }
}

void writeChecked(bool checked) {
    if (checked) {
        Esp32BaseWeb::sendChunk(" checked");
    }
}

void writeModeOptions(SettingsStore::ExecutionMode selected) {
    Esp32BaseWeb::sendChunk("<option value='simultaneous'");
    writeSelected(selected == SettingsStore::MODE_SIMULTANEOUS);
    Esp32BaseWeb::sendChunk(">同时浇水</option><option value='sequential'");
    writeSelected(selected == SettingsStore::MODE_SEQUENTIAL);
    Esp32BaseWeb::sendChunk(">顺序浇水</option>");
}

uint32_t flowRateLiterPerMinuteX100(uint8_t road) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return 0;
    }
    const SettingsStore::RoadConfig& roadConfig = SettingsStore::current().roads[road - 1];
    if (roadConfig.pulsePerLiter == 0) {
        return 0;
    }
    const uint64_t pulseRateX1000 = FlowMeter::pulseRatePerMinuteX1000(road);
    const uint64_t value = pulseRateX1000 * static_cast<uint64_t>(roadConfig.calibrationX1000) * 100ULL;
    const uint64_t denominator = static_cast<uint64_t>(roadConfig.pulsePerLiter) * 1000ULL * 1000ULL;
    return static_cast<uint32_t>(value / denominator);
}

void writeCss() {
    Esp32BaseWeb::sendChunk(
        "<style>"
        ":root{color-scheme:light;--bg:#f7f8fa;--surface:#fff;--muted:#667085;--text:#17202a;--line:#d8dee6;--soft:#edf0f3;--primary:#146c5f;--ok:#087443;--warn:#a15c07;--danger:#b42318}"
        "*{box-sizing:border-box}body{max-width:none;margin:0;background:var(--bg);color:var(--text);font-size:14px;line-height:1.5;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif}"
        "a{color:var(--primary);text-decoration:none}a:hover{text-decoration:underline}"
        "body>nav{position:sticky;top:0;z-index:2;display:flex;gap:4px;align-items:center;overflow-x:auto;padding:9px 18px;background:rgba(255,255,255,.98);border-bottom:1px solid var(--line)}"
        "body>nav a{display:inline-flex;align-items:center;flex:0 0 auto;min-height:34px;padding:0 10px;border-radius:6px;color:#344054;font-weight:600}body>nav a.active,body>nav a.current{background:#e7f1ef;color:var(--primary)}"
        ".shell{width:min(1100px,calc(100% - 28px));margin:0 auto;padding:20px 0 34px}.page-overview .shell{width:min(980px,calc(100% - 28px))}.page-table .shell{width:min(1120px,calc(100% - 36px))}.page-settings .shell{width:min(860px,calc(100% - 28px))}"
        ".page-head{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:14px}h1,h2,h3,p{margin-top:0}h1{margin-bottom:4px;font-size:26px;line-height:1.25}.subtitle,.note{margin-bottom:0;color:var(--muted)}"
        ".grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:12px;align-items:start}.panel{grid-column:span 12;min-width:0;padding:14px;background:var(--surface);border:1px solid var(--line);border-radius:8px}.span-4{grid-column:span 4}.span-6{grid-column:span 6}.span-8{grid-column:span 8}.span-12{grid-column:span 12}.panel h2{margin-bottom:12px;font-size:16px}.panel h3{margin:16px 0 8px;color:#475467;font-size:14px}"
        ".panel-titlebar{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:12px}.panel-titlebar h2{margin-bottom:0}.panel-tools,.actions{display:flex;flex-wrap:wrap;gap:8px}.panel-tools{justify-content:flex-end}.actions{margin-top:14px}.form-actions{justify-content:flex-end}.title-date{margin-left:8px;color:var(--muted);font-size:13px;font-weight:500}"
        ".overview-status{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.overview-status div{display:flex;align-items:center;justify-content:space-between;gap:8px;min-width:0;padding:8px 10px;border:1px solid var(--soft);border-radius:6px;background:#f9fafb}.overview-status span,.matrix-head,.matrix-label{color:var(--muted)}.overview-status strong{overflow-wrap:anywhere}"
        ".valve-matrix{display:grid;grid-template-columns:minmax(56px,.7fr) repeat(2,minmax(0,1fr));border:1px solid var(--line);border-radius:8px;overflow:hidden}.valve-matrix>*{min-width:0;padding:8px 10px;border-right:1px solid var(--soft);border-bottom:1px solid var(--soft)}.valve-matrix>:nth-child(3n){border-right:0}.valve-matrix>:nth-last-child(-n+3){border-bottom:0}.matrix-head{background:#f9fafb;font-size:13px;font-weight:650}.matrix-label{font-weight:650}.status-compact{grid-template-columns:1fr;margin-bottom:12px}.valve-action{display:flex;align-items:center;gap:8px;margin-top:12px}"
        ".badge{display:inline-flex;align-items:center;min-height:24px;padding:2px 8px;border-radius:999px;background:#f0f2f4;color:#475467;font-size:13px;font-weight:650;white-space:nowrap}.badge.ok{background:#e7f5ee;color:var(--ok)}.badge.warn{background:#fff5df;color:var(--warn)}.badge.danger{background:#fff0ee;color:var(--danger)}"
        "input,select{min-width:0;width:100%;min-height:36px;padding:7px 9px;border:1px solid #cfd6df;border-radius:6px;background:#fff;color:var(--text);font:inherit}input[type=checkbox]{position:absolute;opacity:0;width:1px;min-height:0;margin:0;padding:0;border:0}input:disabled,select:disabled{color:#98a2b3;background:#f3f4f6}.field-grid{display:grid;grid-template-columns:repeat(3,minmax(120px,1fr));gap:12px 14px}.field{display:grid;gap:6px;min-width:0}.field label{color:#475467;font-size:13px;font-weight:650}.check-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(92px,1fr));gap:8px}.check-item{position:relative;display:flex;align-items:center;justify-content:center;min-height:36px;padding:0 10px;border:1px solid var(--soft);border-radius:6px;background:#fff;color:#344054;font-weight:650}.check-item:has(input:checked){border-color:var(--primary);background:#e7f1ef;color:var(--primary)}"
        "button,.button,input[type=submit]{display:inline-flex;align-items:center;justify-content:center;min-height:32px;padding:0 11px;border:1px solid var(--primary);border-radius:6px;background:var(--primary);color:#fff;font:inherit;font-weight:650;cursor:default}.button.secondary,button.secondary,input.secondary{border-color:#cfd6df;background:#fff;color:#344054}.button.warn,button.warn,input.warn{border-color:#cfd6df;background:#fff;color:#344054}button:disabled,input:disabled{border-color:#d0d5dd;background:#f2f4f7;color:#98a2b3}"
        ".table-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:8px;background:#fff}table{width:100%;border-collapse:collapse;background:#fff}th,td{padding:9px 10px;border-bottom:1px solid var(--soft);text-align:left;white-space:nowrap}th{background:#f9fafb;color:#475467;font-size:13px;font-weight:650}tr:last-child td{border-bottom:0}.action-table th:last-child,.action-table td:last-child{width:1%;text-align:center}.action-table td:last-child button,.action-table td:last-child input{min-width:82px}"
        ".setting-list{border:1px solid var(--line);border-radius:8px;overflow:hidden}.setting-row{display:grid;grid-template-columns:minmax(82px,1fr) minmax(80px,1fr) auto;gap:8px;align-items:center;min-width:0;padding:7px 9px;border-bottom:1px solid var(--soft);background:#fff}.setting-row:last-child{border-bottom:0}.setting-row span{color:var(--muted);font-weight:650}.setting-row strong{min-width:0;overflow-wrap:anywhere}.summary-line{margin-top:12px;padding:8px 10px;border-radius:6px;background:#f9fafb;color:#344054;font-weight:650}.json-box{margin:0;padding:14px;overflow-x:auto;border-radius:8px;background:#182230;color:#d6e4ff;font-size:13px}"
        ".modal[hidden]{display:none}.modal{position:fixed;inset:0;z-index:20;display:grid;place-items:center;padding:18px;background:rgba(15,23,42,.32)}.modal-card{width:min(420px,100%);padding:16px;background:#fff;border:1px solid var(--line);border-radius:8px;box-shadow:0 18px 40px rgba(15,23,42,.18)}.modal-card h2{margin-bottom:4px;font-size:18px}.modal-card .field-grid{grid-template-columns:1fr;margin-top:12px}.modal-card .actions{justify-content:flex-end}.modal-value{margin:6px 0 0;color:var(--muted)}"
        "@media(max-width:980px){.span-4,.span-6,.span-8{grid-column:span 12}.field-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}"
        "@media(max-width:640px){body{font-size:13px}body>nav{padding:8px 10px}.shell{width:calc(100% - 20px);padding-top:14px}.page-head{display:block;margin-bottom:14px}h1{font-size:22px}.panel{padding:12px}.panel-titlebar{align-items:flex-start;flex-direction:column;gap:8px}.panel-tools{justify-content:flex-start}.field-grid{grid-template-columns:1fr}.check-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.overview-status{grid-template-columns:1fr}.valve-matrix{grid-template-columns:minmax(48px,.6fr) repeat(2,minmax(82px,1fr))}.setting-row{grid-template-columns:1fr auto}.setting-row strong{grid-column:1}.setting-row a,.setting-row button{grid-column:2;grid-row:1/span 2}}"
        "</style><script>document.addEventListener('submit',function(e){var f=e.target;if(!f||String(f.method).toLowerCase()!=='post')return;var raw=f.getAttribute('data-confirm');if(raw==='off')return;var msg=raw||'确认执行此操作？';if(!confirm(msg)){e.preventDefault();}});</script>");
}

void sendHeader(const char* title, const char* pageClass, const char* activePath = nullptr) {
    Esp32BaseWeb::sendHeader(title);
    writeCss();
    Esp32BaseWeb::sendChunk("<script>document.body.className='");
    Esp32BaseWeb::writeHtmlEscaped(pageClass);
    Esp32BaseWeb::sendChunk("';");
    if (activePath && activePath[0]) {
        Esp32BaseWeb::sendChunk("document.querySelectorAll('body>nav a').forEach(function(a){a.classList.remove('active','current');if(a.getAttribute('href')==='");
        Esp32BaseWeb::writeHtmlEscaped(activePath);
        Esp32BaseWeb::sendChunk("'){a.classList.add('active','current');}});");
    }
    Esp32BaseWeb::sendChunk("</script><main class='shell'>");
}

void sendFooter() {
    Esp32BaseWeb::sendChunk("</main>");
    Esp32BaseWeb::sendFooter();
}

void redirectTo(const char* path) {
    Esp32BaseWeb::redirectSeeOther(path);
}

void writePageHead(const char* title, const char* subtitle) {
    Esp32BaseWeb::sendChunk("<header class='page-head'><div><h1>");
    Esp32BaseWeb::writeHtmlEscaped(title);
    Esp32BaseWeb::sendChunk("</h1><p class='subtitle'>");
    Esp32BaseWeb::writeHtmlEscaped(subtitle);
    Esp32BaseWeb::sendChunk("</p></div></header>");
}

void writeWateringStatusPanel(const char* title) {
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>");
    Esp32BaseWeb::writeHtmlEscaped(title);
    Esp32BaseWeb::sendChunk("</h2><div class='overview-status status-compact'><div><span>当前浇水</span><strong>");
    Esp32BaseWeb::writeHtmlEscaped(WateringSession::isActive() ? modeLabel(WateringSession::mode()) : "未运行");
    Esp32BaseWeb::sendChunk("</strong></div></div><div class='valve-matrix' role='table'><div class='matrix-head'>项目</div><div class='matrix-head'>第 1 路</div><div class='matrix-head'>第 2 路</div>");
    const char* labels[] = {"状态", "目标", "剩余", "水量", "流速"};
    for (uint8_t row = 0; row < 5; ++row) {
        Esp32BaseWeb::sendChunk("<div class='matrix-label'>");
        Esp32BaseWeb::sendChunk(labels[row]);
        Esp32BaseWeb::sendChunk("</div>");
        for (uint8_t road = 1; road <= 2; ++road) {
            const WateringSession::RoadStatus& status = WateringSession::roadStatus(road);
            const bool enabled = SettingsStore::isRoadEnabled(road);
            uint32_t remainingSec = 0;
            if (status.state == WateringSession::ROAD_RUNNING && status.targetSec > 0) {
                const uint32_t elapsed = (Esp32BaseSystem::uptimeMs() - status.startedMs) / 1000UL;
                remainingSec = elapsed < status.targetSec ? status.targetSec - elapsed : 0;
            }
            const uint32_t pulses = status.lastPulseCount >= status.startedPulseCount ? status.lastPulseCount - status.startedPulseCount : 0;
            Esp32BaseWeb::sendChunk("<div><strong>");
            if (!enabled && row > 0) {
                Esp32BaseWeb::sendChunk("-");
            } else if (row == 0) {
                Esp32BaseWeb::writeHtmlEscaped(enabled ? roadStateLabel(status.state) : "未启用");
            } else if (row == 1) {
                writeMinutesFromSeconds(status.targetSec);
                Esp32BaseWeb::sendChunk(" 分钟");
            } else if (row == 2) {
                writeMinutesFromSeconds(remainingSec);
                Esp32BaseWeb::sendChunk(" 分钟");
            } else if (row == 3) {
                writeLitersFromMl(SettingsStore::estimateMilliliters(road, pulses));
            } else {
                writeFixed2FromX100(flowRateLiterPerMinuteX100(road));
                Esp32BaseWeb::sendChunk(" L/min");
            }
            Esp32BaseWeb::sendChunk("</strong></div>");
        }
    }
    Esp32BaseWeb::sendChunk("</div><div class='valve-action'>");
    const bool active = WateringSession::isActive();
    const char* disabled = active ? "" : " disabled";
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' data-confirm='确认停止全部浇水？'><input type='hidden' name='road' value='0'><button class='secondary'");
    Esp32BaseWeb::sendChunk(disabled);
    Esp32BaseWeb::sendChunk(">停止全部</button></form>");
    for (uint8_t road = 1; road <= 2; ++road) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' data-confirm='确认停止第 ");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk(" 路？'><input type='hidden' name='road' value='");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("'><button class='secondary'");
        Esp32BaseWeb::sendChunk(active && SettingsStore::isRoadEnabled(road) ? "" : " disabled");
        Esp32BaseWeb::sendChunk(">停止第 ");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk(" 路</button></form>");
    }
    Esp32BaseWeb::sendChunk(active ? "" : "<span class='note'>当前未运行，停止不可用。</span>");
    Esp32BaseWeb::sendChunk("</div></div>");
}

const char* signalQuality(long rssi) {
    if (rssi >= -60) return "优秀";
    if (rssi >= -70) return "良好";
    if (rssi >= -80) return "一般";
    return "较弱";
}

void handleOverviewPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("总览", "page-overview");
    writePageHead("总览", "只看系统状态、阀门状态和需要立即注意的信息。");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><h2>设备状态</h2><div class='overview-status'><div><span>设备</span><strong><span class='badge ok'>正常</span></strong></div><div><span>当前</span><strong>");
    Esp32BaseWeb::sendChunk(WateringSession::isActive() ? "浇水中" : "空闲");
    Esp32BaseWeb::sendChunk("</strong></div><div><span>WiFi</span><strong>");
#if ESP32BASE_ENABLE_WIFI
    Esp32BaseWeb::writeHtmlEscaped(Esp32BaseWiFi::ssid());
    Esp32BaseWeb::sendChunk("</strong></div><div><span>信号</span><strong>");
    Esp32BaseWeb::writeHtmlEscaped(signalQuality(Esp32BaseWiFi::rssi()));
    Esp32BaseWeb::sendChunk(" (");
    char rssi[16];
    snprintf(rssi, sizeof(rssi), "%ld dBm", static_cast<long>(Esp32BaseWiFi::rssi()));
    Esp32BaseWeb::writeHtmlEscaped(rssi);
#else
    Esp32BaseWeb::sendChunk("-");
    Esp32BaseWeb::sendChunk("</strong></div><div><span>信号</span><strong>-");
#endif
    Esp32BaseWeb::sendChunk(")</strong></div></div></div>");
    writeWateringStatusPanel("浇水状态");
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>异常</h2>");
    if (LeakMonitor::hasAlert()) {
        Esp32BaseWeb::sendChunk("<span class='badge danger'>存在异常</span><p class='note'>请确认现场状态，处理后清除提示。</p><form method='post' action='/api/v1/alerts/clear' data-confirm='确认现场已处理并解除异常提示？'><button>解除异常</button></form>");
    } else {
        Esp32BaseWeb::sendChunk("<span class='badge ok'>无当前异常</span><p class='note'>存在异常时在这里显示原因和状态，历史事件在记录页查看。</p>");
    }
    Esp32BaseWeb::sendChunk("</div></section>");
    sendFooter();
}

void handleManualPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const SettingsStore::Settings& settings = SettingsStore::current();
    sendHeader("手动浇水", "page-form");
    writePageHead("手动浇水", "每路独立选择是否参与，本页统一开始；停止操作必须确认。");
    Esp32BaseWeb::sendChunk("<section class='grid'>");
    writeWateringStatusPanel("浇水状态");
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>启动浇水</h2><p class='inline-notice note'>上次操作：");
    Esp32BaseWeb::writeHtmlEscaped(WateringSession::stopReasonName(WateringSession::lastStopReason()));
    Esp32BaseWeb::sendChunk("</p><form method='post' action='/api/v1/water/start' data-confirm='确认开始手动浇水？'><div class='field-grid'><div class='field'><label>执行模式</label><select name='mode'>");
    writeModeOptions(settings.defaultMode);
    Esp32BaseWeb::sendChunk("</select></div></div><div class='valve-matrix config-matrix'><div class='matrix-head'>项目</div><div class='matrix-head'>第 1 路</div><div class='matrix-head'>第 2 路</div><div class='matrix-label'>参与</div>");
    for (uint8_t road = 1; road <= 2; ++road) {
        const bool enabled = SettingsStore::isRoadEnabled(road);
        Esp32BaseWeb::sendChunk("<div><select name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_enabled'");
        Esp32BaseWeb::sendChunk(enabled ? "" : " disabled");
        Esp32BaseWeb::sendChunk("><option value='1'>浇水</option><option value='0'");
        writeSelected(!enabled);
        Esp32BaseWeb::sendChunk(">");
        Esp32BaseWeb::sendChunk(enabled ? "不浇水" : "未启用");
        Esp32BaseWeb::sendChunk("</option></select></div>");
    }
    Esp32BaseWeb::sendChunk("<div class='matrix-label'>时长</div>");
    for (uint8_t road = 1; road <= 2; ++road) {
        Esp32BaseWeb::sendChunk("<div><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='1' max='240' value='");
        writeMinutesFromSeconds(settings.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk("'");
        Esp32BaseWeb::sendChunk(SettingsStore::isRoadEnabled(road) ? "" : " disabled");
        Esp32BaseWeb::sendChunk("></div>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><button>开始浇水</button></div></form></div></section>");
    sendFooter();
}

void writePlanContent(const PlanStore::Plan& plan) {
    bool first = true;
    for (uint8_t road = 1; road <= 2; ++road) {
        if (plan.roadSec[road - 1] == 0) continue;
        if (!first) Esp32BaseWeb::sendChunk("，");
        first = false;
        Esp32BaseWeb::sendChunk("第 ");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk(" 路 ");
        writeMinutesFromSeconds(plan.roadSec[road - 1]);
        Esp32BaseWeb::sendChunk(" 分钟");
    }
    if (first) Esp32BaseWeb::sendChunk("未设置");
}

void writeCycleText(const PlanStore::Plan& plan) {
    writeUIntText(plan.cycleDays);
    Esp32BaseWeb::sendChunk(" 天循环，执行第 ");
    bool first = true;
    for (uint8_t i = 0; i < plan.cycleDays; ++i) {
        if ((plan.cycleMask & (1UL << i)) == 0) continue;
        if (!first) Esp32BaseWeb::sendChunk("、");
        first = false;
        writeUIntText(i + 1);
    }
    Esp32BaseWeb::sendChunk(" 天");
}

const char* statusClass(const char* status) {
    if (strcmp(status, "已完成") == 0) return " ok";
    if (strcmp(status, "已停用") == 0) return " warn";
    if (strcmp(status, "进行中") == 0) return " danger";
    return "";
}

void writeRecentRows(int8_t offset, uint32_t ymd) {
    const uint16_t nowMinute = currentMinuteOfDay();
    bool any = false;
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        if (plan.enabled && !PlanStore::shouldRunOnDate(plan, ymd)) continue;
        any = true;
        const bool skipped = PlanSkipStore::isSkipped(i, ymd);
        const bool completed = plan.lastRunYmd == ymd;
        const bool disabled = !plan.enabled;
        const bool running = WateringSession::isActive() && WateringSession::source() == RecordStore::SOURCE_PLAN && offset == 0 && plan.minuteOfDay == nowMinute;
        const bool pastToday = offset == 0 && plan.minuteOfDay < nowMinute;
        const char* status = disabled ? "已停用" : (skipped ? "已跳过" : (completed ? "已完成" : (running ? "进行中" : (pastToday ? "未执行" : "未开始"))));
        Esp32BaseWeb::sendChunk("<tr><td>");
        writeMinuteOfDay(plan.minuteOfDay);
        Esp32BaseWeb::sendChunk("</td><td>计划 ");
        writeUIntText(i + 1);
        Esp32BaseWeb::sendChunk("</td><td>");
        writePlanContent(plan);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(modeLabel(plan.mode));
        Esp32BaseWeb::sendChunk("</td><td><span class='badge");
        Esp32BaseWeb::sendChunk(statusClass(status));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(status);
        Esp32BaseWeb::sendChunk("</span></td><td>");
        Esp32BaseWeb::sendChunk(disabled ? "计划未启用，不会执行" : (skipped ? "已跳过这一次" : (completed ? "按计划完成" : (pastToday ? "已过执行时间" : "等待执行"))));
        Esp32BaseWeb::sendChunk("</td><td>");
        if (!disabled && !completed && !running && !pastToday && !skipped) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans/skip' data-confirm='确认跳过本次计划？'><input type='hidden' name='action' value='skip_once'><input type='hidden' name='index' value='");
            writeUIntText(i);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='ymd' value='");
            writeUIntText(ymd);
            Esp32BaseWeb::sendChunk("'><button class='warn'>跳过本次</button></form>");
        } else if (skipped) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans/skip' data-confirm='确认取消跳过本次计划？'><input type='hidden' name='action' value='clear_skip'><input type='hidden' name='index' value='");
            writeUIntText(i);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='ymd' value='");
            writeUIntText(ymd);
            Esp32BaseWeb::sendChunk("'><button class='secondary'>取消跳过</button></form>");
        } else {
            Esp32BaseWeb::sendChunk("-");
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>无计划</td></tr>");
    }
}

void writeRecentPanel(const char* label, int8_t offset) {
    tm date = {};
    const bool hasDate = localDateFromOffset(offset, &date);
    const uint32_t ymd = hasDate ? makeYmd(date) : 0;
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><div class='panel-titlebar'><h2>");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk(" <span class='title-date'>");
    if (hasDate) writeYmd(ymd); else Esp32BaseWeb::sendChunk("时间未同步");
    Esp32BaseWeb::sendChunk("</span></h2>");
    if (hasDate && offset >= 0) {
        Esp32BaseWeb::sendChunk("<div class='panel-tools'><form method='post' action='/api/v1/plans/skip' data-confirm='确认跳过");
        Esp32BaseWeb::writeHtmlEscaped(label);
        Esp32BaseWeb::sendChunk(offset == 0 ? "剩余未执行计划？" : "全部未执行计划？");
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='action' value='skip_day'><input type='hidden' name='ymd' value='");
        writeUIntText(ymd);
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='scope' value='");
        Esp32BaseWeb::sendChunk(offset == 0 ? "remaining" : "all");
        Esp32BaseWeb::sendChunk("'><button class='warn'>跳过");
        Esp32BaseWeb::writeHtmlEscaped(label);
        Esp32BaseWeb::sendChunk(offset == 0 ? "剩余" : "全部");
        Esp32BaseWeb::sendChunk("</button></form></div>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='table-wrap'><table class='action-table'><thead><tr><th>时间</th><th>计划</th><th>内容</th><th>模式</th><th>状态</th><th>说明</th><th>操作</th></tr></thead><tbody>");
    if (hasDate) writeRecentRows(offset, ymd); else Esp32BaseWeb::sendChunk("<tr><td colspan='7'>时间未同步</td></tr>");
    Esp32BaseWeb::sendChunk("</tbody></table></div></div>");
}

void handlePlansPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("近期计划", "page-table");
    writePageHead("近期计划", "查看昨日、今日、明日、后天的计划执行状态。");
    Esp32BaseWeb::sendChunk("<section class='grid'>");
    writeRecentPanel("昨日", -1);
    writeRecentPanel("今日", 0);
    writeRecentPanel("明日", 1);
    writeRecentPanel("后天", 2);
    Esp32BaseWeb::sendChunk("</section>");
    sendFooter();
}

void handlePlanConfigPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("计划配置", "page-table");
    writePageHead("计划配置", "这里只修改计划内容；执行结果在近期计划和记录页查看。");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><h2>计划列表</h2><div class='table-wrap'><table><thead><tr><th>状态</th><th>计划</th><th>时间</th><th>循环规则</th><th>内容</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        Esp32BaseWeb::sendChunk("<tr><td><span class='badge");
        Esp32BaseWeb::sendChunk(plan.enabled ? " ok" : "");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(plan.enabled ? "启用" : "停用");
        Esp32BaseWeb::sendChunk("</span></td><td>计划 ");
        writeUIntText(i + 1);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeMinuteOfDay(plan.minuteOfDay);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeCycleText(plan);
        Esp32BaseWeb::sendChunk("</td><td>");
        writePlanContent(plan);
        Esp32BaseWeb::sendChunk("，");
        Esp32BaseWeb::writeHtmlEscaped(modeLabel(plan.mode));
        Esp32BaseWeb::sendChunk("</td><td><a class='button secondary' href='/irrigation/plan?edit=");
        writeUIntText(i);
        Esp32BaseWeb::sendChunk("'>修改</a></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div></div></section>");
    sendFooter();
}

uint32_t readCycleMaskFromForm(uint8_t cycleDays) {
    uint32_t mask = 0;
    if (Esp32BaseWeb::hasParam("cycle_mask")) {
        uint32_t value = 0;
        if (readU32Param("cycle_mask", &value)) {
            mask = value;
        }
    }
    for (uint8_t i = 0; i < cycleDays; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "d%u", static_cast<unsigned>(i));
        if (Esp32BaseWeb::hasParam(name)) {
            mask |= (1UL << i);
        }
    }
    return mask == 0 ? 0x01 : mask;
}

void handlePlanEditPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint16_t raw = 0;
    const uint8_t index = (Esp32BaseWeb::hasParam("edit") && readUIntParam("edit", &raw) && raw < PlanStore::MaxPlans) ? static_cast<uint8_t>(raw) : 0;
    const PlanStore::Plan& plan = PlanStore::get(index);
    sendHeader("编辑计划", "page-form", "/irrigation/plan-config");
    writePageHead("编辑计划", "编辑单个计划的固定配置，保存后用于后续自动浇水。");
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans' data-confirm='确认保存计划？'><input type='hidden' name='index' value='");
    writeUIntText(index);
    Esp32BaseWeb::sendChunk("'><section class='grid'><div class='panel span-12'><div class='panel-titlebar'><h2>计划 ");
    writeUIntText(index + 1);
    Esp32BaseWeb::sendChunk("</h2><a class='button secondary' href='/irrigation/plan-config'>返回计划配置</a></div></div><div class='panel span-12'><h2>计划状态</h2><div class='field-grid'><div class='field'><label>启用状态</label><select name='enabled'><option value='0'");
    writeSelected(!plan.enabled);
    Esp32BaseWeb::sendChunk(">停用</option><option value='1'");
    writeSelected(plan.enabled);
    Esp32BaseWeb::sendChunk(">启用</option></select></div><div class='field'><label>执行时间</label><input name='time' type='time' value='");
    writeMinuteOfDay(plan.minuteOfDay);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>执行模式</label><select name='mode'>");
    writeModeOptions(plan.mode);
    Esp32BaseWeb::sendChunk("</select></div></div></div><div class='panel span-12'><h2>浇水路配置</h2><div class='valve-matrix config-matrix'><div class='matrix-head'>项目</div><div class='matrix-head'>第 1 路</div><div class='matrix-head'>第 2 路</div><div class='matrix-label'>参与</div>");
    for (uint8_t road = 1; road <= 2; ++road) {
        Esp32BaseWeb::sendChunk("<div><select name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_enabled'");
        Esp32BaseWeb::sendChunk(SettingsStore::isRoadEnabled(road) ? "" : " disabled");
        Esp32BaseWeb::sendChunk("><option value='1'");
        writeSelected(plan.roadSec[road - 1] > 0);
        Esp32BaseWeb::sendChunk(">浇水</option><option value='0'");
        writeSelected(plan.roadSec[road - 1] == 0);
        Esp32BaseWeb::sendChunk(">");
        Esp32BaseWeb::sendChunk(SettingsStore::isRoadEnabled(road) ? "不浇水" : "未启用");
        Esp32BaseWeb::sendChunk("</option></select></div>");
    }
    Esp32BaseWeb::sendChunk("<div class='matrix-label'>时长</div>");
    for (uint8_t road = 1; road <= 2; ++road) {
        Esp32BaseWeb::sendChunk("<div><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='0' max='240' value='");
        writeMinutesFromSeconds(plan.roadSec[road - 1]);
        Esp32BaseWeb::sendChunk("'");
        Esp32BaseWeb::sendChunk(SettingsStore::isRoadEnabled(road) ? "" : " disabled");
        Esp32BaseWeb::sendChunk("></div>");
    }
    Esp32BaseWeb::sendChunk("</div></div><div class='panel span-12'><h2>循环规则</h2><div class='field-grid'><div class='field'><label>循环天数</label><input id='cycleDays' name='cycle_days' type='number' min='1' max='30' value='");
    writeUIntText(plan.cycleDays);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>循环开始日期</label><input name='cycle_start_ymd' type='number' min='20000101' max='20991231' value='");
    writeUIntText(plan.cycleStartYmd);
    Esp32BaseWeb::sendChunk("'></div></div><h3>循环执行日</h3><div class='check-grid' id='cycleDayList'>");
    for (uint8_t i = 0; i < 30; ++i) {
        Esp32BaseWeb::sendChunk("<label class='check-item' data-day='");
        writeUIntText(i + 1);
        Esp32BaseWeb::sendChunk("'><input type='checkbox' name='d");
        writeUIntText(i);
        Esp32BaseWeb::sendChunk("'");
        writeChecked((plan.cycleMask & (1UL << i)) != 0);
        Esp32BaseWeb::sendChunk("> 第 ");
        writeUIntText(i + 1);
        Esp32BaseWeb::sendChunk(" 天</label>");
    }
    Esp32BaseWeb::sendChunk("</div><p class='note'>计划只使用循环规则：每天浇水是 1 天循环；浇 2 天停 1 天是 3 天循环并执行第 1、2 天；每周固定日期是 7 天循环。</p></div><div class='panel span-12'><div class='actions form-actions'><a class='button secondary' href='/irrigation/plan-config'>返回计划配置</a><button>保存计划</button></div></div></section></form><script>(function(){var n=document.getElementById('cycleDays');var list=document.getElementById('cycleDayList');function sync(){var max=Math.max(1,Math.min(30,parseInt(n.value||'1',10)||1));n.value=max;list.querySelectorAll('[data-day]').forEach(function(item){var show=parseInt(item.dataset.day,10)<=max;item.style.display=show?'flex':'none';if(!show){var c=item.querySelector('input');if(c)c.checked=false;}});}if(n&&list){n.addEventListener('input',sync);sync();}})();</script>");
    sendFooter();
}

void writeSettingRow(const char* key, const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<div class='setting-row'><span>");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::writeHtmlEscaped(value);
    Esp32BaseWeb::sendChunk("</strong><button type='button' class='secondary' data-setting-open='setting-");
    Esp32BaseWeb::writeHtmlEscaped(key);
    Esp32BaseWeb::sendChunk("'>修改</button></div>");
}

void writeSettingReadOnlyRow(const char* label, const char* value, const char* note) {
    Esp32BaseWeb::sendChunk("<div class='setting-row'><span>");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk("</span><strong>");
    Esp32BaseWeb::writeHtmlEscaped(value);
    Esp32BaseWeb::sendChunk("</strong><small class='note'>");
    Esp32BaseWeb::writeHtmlEscaped(note);
    Esp32BaseWeb::sendChunk("</small></div>");
}

void writeSettingsModalScript() {
    Esp32BaseWeb::sendChunk(
        "<script>(function(){"
        "function closeAll(){document.querySelectorAll('.modal').forEach(function(m){m.hidden=true;});}"
        "document.addEventListener('click',function(e){"
        "var open=e.target.closest('[data-setting-open]');"
        "if(open){var m=document.getElementById(open.getAttribute('data-setting-open'));if(m){m.hidden=false;var f=m.querySelector('input,select,button');if(f)f.focus();}return;}"
        "if(e.target.matches('[data-setting-close]')||e.target.classList.contains('modal'))closeAll();"
        "});"
        "document.addEventListener('keydown',function(e){if(e.key==='Escape')closeAll();});"
        "})();</script>");
}

void writeSettingEditModal(const char* edit, const char* title, const char* currentValue) {
    if (!edit || edit[0] == '\0') {
        return;
    }
    const SettingsStore::Settings& s = SettingsStore::current();
    Esp32BaseWeb::sendChunk("<div class='modal' id='setting-");
    Esp32BaseWeb::writeHtmlEscaped(edit);
    Esp32BaseWeb::sendChunk("' hidden><div class='modal-card' role='dialog' aria-modal='true'><h2>修改参数</h2><p class='modal-value'>");
    Esp32BaseWeb::writeHtmlEscaped(title);
    Esp32BaseWeb::sendChunk("：");
    Esp32BaseWeb::writeHtmlEscaped(currentValue);
    Esp32BaseWeb::sendChunk("</p><form method='post' action='/api/v1/config' data-confirm='确认保存设置？'><div class='field-grid'>");
    if (strcmp(edit, "default_mode") == 0) {
        Esp32BaseWeb::sendChunk("<div class='field'><label>默认模式</label><select name='default_mode'>");
        writeModeOptions(s.defaultMode);
        Esp32BaseWeb::sendChunk("</select></div>");
    } else if (strcmp(edit, "r1_enabled") == 0 || strcmp(edit, "r2_enabled") == 0) {
        const uint8_t road = strcmp(edit, "r1_enabled") == 0 ? 1 : 2;
        Esp32BaseWeb::sendChunk("<input type='hidden' name='road' value='");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("'><div class='field'><label>启用状态</label><select name='enabled'><option value='1'");
        writeSelected(SettingsStore::isRoadEnabled(road));
        Esp32BaseWeb::sendChunk(">已启用</option><option value='0'");
        writeSelected(!SettingsStore::isRoadEnabled(road));
        Esp32BaseWeb::sendChunk(">未启用</option></select></div>");
    } else if (strcmp(edit, "r1_name") == 0 || strcmp(edit, "r2_name") == 0) {
        const uint8_t road = strcmp(edit, "r1_name") == 0 ? 1 : 2;
        Esp32BaseWeb::sendChunk("<div class='field'><label>名称</label><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_name' maxlength='11' value='");
        Esp32BaseWeb::writeHtmlEscaped(s.roads[road - 1].name);
        Esp32BaseWeb::sendChunk("'></div>");
    } else if (strcmp(edit, "quick_r1_min") == 0 || strcmp(edit, "quick_r2_min") == 0) {
        const uint8_t road = strcmp(edit, "quick_r1_min") == 0 ? 1 : 2;
        Esp32BaseWeb::sendChunk("<div class='field'><label>默认时长（分钟）</label><input name='quick_r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='1' max='240' value='");
        writeMinutesFromSeconds(s.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk("'></div>");
    } else if (strcmp(edit, "r1_pulse_per_liter") == 0 || strcmp(edit, "r2_pulse_per_liter") == 0) {
        const uint8_t road = strcmp(edit, "r1_pulse_per_liter") == 0 ? 1 : 2;
        Esp32BaseWeb::sendChunk("<div class='field'><label>每升脉冲</label><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_pulse_per_liter' type='number' min='1' max='10000' value='");
        writeUIntText(s.roads[road - 1].pulsePerLiter);
        Esp32BaseWeb::sendChunk("'></div>");
    } else if (strcmp(edit, "r1_calibration_x1000") == 0 || strcmp(edit, "r2_calibration_x1000") == 0) {
        const uint8_t road = strcmp(edit, "r1_calibration_x1000") == 0 ? 1 : 2;
        Esp32BaseWeb::sendChunk("<div class='field'><label>校准系数</label><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_calibration_x1000' type='number' min='100' max='10000' value='");
        writeUIntText(s.roads[road - 1].calibrationX1000);
        Esp32BaseWeb::sendChunk("'></div>");
    } else if (strcmp(edit, "flow_no_pulse_timeout_s") == 0 || strcmp(edit, "idle_leak_window_s") == 0 || strcmp(edit, "idle_leak_pulse_threshold") == 0) {
        uint16_t current = s.flowNoPulseTimeoutSec;
        const char* label = "无脉冲超时";
        uint16_t max = 60;
        if (strcmp(edit, "idle_leak_window_s") == 0) {
            current = s.idleLeakWindowSec;
            label = "漏水窗口";
        } else if (strcmp(edit, "idle_leak_pulse_threshold") == 0) {
            current = s.idleLeakPulseThreshold;
            label = "漏水脉冲阈值";
            max = 100;
        }
        Esp32BaseWeb::sendChunk("<div class='field'><label>");
        Esp32BaseWeb::writeHtmlEscaped(label);
        Esp32BaseWeb::sendChunk("</label><input name='");
        Esp32BaseWeb::writeHtmlEscaped(edit);
        Esp32BaseWeb::sendChunk("' type='number' min='1' max='");
        writeUIntText(max);
        Esp32BaseWeb::sendChunk("' value='");
        writeUIntText(current);
        Esp32BaseWeb::sendChunk("'></div>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><button type='button' class='secondary' data-setting-close>取消</button><button>保存</button></div></form></div></div>");
}

void writeSettingEditModals() {
    const SettingsStore::Settings& s = SettingsStore::current();
    char value[32];
    writeSettingEditModal("default_mode", "默认模式", modeLabel(s.defaultMode));
    for (uint8_t road = 1; road <= 2; ++road) {
        const SettingsStore::RoadConfig& r = s.roads[road - 1];
        snprintf(value, sizeof(value), "%s", SettingsStore::isRoadEnabled(road) ? "已启用" : "未启用");
        writeSettingEditModal(road == 1 ? "r1_enabled" : "r2_enabled", road == 1 ? "第 1 路启用状态" : "第 2 路启用状态", value);
        writeSettingEditModal(road == 1 ? "r1_name" : "r2_name", road == 1 ? "第 1 路名称" : "第 2 路名称", r.name);
        snprintf(value, sizeof(value), "%u 分钟", static_cast<unsigned>(s.quickDurationSec[road - 1] / 60U));
        writeSettingEditModal(road == 1 ? "quick_r1_min" : "quick_r2_min", road == 1 ? "第 1 路默认时长" : "第 2 路默认时长", value);
        snprintf(value, sizeof(value), "%u pulse/L", static_cast<unsigned>(r.pulsePerLiter));
        writeSettingEditModal(road == 1 ? "r1_pulse_per_liter" : "r2_pulse_per_liter", road == 1 ? "第 1 路每升脉冲" : "第 2 路每升脉冲", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(r.calibrationX1000));
        writeSettingEditModal(road == 1 ? "r1_calibration_x1000" : "r2_calibration_x1000", road == 1 ? "第 1 路校准系数" : "第 2 路校准系数", value);
    }
    snprintf(value, sizeof(value), "%u 秒", static_cast<unsigned>(s.flowNoPulseTimeoutSec));
    writeSettingEditModal("flow_no_pulse_timeout_s", "无脉冲超时", value);
    snprintf(value, sizeof(value), "%u 秒", static_cast<unsigned>(s.idleLeakWindowSec));
    writeSettingEditModal("idle_leak_window_s", "漏水窗口", value);
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s.idleLeakPulseThreshold));
    writeSettingEditModal("idle_leak_pulse_threshold", "漏水脉冲阈值", value);
}

void handleSettingsPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const SettingsStore::Settings& s = SettingsStore::current();
    sendHeader("设置", "page-settings");
    writePageHead("设置", "查看当前灌溉参数，每次只修改一个参数。");
    char value[32];
    Esp32BaseWeb::sendChunk("<section class='grid'>");
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>灌溉设置</h2><div class='setting-list'>");
    snprintf(value, sizeof(value), "%u 路", static_cast<unsigned>(SettingsStore::enabledRoads()));
    writeSettingReadOnlyRow("已启用路数", value, "由各路启用状态计算");
    writeSettingRow("default_mode", "默认模式", modeLabel(s.defaultMode));
    Esp32BaseWeb::sendChunk("</div></div>");
    for (uint8_t road = 1; road <= 2; ++road) {
        const SettingsStore::RoadConfig& r = s.roads[road - 1];
        Esp32BaseWeb::sendChunk("<div class='panel span-6'><h2>第 ");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk(" 路配置</h2><div class='setting-list'>");
        writeSettingRow(road == 1 ? "r1_enabled" : "r2_enabled", "启用状态", SettingsStore::isRoadEnabled(road) ? "已启用" : "未启用");
        writeSettingRow(road == 1 ? "r1_name" : "r2_name", "名称", r.name);
        snprintf(value, sizeof(value), "%u 分钟", static_cast<unsigned>(s.quickDurationSec[road - 1] / 60U));
        writeSettingRow(road == 1 ? "quick_r1_min" : "quick_r2_min", "默认时长", value);
        snprintf(value, sizeof(value), "%u pulse/L", static_cast<unsigned>(r.pulsePerLiter));
        writeSettingRow(road == 1 ? "r1_pulse_per_liter" : "r2_pulse_per_liter", "每升脉冲", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(r.calibrationX1000));
        writeSettingRow(road == 1 ? "r1_calibration_x1000" : "r2_calibration_x1000", "校准系数", value);
        Esp32BaseWeb::sendChunk("</div></div>");
    }
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>水流检测</h2><div class='setting-list'>");
    snprintf(value, sizeof(value), "%u 秒", static_cast<unsigned>(s.flowNoPulseTimeoutSec));
    writeSettingRow("flow_no_pulse_timeout_s", "无脉冲超时", value);
    snprintf(value, sizeof(value), "%u 秒", static_cast<unsigned>(s.idleLeakWindowSec));
    writeSettingRow("idle_leak_window_s", "漏水窗口", value);
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s.idleLeakPulseThreshold));
    writeSettingRow("idle_leak_pulse_threshold", "漏水脉冲阈值", value);
    Esp32BaseWeb::sendChunk("</div></div>");
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>维护</h2><div class='setting-list'>");
    writeSettingReadOnlyRow("恢复出厂请求", SafetyManager::factoryResetRequested() ? "已由 BOOT 键请求" : "未请求", "BOOT 长按会直接恢复配置并重启");
    writeSettingReadOnlyRow("恢复出厂状态", MaintenanceService::factoryResetPending() ? "等待重启执行" : "空闲", "执行时会先关闭所有阀门");
    Esp32BaseWeb::sendChunk("</div><form method='post' action='/api/v1/maintenance/factory-reset' data-confirm='确认恢复出厂？设备会关闭阀门并重启。'><div class='field-grid' style='margin-top:12px'><div class='field'><label>确认文本</label><input name='confirm' maxlength='5' placeholder='RESET'></div><div class='field'><label>记录处理</label><select name='clear_records'><option value='0'>保留记录和事件</option><option value='1'>同时清空记录和事件</option></select></div></div><div class='actions'><button class='warn'>恢复出厂</button></div></form></div>");
    Esp32BaseWeb::sendChunk("</section>");
    writeSettingEditModals();
    writeSettingsModalScript();
    sendFooter();
}

void handleDataPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("数据记录", "page-table");
    writePageHead("数据记录", "查看什么时候浇水、实际执行多久、为什么结束。");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><div class='panel-titlebar'><h2>浇水记录</h2><div class='panel-tools'><a class='button secondary' href='/api/v1/records'>JSON</a><a class='button secondary' href='/api/v1/records.csv'>CSV 导出</a></div></div><div class='table-wrap'><table><thead><tr><th>ID</th><th>来源</th><th>模式</th><th>结束原因</th><th>路</th><th>目标</th><th>实际</th><th>水量</th></tr></thead><tbody>");
    auto recordCb = [](const RecordStore::Record& record, void*) {
        for (uint8_t road = 1; road <= 2; ++road) {
            const RecordStore::RoadRecord& rr = record.roads[road - 1];
            const uint32_t actualSec = rr.endedMs >= rr.startedMs && rr.startedMs > 0 ? (rr.endedMs - rr.startedMs) / 1000UL : 0;
            Esp32BaseWeb::sendChunk("<tr><td>");
            writeUIntText(record.id);
            Esp32BaseWeb::sendChunk("</td><td>");
            Esp32BaseWeb::writeHtmlEscaped(RecordStore::sourceName(static_cast<RecordStore::Source>(record.source)));
            Esp32BaseWeb::sendChunk("</td><td>");
            Esp32BaseWeb::writeHtmlEscaped(modeLabel(static_cast<SettingsStore::ExecutionMode>(record.mode)));
            Esp32BaseWeb::sendChunk("</td><td>");
            Esp32BaseWeb::writeHtmlEscaped(WateringSession::stopReasonName(static_cast<WateringSession::StopReason>(record.stopReason)));
            Esp32BaseWeb::sendChunk("</td><td>第 ");
            writeUIntText(road);
            Esp32BaseWeb::sendChunk(" 路</td><td>");
            writeMinutesFromSeconds(rr.targetSec);
            Esp32BaseWeb::sendChunk(" 分钟</td><td>");
            writeUIntText(actualSec);
            Esp32BaseWeb::sendChunk(" 秒</td><td>");
            writeLitersFromMl(rr.estimatedMilliliters);
            Esp32BaseWeb::sendChunk("</td></tr>");
        }
    };
    (void)RecordStore::readLatest(0, 10, recordCb, nullptr);
    Esp32BaseWeb::sendChunk("</tbody></table></div></div><div class='panel span-12'><div class='panel-titlebar'><h2>系统事件</h2><div class='panel-tools'><a class='button secondary' href='/api/v1/events'>JSON</a><a class='button secondary' href='/api/v1/events.csv'>CSV 导出</a></div></div><p class='note'>系统事件来自 EventStore，用于排查启动、配置变更、计划变更、浇水开始/停止/错误、漏水告警和告警解除。</p><div class='table-wrap'><table><thead><tr><th>ID</th><th>类型</th><th>来源</th><th>影响对象</th><th>说明</th></tr></thead><tbody>");
    auto eventCb = [](const EventStore::Event& event, void*) {
        Esp32BaseWeb::sendChunk("<tr><td>");
        writeUIntText(event.id);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(EventStore::typeName(static_cast<EventStore::Type>(event.type)));
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(EventStore::sourceName(static_cast<EventStore::Source>(event.source)));
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUIntText(event.road);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(event.text);
        Esp32BaseWeb::sendChunk("</td></tr>");
    };
    (void)EventStore::readLatest(0, 20, eventCb, nullptr);
    Esp32BaseWeb::sendChunk("</tbody></table></div></div></section>");
    sendFooter();
}

void handleDebugPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("调试", "page-table");
    writePageHead("调试与状态 API", "查看业务状态 JSON，用于联调和问题排查。");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><h2>状态 JSON</h2><p class='note'>实时内容由接口返回。</p><div class='actions'><a class='button secondary' href='/api/v1/status'>打开 /api/v1/status</a></div></div></section>");
    sendFooter();
}

void writeSettingsJson() {
    const SettingsStore::Settings& s = SettingsStore::current();
    Esp32BaseWeb::sendChunk("\"settings\":{\"enabled_roads\":");
    writeUInt(SettingsStore::enabledRoads());
    Esp32BaseWeb::sendChunk(",\"road_enabled_mask\":");
    writeUInt(SettingsStore::roadEnabledMask());
    Esp32BaseWeb::sendChunk(",\"default_mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(modeName(s.defaultMode));
    Esp32BaseWeb::sendChunk("\",\"flow_no_pulse_timeout_s\":");
    writeUInt(s.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk(",\"idle_leak_window_s\":");
    writeUInt(s.idleLeakWindowSec);
    Esp32BaseWeb::sendChunk(",\"idle_leak_pulse_threshold\":");
    writeUInt(s.idleLeakPulseThreshold);
    Esp32BaseWeb::sendChunk(",\"roads\":{\"r1\":{\"enabled\":");
    writeBool(SettingsStore::isRoadEnabled(1));
    Esp32BaseWeb::sendChunk(",\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(s.roads[0].name);
    Esp32BaseWeb::sendChunk("\",\"quick_duration_sec\":");
    writeUInt(s.quickDurationSec[0]);
    Esp32BaseWeb::sendChunk(",\"pulse_per_liter\":");
    writeUInt(s.roads[0].pulsePerLiter);
    Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
    writeUInt(s.roads[0].calibrationX1000);
    Esp32BaseWeb::sendChunk("},\"r2\":{\"enabled\":");
    writeBool(SettingsStore::isRoadEnabled(2));
    Esp32BaseWeb::sendChunk(",\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(s.roads[1].name);
    Esp32BaseWeb::sendChunk("\",\"quick_duration_sec\":");
    writeUInt(s.quickDurationSec[1]);
    Esp32BaseWeb::sendChunk(",\"pulse_per_liter\":");
    writeUInt(s.roads[1].pulsePerLiter);
    Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
    writeUInt(s.roads[1].calibrationX1000);
    Esp32BaseWeb::sendChunk("}}}");
}

void writeRoadJson(uint8_t road) {
    const WateringSession::RoadStatus& st = WateringSession::roadStatus(road);
    const uint32_t pulses = st.lastPulseCount >= st.startedPulseCount ? st.lastPulseCount - st.startedPulseCount : 0;
    Esp32BaseWeb::sendChunk("{\"enabled\":");
    writeBool(SettingsStore::isRoadEnabled(road));
    Esp32BaseWeb::sendChunk(",\"state\":\"");
    Esp32BaseWeb::writeJsonEscaped(WateringSession::roadStateName(st.state));
    Esp32BaseWeb::sendChunk("\",\"target_sec\":");
    writeUInt(st.targetSec);
    Esp32BaseWeb::sendChunk(",\"estimated_ml\":");
    writeUInt(SettingsStore::estimateMilliliters(road, pulses));
    Esp32BaseWeb::sendChunk(",\"flow_l_min\":");
    writeFixed2FromX100(flowRateLiterPerMinuteX100(road));
    Esp32BaseWeb::sendChunk(",\"valve_open\":");
    writeBool(ValveController::isOpen(road));
    Esp32BaseWeb::sendChunk("}");
}

void writeWateringJson() {
    Esp32BaseWeb::sendChunk("\"watering\":{\"active\":");
    writeBool(WateringSession::isActive());
    Esp32BaseWeb::sendChunk(",\"mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(modeName(WateringSession::mode()));
    Esp32BaseWeb::sendChunk("\",\"last_stop_reason\":\"");
    Esp32BaseWeb::writeJsonEscaped(WateringSession::stopReasonName(WateringSession::lastStopReason()));
    Esp32BaseWeb::sendChunk("\",\"roads\":{\"r1\":");
    writeRoadJson(1);
    Esp32BaseWeb::sendChunk(",\"r2\":");
    writeRoadJson(2);
    Esp32BaseWeb::sendChunk("}}");
}

void writePlanJson(uint8_t index, const PlanStore::Plan& p) {
    Esp32BaseWeb::sendChunk("{\"index\":");
    writeUInt(index);
    Esp32BaseWeb::sendChunk(",\"enabled\":");
    writeBool(p.enabled);
    Esp32BaseWeb::sendChunk(",\"minute_of_day\":");
    writeUInt(p.minuteOfDay);
    Esp32BaseWeb::sendChunk(",\"road_sec\":{\"r1\":");
    writeUInt(p.roadSec[0]);
    Esp32BaseWeb::sendChunk(",\"r2\":");
    writeUInt(p.roadSec[1]);
    Esp32BaseWeb::sendChunk("},\"mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(modeName(p.mode));
    Esp32BaseWeb::sendChunk("\",\"cycle_days\":");
    writeUInt(p.cycleDays);
    Esp32BaseWeb::sendChunk(",\"cycle_mask\":");
    writeUInt(p.cycleMask);
    Esp32BaseWeb::sendChunk(",\"cycle_start_ymd\":");
    writeUInt(p.cycleStartYmd);
    Esp32BaseWeb::sendChunk(",\"last_run_ymd\":");
    writeUInt(p.lastRunYmd);
    Esp32BaseWeb::sendChunk("}");
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    char text[32];
    beginJson();
    Esp32BaseWeb::sendChunk("{\"firmware\":{\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32Base::firmwareName());
    Esp32BaseWeb::sendChunk("\",\"version\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32Base::firmwareVersion());
    Esp32BaseWeb::sendChunk("\"},\"wifi\":{\"connected\":");
#if ESP32BASE_ENABLE_WIFI
    writeBool(Esp32BaseWiFi::isConnected());
    Esp32BaseWeb::sendChunk(",\"ssid\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32BaseWiFi::ssid());
    Esp32BaseWeb::sendChunk("\",\"rssi\":");
    writeInt(static_cast<int32_t>(Esp32BaseWiFi::rssi()));
    Esp32BaseWeb::sendChunk(",\"ip\":\"");
    char ip[24] = "";
    (void)Esp32BaseWiFi::ip(ip, sizeof(ip));
    Esp32BaseWeb::writeJsonEscaped(ip);
#else
    writeBool(false);
    Esp32BaseWeb::sendChunk(",\"ssid\":\"\",\"rssi\":0,\"ip\":\"");
#endif
    Esp32BaseWeb::sendChunk("\"},\"time\":{\"synced\":");
    writeBool(Esp32BaseNtp::isTimeSynced());
    Esp32BaseWeb::sendChunk(",\"current\":\"");
    if (!Esp32BaseNtp::formatTime(text, sizeof(text), "%Y-%m-%d %H:%M:%S")) text[0] = '\0';
    Esp32BaseWeb::writeJsonEscaped(text);
    Esp32BaseWeb::sendChunk("\"},\"records\":{\"count\":");
    writeUInt(RecordStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(RecordStore::capacity());
    Esp32BaseWeb::sendChunk("},\"events\":{\"count\":");
    writeUInt(EventStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(EventStore::capacity());
    Esp32BaseWeb::sendChunk("},");
    writeSettingsJson();
    Esp32BaseWeb::sendChunk(",");
    writeWateringJson();
    Esp32BaseWeb::sendChunk("}");
    endJson();
}

void handleConfigApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        beginJson();
        Esp32BaseWeb::sendChunk("{");
        writeSettingsJson();
        Esp32BaseWeb::sendChunk("}");
        endJson();
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("GET,POST");
        return;
    }
    uint16_t value = 0;
    bool boolValue = false;
    char text[12] = "";
    bool ok = true;
    if (Esp32BaseWeb::hasParam("road")) {
        uint16_t road = 0;
        ok = readUIntParam("road", &road) && readBoolParam("enabled", &boolValue) && SettingsStore::setRoadEnabled(static_cast<uint8_t>(road), boolValue);
    } else if (Esp32BaseWeb::hasParam("default_mode")) {
        SettingsStore::ExecutionMode mode = SettingsStore::MODE_SIMULTANEOUS;
        ok = readModeParam("default_mode", &mode) && SettingsStore::setDefaultExecutionMode(mode);
    } else if (Esp32BaseWeb::hasParam("quick_r1_min")) {
        uint16_t seconds = 0;
        ok = readDurationSecondsParam("quick_r1_min", &seconds) && SettingsStore::setQuickDurationSec(1, seconds);
    } else if (Esp32BaseWeb::hasParam("quick_r2_min")) {
        uint16_t seconds = 0;
        ok = readDurationSecondsParam("quick_r2_min", &seconds) && SettingsStore::setQuickDurationSec(2, seconds);
    } else if (Esp32BaseWeb::hasParam("flow_no_pulse_timeout_s")) {
        ok = readUIntParam("flow_no_pulse_timeout_s", &value) && value <= 60 && SettingsStore::setFlowNoPulseTimeoutSec(static_cast<uint8_t>(value));
    } else if (Esp32BaseWeb::hasParam("idle_leak_window_s")) {
        ok = readUIntParam("idle_leak_window_s", &value) && value <= 60 && SettingsStore::setIdleLeakWindowSec(static_cast<uint8_t>(value));
    } else if (Esp32BaseWeb::hasParam("idle_leak_pulse_threshold")) {
        ok = readUIntParam("idle_leak_pulse_threshold", &value) && value <= 100 && SettingsStore::setIdleLeakPulseThreshold(static_cast<uint8_t>(value));
    } else if (Esp32BaseWeb::hasParam("r1_name")) {
        ok = Esp32BaseWeb::getParam("r1_name", text, sizeof(text)) && SettingsStore::setRoadName(1, text);
    } else if (Esp32BaseWeb::hasParam("r2_name")) {
        ok = Esp32BaseWeb::getParam("r2_name", text, sizeof(text)) && SettingsStore::setRoadName(2, text);
    } else if (Esp32BaseWeb::hasParam("r1_pulse_per_liter")) {
        ok = readUIntParam("r1_pulse_per_liter", &value) && SettingsStore::setRoadPulsePerLiter(1, value);
    } else if (Esp32BaseWeb::hasParam("r2_pulse_per_liter")) {
        ok = readUIntParam("r2_pulse_per_liter", &value) && SettingsStore::setRoadPulsePerLiter(2, value);
    } else if (Esp32BaseWeb::hasParam("r1_calibration_x1000")) {
        ok = readUIntParam("r1_calibration_x1000", &value) && SettingsStore::setRoadCalibrationX1000(1, value);
    } else if (Esp32BaseWeb::hasParam("r2_calibration_x1000")) {
        ok = readUIntParam("r2_calibration_x1000", &value) && SettingsStore::setRoadCalibrationX1000(2, value);
    } else {
        ok = false;
    }
    if (!ok) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_config\"}");
        return;
    }
    (void)EventStore::append(EventStore::TYPE_CONFIG_CHANGED, EventStore::SOURCE_WEB, 0, 0, SettingsStore::roadEnabledMask(), 0, "config saved");
    redirectTo("/irrigation/settings");
}

void handleWaterStartApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint16_t r1Min = 0;
    uint16_t r2Min = 0;
    bool useR1 = true;
    bool useR2 = false;
    SettingsStore::ExecutionMode mode = SettingsStore::defaultExecutionMode();
    (void)readBoolParam("r1_enabled", &useR1);
    (void)readBoolParam("r2_enabled", &useR2);
    if ((useR1 && (!readUIntParam("r1_min", &r1Min) || r1Min < 1 || r1Min > 240)) ||
        (useR2 && (!readUIntParam("r2_min", &r2Min) || r2Min < 1 || r2Min > 240))) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_duration\"}");
        return;
    }
    if (Esp32BaseWeb::hasParam("mode") && !readModeParam("mode", &mode)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_mode\"}");
        return;
    }
    const uint16_t r1Sec = useR1 ? minutesToSeconds(r1Min) : 0;
    const uint16_t r2Sec = useR2 ? minutesToSeconds(r2Min) : 0;
    if (!WateringSession::startManual(r1Sec, r2Sec, mode, RecordStore::SOURCE_WEB, "web manual")) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_watering_request\"}");
        return;
    }
    redirectTo("/irrigation/manual");
}

void handleWaterStopApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint16_t road = 0;
    (void)readUIntParam("road", &road);
    if (road == 0) {
        WateringSession::stopAll(WateringSession::REASON_MANUAL_STOP, "web stop all");
    } else if (road <= 2) {
        WateringSession::stopRoad(static_cast<uint8_t>(road), WateringSession::REASON_MANUAL_STOP, "web stop road");
    } else {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_road\"}");
        return;
    }
    redirectTo("/irrigation/manual");
}

void handleAlertsClearApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    LeakMonitor::clearAlerts(EventStore::SOURCE_WEB);
    redirectTo("/irrigation");
}

void handleFactoryResetApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    char confirm[8] = "";
    bool clearRecords = false;
    (void)readBoolParam("clear_records", &clearRecords);
    if (!Esp32BaseWeb::getParam("confirm", confirm, sizeof(confirm)) || strcmp(confirm, "RESET") != 0) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"confirmation_required\"}");
        return;
    }
    if (!MaintenanceService::requestFactoryReset(clearRecords)) {
        Esp32BaseWeb::sendJson(409, "{\"ok\":false,\"error\":\"factory_reset_pending\"}");
        return;
    }
    redirectTo("/irrigation/settings");
}

void handlePlansApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        beginJson();
        Esp32BaseWeb::sendChunk("{\"count\":");
        writeUInt(PlanStore::MaxPlans);
        Esp32BaseWeb::sendChunk(",\"plans\":[");
        for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
            if (i) Esp32BaseWeb::sendChunk(",");
            writePlanJson(i, PlanStore::get(i));
        }
        Esp32BaseWeb::sendChunk("]}");
        endJson();
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("GET,POST");
        return;
    }
    uint16_t indexRaw = 0;
    if (!readUIntParam("index", &indexRaw) || indexRaw >= PlanStore::MaxPlans) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_index\"}");
        return;
    }
    PlanStore::Plan plan = PlanStore::get(static_cast<uint8_t>(indexRaw));
    bool enabled = false;
    uint16_t value = 0;
    if (readBoolParam("enabled", &enabled)) plan.enabled = enabled;
    if (Esp32BaseWeb::hasParam("time") && !readMinuteOfDayParam("time", &plan.minuteOfDay)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_time\"}");
        return;
    }
    if (Esp32BaseWeb::hasParam("r1_min")) {
        uint16_t seconds = 0;
        if (!readPlanDurationSecondsParam("r1_min", &seconds)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_duration\"}");
            return;
        }
        plan.roadSec[0] = seconds;
    }
    if (Esp32BaseWeb::hasParam("r2_min")) {
        uint16_t seconds = 0;
        if (!readPlanDurationSecondsParam("r2_min", &seconds)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_duration\"}");
            return;
        }
        plan.roadSec[1] = seconds;
    }
    bool useRoad = false;
    if (readBoolParam("r1_enabled", &useRoad) && !useRoad) plan.roadSec[0] = 0;
    if (readBoolParam("r2_enabled", &useRoad) && !useRoad) plan.roadSec[1] = 0;
    SettingsStore::ExecutionMode mode = plan.mode;
    if (Esp32BaseWeb::hasParam("mode")) {
        if (!readModeParam("mode", &mode)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_mode\"}");
            return;
        }
        plan.mode = mode;
    }
    if (Esp32BaseWeb::hasParam("cycle_days")) {
        if (!readUIntParam("cycle_days", &value) || value < 1 || value > 30) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_cycle\"}");
            return;
        }
        plan.cycleDays = static_cast<uint8_t>(value);
    }
    uint32_t ymd = 0;
    if (readU32Param("cycle_start_ymd", &ymd)) plan.cycleStartYmd = ymd;
    plan.cycleMask = readCycleMaskFromForm(plan.cycleDays);
    if (!PlanStore::set(static_cast<uint8_t>(indexRaw), plan)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_plan\"}");
        return;
    }
    (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED, EventStore::SOURCE_WEB, 0, 0, indexRaw, plan.cycleDays, "plan saved");
    redirectTo("/irrigation/plan-config");
}

void handlePlanSkipApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    char action[16] = "";
    uint32_t ymd = 0;
    if (!Esp32BaseWeb::getParam("action", action, sizeof(action)) || !readU32Param("ymd", &ymd)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_skip_request\"}");
        return;
    }
    bool ok = true;
    if (strcmp(action, "skip_once") == 0 || strcmp(action, "clear_skip") == 0) {
        uint16_t index = 0;
        ok = readUIntParam("index", &index) && index < PlanStore::MaxPlans;
        if (ok) {
            ok = strcmp(action, "skip_once") == 0
                ? PlanSkipStore::setSkipped(static_cast<uint8_t>(index), ymd)
                : PlanSkipStore::clearSkipped(static_cast<uint8_t>(index), ymd);
        }
    } else if (strcmp(action, "skip_day") == 0) {
        char scope[12] = "all";
        (void)Esp32BaseWeb::getParam("scope", scope, sizeof(scope));
        const uint32_t today = currentYmd();
        const bool wantsRemaining = strcmp(scope, "remaining") == 0;
        if (wantsRemaining && today == 0) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"time_not_synced\"}");
            return;
        }
        const bool remaining = wantsRemaining && ymd == today;
        const uint16_t nowMinute = currentMinuteOfDay();
        for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
            const PlanStore::Plan& plan = PlanStore::get(i);
            if (!plan.enabled || !PlanStore::shouldRunOnDate(plan, ymd) || plan.lastRunYmd == ymd) continue;
            if (remaining && plan.minuteOfDay <= nowMinute) continue;
            ok = PlanSkipStore::setSkipped(i, ymd) && ok;
        }
    } else {
        ok = false;
    }
    if (!ok) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"skip_failed\"}");
        return;
    }
    (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED, EventStore::SOURCE_WEB, 0, 0, static_cast<int32_t>(ymd), 0, action);
    redirectTo("/irrigation/plans");
}

void writeRecordJson(const RecordStore::Record& record, void* user) {
    bool* first = static_cast<bool*>(user);
    if (!*first) Esp32BaseWeb::sendChunk(",");
    *first = false;
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(record.id);
    Esp32BaseWeb::sendChunk(",\"session_started_ms\":");
    writeUInt(record.sessionStartedMs);
    Esp32BaseWeb::sendChunk(",\"session_ended_ms\":");
    writeUInt(record.sessionEndedMs);
    Esp32BaseWeb::sendChunk(",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::sourceName(static_cast<RecordStore::Source>(record.source)));
    Esp32BaseWeb::sendChunk("\",\"mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(modeName(static_cast<SettingsStore::ExecutionMode>(record.mode)));
    Esp32BaseWeb::sendChunk("\",\"stop_reason\":\"");
    Esp32BaseWeb::writeJsonEscaped(WateringSession::stopReasonName(static_cast<WateringSession::StopReason>(record.stopReason)));
    Esp32BaseWeb::sendChunk("\",\"enabled_roads\":");
    writeUInt(record.enabledRoads);
    Esp32BaseWeb::sendChunk(",\"flow_no_pulse_timeout_s\":");
    writeUInt(record.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk(",\"roads\":[");
    for (uint8_t road = 1; road <= 2; ++road) {
        if (road > 1) Esp32BaseWeb::sendChunk(",");
        const RecordStore::RoadRecord& rr = record.roads[road - 1];
        const uint32_t pulses = rr.endedPulseCount >= rr.startedPulseCount ? rr.endedPulseCount - rr.startedPulseCount : 0;
        const uint32_t actualSec = rr.endedMs >= rr.startedMs && rr.startedMs > 0 ? (rr.endedMs - rr.startedMs) / 1000UL : 0;
        Esp32BaseWeb::sendChunk("{\"road\":");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(",\"state\":\"");
        Esp32BaseWeb::writeJsonEscaped(WateringSession::roadStateName(static_cast<WateringSession::RoadState>(rr.state)));
        Esp32BaseWeb::sendChunk("\",\"target_sec\":");
        writeUInt(rr.targetSec);
        Esp32BaseWeb::sendChunk(",\"actual_sec\":");
        writeUInt(actualSec);
        Esp32BaseWeb::sendChunk(",\"started_ms\":");
        writeUInt(rr.startedMs);
        Esp32BaseWeb::sendChunk(",\"ended_ms\":");
        writeUInt(rr.endedMs);
        Esp32BaseWeb::sendChunk(",\"started_pulses\":");
        writeUInt(rr.startedPulseCount);
        Esp32BaseWeb::sendChunk(",\"ended_pulses\":");
        writeUInt(rr.endedPulseCount);
        Esp32BaseWeb::sendChunk(",\"pulses\":");
        writeUInt(pulses);
        Esp32BaseWeb::sendChunk(",\"pulse_per_liter\":");
        writeUInt(rr.pulsePerLiter);
        Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
        writeUInt(rr.calibrationX1000);
        Esp32BaseWeb::sendChunk(",\"estimated_ml\":");
        writeUInt(rr.estimatedMilliliters);
        Esp32BaseWeb::sendChunk("}");
    }
    Esp32BaseWeb::sendChunk("]}");
}

void handleRecordsApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    beginJson();
    Esp32BaseWeb::sendChunk("{\"count\":");
    writeUInt(RecordStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(RecordStore::capacity());
    Esp32BaseWeb::sendChunk(",\"records\":[");
    bool first = true;
    (void)RecordStore::readLatest(0, readLimitParam(20, 50), writeRecordJson, &first);
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

void handleRecordsCsvApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::beginCsv(200, "irrigation-records.csv")) return;
    Esp32BaseWeb::sendChunk("session_id,source,mode,stop_reason,session_started_ms,session_ended_ms,enabled_roads,flow_no_pulse_timeout_s,road,state,target_sec,actual_sec,road_started_ms,road_ended_ms,pulse_start,pulse_end,pulses,pulse_per_liter,calibration_x1000,estimated_ml\r\n");
    auto cb = [](const RecordStore::Record& record, void*) {
        for (uint8_t road = 1; road <= 2; ++road) {
            const RecordStore::RoadRecord& rr = record.roads[road - 1];
            const uint32_t pulses = rr.endedPulseCount >= rr.startedPulseCount ? rr.endedPulseCount - rr.startedPulseCount : 0;
            const uint32_t actualSec = rr.endedMs >= rr.startedMs && rr.startedMs > 0 ? (rr.endedMs - rr.startedMs) / 1000UL : 0;
            writeUInt(record.id); Esp32BaseWeb::sendChunk(",");
            Esp32BaseWeb::writeCsvEscaped(RecordStore::sourceName(static_cast<RecordStore::Source>(record.source))); Esp32BaseWeb::sendChunk(",");
            Esp32BaseWeb::writeCsvEscaped(modeName(static_cast<SettingsStore::ExecutionMode>(record.mode))); Esp32BaseWeb::sendChunk(",");
            Esp32BaseWeb::writeCsvEscaped(WateringSession::stopReasonName(static_cast<WateringSession::StopReason>(record.stopReason))); Esp32BaseWeb::sendChunk(",");
            writeUInt(record.sessionStartedMs); Esp32BaseWeb::sendChunk(",");
            writeUInt(record.sessionEndedMs); Esp32BaseWeb::sendChunk(",");
            writeUInt(record.enabledRoads); Esp32BaseWeb::sendChunk(",");
            writeUInt(record.flowNoPulseTimeoutSec); Esp32BaseWeb::sendChunk(",");
            writeUInt(road); Esp32BaseWeb::sendChunk(",");
            Esp32BaseWeb::writeCsvEscaped(WateringSession::roadStateName(static_cast<WateringSession::RoadState>(rr.state))); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.targetSec); Esp32BaseWeb::sendChunk(",");
            writeUInt(actualSec); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.startedMs); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.endedMs); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.startedPulseCount); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.endedPulseCount); Esp32BaseWeb::sendChunk(",");
            writeUInt(pulses); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.pulsePerLiter); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.calibrationX1000); Esp32BaseWeb::sendChunk(",");
            writeUInt(rr.estimatedMilliliters); Esp32BaseWeb::sendChunk("\r\n");
        }
    };
    (void)RecordStore::readLatest(0, RecordStore::capacity(), cb, nullptr);
    Esp32BaseWeb::endResponse();
}

void writeEventJsonCb(const EventStore::Event& event, void* user) {
    bool* first = static_cast<bool*>(user);
    if (!*first) Esp32BaseWeb::sendChunk(",");
    *first = false;
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(event.id);
    Esp32BaseWeb::sendChunk(",\"uptime_ms\":");
    writeUInt(event.uptimeMs);
    Esp32BaseWeb::sendChunk(",\"epoch\":");
    writeUInt(event.epoch);
    Esp32BaseWeb::sendChunk(",\"type\":\"");
    Esp32BaseWeb::writeJsonEscaped(EventStore::typeName(static_cast<EventStore::Type>(event.type)));
    Esp32BaseWeb::sendChunk("\",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(EventStore::sourceName(static_cast<EventStore::Source>(event.source)));
    Esp32BaseWeb::sendChunk("\",\"road\":");
    writeUInt(event.road);
    Esp32BaseWeb::sendChunk(",\"code\":");
    writeUInt(event.code);
    Esp32BaseWeb::sendChunk(",\"value1\":");
    writeInt(event.value1);
    Esp32BaseWeb::sendChunk(",\"value2\":");
    writeInt(event.value2);
    Esp32BaseWeb::sendChunk(",\"text\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.text);
    Esp32BaseWeb::sendChunk("\"}");
}

void handleEventsApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    beginJson();
    Esp32BaseWeb::sendChunk("{\"count\":");
    writeUInt(EventStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(EventStore::capacity());
    Esp32BaseWeb::sendChunk(",\"events\":[");
    bool first = true;
    (void)EventStore::readLatest(0, readLimitParam(20, 50), writeEventJsonCb, &first);
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

void handleEventsCsvApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::beginCsv(200, "irrigation-events.csv")) return;
    Esp32BaseWeb::sendChunk("event_id,type,source,uptime_ms,epoch,road,code,value1,value2,text\r\n");
    auto cb = [](const EventStore::Event& event, void*) {
        writeUInt(event.id); Esp32BaseWeb::sendChunk(",");
        Esp32BaseWeb::writeCsvEscaped(EventStore::typeName(static_cast<EventStore::Type>(event.type))); Esp32BaseWeb::sendChunk(",");
        Esp32BaseWeb::writeCsvEscaped(EventStore::sourceName(static_cast<EventStore::Source>(event.source))); Esp32BaseWeb::sendChunk(",");
        writeUInt(event.uptimeMs); Esp32BaseWeb::sendChunk(",");
        writeUInt(event.epoch); Esp32BaseWeb::sendChunk(",");
        writeUInt(event.road); Esp32BaseWeb::sendChunk(",");
        writeUInt(event.code); Esp32BaseWeb::sendChunk(",");
        writeInt(event.value1); Esp32BaseWeb::sendChunk(",");
        writeInt(event.value2); Esp32BaseWeb::sendChunk(",");
        Esp32BaseWeb::writeCsvEscaped(event.text); Esp32BaseWeb::sendChunk("\r\n");
    };
    (void)EventStore::readLatest(0, EventStore::capacity(), cb, nullptr);
    Esp32BaseWeb::endResponse();
}

}

namespace IrrigationWeb {

void begin() {
    const bool overviewOk = Esp32BaseWeb::addPage("/irrigation", "总览", handleOverviewPage);
    const bool manualOk = Esp32BaseWeb::addPage("/irrigation/manual", "手动", handleManualPage);
    const bool recentOk = Esp32BaseWeb::addPage("/irrigation/plans", "近期计划", handlePlansPage);
    const bool configPageOk = Esp32BaseWeb::addPage("/irrigation/plan-config", "计划配置", handlePlanConfigPage);
    const bool dataOk = Esp32BaseWeb::addPage("/irrigation/data", "记录", handleDataPage);
    const bool settingsOk = Esp32BaseWeb::addPage("/irrigation/settings", "设置", handleSettingsPage);
    const bool debugOk = Esp32BaseWeb::addPage("/irrigation/debug", "调试", handleDebugPage);
    const bool planEditOk = Esp32BaseWeb::addRoute("/irrigation/plan", Esp32BaseWeb::METHOD_GET, handlePlanEditPage);
    const bool statusOk = Esp32BaseWeb::addApi("/api/v1/status", handleStatusApi);
    const bool configOk = Esp32BaseWeb::addApi("/api/v1/config", handleConfigApi);
    const bool startOk = Esp32BaseWeb::addApi("/api/v1/water/start", handleWaterStartApi);
    const bool stopOk = Esp32BaseWeb::addApi("/api/v1/water/stop", handleWaterStopApi);
    const bool recordsOk = Esp32BaseWeb::addApi("/api/v1/records", handleRecordsApi);
    const bool recordsCsvOk = Esp32BaseWeb::addApi("/api/v1/records.csv", handleRecordsCsvApi);
    const bool eventsOk = Esp32BaseWeb::addApi("/api/v1/events", handleEventsApi);
    const bool eventsCsvOk = Esp32BaseWeb::addApi("/api/v1/events.csv", handleEventsCsvApi);
    const bool plansOk = Esp32BaseWeb::addApi("/api/v1/plans", handlePlansApi);
    const bool skipOk = Esp32BaseWeb::addApi("/api/v1/plans/skip", handlePlanSkipApi);
    const bool alertsOk = Esp32BaseWeb::addApi("/api/v1/alerts/clear", handleAlertsClearApi);
    const bool factoryResetOk = Esp32BaseWeb::addApi("/api/v1/maintenance/factory-reset", handleFactoryResetApi);
    ESP32BASE_LOG_I("irrigation.web", "routes overview=%s manual=%s recent=%s planConfig=%s data=%s settings=%s debug=%s planEdit=%s status=%s config=%s start=%s stop=%s records=%s recordsCsv=%s events=%s eventsCsv=%s plans=%s skip=%s alerts=%s factoryReset=%s firmware=%s",
                    overviewOk ? "ok" : "fail",
                    manualOk ? "ok" : "fail",
                    recentOk ? "ok" : "fail",
                    configPageOk ? "ok" : "fail",
                    dataOk ? "ok" : "fail",
                    settingsOk ? "ok" : "fail",
                    debugOk ? "ok" : "fail",
                    planEditOk ? "ok" : "fail",
                    statusOk ? "ok" : "fail",
                    configOk ? "ok" : "fail",
                    startOk ? "ok" : "fail",
                    stopOk ? "ok" : "fail",
                    recordsOk ? "ok" : "fail",
                    recordsCsvOk ? "ok" : "fail",
                    eventsOk ? "ok" : "fail",
                    eventsCsvOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    skipOk ? "ok" : "fail",
                    alertsOk ? "ok" : "fail",
                    factoryResetOk ? "ok" : "fail",
                    IrrigationVersion::FirmwareName);
}

}
