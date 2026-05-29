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
#include "storage/PlanResultStore.h"
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

bool isWebPageSourceParam() {
    char text[16] = "";
    return Esp32BaseWeb::getParam("source", text, sizeof(text)) && strcmp(text, "web_page") == 0;
}

RecordStore::TriggerSource requestTriggerSource() {
    return isWebPageSourceParam() ? RecordStore::SOURCE_WEB_PAGE : RecordStore::SOURCE_HTTP_API;
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

void writeRoadConfiguredName(uint8_t road) {
    const SettingsStore::Settings& s = SettingsStore::current();
    if (road >= 1 && road <= IrrigationPins::MaxRoads && s.roads[road - 1].name[0] != '\0') {
        Esp32BaseWeb::writeHtmlEscaped(s.roads[road - 1].name);
    } else {
        Esp32BaseWeb::sendChunk("未命名");
    }
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

void writeIrrigationHeadExtra() {
    Esp32BaseWeb::sendChunk(
        "<style>"
        "nav a,.brand,.page b,.page strong,.page label,.page .uvalue,.page .tag,.page .btnlink,.page button,.page input[type=submit],.page input[type=button],.page .part th,.page .kv th{font-weight:400}"
        ".page h2{font-weight:500}.page h3{font-weight:500;margin:14px 0 8px}.page .metric b{font-weight:500}.page .notice b{font-weight:500}"
        ".roadcfggrid,.planroadgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px;align-items:start}"
        ".roadcfg,.plangroup{border:1px solid var(--eb-line);border-radius:8px;background:#fff;padding:10px;min-width:0}"
        ".roadcfg.enabled,.plangroup.enabled{border-color:#bfe0d4;background:#fbfffd}.roadcfg.disabled,.plangroup.disabled{border-color:#d8dee5;background:#f7f8fa;color:#667085}"
        ".roadhead,.planhead{display:flex;justify-content:space-between;gap:8px;align-items:flex-start;margin-bottom:8px}.roadtitle,.plantitle{min-width:0}.roadtitle span,.plantitle span{display:block;color:var(--eb-muted);font-size:12px;margin-top:2px}"
        ".cfgrow,.planitem{display:grid;grid-template-columns:minmax(0,1fr) auto auto;gap:8px;align-items:center;border-top:1px solid var(--eb-line-soft);padding:7px 0;font-size:13px}.cfgrow:first-of-type,.planitem:first-of-type{border-top:0}"
        ".cfgrow span,.planitem span{min-width:0;overflow-wrap:anywhere}.cfgvalue{color:#344054;text-align:right;white-space:nowrap}.cfgrow .btnlink,.planitem .btnlink,.planitem input[type=submit],.planhead input[type=submit]{min-height:28px;padding:0 8px;font-size:12px}"
        ".emptyline{border-top:1px solid var(--eb-line-soft);padding-top:8px;color:var(--eb-muted);font-size:13px}.planmeta{color:var(--eb-muted);font-size:12px;margin-top:2px}.planactions{display:flex;flex-wrap:wrap;gap:6px;align-items:center;justify-content:flex-end}.planactions form,.planhead form{margin:0}"
        "@media(max-width:760px){.cfgrow,.planitem{grid-template-columns:1fr}.cfgvalue{text-align:left}.planactions{justify-content:flex-start}}"
        "</style>");
}

void sendHeader(const char* title, const char* pageClass) {
    (void)pageClass;
    Esp32BaseWeb::sendHeader(title);
}

void sendFooter() {
    Esp32BaseWeb::sendFooter();
}

void redirectTo(const char* path) {
    Esp32BaseWeb::redirectSeeOther(path);
}

void writePageHead(const char* title, const char* subtitle) {
    Esp32BaseWeb::sendPageTitle(title, subtitle);
}

void writeWateringStatusPanel(const char* title) {
    Esp32BaseWeb::beginPanel(title);
    Esp32BaseWeb::sendInfoRowCompact("当前浇水", "各路独立计时、计量和停止。", WateringSession::isActive() ? "浇水中" : "未运行");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>项目</th>");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        Esp32BaseWeb::sendChunk("<th>第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路</th>");
    }
    Esp32BaseWeb::sendChunk("</tr></thead><tbody>");
    const char* labels[] = {"状态", "目标", "剩余", "水量", "流速"};
    for (uint8_t row = 0; row < 5; ++row) {
        Esp32BaseWeb::sendChunk("<tr><th>");
        Esp32BaseWeb::sendChunk(labels[row]);
        Esp32BaseWeb::sendChunk("</th>");
        for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
            const WateringSession::RoadStatus& status = WateringSession::roadStatus(road);
            const bool enabled = SettingsStore::isRoadEnabled(road);
            uint32_t remainingSec = 0;
            if (status.state == WateringSession::ROAD_RUNNING && status.targetSec > 0) {
                const uint32_t elapsed = (Esp32BaseSystem::uptimeMs() - status.startedMs) / 1000UL;
                remainingSec = elapsed < status.targetSec ? status.targetSec - elapsed : 0;
            }
            const uint32_t pulses = status.lastPulseCount >= status.startedPulseCount ? status.lastPulseCount - status.startedPulseCount : 0;
            Esp32BaseWeb::sendChunk("<td>");
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
            Esp32BaseWeb::sendChunk("</td>");
        }
        Esp32BaseWeb::sendChunk("</tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><div class='actions'>");
    const bool active = WateringSession::isActive();
    const char* disabled = active ? "" : " disabled";
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' onsubmit=\"return confirm('确认停止全部浇水？')&&once(this)\"><input type='hidden' name='source' value='web_page'><input type='hidden' name='road' value='0'><button type='submit'");
    Esp32BaseWeb::sendChunk(disabled);
    Esp32BaseWeb::sendChunk(">停止全部</button></form>");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' onsubmit=\"return confirm('确认停止第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路？')&&once(this)\"><input type='hidden' name='source' value='web_page'><input type='hidden' name='road' value='");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("'><button type='submit'");
        Esp32BaseWeb::sendChunk(WateringSession::isRoadActive(road) ? "" : " disabled");
        Esp32BaseWeb::sendChunk(">停止第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路</button></form>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    if (!active) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "当前未运行", "停止操作会在有水路运行时启用。");
    }
    Esp32BaseWeb::endPanel();
}

void writeManualStartPanel() {
    const SettingsStore::Settings& settings = SettingsStore::current();
    Esp32BaseWeb::beginPanel("手动浇水");
    Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/water/start' onsubmit=\"return confirm('确认开始手动浇水？')&&once(this)\"><input type='hidden' name='source' value='web_page'><div class='fieldgrid'>");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        const bool enabled = SettingsStore::isRoadEnabled(road);
        Esp32BaseWeb::sendChunk("<p class='field med'><label>第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路 ");
        writeRoadDisplayName(road);
        Esp32BaseWeb::sendChunk("</label><span class='tag");
        Esp32BaseWeb::sendChunk(enabled ? " ok" : "");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(enabled ? "已启用" : "未启用");
        Esp32BaseWeb::sendChunk("</span><select name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_enabled'");
        Esp32BaseWeb::sendChunk(enabled ? "" : " disabled");
        Esp32BaseWeb::sendChunk("><option value='1'>浇水</option><option value='0'");
        writeSelected(!enabled);
        Esp32BaseWeb::sendChunk(">");
        Esp32BaseWeb::sendChunk(enabled ? "不浇水" : "未启用");
        Esp32BaseWeb::sendChunk("</option></select><small>选择是否参与本次手动浇水。</small></p><p class='field short'><label>时长</label><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='1' max='240' value='");
        writeMinutesFromSeconds(settings.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk("'");
        Esp32BaseWeb::sendChunk(enabled ? "" : " disabled");
        Esp32BaseWeb::sendChunk("><small>分钟，范围 1-240。</small></p>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='submit' value='开始浇水'></div></form>");
    Esp32BaseWeb::endPanel();
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
    Esp32BaseWeb::beginPanel("设备状态");
    Esp32BaseWeb::sendNotice(LeakMonitor::hasAlert() ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK,
                             LeakMonitor::hasAlert() ? "存在当前告警" : "设备正常",
                             LeakMonitor::hasAlert() ? "请确认现场状态，处理后再清除提示。" : "当前安全状态正常。");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("设备", LeakMonitor::hasAlert() ? "告警" : "正常");
    Esp32BaseWeb::sendMetric("当前", WateringSession::isActive() ? "浇水中" : "空闲");
#if ESP32BASE_ENABLE_WIFI
    char rssi[16];
    snprintf(rssi, sizeof(rssi), "%ld dBm", static_cast<long>(Esp32BaseWiFi::rssi()));
    Esp32BaseWeb::sendMetric("WiFi", Esp32BaseWiFi::ssid());
    Esp32BaseWeb::sendMetric("信号", signalQuality(Esp32BaseWiFi::rssi()), rssi);
#else
    Esp32BaseWeb::sendMetric("WiFi", "-");
    Esp32BaseWeb::sendMetric("信号", "-");
#endif
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::endPanel();
    writeWateringStatusPanel("浇水状态");
    writeManualStartPanel();
    Esp32BaseWeb::beginPanel("当前告警");
    if (LeakMonitor::hasAlert()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "存在异常", "请确认现场状态，处理后清除提示。");
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/alerts/clear' onsubmit=\"return confirm('确认现场已处理并解除异常提示？')&&once(this)\"><div class='actions'><input class='danger' type='submit' value='解除异常'></div></form>");
    } else {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "无当前异常", "存在异常时在这里显示当前原因和状态；设备级诊断请查看基础库日志。");
    }
    Esp32BaseWeb::endPanel();
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

const char* planResultStatus(PlanResultStore::Result result) {
    switch (result) {
        case PlanResultStore::RESULT_STARTED: return "已启动";
        case PlanResultStore::RESULT_SKIPPED_MANUAL: return "已跳过";
        case PlanResultStore::RESULT_SKIPPED_ROAD_DISABLED:
        case PlanResultStore::RESULT_SKIPPED_ROAD_BUSY:
        case PlanResultStore::RESULT_REJECTED:
        case PlanResultStore::RESULT_CONFIG_INVALID:
        case PlanResultStore::RESULT_FACTORY_RESET_PENDING:
        case PlanResultStore::RESULT_LEAK_ALERT:
            return "未执行";
        case PlanResultStore::RESULT_NONE:
        default:
            return "";
    }
}

const char* planResultStatusClass(PlanResultStore::Result result) {
    switch (result) {
        case PlanResultStore::RESULT_STARTED: return " ok";
        case PlanResultStore::RESULT_SKIPPED_MANUAL: return " warn";
        case PlanResultStore::RESULT_SKIPPED_ROAD_DISABLED:
        case PlanResultStore::RESULT_SKIPPED_ROAD_BUSY:
        case PlanResultStore::RESULT_REJECTED:
        case PlanResultStore::RESULT_CONFIG_INVALID:
        case PlanResultStore::RESULT_FACTORY_RESET_PENDING:
        case PlanResultStore::RESULT_LEAK_ALERT:
            return " warn";
        case PlanResultStore::RESULT_NONE:
        default:
            return "";
    }
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
    uint8_t indices[PlanStore::MaxPlans] = {};
    uint8_t rendered = 0;
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        if (!plan.exists) continue;
        const bool handled = plan.lastRunYmd == ymd;
        const WateringSession::RoadStatus& roadStatus = WateringSession::roadStatus(plan.roadId);
        const bool running = offset == 0 && plan.minuteOfDay == nowMinute && WateringSession::isRoadActive(plan.roadId) && roadStatus.taskType == RecordStore::TASK_PLAN && roadStatus.planSlot == plan.slotIndex;
        if (!plan.enabled && !handled && !running) continue;
        if (plan.enabled && !PlanStore::shouldRunOnDate(plan, ymd)) continue;
        indices[rendered++] = i;
    }
    for (uint8_t i = 0; i < rendered; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < rendered; ++j) {
            const PlanStore::Plan& a = PlanStore::get(indices[i]);
            const PlanStore::Plan& b = PlanStore::get(indices[j]);
            if (b.minuteOfDay < a.minuteOfDay ||
                (b.minuteOfDay == a.minuteOfDay && b.roadId < a.roadId) ||
                (b.minuteOfDay == a.minuteOfDay && b.roadId == a.roadId && b.slotIndex < a.slotIndex)) {
                const uint8_t tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }
    for (uint8_t row = 0; row < rendered; ++row) {
        const uint8_t i = indices[row];
        const PlanStore::Plan& plan = PlanStore::get(i);
        const bool skipped = PlanSkipStore::isSkipped(i, ymd);
        const bool handled = plan.lastRunYmd == ymd;
        PlanResultStore::Result planResult = PlanResultStore::RESULT_NONE;
        const bool hasPlanResult = PlanResultStore::getResult(i, ymd, &planResult);
        const WateringSession::RoadStatus& roadStatus = WateringSession::roadStatus(plan.roadId);
        const bool running = offset == 0 && plan.minuteOfDay == nowMinute && WateringSession::isRoadActive(plan.roadId) && roadStatus.taskType == RecordStore::TASK_PLAN && roadStatus.planSlot == plan.slotIndex;
        const bool executable = hasEffectivePlanRoad(plan);
        any = true;
        const bool pastToday = offset == 0 && plan.minuteOfDay < nowMinute;
        const char* status = running ? "进行中" : (hasPlanResult ? planResultStatus(planResult) : (handled ? "已处理" : (skipped ? "已跳过" : (!executable ? "不可执行" : (pastToday ? "未执行" : "未开始")))));
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(label);
        Esp32BaseWeb::sendChunk("<br><small class='muted'>");
        writeYmd(ymd);
        Esp32BaseWeb::sendChunk("</small></td><td>");
        writeMinuteOfDay(plan.minuteOfDay);
        Esp32BaseWeb::sendChunk("</td><td>计划 ");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk("</td><td>");
        writePlanContent(plan, true);
        Esp32BaseWeb::sendChunk("</td><td><span class='tag");
        Esp32BaseWeb::sendChunk(hasPlanResult ? planResultStatusClass(planResult) : statusClass(status));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(status);
        Esp32BaseWeb::sendChunk("</span></td><td>");
        if (hasPlanResult) {
            Esp32BaseWeb::writeHtmlEscaped(PlanResultStore::resultLabel(planResult));
        } else {
            Esp32BaseWeb::sendChunk(running ? "正在执行" : (handled ? "今日已触发处理" : (skipped ? "已跳过这一次" : (!executable ? "无启用水路" : (pastToday ? "已过执行时间" : "等待执行")))));
        }
        Esp32BaseWeb::sendChunk("</td><td>");
        if (plan.enabled && executable && !handled && !running && !pastToday && !skipped) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans/skip' onsubmit=\"return confirm('确认跳过本次计划？')&&once(this)\"><input type='hidden' name='source' value='web_page'><input type='hidden' name='action' value='skip_once'><input type='hidden' name='index' value='");
            writeUInt(i);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='ymd' value='");
            writeUInt(ymd);
            Esp32BaseWeb::sendChunk("'><input type='submit' value='跳过本次'></form>");
        } else if (skipped) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans/skip' onsubmit=\"return confirm('确认取消跳过本次计划？')&&once(this)\"><input type='hidden' name='source' value='web_page'><input type='hidden' name='action' value='clear_skip'><input type='hidden' name='index' value='");
            writeUInt(i);
            Esp32BaseWeb::sendChunk("'><input type='hidden' name='ymd' value='");
            writeUInt(ymd);
            Esp32BaseWeb::sendChunk("'><input type='submit' value='取消跳过'></form>");
        } else {
            Esp32BaseWeb::sendChunk("-");
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    return any;
}

void writePlanRoadGroup(uint8_t road);

void handlePlansPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("近期计划", "page-table");
    writePageHead("近期计划", "查看今天、明天、后天的计划执行状态，并按单次跳过未来未执行计划。");
    Esp32BaseWeb::beginPanel("近期计划");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>日期</th><th>时间</th><th>计划</th><th>内容</th><th>状态</th><th>说明</th><th>操作</th></tr></thead><tbody>");
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
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    sendFooter();
}

void handlePlanConfigPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    sendHeader("计划配置", "page-plans");
    writePageHead("计划配置", "计划配置页按水路分组；执行结果在近期计划和历史记录页查看。");
    Esp32BaseWeb::beginPanel("按水路配置计划");
    Esp32BaseWeb::sendChunk("<div class='planroadgrid'>");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        writePlanRoadGroup(road);
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();
    sendFooter();
}

void writePlanRoadGroup(uint8_t road) {
    const bool roadEnabled = SettingsStore::isRoadEnabled(road);
    const uint8_t count = PlanStore::countForRoad(road);
    uint8_t indices[PlanStore::MaxPlansPerRoad] = {};
    uint8_t rendered = 0;
    for (uint8_t slot = 0; slot < PlanStore::MaxPlansPerRoad; ++slot) {
        uint8_t index = 0;
        (void)PlanStore::flatIndex(road, slot, &index);
        if (PlanStore::get(index).exists) {
            indices[rendered++] = index;
        }
    }
    for (uint8_t i = 0; i < rendered; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < rendered; ++j) {
            const PlanStore::Plan& a = PlanStore::get(indices[i]);
            const PlanStore::Plan& b = PlanStore::get(indices[j]);
            if (b.minuteOfDay < a.minuteOfDay || (b.minuteOfDay == a.minuteOfDay && b.slotIndex < a.slotIndex)) {
                const uint8_t tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }

    Esp32BaseWeb::sendChunk("<section class='plangroup ");
    Esp32BaseWeb::sendChunk(roadEnabled ? "enabled" : "disabled");
    Esp32BaseWeb::sendChunk("'><div class='planhead'><div class='plantitle'>第 ");
    writeUInt(road);
    Esp32BaseWeb::sendChunk(" 路<span>");
    writeRoadConfiguredName(road);
    Esp32BaseWeb::sendChunk(" · ");
    writeUInt(count);
    Esp32BaseWeb::sendChunk("/");
    writeUInt(PlanStore::MaxPlansPerRoad);
    Esp32BaseWeb::sendChunk(" 条</span></div>");
    if (count < PlanStore::MaxPlansPerRoad) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans' onsubmit=\"return confirm('确认为第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路新建计划？')&&once(this)\"><input type='hidden' name='action' value='create'><input type='hidden' name='road' value='");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("'><input type='submit' value='新增'></form>");
    } else {
        Esp32BaseWeb::sendChunk("<span class='tag warn'>已满</span>");
    }
    Esp32BaseWeb::sendChunk("</div>");

    if (rendered == 0) {
        Esp32BaseWeb::sendChunk("<div class='emptyline'>暂无计划</div>");
    }
    for (uint8_t i = 0; i < rendered; ++i) {
        const uint8_t index = indices[i];
        const PlanStore::Plan& plan = PlanStore::get(index);
        Esp32BaseWeb::sendChunk("<div class='planitem'><span><span class='tag");
        Esp32BaseWeb::sendChunk(plan.enabled ? " ok" : "");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(plan.enabled ? "启用" : "停用");
        Esp32BaseWeb::sendChunk("</span> ");
        writeMinuteOfDay(plan.minuteOfDay);
        Esp32BaseWeb::sendChunk("<div class='planmeta'>");
        writeCycleText(plan);
        Esp32BaseWeb::sendChunk(" · ");
        writePlanContent(plan, false);
        Esp32BaseWeb::sendChunk("</div></span><span></span><div class='planactions'><a class='btnlink info' href='/irrigation/plan?edit=");
        writeUInt(index);
        Esp32BaseWeb::sendChunk("'>修改</a><form method='post' action='/api/v1/plans' onsubmit=\"return confirm('确认");
        Esp32BaseWeb::sendChunk(plan.enabled ? "停用" : "启用");
        Esp32BaseWeb::sendChunk("该计划？')&&once(this)\"><input type='hidden' name='action' value='toggle'><input type='hidden' name='index' value='");
        writeUInt(index);
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='enabled' value='");
        Esp32BaseWeb::sendChunk(plan.enabled ? "0" : "1");
        Esp32BaseWeb::sendChunk("'><input type='submit' value='");
        Esp32BaseWeb::sendChunk(plan.enabled ? "停用" : "启用");
        Esp32BaseWeb::sendChunk("'></form><form method='post' action='/api/v1/plans' onsubmit=\"return confirm('确认删除该计划？')&&once(this)\"><input type='hidden' name='action' value='delete'><input type='hidden' name='index' value='");
        writeUInt(index);
        Esp32BaseWeb::sendChunk("'><input class='danger' type='submit' value='删除'></form></div></div>");
    }
    Esp32BaseWeb::sendChunk("</section>");
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
    const bool hasValidIndex = Esp32BaseWeb::hasParam("edit") && readUIntParam("edit", &raw) && raw < PlanStore::MaxPlans;
    const uint8_t index = hasValidIndex ? static_cast<uint8_t>(raw) : 0;
    const PlanStore::Plan& plan = PlanStore::get(index);
    sendHeader("编辑计划", "page-form");
    writePageHead("编辑计划", "从计划配置页按水路分组进入，保存后用于后续自动浇水。");
    if (!hasValidIndex || !plan.exists) {
        Esp32BaseWeb::beginPanel("计划编辑");
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "计划不存在", "请返回计划配置页，新建或选择已有计划。");
        Esp32BaseWeb::sendInfoRowCompactLink("计划配置", "按水路查看和新建计划。", nullptr, "/irrigation/plan-config", "返回");
        Esp32BaseWeb::endPanel();
        sendFooter();
        return;
    }
    char planTitle[40];
    snprintf(planTitle, sizeof(planTitle), "第 %u 路 / 计划 %u", static_cast<unsigned>(plan.roadId), static_cast<unsigned>(plan.slotIndex + 1));
    Esp32BaseWeb::beginPanel("计划编辑");
    Esp32BaseWeb::sendInfoRowCompactLink("计划位置", "每一路最多 6 条计划，计划只绑定一个水路。", planTitle, "/irrigation/plan-config", "返回");
    Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/api/v1/plans' onsubmit=\"return confirm('确认保存计划？')&&once(this)\"><input type='hidden' name='action' value='save'><input type='hidden' name='index' value='");
    writeUInt(index);
    Esp32BaseWeb::sendChunk("'><h3>计划状态</h3><div class='fieldgrid'><p class='field short'><label>启用状态</label><select name='enabled'><option value='0'");
    writeSelected(!plan.enabled);
    Esp32BaseWeb::sendChunk(">停用</option><option value='1'");
    writeSelected(plan.enabled);
    Esp32BaseWeb::sendChunk(">启用</option></select><small>是否参与自动调度。</small></p><p class='field short'><label>执行时间</label><input name='time' type='time' value='");
    writeMinuteOfDay(plan.minuteOfDay);
    Esp32BaseWeb::sendChunk("'><small>按设备本地时间执行。</small></p><p class='field med'><label>水路</label><input disabled value='第 ");
    writeUInt(plan.roadId);
    Esp32BaseWeb::sendChunk(" 路 ");
    writeRoadDisplayName(plan.roadId);
    Esp32BaseWeb::sendChunk("'><small>固定计划槽所属水路。</small></p><p class='field short'><label>浇水时长</label><input name='duration_min' type='number' min='1' max='240' value='");
    writeMinutesFromSeconds(plan.durationSec);
    Esp32BaseWeb::sendChunk("'><small>分钟，范围 1-240。</small></p></div>");
    const uint32_t editCycleStartYmd = cycleStartYmdForEdit(plan);
    Esp32BaseWeb::sendChunk("<h3>循环规则</h3><div class='fieldgrid'><p class='field short'><label>循环天数</label><input id='cycleDays' name='cycle_days' type='number' min='1' max='30' value='");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk("'><small>范围 1-30 天。</small></p><p class='field med'><label>循环开始日期</label><input name='cycle_start_ymd' type='date' value='");
    writeYmd(editCycleStartYmd);
    Esp32BaseWeb::sendChunk("'><small>作为第 1 天计算循环。</small></p></div><h3>循环执行日</h3><div class='radioopts' id='cycleDayList'>");
    for (uint8_t i = 0; i < 30; ++i) {
        Esp32BaseWeb::sendChunk("<label data-day='");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk("'><input type='checkbox' name='d");
        writeUInt(i);
        Esp32BaseWeb::sendChunk("'");
        writeChecked((plan.cycleMask & (1UL << i)) != 0);
        Esp32BaseWeb::sendChunk("> 第 ");
        writeUInt(i + 1);
        Esp32BaseWeb::sendChunk(" 天</label>");
    }
    Esp32BaseWeb::sendChunk("</div><p><small class='muted'>计划只使用循环规则：每天浇水是 1 天循环；浇 2 天停 1 天是 3 天循环并执行第 1、2 天；每周固定日期是 7 天循环。</small></p><div class='actions'><input type='button' value='返回计划配置' onclick=\"location.href='/irrigation/plan-config'\"><input type='submit' value='保存计划'></div></form><script>(function(){var n=document.getElementById('cycleDays');var list=document.getElementById('cycleDayList');function sync(){var max=Math.max(1,Math.min(30,parseInt(n.value||'1',10)||1));n.value=max;list.querySelectorAll('[data-day]').forEach(function(item){var show=parseInt(item.dataset.day,10)<=max;item.style.display=show?'flex':'none';if(!show){var c=item.querySelector('input');if(c)c.checked=false;}});}if(n&&list){n.addEventListener('input',sync);sync();}})();</script>");
    Esp32BaseWeb::endPanel();
    sendFooter();
}

