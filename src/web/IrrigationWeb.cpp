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

bool readYmdParam(const char* name, uint32_t* value) {
    char text[16] = "";
    if (!value || !Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    if (strlen(text) == 10 && text[4] == '-' && text[7] == '-') {
        for (uint8_t i = 0; i < 10; ++i) {
            if ((i == 4 || i == 7) ? text[i] != '-' : (text[i] < '0' || text[i] > '9')) {
                return false;
            }
        }
        const uint32_t year = static_cast<uint32_t>((text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0'));
        const uint32_t month = static_cast<uint32_t>((text[5] - '0') * 10 + (text[6] - '0'));
        const uint32_t day = static_cast<uint32_t>((text[8] - '0') * 10 + (text[9] - '0'));
        *value = year * 10000UL + month * 100UL + day;
        return true;
    }
    return readU32Param(name, value);
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

uint8_t countConfigFields() {
    uint8_t count = 0;
    const char* fields[] = {
        "road",
        "quick_r1_min",
        "quick_r2_min",
        "quick_r3_min",
        "quick_r4_min",
        "flow_no_pulse_timeout_s",
        "idle_leak_window_s",
        "idle_leak_pulse_threshold",
        "r1_name",
        "r2_name",
        "r3_name",
        "r4_name",
        "r1_pulse_per_liter",
        "r2_pulse_per_liter",
        "r3_pulse_per_liter",
        "r4_pulse_per_liter",
        "r1_calibration_x1000",
        "r2_calibration_x1000",
        "r3_calibration_x1000",
        "r4_calibration_x1000",
    };
    for (const char* field : fields) {
        if (Esp32BaseWeb::hasParam(field)) {
            ++count;
        }
    }
    return count;
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

const char* roadStateLabel(WateringSession::RoadState state) {
    switch (state) {
        case WateringSession::ROAD_DISABLED: return "未启用";
        case WateringSession::ROAD_IDLE: return "关闭";
        case WateringSession::ROAD_STARTING: return "启动中";
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

uint32_t cycleStartYmdForEdit(const PlanStore::Plan& plan) {
    const uint32_t today = currentYmd();
    if (today >= 20000101UL && !plan.enabled && plan.lastRunYmd == 0 && plan.cycleStartYmd == PlanStore::DefaultCycleStartYmd) {
        return today;
    }
    return plan.cycleStartYmd;
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

uint8_t parseRoadFromKey(const char* key, const char* suffix) {
    if (!key || !suffix || key[0] != 'r' || key[1] < '1' || key[1] > '4') {
        return 0;
    }
    return strcmp(key + 2, suffix) == 0 ? static_cast<uint8_t>(key[1] - '0') : 0;
}

void writeRoadDisplayName(uint8_t road) {
    const SettingsStore::Settings& s = SettingsStore::current();
    if (road >= 1 && road <= IrrigationPins::MaxRoads && s.roads[road - 1].name[0] != '\0') {
        Esp32BaseWeb::writeHtmlEscaped(s.roads[road - 1].name);
        return;
    }
    Esp32BaseWeb::sendChunk("第 ");
    writeUInt(road);
    Esp32BaseWeb::sendChunk(" 路");
}

const char* valvePinName(uint8_t road) {
    switch (road) {
        case 1: return "GPIO13";
        case 2: return "GPIO14";
        case 3: return "GPIO16";
        case 4: return "GPIO27";
        default: return "-";
    }
}

const char* flowPinName(uint8_t road) {
    switch (road) {
        case 1: return "GPIO34";
        case 2: return "GPIO35";
        case 3: return "GPIO36";
        case 4: return "GPIO39";
        default: return "-";
    }
}

void writeCss() {
    Esp32BaseWeb::sendChunk(
        "<style>"
        ":root{color-scheme:light;--bg:#f7f8fa;--surface:#fff;--muted:#667085;--text:#17202a;--line:#d8dee6;--soft:#edf0f3;--primary:#146c5f;--ok:#087443;--warn:#a15c07;--danger:#b42318}"
        ".shell,.shell *{box-sizing:border-box}.shell{--page-width:1040px;width:100%;max-width:var(--page-width);margin:0 auto;padding:6px 0 34px;color:var(--text);font-size:14px;line-height:1.5}.page-overview .shell{--page-width:980px}.page-table .shell{--page-width:1040px}.page-form .shell{--page-width:1040px}.page-settings .shell{--page-width:860px}"
        ".shell a{color:var(--primary);text-decoration:none}.shell a:hover{text-decoration:underline}"
        ".page-head{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:14px}.shell h1,.shell h2,.shell h3,.shell p{margin-top:0}.shell h1{margin-bottom:4px;font-size:26px;line-height:1.25}.subtitle,.note{margin-bottom:0;color:var(--muted)}"
        ".grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:12px;align-items:start}.panel{grid-column:span 12;min-width:0;padding:14px;background:var(--surface);border:1px solid var(--line);border-radius:8px}.span-4{grid-column:span 4}.span-6{grid-column:span 6}.span-8{grid-column:span 8}.span-12{grid-column:span 12}.panel h2{margin-bottom:12px;font-size:16px}.panel h3{margin:16px 0 8px;color:#475467;font-size:14px}"
        ".panel-titlebar{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:12px}.panel-titlebar h2{margin-bottom:0}.panel-tools,.actions{display:flex;flex-wrap:wrap;gap:8px}.panel-tools{justify-content:flex-end}.actions{margin-top:14px}.form-actions{justify-content:flex-end}.title-date{margin-left:8px;color:var(--muted);font-size:13px;font-weight:500}"
        ".overview-status{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.overview-status div{display:flex;align-items:center;justify-content:space-between;gap:8px;min-width:0;padding:8px 10px;border:1px solid var(--soft);border-radius:6px;background:#f9fafb}.overview-status span,.matrix-head,.matrix-label{color:var(--muted)}.overview-status strong{overflow-wrap:anywhere}"
        ".valve-matrix{display:grid;grid-template-columns:minmax(56px,.7fr) repeat(4,minmax(0,1fr));border:1px solid var(--line);border-radius:8px;overflow:hidden}.valve-matrix>*{min-width:0;padding:8px 10px;border-right:1px solid var(--soft);border-bottom:1px solid var(--soft)}.valve-matrix>:nth-child(5n){border-right:0}.valve-matrix>:nth-last-child(-n+5){border-bottom:0}.matrix-head{background:#f9fafb;font-size:13px;font-weight:650}.matrix-label{font-weight:650}.status-compact{grid-template-columns:1fr;margin-bottom:12px}.valve-action{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-top:12px}"
        ".badge{display:inline-flex;align-items:center;min-height:24px;padding:2px 8px;border-radius:999px;background:#f0f2f4;color:#475467;font-size:13px;font-weight:650;white-space:nowrap}.badge.ok{background:#e7f5ee;color:var(--ok)}.badge.warn{background:#fff5df;color:var(--warn)}.badge.danger{background:#fff0ee;color:var(--danger)}.badge.off{background:#f7f1e8;color:#875a20}"
        "input,select{min-width:0;width:100%;min-height:36px;padding:7px 9px;border:1px solid #cfd6df;border-radius:6px;background:#fff;color:var(--text);font:inherit}input[type=checkbox]{position:absolute;opacity:0;width:1px;min-height:0;margin:0;padding:0;border:0}input:disabled,select:disabled{color:#98a2b3;background:#f3f4f6}.field-grid{display:grid;grid-template-columns:repeat(3,minmax(120px,1fr));gap:12px 14px}.field{display:grid;gap:6px;min-width:0}.field label{color:#475467;font-size:13px;font-weight:650}.check-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(92px,1fr));gap:8px}.check-item{position:relative;display:flex;align-items:center;justify-content:center;min-height:36px;padding:0 10px;border:1px solid var(--soft);border-radius:6px;background:#fff;color:#344054;font-weight:650}.check-item:has(input:checked){border-color:var(--primary);background:#e7f1ef;color:var(--primary)}"
        ".manual-start{padding:12px}.manual-start form{display:grid;gap:12px}.manual-road-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.manual-road{display:grid;gap:9px;min-width:0;padding:10px;border:1px solid var(--line);border-radius:8px;background:#fff}.manual-road.off{background:#f8f9fa}.manual-road-head{display:flex;align-items:center;justify-content:space-between;gap:8px}.manual-road-head strong{font-size:15px}.manual-fields{display:grid;grid-template-columns:1fr 1fr;gap:8px}.manual-fields label{display:grid;gap:5px;color:#475467;font-size:13px;font-weight:650}.input-suffix{display:flex;align-items:center;border:1px solid #cfd6df;border-radius:6px;background:#fff;overflow:hidden}.input-suffix input{border:0;border-radius:0}.input-suffix em{padding:0 9px;color:var(--muted);font-style:normal;white-space:nowrap}.manual-road.off .input-suffix{background:#f3f4f6}.manual-road.off .input-suffix input{background:#f3f4f6}"
        "button,.button,input[type=submit]{display:inline-flex;align-items:center;justify-content:center;min-height:32px;padding:0 11px;border:1px solid var(--primary);border-radius:6px;background:var(--primary);color:#fff;font:inherit;font-weight:650;cursor:default}.button.secondary,button.secondary,input.secondary{border-color:#cfd6df;background:#fff;color:#344054}.button.warn,button.warn,input.warn{border-color:#cfd6df;background:#fff;color:#344054}button:disabled,input:disabled{border-color:#d0d5dd;background:#f2f4f7;color:#98a2b3}.input-suffix input,.input-suffix input:disabled{border:0;border-radius:0;margin:0}"
        ".table-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:8px;background:#fff}table{width:100%;border-collapse:collapse;background:#fff}th,td{padding:9px 10px;border-bottom:1px solid var(--soft);text-align:left;white-space:nowrap}th{background:#f9fafb;color:#475467;font-size:13px;font-weight:650}tr:last-child td{border-bottom:0}.action-table th:last-child,.action-table td:last-child{width:1%;text-align:center}.action-table td:last-child button,.action-table td:last-child input{min-width:82px}"
        ".setting-list{border:1px solid var(--line);border-radius:8px;overflow:hidden}.setting-row{display:grid;grid-template-columns:minmax(82px,1fr) minmax(80px,1fr) auto;gap:8px;align-items:center;min-width:0;padding:7px 9px;border-bottom:1px solid var(--soft);background:#fff}.setting-row:last-child{border-bottom:0}.setting-row span{color:var(--muted);font-weight:650}.setting-row strong{min-width:0;overflow-wrap:anywhere}.summary-line{margin-top:12px;padding:8px 10px;border-radius:6px;background:#f9fafb;color:#344054;font-weight:650}.json-box{margin:0;padding:14px;overflow-x:auto;border-radius:8px;background:#182230;color:#d6e4ff;font-size:13px}"
        ".modal[hidden]{display:none}.modal{position:fixed;inset:0;z-index:20;display:grid;place-items:center;padding:18px;background:rgba(15,23,42,.32)}.modal-card{width:min(420px,100%);padding:16px;background:#fff;border:1px solid var(--line);border-radius:8px;box-shadow:0 18px 40px rgba(15,23,42,.18)}.modal-card h2{margin-bottom:4px;font-size:18px}.modal-card .field-grid{grid-template-columns:1fr;margin-top:12px}.modal-card .actions{justify-content:flex-end}.modal-value{margin:6px 0 0;color:var(--muted)}"
        "@media(max-width:980px){.span-4,.span-6,.span-8{grid-column:span 12}.field-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.manual-road-grid{grid-template-columns:1fr}}"
        "@media(max-width:640px){.shell{padding-top:0;font-size:13px}.page-head{display:block;margin-bottom:14px}.shell h1{font-size:22px}.panel{padding:12px}.panel-titlebar{align-items:flex-start;flex-direction:column;gap:8px}.panel-tools{justify-content:flex-start}.field-grid{grid-template-columns:1fr}.check-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.overview-status{grid-template-columns:1fr}.valve-matrix{grid-template-columns:minmax(48px,.6fr) repeat(4,minmax(82px,1fr))}.manual-fields{grid-template-columns:1fr}.setting-row{grid-template-columns:1fr auto}.setting-row strong{grid-column:1}.setting-row a,.setting-row button{grid-column:2;grid-row:1/span 2}}"
        "</style><script>document.addEventListener('submit',function(e){var f=e.target;if(!f||String(f.method).toLowerCase()!=='post')return;var raw=f.getAttribute('data-confirm');if(raw==='off')return;var msg=raw||'确认执行此操作？';if(!confirm(msg)){e.preventDefault();}});</script>");
}

void sendHeader(const char* title, const char* pageClass) {
    Esp32BaseWeb::setHeadExtraCallback(writeCss);
    Esp32BaseWeb::sendHeader(title);
    Esp32BaseWeb::setHeadExtraCallback(nullptr);
    Esp32BaseWeb::sendChunk("<script>document.body.className='");
    Esp32BaseWeb::writeHtmlEscaped(pageClass);
    Esp32BaseWeb::sendChunk("';</script><main class='shell'>");
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
    Esp32BaseWeb::writeHtmlEscaped(WateringSession::isActive() ? "浇水中" : "未运行");
    Esp32BaseWeb::sendChunk("</strong></div></div><div class='valve-matrix' role='table'><div class='matrix-head'>项目</div>");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        Esp32BaseWeb::sendChunk("<div class='matrix-head'>第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路</div>");
    }
    const char* labels[] = {"状态", "目标", "剩余", "水量", "流速"};
    for (uint8_t row = 0; row < 5; ++row) {
        Esp32BaseWeb::sendChunk("<div class='matrix-label'>");
        Esp32BaseWeb::sendChunk(labels[row]);
        Esp32BaseWeb::sendChunk("</div>");
        for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
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
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' data-confirm='确认停止第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路？'><input type='hidden' name='road' value='");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("'><button class='secondary'");
        Esp32BaseWeb::sendChunk(WateringSession::isRoadActive(road) ? "" : " disabled");
        Esp32BaseWeb::sendChunk(">停止第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路</button></form>");
    }
    Esp32BaseWeb::sendChunk(active ? "" : "<span class='note'>当前未运行，停止不可用。</span>");
    Esp32BaseWeb::sendChunk("</div></div>");
}

void writeManualStartPanel() {
    const SettingsStore::Settings& settings = SettingsStore::current();
    Esp32BaseWeb::sendChunk("<div class='panel span-12 manual-start'><h2>手动浇水</h2><form method='post' action='/api/v1/water/start' data-confirm='确认开始手动浇水？'><div class='manual-road-grid'>");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        const bool enabled = SettingsStore::isRoadEnabled(road);
        Esp32BaseWeb::sendChunk("<div class='manual-road");
        Esp32BaseWeb::sendChunk(enabled ? "" : " off");
        Esp32BaseWeb::sendChunk("'><div class='manual-road-head'><strong>第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路 ");
        writeRoadDisplayName(road);
        Esp32BaseWeb::sendChunk("</strong><span class='badge");
        Esp32BaseWeb::sendChunk(enabled ? " ok" : " off");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(enabled ? "已启用" : "未启用");
        Esp32BaseWeb::sendChunk("</span></div><div class='manual-fields'><label><span>参与</span><select name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_enabled'");
        Esp32BaseWeb::sendChunk(enabled ? "" : " disabled");
        Esp32BaseWeb::sendChunk("><option value='1'>浇水</option><option value='0'");
        writeSelected(!enabled);
        Esp32BaseWeb::sendChunk(">");
        Esp32BaseWeb::sendChunk(enabled ? "不浇水" : "未启用");
        Esp32BaseWeb::sendChunk("</option></select></label><label><span>时长</span><span class='input-suffix'><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='1' max='240' value='");
        writeMinutesFromSeconds(settings.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk("'");
        Esp32BaseWeb::sendChunk(enabled ? "" : " disabled");
        Esp32BaseWeb::sendChunk("><em>分钟</em></span></label></div></div>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><button>开始浇水</button></div></form></div>");
}

const char* signalQuality(long rssi) {
    if (rssi >= -60) return "优秀";
    if (rssi >= -70) return "良好";
    if (rssi >= -80) return "一般";
    return "较弱";
}

void handleOverviewPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("首页", "page-overview");
    writePageHead("首页", "查看当前状态，并执行即时浇水和停止操作。");
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
    writeManualStartPanel();
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>当前告警</h2>");
    if (LeakMonitor::hasAlert()) {
        Esp32BaseWeb::sendChunk("<span class='badge danger'>存在异常</span><p class='note'>请确认现场状态，处理后清除提示。</p><form method='post' action='/api/v1/alerts/clear' data-confirm='确认现场已处理并解除异常提示？'><button>解除异常</button></form>");
    } else {
        Esp32BaseWeb::sendChunk("<span class='badge ok'>无当前异常</span><p class='note'>存在异常时在这里显示当前原因和状态；设备级诊断请查看基础库日志。</p>");
    }
    Esp32BaseWeb::sendChunk("</div></section>");
    sendFooter();
}

uint16_t effectivePlanRoadSec(const PlanStore::Plan& plan, uint8_t road) {
    if (road != plan.roadId || !SettingsStore::isRoadEnabled(road)) {
        return 0;
    }
    return plan.durationSec;
}

bool hasEffectivePlanRoad(const PlanStore::Plan& plan) {
    return effectivePlanRoadSec(plan, plan.roadId) > 0;
}

void writePlanContent(const PlanStore::Plan& plan, bool effectiveOnly = false) {
    const uint16_t seconds = effectiveOnly ? effectivePlanRoadSec(plan, plan.roadId) : plan.durationSec;
    if (seconds == 0) {
        Esp32BaseWeb::sendChunk(effectiveOnly ? "当前无可执行水路" : "未设置");
        return;
    }
    Esp32BaseWeb::sendChunk("第 ");
    writeUInt(plan.roadId);
    Esp32BaseWeb::sendChunk(" 路 ");
    writeRoadDisplayName(plan.roadId);
    Esp32BaseWeb::sendChunk(" ");
    writeMinutesFromSeconds(seconds);
    Esp32BaseWeb::sendChunk(" 分钟");
}

void writeCycleText(const PlanStore::Plan& plan) {
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk(" 天循环，执行第 ");
    bool first = true;
    for (uint8_t i = 0; i < plan.cycleDays; ++i) {
        if ((plan.cycleMask & (1UL << i)) == 0) continue;
        if (!first) Esp32BaseWeb::sendChunk("、");
        first = false;
        writeUInt(i + 1);
    }
    Esp32BaseWeb::sendChunk(" 天");
}

const char* statusClass(const char* status) {
    if (strcmp(status, "已完成") == 0) return " ok";
    if (strcmp(status, "已处理") == 0) return " ok";
    if (strcmp(status, "进行中") == 0) return " danger";
    if (strcmp(status, "已跳过") == 0) return " warn";
    if (strcmp(status, "不可执行") == 0) return " warn";
    return "";
}

const char* dayLabel(int8_t offset) {
    if (offset == 0) return "今天";
    if (offset == 1) return "明天";
    if (offset == 2) return "后天";
    return "近期";
}

bool writeRecentRows(const char* label, int8_t offset, uint32_t ymd) {
    const uint16_t nowMinute = currentMinuteOfDay();
    bool any = false;
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        const bool skipped = PlanSkipStore::isSkipped(i, ymd);
        const bool handled = plan.lastRunYmd == ymd;
        const WateringSession::RoadStatus& roadStatus = WateringSession::roadStatus(plan.roadId);
        const bool running = offset == 0 && plan.minuteOfDay == nowMinute && WateringSession::isRoadActive(plan.roadId) && roadStatus.taskType == RecordStore::TASK_PLAN && roadStatus.planSlot == plan.slotIndex;
        const bool executable = hasEffectivePlanRoad(plan);
        if (!plan.enabled && !handled && !running) continue;
        if (plan.enabled && !PlanStore::shouldRunOnDate(plan, ymd)) continue;
        any = true;
        const bool pastToday = offset == 0 && plan.minuteOfDay < nowMinute;
        const char* status = running ? "进行中" : (handled ? "已处理" : (skipped ? "已跳过" : (!executable ? "不可执行" : (pastToday ? "未执行" : "未开始"))));
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(label);
        Esp32BaseWeb::sendChunk("<span class='title-date'>");
        writeYmd(ymd);
        Esp32BaseWeb::sendChunk("</span></td><td>");
        writeMinuteOfDay(plan.minuteOfDay);
        Esp32BaseWeb::sendChunk("</td><td>计划 ");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk("</td><td>");
        writePlanContent(plan, true);
        Esp32BaseWeb::sendChunk("</td><td><span class='badge");
        Esp32BaseWeb::sendChunk(statusClass(status));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(status);
        Esp32BaseWeb::sendChunk("</span></td><td>");
        Esp32BaseWeb::sendChunk(running ? "正在执行" : (handled ? "今日已触发处理" : (skipped ? "已跳过这一次" : (!executable ? "无启用水路" : (pastToday ? "已过执行时间" : "等待执行")))));
        Esp32BaseWeb::sendChunk("</td><td>");
        if (plan.enabled && executable && !handled && !running && !pastToday && !skipped) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans/skip' data-confirm='确认跳过本次计划？'><input type='hidden' name='action' value='skip_once'><input type='hidden' name='index' value='");
            writeUInt(i);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='ymd' value='");
            writeUInt(ymd);
            Esp32BaseWeb::sendChunk("'><button class='warn'>跳过本次</button></form>");
        } else if (skipped) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans/skip' data-confirm='确认取消跳过本次计划？'><input type='hidden' name='action' value='clear_skip'><input type='hidden' name='index' value='");
            writeUInt(i);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='ymd' value='");
            writeUInt(ymd);
            Esp32BaseWeb::sendChunk("'><button class='secondary'>取消跳过</button></form>");
        } else {
            Esp32BaseWeb::sendChunk("-");
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    return any;
}

void handlePlansPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("近期计划", "page-table");
    writePageHead("近期计划", "查看今天、明天、后天的计划执行状态，并按单次跳过未来未执行计划。");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><div class='table-wrap'><table class='action-table'><thead><tr><th>日期</th><th>时间</th><th>计划</th><th>内容</th><th>状态</th><th>说明</th><th>操作</th></tr></thead><tbody>");
    bool any = false;
    bool timeSynced = true;
    for (int8_t offset = 0; offset <= 2; ++offset) {
        tm date = {};
        if (!localDateFromOffset(offset, &date)) {
            timeSynced = false;
            break;
        }
        const uint32_t ymd = makeYmd(date);
        any = writeRecentRows(dayLabel(offset), offset, ymd) || any;
    }
    if (!timeSynced) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>时间未同步</td></tr>");
    } else if (!any) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>今天、明天、后天无计划</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div></div></section>");
    sendFooter();
}

void handlePlanConfigPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("计划配置", "page-table");
    writePageHead("计划配置", "这里只修改计划内容；执行结果在近期计划和历史记录页查看。");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><h2>计划列表</h2><div class='table-wrap'><table><thead><tr><th>状态</th><th>计划</th><th>时间</th><th>循环规则</th><th>内容</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        Esp32BaseWeb::sendChunk("<tr><td><span class='badge");
        Esp32BaseWeb::sendChunk(plan.enabled ? " ok" : " off");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(plan.enabled ? "启用" : "停用");
        Esp32BaseWeb::sendChunk("</span></td><td>第 ");
        writeUInt(plan.roadId);
        Esp32BaseWeb::sendChunk(" 路 / 计划 ");
        writeUInt(plan.slotIndex + 1);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeMinuteOfDay(plan.minuteOfDay);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeCycleText(plan);
        Esp32BaseWeb::sendChunk("</td><td>");
        writePlanContent(plan, true);
        Esp32BaseWeb::sendChunk("</td><td><a class='button secondary' href='/irrigation/plan?edit=");
        writeUInt(i);
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
    sendHeader("编辑计划", "page-form");
    writePageHead("编辑计划", "编辑单个计划的固定配置，保存后用于后续自动浇水。");
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans' data-confirm='确认保存计划？'><input type='hidden' name='index' value='");
    writeUInt(index);
    Esp32BaseWeb::sendChunk("'><section class='grid'><div class='panel span-12'><div class='panel-titlebar'><h2>第 ");
    writeUInt(plan.roadId);
    Esp32BaseWeb::sendChunk(" 路 / 计划 ");
    writeUInt(plan.slotIndex + 1);
    Esp32BaseWeb::sendChunk("</h2><a class='button secondary' href='/irrigation/plan-config'>返回计划配置</a></div></div><div class='panel span-12'><h2>计划状态</h2><div class='field-grid'><div class='field'><label>启用状态</label><select name='enabled'><option value='0'");
    writeSelected(!plan.enabled);
    Esp32BaseWeb::sendChunk(">停用</option><option value='1'");
    writeSelected(plan.enabled);
    Esp32BaseWeb::sendChunk(">启用</option></select></div><div class='field'><label>执行时间</label><input name='time' type='time' value='");
    writeMinuteOfDay(plan.minuteOfDay);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>水路</label><input disabled value='第 ");
    writeUInt(plan.roadId);
    Esp32BaseWeb::sendChunk(" 路 ");
    writeRoadDisplayName(plan.roadId);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>浇水时长</label><span class='input-suffix'><input name='duration_min' type='number' min='1' max='240' value='");
    writeMinutesFromSeconds(plan.durationSec);
    Esp32BaseWeb::sendChunk("'><em>分钟</em></span></div></div></div>");
    const uint32_t editCycleStartYmd = cycleStartYmdForEdit(plan);
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>循环规则</h2><div class='field-grid'><div class='field'><label>循环天数</label><input id='cycleDays' name='cycle_days' type='number' min='1' max='30' value='");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>循环开始日期</label><input name='cycle_start_ymd' type='date' value='");
    writeYmd(editCycleStartYmd);
    Esp32BaseWeb::sendChunk("'></div></div><h3>循环执行日</h3><div class='check-grid' id='cycleDayList'>");
    for (uint8_t i = 0; i < 30; ++i) {
        Esp32BaseWeb::sendChunk("<label class='check-item' data-day='");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk("'><input type='checkbox' name='d");
        writeUInt(i);
        Esp32BaseWeb::sendChunk("'");
        writeChecked((plan.cycleMask & (1UL << i)) != 0);
        Esp32BaseWeb::sendChunk("> 第 ");
        writeUInt(i + 1);
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
    Esp32BaseWeb::sendChunk("</p><form method='post' action='/irrigation/settings/config' data-confirm='确认保存设置？'><div class='field-grid'>");
    uint8_t road = parseRoadFromKey(edit, "_enabled");
    if (road > 0) {
        Esp32BaseWeb::sendChunk("<input type='hidden' name='road' value='");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("'><div class='field'><label>启用状态</label><select name='enabled'><option value='1'");
        writeSelected(SettingsStore::isRoadEnabled(road));
        Esp32BaseWeb::sendChunk(">已启用</option><option value='0'");
        writeSelected(!SettingsStore::isRoadEnabled(road));
        Esp32BaseWeb::sendChunk(">未启用</option></select></div>");
    } else if ((road = parseRoadFromKey(edit, "_name")) > 0) {
        Esp32BaseWeb::sendChunk("<div class='field'><label>名称</label><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_name' maxlength='11' value='");
        Esp32BaseWeb::writeHtmlEscaped(s.roads[road - 1].name);
        Esp32BaseWeb::sendChunk("'></div>");
    } else if (strncmp(edit, "quick_r", 7) == 0 && edit[7] >= '1' && edit[7] <= '4' && strcmp(edit + 8, "_min") == 0) {
        road = static_cast<uint8_t>(edit[7] - '0');
        Esp32BaseWeb::sendChunk("<div class='field'><label>默认时长（分钟）</label><input name='quick_r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='1' max='240' value='");
        writeMinutesFromSeconds(s.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk("'></div>");
    } else if ((road = parseRoadFromKey(edit, "_pulse_per_liter")) > 0) {
        Esp32BaseWeb::sendChunk("<div class='field'><label>每升脉冲</label><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_pulse_per_liter' type='number' min='1' max='10000' value='");
        writeUInt(s.roads[road - 1].pulsePerLiter);
        Esp32BaseWeb::sendChunk("'></div>");
    } else if ((road = parseRoadFromKey(edit, "_calibration_x1000")) > 0) {
        Esp32BaseWeb::sendChunk("<div class='field'><label>校准系数</label><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_calibration_x1000' type='number' min='100' max='10000' value='");
        writeUInt(s.roads[road - 1].calibrationX1000);
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
        writeUInt(max);
        Esp32BaseWeb::sendChunk("' value='");
        writeUInt(current);
        Esp32BaseWeb::sendChunk("'></div>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><button type='button' class='secondary' data-setting-close>取消</button><button>保存</button></div></form></div></div>");
}

void writeSettingEditModals() {
    const SettingsStore::Settings& s = SettingsStore::current();
    char value[32];
    char key[32];
    char title[32];
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        const SettingsStore::RoadConfig& r = s.roads[road - 1];
        snprintf(key, sizeof(key), "r%u_enabled", static_cast<unsigned>(road));
        snprintf(title, sizeof(title), "第 %u 路启用状态", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%s", SettingsStore::isRoadEnabled(road) ? "已启用" : "未启用");
        writeSettingEditModal(key, title, value);
        snprintf(key, sizeof(key), "r%u_name", static_cast<unsigned>(road));
        snprintf(title, sizeof(title), "第 %u 路名称", static_cast<unsigned>(road));
        writeSettingEditModal(key, title, r.name);
        snprintf(key, sizeof(key), "quick_r%u_min", static_cast<unsigned>(road));
        snprintf(title, sizeof(title), "第 %u 路默认时长", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u 分钟", static_cast<unsigned>(s.quickDurationSec[road - 1] / 60U));
        writeSettingEditModal(key, title, value);
        snprintf(key, sizeof(key), "r%u_pulse_per_liter", static_cast<unsigned>(road));
        snprintf(title, sizeof(title), "第 %u 路每升脉冲", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u pulse/L", static_cast<unsigned>(r.pulsePerLiter));
        writeSettingEditModal(key, title, value);
        snprintf(key, sizeof(key), "r%u_calibration_x1000", static_cast<unsigned>(road));
        snprintf(title, sizeof(title), "第 %u 路校准系数", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(r.calibrationX1000));
        writeSettingEditModal(key, title, value);
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
    sendHeader("灌溉设置", "page-settings");
    writePageHead("灌溉设置", "查看和修改水路、流量计量、默认浇水和安全检测参数。");
    char value[32];
    Esp32BaseWeb::sendChunk("<section class='grid'>");
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h2>灌溉设置</h2><div class='setting-list'>");
    snprintf(value, sizeof(value), "%u 路", static_cast<unsigned>(SettingsStore::enabledRoads()));
    writeSettingReadOnlyRow("已启用路数", value, "由各路启用状态计算");
    writeSettingReadOnlyRow("阀门 PWM", "启动 5 秒后 70%", "固定硬件策略");
    Esp32BaseWeb::sendChunk("</div></div>");
    char key[32];
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        const SettingsStore::RoadConfig& r = s.roads[road - 1];
        Esp32BaseWeb::sendChunk("<div class='panel span-6'><h2>第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路配置</h2><div class='setting-list'>");
        snprintf(key, sizeof(key), "r%u_enabled", static_cast<unsigned>(road));
        writeSettingRow(key, "启用状态", SettingsStore::isRoadEnabled(road) ? "已启用" : "未启用");
        snprintf(key, sizeof(key), "r%u_name", static_cast<unsigned>(road));
        writeSettingRow(key, "名称", r.name);
        writeSettingReadOnlyRow("阀门引脚", valvePinName(road), "固定硬件配置");
        writeSettingReadOnlyRow("流量引脚", flowPinName(road), "固定硬件配置");
        snprintf(key, sizeof(key), "quick_r%u_min", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u 分钟", static_cast<unsigned>(s.quickDurationSec[road - 1] / 60U));
        writeSettingRow(key, "默认时长", value);
        snprintf(key, sizeof(key), "r%u_pulse_per_liter", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u pulse/L", static_cast<unsigned>(r.pulsePerLiter));
        writeSettingRow(key, "每升脉冲", value);
        snprintf(key, sizeof(key), "r%u_calibration_x1000", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(r.calibrationX1000));
        writeSettingRow(key, "校准系数", value);
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
    Esp32BaseWeb::sendChunk("</section>");
    writeSettingEditModals();
    writeSettingsModalScript();
    sendFooter();
}

void handleDataPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("历史记录", "page-table");
    writePageHead("历史记录", "查看每一次浇水任务的来源、停止来源、执行结果和实际水量。");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><div class='panel-titlebar'><h2>浇水记录</h2></div><div class='table-wrap'><table><thead><tr><th>ID</th><th>路</th><th>任务</th><th>启动来源</th><th>停止来源</th><th>结果</th><th>目标</th><th>实际</th><th>水量</th></tr></thead><tbody>");
    auto recordCb = [](const RecordStore::Record& record, void*) {
        const uint32_t actualSec = record.endedMs >= record.startedMs && record.startedMs > 0 ? (record.endedMs - record.startedMs) / 1000UL : 0;
        Esp32BaseWeb::sendChunk("<tr><td>");
        writeUInt(record.id);
        Esp32BaseWeb::sendChunk("</td><td>第 ");
        writeUInt(record.roadId);
        Esp32BaseWeb::sendChunk(" 路</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(RecordStore::taskTypeName(static_cast<RecordStore::TaskType>(record.taskType)));
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(RecordStore::triggerSourceName(static_cast<RecordStore::TriggerSource>(record.startSource)));
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(RecordStore::triggerSourceName(static_cast<RecordStore::TriggerSource>(record.stopSource)));
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(RecordStore::resultName(static_cast<RecordStore::Result>(record.result)));
        Esp32BaseWeb::sendChunk("</td><td>");
        writeMinutesFromSeconds(record.targetSec);
        Esp32BaseWeb::sendChunk(" 分钟</td><td>");
        writeUInt(actualSec);
        Esp32BaseWeb::sendChunk(" 秒</td><td>");
        writeLitersFromMl(record.estimatedMilliliters);
        Esp32BaseWeb::sendChunk("</td></tr>");
    };
    (void)RecordStore::readLatest(0, 10, recordCb, nullptr);
    Esp32BaseWeb::sendChunk("</tbody></table></div></div></section>");
    sendFooter();
}

void writeSettingsJson() {
    const SettingsStore::Settings& s = SettingsStore::current();
    Esp32BaseWeb::sendChunk("\"settings\":{\"enabled_roads\":");
    writeUInt(SettingsStore::enabledRoads());
    Esp32BaseWeb::sendChunk(",\"road_enabled_mask\":");
    writeUInt(SettingsStore::roadEnabledMask());
    Esp32BaseWeb::sendChunk(",\"flow_no_pulse_timeout_s\":");
    writeUInt(s.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk(",\"idle_leak_window_s\":");
    writeUInt(s.idleLeakWindowSec);
    Esp32BaseWeb::sendChunk(",\"idle_leak_pulse_threshold\":");
    writeUInt(s.idleLeakPulseThreshold);
    Esp32BaseWeb::sendChunk(",\"roads\":{");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        if (road > 1) Esp32BaseWeb::sendChunk(",");
        Esp32BaseWeb::sendChunk("\"r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("\":{\"enabled\":");
        writeBool(SettingsStore::isRoadEnabled(road));
        Esp32BaseWeb::sendChunk(",\"name\":\"");
        Esp32BaseWeb::writeJsonEscaped(s.roads[road - 1].name);
        Esp32BaseWeb::sendChunk("\",\"valve_pin\":\"");
        Esp32BaseWeb::writeJsonEscaped(valvePinName(road));
        Esp32BaseWeb::sendChunk("\",\"flow_pin\":\"");
        Esp32BaseWeb::writeJsonEscaped(flowPinName(road));
        Esp32BaseWeb::sendChunk("\",\"quick_duration_sec\":");
        writeUInt(s.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk(",\"pulse_per_liter\":");
        writeUInt(s.roads[road - 1].pulsePerLiter);
        Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
        writeUInt(s.roads[road - 1].calibrationX1000);
        Esp32BaseWeb::sendChunk("}");
    }
    Esp32BaseWeb::sendChunk("}}");
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
    Esp32BaseWeb::sendChunk(",\"roads\":{");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        if (road > 1) Esp32BaseWeb::sendChunk(",");
        Esp32BaseWeb::sendChunk("\"r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("\":");
        writeRoadJson(road);
    }
    Esp32BaseWeb::sendChunk("}}");
}

void writePlanJson(uint8_t index, const PlanStore::Plan& p) {
    Esp32BaseWeb::sendChunk("{\"index\":");
    writeUInt(index);
    Esp32BaseWeb::sendChunk(",\"enabled\":");
    writeBool(p.enabled);
    Esp32BaseWeb::sendChunk(",\"minute_of_day\":");
    writeUInt(p.minuteOfDay);
    Esp32BaseWeb::sendChunk(",\"road_id\":");
    writeUInt(p.roadId);
    Esp32BaseWeb::sendChunk(",\"slot_index\":");
    writeUInt(p.slotIndex);
    Esp32BaseWeb::sendChunk(",\"duration_sec\":");
    writeUInt(p.durationSec);
    Esp32BaseWeb::sendChunk(",\"cycle_days\":");
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

bool rejectIfFactoryResetPending() {
    if (!MaintenanceService::factoryResetPending()) {
        return false;
    }
    Esp32BaseWeb::sendJson(409, "{\"ok\":false,\"error\":\"factory_reset_pending\"}");
    return true;
}

void sendConfigError(const char* error) {
    beginJson(400);
    Esp32BaseWeb::sendChunk("{\"ok\":false,\"error\":\"");
    Esp32BaseWeb::writeJsonEscaped(error ? error : "invalid_config");
    Esp32BaseWeb::sendChunk("\"}");
    endJson();
}

bool saveConfigFromRequest(const char** error) {
    if (error) {
        *error = nullptr;
    }
    uint16_t value = 0;
    bool boolValue = false;
    char text[12] = "";
    bool ok = true;
    if (countConfigFields() != 1) {
        if (error) {
            *error = "one_config_field_required";
        }
        return false;
    }
    if (Esp32BaseWeb::hasParam("road")) {
        uint16_t road = 0;
        ok = readUIntParam("road", &road) && readBoolParam("enabled", &boolValue) && SettingsStore::setRoadEnabled(static_cast<uint8_t>(road), boolValue);
    } else if (Esp32BaseWeb::hasParam("flow_no_pulse_timeout_s")) {
        ok = readUIntParam("flow_no_pulse_timeout_s", &value) && value <= 60 && SettingsStore::setFlowNoPulseTimeoutSec(static_cast<uint8_t>(value));
    } else if (Esp32BaseWeb::hasParam("idle_leak_window_s")) {
        ok = readUIntParam("idle_leak_window_s", &value) && value <= 60 && SettingsStore::setIdleLeakWindowSec(static_cast<uint8_t>(value));
    } else if (Esp32BaseWeb::hasParam("idle_leak_pulse_threshold")) {
        ok = readUIntParam("idle_leak_pulse_threshold", &value) && value <= 100 && SettingsStore::setIdleLeakPulseThreshold(static_cast<uint8_t>(value));
    } else {
        ok = false;
        for (uint8_t road = 1; road <= IrrigationPins::MaxRoads && !ok; ++road) {
            char key[32];
            snprintf(key, sizeof(key), "quick_r%u_min", static_cast<unsigned>(road));
            if (Esp32BaseWeb::hasParam(key)) {
                uint16_t seconds = 0;
                ok = readDurationSecondsParam(key, &seconds) && SettingsStore::setQuickDurationSec(road, seconds);
                break;
            }
            snprintf(key, sizeof(key), "r%u_name", static_cast<unsigned>(road));
            if (Esp32BaseWeb::hasParam(key)) {
                ok = Esp32BaseWeb::getParam(key, text, sizeof(text)) && SettingsStore::setRoadName(road, text);
                break;
            }
            snprintf(key, sizeof(key), "r%u_pulse_per_liter", static_cast<unsigned>(road));
            if (Esp32BaseWeb::hasParam(key)) {
                ok = readUIntParam(key, &value) && SettingsStore::setRoadPulsePerLiter(road, value);
                break;
            }
            snprintf(key, sizeof(key), "r%u_calibration_x1000", static_cast<unsigned>(road));
            if (Esp32BaseWeb::hasParam(key)) {
                ok = readUIntParam(key, &value) && SettingsStore::setRoadCalibrationX1000(road, value);
                break;
            }
        }
    }
    if (!ok) {
        if (error) {
            *error = "invalid_config";
        }
        return false;
    }
    (void)EventStore::append(EventStore::TYPE_CONFIG_CHANGED, EventStore::SOURCE_WEB, 0, 0, SettingsStore::roadEnabledMask(), 0, "config saved");
    return true;
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
    if (rejectIfFactoryResetPending()) {
        return;
    }
    const char* error = nullptr;
    if (!saveConfigFromRequest(&error)) {
        sendConfigError(error);
        return;
    }
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

void handleSettingsConfigForm() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    if (rejectIfFactoryResetPending()) {
        return;
    }
    const char* error = nullptr;
    if (!saveConfigFromRequest(&error)) {
        sendConfigError(error);
        return;
    }
    redirectTo("/irrigation/settings");
}

void handleWaterStartApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    if (rejectIfFactoryResetPending()) {
        return;
    }
    bool anyRequested = false;
    bool anyStarted = false;
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        bool useRoad = SettingsStore::isRoadEnabled(road);
        char key[16];
        snprintf(key, sizeof(key), "r%u_enabled", static_cast<unsigned>(road));
        (void)readBoolParam(key, &useRoad);
        if (!useRoad) {
            continue;
        }
        anyRequested = true;
        snprintf(key, sizeof(key), "r%u_min", static_cast<unsigned>(road));
        uint16_t minutes = 0;
        if (!readUIntParam(key, &minutes) || minutes < 1 || minutes > 240) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_duration\"}");
            return;
        }
        anyStarted = WateringSession::startRoadTask(road, minutesToSeconds(minutes), RecordStore::TASK_MANUAL, RecordStore::SOURCE_WEB_PAGE, 0xFF, "web manual") || anyStarted;
    }
    if (!anyRequested || !anyStarted) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_watering_request\"}");
        return;
    }
    redirectTo("/irrigation");
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
        WateringSession::stopAll(RecordStore::SOURCE_WEB_PAGE, RecordStore::RESULT_USER_STOPPED, "web stop all");
    } else if (road <= IrrigationPins::MaxRoads) {
        WateringSession::stopRoad(static_cast<uint8_t>(road), RecordStore::SOURCE_WEB_PAGE, "web stop road");
    } else {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_road\"}");
        return;
    }
    redirectTo("/irrigation");
}

void handleAlertsClearApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    if (rejectIfFactoryResetPending()) {
        return;
    }
    LeakMonitor::clearAlerts(EventStore::SOURCE_WEB);
    redirectTo("/irrigation");
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
    if (rejectIfFactoryResetPending()) {
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
    if (Esp32BaseWeb::hasParam("duration_min")) {
        uint16_t seconds = 0;
        if (!readDurationSecondsParam("duration_min", &seconds)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_duration\"}");
            return;
        }
        plan.durationSec = seconds;
    }
    if (Esp32BaseWeb::hasParam("cycle_days")) {
        if (!readUIntParam("cycle_days", &value) || value < 1 || value > 30) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_cycle\"}");
            return;
        }
        plan.cycleDays = static_cast<uint8_t>(value);
    }
    uint32_t ymd = 0;
    if (Esp32BaseWeb::hasParam("cycle_start_ymd")) {
        if (!readYmdParam("cycle_start_ymd", &ymd)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_cycle_start\"}");
            return;
        }
        plan.cycleStartYmd = ymd;
    }
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
    if (rejectIfFactoryResetPending()) {
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
    const uint32_t pulses = record.endedPulseCount >= record.startedPulseCount ? record.endedPulseCount - record.startedPulseCount : 0;
    const uint32_t actualSec = record.endedMs >= record.startedMs && record.startedMs > 0 ? (record.endedMs - record.startedMs) / 1000UL : 0;
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(record.id);
    Esp32BaseWeb::sendChunk(",\"road_id\":");
    writeUInt(record.roadId);
    Esp32BaseWeb::sendChunk(",\"task_type\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::taskTypeName(static_cast<RecordStore::TaskType>(record.taskType)));
    Esp32BaseWeb::sendChunk("\",\"start_source\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::triggerSourceName(static_cast<RecordStore::TriggerSource>(record.startSource)));
    Esp32BaseWeb::sendChunk("\",\"stop_source\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::triggerSourceName(static_cast<RecordStore::TriggerSource>(record.stopSource)));
    Esp32BaseWeb::sendChunk("\",\"stop_scope\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::stopScopeName(static_cast<RecordStore::StopScope>(record.stopScope)));
    Esp32BaseWeb::sendChunk("\",\"result\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::resultName(static_cast<RecordStore::Result>(record.result)));
    Esp32BaseWeb::sendChunk("\",\"plan_slot\":");
    writeUInt(record.planSlot);
    Esp32BaseWeb::sendChunk(",\"enabled_roads\":");
    writeUInt(record.enabledRoads);
    Esp32BaseWeb::sendChunk(",\"flow_no_pulse_timeout_s\":");
    writeUInt(record.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk(",\"target_sec\":");
    writeUInt(record.targetSec);
    Esp32BaseWeb::sendChunk(",\"actual_sec\":");
    writeUInt(actualSec);
    Esp32BaseWeb::sendChunk(",\"started_ms\":");
    writeUInt(record.startedMs);
    Esp32BaseWeb::sendChunk(",\"ended_ms\":");
    writeUInt(record.endedMs);
    Esp32BaseWeb::sendChunk(",\"started_pulses\":");
    writeUInt(record.startedPulseCount);
    Esp32BaseWeb::sendChunk(",\"ended_pulses\":");
    writeUInt(record.endedPulseCount);
    Esp32BaseWeb::sendChunk(",\"pulses\":");
    writeUInt(pulses);
    Esp32BaseWeb::sendChunk(",\"pulse_per_liter\":");
    writeUInt(record.pulsePerLiter);
    Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
    writeUInt(record.calibrationX1000);
    Esp32BaseWeb::sendChunk(",\"estimated_ml\":");
    writeUInt(record.estimatedMilliliters);
    Esp32BaseWeb::sendChunk("}");
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

}

namespace IrrigationWeb {

void begin() {
    const bool overviewOk = Esp32BaseWeb::addPage("/irrigation", "首页", handleOverviewPage);
    const bool recentOk = Esp32BaseWeb::addPage("/irrigation/plans", "近期计划", handlePlansPage);
    const bool dataOk = Esp32BaseWeb::addPage("/irrigation/data", "历史记录", handleDataPage);
    const bool configPageOk = Esp32BaseWeb::addPage("/irrigation/plan-config", "计划配置", handlePlanConfigPage);
    const bool settingsOk = Esp32BaseWeb::addPage("/irrigation/settings", "灌溉设置", handleSettingsPage);
    const bool planEditOk = Esp32BaseWeb::addRoute("/irrigation/plan", Esp32BaseWeb::METHOD_GET, handlePlanEditPage);
    const bool settingsConfigOk = Esp32BaseWeb::addRoute("/irrigation/settings/config", Esp32BaseWeb::METHOD_POST, handleSettingsConfigForm);
    const bool statusOk = Esp32BaseWeb::addApi("/api/v1/status", handleStatusApi);
    const bool configOk = Esp32BaseWeb::addApi("/api/v1/config", handleConfigApi);
    const bool startOk = Esp32BaseWeb::addApi("/api/v1/water/start", handleWaterStartApi);
    const bool stopOk = Esp32BaseWeb::addApi("/api/v1/water/stop", handleWaterStopApi);
    const bool recordsOk = Esp32BaseWeb::addApi("/api/v1/records", handleRecordsApi);
    const bool eventsOk = Esp32BaseWeb::addApi("/api/v1/events", handleEventsApi);
    const bool plansOk = Esp32BaseWeb::addApi("/api/v1/plans", handlePlansApi);
    const bool skipOk = Esp32BaseWeb::addApi("/api/v1/plans/skip", handlePlanSkipApi);
    const bool alertsOk = Esp32BaseWeb::addApi("/api/v1/alerts/clear", handleAlertsClearApi);
    ESP32BASE_LOG_I("irrigation.web", "routes overview=%s recent=%s planConfig=%s data=%s settings=%s planEdit=%s settingsConfig=%s status=%s config=%s start=%s stop=%s records=%s events=%s plans=%s skip=%s alerts=%s firmware=%s",
                    overviewOk ? "ok" : "fail",
                    recentOk ? "ok" : "fail",
                    configPageOk ? "ok" : "fail",
                    dataOk ? "ok" : "fail",
                    settingsOk ? "ok" : "fail",
                    planEditOk ? "ok" : "fail",
                    settingsConfigOk ? "ok" : "fail",
                    statusOk ? "ok" : "fail",
                    configOk ? "ok" : "fail",
                    startOk ? "ok" : "fail",
                    stopOk ? "ok" : "fail",
                    recordsOk ? "ok" : "fail",
                    eventsOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    skipOk ? "ok" : "fail",
                    alertsOk ? "ok" : "fail",
                    IrrigationVersion::FirmwareName);
}

}