void writeSettingRow(const char* key, const char* label, const char* value) {
    char href[96];
    snprintf(href, sizeof(href), "/irrigation/settings?edit=%s", key ? key : "");
    Esp32BaseWeb::sendInfoRowCompactLink(label, "点击修改该业务参数。", value, href, "修改");
}

void writeSettingReadOnlyRow(const char* label, const char* value, const char* note) {
    Esp32BaseWeb::sendInfoRowCompact(label, note, value);
}

void writeSettingCardRow(const char* key, const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<div class='cfgrow'><span>");
    Esp32BaseWeb::writeHtmlEscaped(label ? label : "");
    Esp32BaseWeb::sendChunk("</span><span class='cfgvalue'>");
    Esp32BaseWeb::writeHtmlEscaped(value ? value : "");
    Esp32BaseWeb::sendChunk("</span><a class='btnlink info' href='/irrigation/settings?edit=");
    Esp32BaseWeb::writeHtmlEscaped(key ? key : "");
    Esp32BaseWeb::sendChunk("'>修改</a></div>");
}

void writeSettingCardReadOnly(const char* label, const char* value) {
    Esp32BaseWeb::sendChunk("<div class='cfgrow'><span>");
    Esp32BaseWeb::writeHtmlEscaped(label ? label : "");
    Esp32BaseWeb::sendChunk("</span><span class='cfgvalue'>");
    Esp32BaseWeb::writeHtmlEscaped(value ? value : "");
    Esp32BaseWeb::sendChunk("</span><span></span></div>");
}

uint8_t parseQuickDurationKey(const char* key) {
    if (!key || strncmp(key, "quick_r", 7) != 0 || key[7] < '1' || key[7] > '4' || strcmp(key + 8, "_min") != 0) {
        return 0;
    }
    return static_cast<uint8_t>(key[7] - '0');
}

bool describeSettingEdit(const char* edit, char* title, size_t titleLen, char* currentValue, size_t valueLen) {
    if (!edit || edit[0] == '\0' || !title || !currentValue) {
        return false;
    }
    const SettingsStore::Settings& s = SettingsStore::current();
    uint8_t road = 0;
    if ((road = parseRoadFromKey(edit, "_enabled")) > 0) {
        snprintf(title, titleLen, "第 %u 路启用状态", static_cast<unsigned>(road));
        snprintf(currentValue, valueLen, "%s", SettingsStore::isRoadEnabled(road) ? "已启用" : "未启用");
        return true;
    }
    if ((road = parseRoadFromKey(edit, "_name")) > 0) {
        snprintf(title, titleLen, "第 %u 路名称", static_cast<unsigned>(road));
        snprintf(currentValue, valueLen, "%s", s.roads[road - 1].name[0] ? s.roads[road - 1].name : "未命名");
        return true;
    }
    if ((road = parseQuickDurationKey(edit)) > 0) {
        snprintf(title, titleLen, "第 %u 路默认时长", static_cast<unsigned>(road));
        snprintf(currentValue, valueLen, "%u 分钟", static_cast<unsigned>(s.quickDurationSec[road - 1] / 60U));
        return true;
    }
    if ((road = parseRoadFromKey(edit, "_pulse_per_liter")) > 0) {
        snprintf(title, titleLen, "第 %u 路每升脉冲", static_cast<unsigned>(road));
        snprintf(currentValue, valueLen, "%u pulse/L", static_cast<unsigned>(s.roads[road - 1].pulsePerLiter));
        return true;
    }
    if ((road = parseRoadFromKey(edit, "_calibration_x1000")) > 0) {
        snprintf(title, titleLen, "第 %u 路校准系数", static_cast<unsigned>(road));
        snprintf(currentValue, valueLen, "%u", static_cast<unsigned>(s.roads[road - 1].calibrationX1000));
        return true;
    }
    if (strcmp(edit, "flow_no_pulse_timeout_s") == 0) {
        snprintf(title, titleLen, "无脉冲超时");
        snprintf(currentValue, valueLen, "%u 秒", static_cast<unsigned>(s.flowNoPulseTimeoutSec));
        return true;
    }
    if (strcmp(edit, "idle_leak_window_s") == 0) {
        snprintf(title, titleLen, "漏水窗口");
        snprintf(currentValue, valueLen, "%u 秒", static_cast<unsigned>(s.idleLeakWindowSec));
        return true;
    }
    if (strcmp(edit, "idle_leak_pulse_threshold") == 0) {
        snprintf(title, titleLen, "漏水脉冲阈值");
        snprintf(currentValue, valueLen, "%u", static_cast<unsigned>(s.idleLeakPulseThreshold));
        return true;
    }
    return false;
}

void writeSettingEditFields(const char* edit) {
    const SettingsStore::Settings& s = SettingsStore::current();
    uint8_t road = parseRoadFromKey(edit, "_enabled");
    if (road > 0) {
        Esp32BaseWeb::sendChunk("<input type='hidden' name='road' value='");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("'><p class='field med'><label>启用状态</label><select name='enabled'><option value='1'");
        writeSelected(SettingsStore::isRoadEnabled(road));
        Esp32BaseWeb::sendChunk(">已启用</option><option value='0'");
        writeSelected(!SettingsStore::isRoadEnabled(road));
        Esp32BaseWeb::sendChunk(">未启用</option></select><small>停用后该路不会被手动或计划任务启动。</small></p>");
    } else if ((road = parseRoadFromKey(edit, "_name")) > 0) {
        Esp32BaseWeb::sendChunk("<p class='field med'><label>名称</label><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_name' maxlength='11' value='");
        Esp32BaseWeb::writeHtmlEscaped(s.roads[road - 1].name);
        Esp32BaseWeb::sendChunk("'><small>用于页面和记录展示。</small></p>");
    } else if ((road = parseQuickDurationKey(edit)) > 0) {
        Esp32BaseWeb::sendChunk("<p class='field short'><label>默认时长</label><input name='quick_r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='1' max='240' value='");
        writeMinutesFromSeconds(s.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk("'><small>分钟，范围 1-240。</small></p>");
    } else if ((road = parseRoadFromKey(edit, "_pulse_per_liter")) > 0) {
        Esp32BaseWeb::sendChunk("<p class='field short'><label>每升脉冲</label><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_pulse_per_liter' type='number' min='1' max='10000' value='");
        writeUInt(s.roads[road - 1].pulsePerLiter);
        Esp32BaseWeb::sendChunk("'><small>流量计 pulse/L。</small></p>");
    } else if ((road = parseRoadFromKey(edit, "_calibration_x1000")) > 0) {
        Esp32BaseWeb::sendChunk("<p class='field short'><label>校准系数</label><input name='r");
        writeUInt(road);
        Esp32BaseWeb::sendChunk("_calibration_x1000' type='number' min='100' max='10000' value='");
        writeUInt(s.roads[road - 1].calibrationX1000);
        Esp32BaseWeb::sendChunk("'><small>1000 表示 1.000 倍。</small></p>");
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
        Esp32BaseWeb::sendChunk("<p class='field short'><label>");
        Esp32BaseWeb::writeHtmlEscaped(label);
        Esp32BaseWeb::sendChunk("</label><input name='");
        Esp32BaseWeb::writeHtmlEscaped(edit);
        Esp32BaseWeb::sendChunk("' type='number' min='1' max='");
        writeUInt(max);
        Esp32BaseWeb::sendChunk("' value='");
        writeUInt(current);
        Esp32BaseWeb::sendChunk("'><small>范围 1-");
        writeUInt(max);
        Esp32BaseWeb::sendChunk("。</small></p>");
    }
}

void writeSettingEditPanel(const char* edit) {
    if (!edit || edit[0] == '\0') {
        return;
    }
    char title[48];
    char currentValue[64];
    if (!describeSettingEdit(edit, title, sizeof(title), currentValue, sizeof(currentValue))) {
        Esp32BaseWeb::beginPanel("修改设置");
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "未知设置项", "请返回设置列表重新选择需要修改的项目。");
        Esp32BaseWeb::sendInfoRowCompactLink("设置列表", "返回可编辑的灌溉业务参数。", nullptr, "/irrigation/settings", "返回");
        Esp32BaseWeb::endPanel();
        return;
    }
    Esp32BaseWeb::beginPanel("修改设置");
    Esp32BaseWeb::sendInfoRowCompact("当前项目", title, currentValue);
    Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/irrigation/settings/config' onsubmit=\"return confirm('确认保存设置？')&&once(this)\"><div class='fieldgrid'>");
    writeSettingEditFields(edit);
    Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='button' value='取消' onclick=\"location.href='/irrigation/settings'\"><input type='submit' value='保存'></div></form>");
    Esp32BaseWeb::endPanel();
}

void handleSettingsPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    const SettingsStore::Settings& s = SettingsStore::current();
    sendHeader("灌溉设置", "page-settings");
    writePageHead("灌溉设置", "查看和修改水路、流量计量、默认浇水和安全检测参数。");
    char edit[32] = "";
    (void)Esp32BaseWeb::getParam("edit", edit, sizeof(edit));
    char value[32];
    Esp32BaseWeb::beginPanel("灌溉设置");
    snprintf(value, sizeof(value), "%u 路", static_cast<unsigned>(SettingsStore::enabledRoads()));
    writeSettingReadOnlyRow("已启用路数", value, "由各路启用状态计算");
    writeSettingReadOnlyRow("阀门 PWM", "启动 5 秒后 70%", "固定硬件策略");
    Esp32BaseWeb::endPanel();
    char key[32];
    Esp32BaseWeb::beginPanel("水路配置");
    Esp32BaseWeb::sendChunk("<div class='roadcfggrid'>");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        const SettingsStore::RoadConfig& r = s.roads[road - 1];
        const bool enabled = SettingsStore::isRoadEnabled(road);
        Esp32BaseWeb::sendChunk("<section class='roadcfg ");
        Esp32BaseWeb::sendChunk(enabled ? "enabled" : "disabled");
        Esp32BaseWeb::sendChunk("'><div class='roadhead'><div class='roadtitle'>第 ");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(" 路<span>");
        writeRoadConfiguredName(road);
        Esp32BaseWeb::sendChunk("</span></div><span class='tag");
        Esp32BaseWeb::sendChunk(enabled ? " ok" : "");
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::sendChunk(enabled ? "已启用" : "未启用");
        Esp32BaseWeb::sendChunk("</span></div>");
        snprintf(key, sizeof(key), "r%u_enabled", static_cast<unsigned>(road));
        writeSettingCardRow(key, "启用状态", enabled ? "已启用" : "未启用");
        snprintf(key, sizeof(key), "r%u_name", static_cast<unsigned>(road));
        writeSettingCardRow(key, "名称", r.name[0] ? r.name : "未命名");
        writeSettingCardReadOnly("阀门引脚", valvePinName(road));
        writeSettingCardReadOnly("流量引脚", flowPinName(road));
        snprintf(key, sizeof(key), "quick_r%u_min", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u 分钟", static_cast<unsigned>(s.quickDurationSec[road - 1] / 60U));
        writeSettingCardRow(key, "默认时长", value);
        snprintf(key, sizeof(key), "r%u_pulse_per_liter", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u pulse/L", static_cast<unsigned>(r.pulsePerLiter));
        writeSettingCardRow(key, "每升脉冲", value);
        snprintf(key, sizeof(key), "r%u_calibration_x1000", static_cast<unsigned>(road));
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(r.calibrationX1000));
        writeSettingCardRow(key, "校准系数", value);
        Esp32BaseWeb::sendChunk("</section>");
    }
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::beginPanel("水流检测");
    snprintf(value, sizeof(value), "%u 秒", static_cast<unsigned>(s.flowNoPulseTimeoutSec));
    writeSettingRow("flow_no_pulse_timeout_s", "无脉冲超时", value);
    snprintf(value, sizeof(value), "%u 秒", static_cast<unsigned>(s.idleLeakWindowSec));
    writeSettingRow("idle_leak_window_s", "漏水窗口", value);
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s.idleLeakPulseThreshold));
    writeSettingRow("idle_leak_pulse_threshold", "漏水脉冲阈值", value);
    Esp32BaseWeb::endPanel();
    writeSettingEditPanel(edit);
    sendFooter();
}

void handleDataPage() {
    if (!Esp32BaseWeb::checkAuth()) return;
    uint16_t page = 1;
    uint16_t perPage = 10;
    if (Esp32BaseWeb::hasParam("page") && (!readUIntParam("page", &page) || page == 0)) {
        page = 1;
    }
    if (Esp32BaseWeb::hasParam("per") && (!readUIntParam("per", &perPage) || (perPage != 10 && perPage != 20 && perPage != 50))) {
        perPage = 10;
    }
    const uint16_t total = RecordStore::count();
    const uint16_t totalPages = total == 0 ? 1 : static_cast<uint16_t>((total + perPage - 1) / perPage);
    if (page > totalPages) {
        page = totalPages;
    }
    const uint16_t offset = static_cast<uint16_t>((page - 1) * perPage);
    sendHeader("历史记录", "page-table");
    writePageHead("历史记录", "查看每一次浇水任务的来源、停止来源、执行结果和实际水量。");
    Esp32BaseWeb::beginPanel("浇水记录");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>ID</th><th>路</th><th>任务</th><th>启动来源</th><th>停止来源</th><th>结果</th><th>目标</th><th>实际</th><th>水量</th></tr></thead><tbody>");
    uint16_t rendered = 0;
    auto recordCb = [](const RecordStore::Record& record, void* user) {
        uint16_t* renderedRows = static_cast<uint16_t*>(user);
        if (renderedRows) {
            ++(*renderedRows);
        }
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
        Esp32BaseWeb::sendChunk("<span class='tag");
        Esp32BaseWeb::sendChunk(record.result == RecordStore::RESULT_COMPLETED ? " ok" : (record.result == RecordStore::RESULT_NONE ? "" : " warn"));
        Esp32BaseWeb::sendChunk("'>");
        Esp32BaseWeb::writeHtmlEscaped(RecordStore::resultName(static_cast<RecordStore::Result>(record.result)));
        Esp32BaseWeb::sendChunk("</span>");
        Esp32BaseWeb::sendChunk("</td><td>");
        writeMinutesFromSeconds(record.targetSec);
        Esp32BaseWeb::sendChunk(" 分钟</td><td>");
        writeUInt(actualSec);
        Esp32BaseWeb::sendChunk(" 秒</td><td>");
        writeLitersFromMl(record.estimatedMilliliters);
        Esp32BaseWeb::sendChunk("</td></tr>");
    };
    (void)RecordStore::readLatest(offset, perPage, recordCb, &rendered);
    if (rendered == 0) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='9'><span class='muted'>暂无浇水记录</span></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::Pagination pagination = {"/irrigation/data", nullptr, page, perPage, total};
    Esp32BaseWeb::sendPagination(pagination);
    Esp32BaseWeb::endPanel();
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
    Esp32BaseWeb::sendChunk(",\"exists\":");
    writeBool(p.exists);
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

bool writeRecentPlanJson(uint8_t index, const char* label, int8_t offset, uint32_t ymd, bool* first) {
    const PlanStore::Plan& plan = PlanStore::get(index);
    if (!plan.exists) return false;
    const bool skipped = PlanSkipStore::isSkipped(index, ymd);
    const bool handled = plan.lastRunYmd == ymd;
    PlanResultStore::Result planResult = PlanResultStore::RESULT_NONE;
    const bool hasPlanResult = PlanResultStore::getResult(index, ymd, &planResult);
    const WateringSession::RoadStatus& roadStatus = WateringSession::roadStatus(plan.roadId);
    const uint16_t nowMinute = currentMinuteOfDay();
    const bool running = offset == 0 && plan.minuteOfDay == nowMinute && WateringSession::isRoadActive(plan.roadId) && roadStatus.taskType == RecordStore::TASK_PLAN && roadStatus.planSlot == plan.slotIndex;
    const bool shouldRun = plan.enabled && PlanStore::shouldRunOnDate(plan, ymd);
    const bool executable = hasEffectivePlanRoad(plan);
    if (!plan.enabled && !handled && !running) return false;
    if (plan.enabled && !shouldRun) return false;
    const bool pastToday = offset == 0 && plan.minuteOfDay < nowMinute;
    const char* status = running ? "running" : (hasPlanResult ? PlanResultStore::resultName(planResult) : (handled ? "handled" : (skipped ? "skipped" : (!executable ? "not_executable" : (pastToday ? "missed" : "pending")))));
    if (!*first) Esp32BaseWeb::sendChunk(",");
    *first = false;
    Esp32BaseWeb::sendChunk("{\"date_label\":\"");
    Esp32BaseWeb::writeJsonEscaped(label);
    Esp32BaseWeb::sendChunk("\",\"ymd\":");
    writeUInt(ymd);
    Esp32BaseWeb::sendChunk(",\"index\":");
    writeUInt(index);
    Esp32BaseWeb::sendChunk(",\"road_id\":");
    writeUInt(plan.roadId);
    Esp32BaseWeb::sendChunk(",\"slot_index\":");
    writeUInt(plan.slotIndex);
    Esp32BaseWeb::sendChunk(",\"minute_of_day\":");
    writeUInt(plan.minuteOfDay);
    Esp32BaseWeb::sendChunk(",\"enabled\":");
    writeBool(plan.enabled);
    Esp32BaseWeb::sendChunk(",\"should_run\":");
    writeBool(shouldRun);
    Esp32BaseWeb::sendChunk(",\"skipped\":");
    writeBool(skipped);
    Esp32BaseWeb::sendChunk(",\"handled\":");
    writeBool(handled);
    Esp32BaseWeb::sendChunk(",\"running\":");
    writeBool(running);
    Esp32BaseWeb::sendChunk(",\"executable\":");
    writeBool(executable);
    Esp32BaseWeb::sendChunk(",\"status\":\"");
    Esp32BaseWeb::writeJsonEscaped(status);
    Esp32BaseWeb::sendChunk("\",\"plan_result\":\"");
    Esp32BaseWeb::writeJsonEscaped(PlanResultStore::resultName(planResult));
    Esp32BaseWeb::sendChunk("\",\"plan_result_label\":\"");
    Esp32BaseWeb::writeJsonEscaped(PlanResultStore::resultLabel(planResult));
    Esp32BaseWeb::sendChunk("\"}");
    return true;
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
    const bool pageRequest = isWebPageSourceParam();
    const RecordStore::TriggerSource source = requestTriggerSource();
    bool anyRequested = false;
    bool anyStarted = false;
    bool anyInvalid = false;
    const char* result[IrrigationPins::MaxRoads] = {};
    uint16_t requestedSec[IrrigationPins::MaxRoads] = {};
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        bool useRoad = SettingsStore::isRoadEnabled(road);
        char key[16];
        snprintf(key, sizeof(key), "r%u_enabled", static_cast<unsigned>(road));
        (void)readBoolParam(key, &useRoad);
        if (!useRoad) {
            result[road - 1] = "not_requested";
            continue;
        }
        anyRequested = true;
        if (!SettingsStore::isRoadEnabled(road)) {
            result[road - 1] = "disabled";
            continue;
        }
        snprintf(key, sizeof(key), "r%u_min", static_cast<unsigned>(road));
        uint16_t minutes = 0;
        if (!readUIntParam(key, &minutes) || minutes < 1 || minutes > 240) {
            result[road - 1] = "invalid_duration";
            anyInvalid = true;
            continue;
        }
        requestedSec[road - 1] = minutesToSeconds(minutes);
        if (WateringSession::isRoadActive(road)) {
            result[road - 1] = "busy";
            continue;
        }
        const bool started = WateringSession::startRoadTask(road, requestedSec[road - 1], RecordStore::TASK_MANUAL, source, 0xFF, pageRequest ? "web page manual" : "http api manual");
        result[road - 1] = started ? "started" : "rejected";
        anyStarted = started || anyStarted;
    }
    if (pageRequest && anyStarted) {
        redirectTo("/irrigation");
        return;
    }
    beginJson(anyStarted ? 200 : 400);
    Esp32BaseWeb::sendChunk("{\"ok\":");
    writeBool(anyStarted);
    Esp32BaseWeb::sendChunk(",\"error\":\"");
    Esp32BaseWeb::writeJsonEscaped(anyStarted ? "" : (anyInvalid ? "invalid_duration" : (anyRequested ? "no_road_started" : "no_road_requested")));
    Esp32BaseWeb::sendChunk("\",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::triggerSourceName(source));
    Esp32BaseWeb::sendChunk("\",\"roads\":[");
    for (uint8_t road = 1; road <= IrrigationPins::MaxRoads; ++road) {
        if (road > 1) Esp32BaseWeb::sendChunk(",");
        Esp32BaseWeb::sendChunk("{\"road\":");
        writeUInt(road);
        Esp32BaseWeb::sendChunk(",\"result\":\"");
        Esp32BaseWeb::writeJsonEscaped(result[road - 1] ? result[road - 1] : "not_requested");
        Esp32BaseWeb::sendChunk("\",\"target_sec\":");
        writeUInt(requestedSec[road - 1]);
        Esp32BaseWeb::sendChunk("}");
    }
    Esp32BaseWeb::sendChunk("]}");
    endJson();
}

void handleWaterStopApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }
    uint16_t road = 0;
    (void)readUIntParam("road", &road);
    const bool pageRequest = isWebPageSourceParam();
    const RecordStore::TriggerSource source = requestTriggerSource();
    bool stopped = false;
    if (road == 0) {
        stopped = WateringSession::isActive();
        WateringSession::stopAll(source, RecordStore::RESULT_USER_STOPPED, pageRequest ? "web page stop all" : "http api stop all");
    } else if (road <= IrrigationPins::MaxRoads) {
        stopped = WateringSession::stopRoad(static_cast<uint8_t>(road), source, pageRequest ? "web page stop road" : "http api stop road");
    } else {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_road\"}");
        return;
    }
    if (pageRequest) {
        redirectTo("/irrigation");
        return;
    }
    beginJson();
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"stopped\":");
    writeBool(stopped);
    Esp32BaseWeb::sendChunk(",\"road\":");
    writeUInt(road);
    Esp32BaseWeb::sendChunk(",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::triggerSourceName(source));
    Esp32BaseWeb::sendChunk("\"}");
    endJson();
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
        writeUInt(PlanStore::count());
        Esp32BaseWeb::sendChunk(",\"capacity\":");
        writeUInt(PlanStore::MaxPlans);
        Esp32BaseWeb::sendChunk(",\"max_per_road\":");
        writeUInt(PlanStore::MaxPlansPerRoad);
        Esp32BaseWeb::sendChunk(",\"plans\":[");
        bool first = true;
        for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
            if (!PlanStore::get(i).exists) continue;
            if (!first) Esp32BaseWeb::sendChunk(",");
            first = false;
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
    char action[12] = "save";
    (void)Esp32BaseWeb::getParam("action", action, sizeof(action));
    if (strcmp(action, "create") == 0) {
        uint16_t roadRaw = 0;
        uint8_t index = 0;
        if (!readUIntParam("road", &roadRaw) || !PlanStore::create(static_cast<uint8_t>(roadRaw), &index)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"plan_create_failed\"}");
            return;
        }
        PlanStore::Plan plan = PlanStore::get(index);
        const uint32_t today = currentYmd();
        if (today >= 20000101UL) {
            plan.cycleStartYmd = today;
            (void)PlanStore::set(index, plan);
        }
        (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED, EventStore::SOURCE_WEB, 0, 0, index, plan.roadId, "plan created");
        char location[40];
        snprintf(location, sizeof(location), "/irrigation/plan?edit=%u", static_cast<unsigned>(index));
        redirectTo(location);
        return;
    }
    uint16_t indexRaw = 0;
    if (!readUIntParam("index", &indexRaw) || indexRaw >= PlanStore::MaxPlans) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_index\"}");
        return;
    }
    PlanStore::Plan plan = PlanStore::get(static_cast<uint8_t>(indexRaw));
    if (!plan.exists) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"plan_not_found\"}");
        return;
    }
    if (strcmp(action, "delete") == 0) {
        if (!PlanStore::remove(static_cast<uint8_t>(indexRaw))) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"plan_delete_failed\"}");
            return;
        }
        (void)PlanSkipStore::clearSkipped(static_cast<uint8_t>(indexRaw), currentYmd());
        (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED, EventStore::SOURCE_WEB, 0, 0, indexRaw, plan.roadId, "plan deleted");
        redirectTo("/irrigation/plan-config");
        return;
    }
    if (strcmp(action, "toggle") == 0) {
        bool enabled = false;
        if (!readBoolParam("enabled", &enabled)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_enabled\"}");
            return;
        }
        plan.enabled = enabled;
        if (!PlanStore::set(static_cast<uint8_t>(indexRaw), plan)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_plan\"}");
            return;
        }
        (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED, EventStore::SOURCE_WEB, 0, 0, indexRaw, enabled ? 1 : 0, enabled ? "plan enabled" : "plan disabled");
        redirectTo("/irrigation/plan-config");
        return;
    }
    if (strcmp(action, "save") != 0) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_action\"}");
        return;
    }
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

void handleRecentPlansApi() {
    if (!Esp32BaseWeb::checkAuth()) return;
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }
    beginJson();
    Esp32BaseWeb::sendChunk("{\"plans\":[");
    bool first = true;
    bool timeSynced = true;
    for (int8_t offset = 0; offset <= 2; ++offset) {
        tm date = {};
        if (!localDateFromOffset(offset, &date)) {
            timeSynced = false;
            break;
        }
        const uint32_t ymd = makeYmd(date);
        uint8_t indices[PlanStore::MaxPlans] = {};
        uint8_t rendered = 0;
        for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
            if (PlanStore::get(i).exists) {
                indices[rendered++] = i;
            }
        }
        for (uint8_t i = 0; i < rendered; ++i) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < rendered; ++j) {
                const PlanStore::Plan& a = PlanStore::get(indices[i]);
                const PlanStore::Plan& b = PlanStore::get(indices[j]);
                if (b.minuteOfDay < a.minuteOfDay ||
                    (b.minuteOfDay == a.minuteOfDay && b.roadId < a.roadId) ||
                    (b.minuteOfDay == a.minuteOfDay && b.roadId == a.roadId && b.slotIndex < a.slotIndex)) {
                    const uint8_t tmp = indices[i];
                    indices[i] = indices[j];
                    indices[j] = tmp;
                }
            }
        }
        for (uint8_t i = 0; i < rendered; ++i) {
            (void)writeRecentPlanJson(indices[i], dayLabel(offset), offset, ymd, &first);
        }
    }
    Esp32BaseWeb::sendChunk("],\"time_synced\":");
    writeBool(timeSynced);
    Esp32BaseWeb::sendChunk("}");
    endJson();
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
    const bool pageRequest = isWebPageSourceParam();
    if (!Esp32BaseWeb::getParam("action", action, sizeof(action)) || !readU32Param("ymd", &ymd)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_skip_request\"}");
        return;
    }
    bool ok = true;
    if (strcmp(action, "skip_once") == 0 || strcmp(action, "clear_skip") == 0) {
        uint16_t index = 0;
        ok = readUIntParam("index", &index) && index < PlanStore::MaxPlans && PlanStore::get(static_cast<uint8_t>(index)).exists;
        if (ok) {
            if (strcmp(action, "skip_once") == 0) {
                ok = PlanSkipStore::setSkipped(static_cast<uint8_t>(index), ymd) &&
                     PlanResultStore::setResult(static_cast<uint8_t>(index), ymd, PlanResultStore::RESULT_SKIPPED_MANUAL);
            } else {
                ok = PlanSkipStore::clearSkipped(static_cast<uint8_t>(index), ymd) &&
                     PlanResultStore::clearResult(static_cast<uint8_t>(index), ymd);
            }
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
            if (!plan.exists || !plan.enabled || !PlanStore::shouldRunOnDate(plan, ymd) || plan.lastRunYmd == ymd) continue;
            if (remaining && plan.minuteOfDay <= nowMinute) continue;
            ok = PlanSkipStore::setSkipped(i, ymd) &&
                 PlanResultStore::setResult(i, ymd, PlanResultStore::RESULT_SKIPPED_MANUAL) &&
                 ok;
        }
    } else {
        ok = false;
    }
    if (!ok) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"skip_failed\"}");
        return;
    }
    (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED, EventStore::SOURCE_WEB, 0, 0, static_cast<int32_t>(ymd), 0, action);
    if (pageRequest) {
        redirectTo("/irrigation/plans");
        return;
    }
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
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
    Esp32BaseWeb::setHeadExtraCallback(writeIrrigationHeadExtra);
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
    const bool recentPlansOk = Esp32BaseWeb::addApi("/api/v1/plans/recent", handleRecentPlansApi);
    const bool skipOk = Esp32BaseWeb::addApi("/api/v1/plans/skip", handlePlanSkipApi);
    const bool alertsOk = Esp32BaseWeb::addApi("/api/v1/alerts/clear", handleAlertsClearApi);
    ESP32BASE_LOG_I("irrigation.web", "routes overview=%s recent=%s planConfig=%s data=%s settings=%s planEdit=%s settingsConfig=%s status=%s config=%s start=%s stop=%s records=%s events=%s plans=%s recentPlans=%s skip=%s alerts=%s firmware=%s",
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
                    recentPlansOk ? "ok" : "fail",
                    skipOk ? "ok" : "fail",
                    alertsOk ? "ok" : "fail",
                    IrrigationVersion::FirmwareName);
}

}
