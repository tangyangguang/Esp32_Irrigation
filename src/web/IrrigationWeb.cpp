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
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace {

void writeBool(bool value) {
    Esp32BaseWeb::sendChunk(value ? "true" : "false");
}

void beginJsonStream(int code) {
    (void)Esp32BaseWeb::beginResponse(code, "application/json", nullptr);
}

void endJsonStream() {
    Esp32BaseWeb::endResponse();
}

void writeUInt(uint32_t value) {
    char number[16];
    snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(number);
}

void writeFixed2FromX100(uint32_t value) {
    char number[20];
    snprintf(number, sizeof(number), "%lu.%02lu", static_cast<unsigned long>(value / 100UL), static_cast<unsigned long>(value % 100UL));
    Esp32BaseWeb::sendChunk(number);
}

void writeLitersFromMl(uint32_t ml) {
    char number[20];
    snprintf(number, sizeof(number), "%lu.%03lu L", static_cast<unsigned long>(ml / 1000UL), static_cast<unsigned long>(ml % 1000UL));
    Esp32BaseWeb::sendChunk(number);
}

void writeBytesHuman(uint32_t bytes) {
    char text[24];
    if (bytes >= 1048576UL) {
        snprintf(text, sizeof(text), "%lu.%02lu MB",
                 static_cast<unsigned long>(bytes / 1048576UL),
                 static_cast<unsigned long>((bytes % 1048576UL) * 100UL / 1048576UL));
    } else if (bytes >= 1024UL) {
        snprintf(text, sizeof(text), "%lu.%01lu KB",
                 static_cast<unsigned long>(bytes / 1024UL),
                 static_cast<unsigned long>((bytes % 1024UL) * 10UL / 1024UL));
    } else {
        snprintf(text, sizeof(text), "%lu B", static_cast<unsigned long>(bytes));
    }
    Esp32BaseWeb::sendChunk(text);
}

void writeMinutesFromSeconds(uint32_t seconds) {
    char number[20];
    const uint32_t whole = seconds / 60UL;
    const uint32_t rem = seconds % 60UL;
    if (rem == 0) {
        snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(whole));
    } else {
        snprintf(number, sizeof(number), "%lu.%02lu", static_cast<unsigned long>(whole), static_cast<unsigned long>((rem * 100UL) / 60UL));
    }
    Esp32BaseWeb::sendChunk(number);
}

uint16_t minutesToSeconds(uint16_t minutes) {
    if (minutes > 240) {
        return 0;
    }
    return static_cast<uint16_t>(minutes * 60U);
}

void writeFixedX1000(uint16_t value) {
    char number[16];
    snprintf(number, sizeof(number), "%u.%03u", static_cast<unsigned>(value / 1000), static_cast<unsigned>(value % 1000));
    Esp32BaseWeb::sendChunk(number);
}

void writeUIntText(uint32_t value) {
    char number[16];
    snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(number);
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
    Esp32BaseWeb::sendChunk(">simultaneous</option><option value='sequential'");
    writeSelected(selected == SettingsStore::MODE_SEQUENTIAL);
    Esp32BaseWeb::sendChunk(">sequential</option>");
}

void writeRepeatOptions(PlanStore::RepeatMode selected) {
    Esp32BaseWeb::sendChunk("<option value='weekly'");
    writeSelected(selected == PlanStore::REPEAT_WEEKLY);
    Esp32BaseWeb::sendChunk(">weekly</option><option value='interval'");
    writeSelected(selected == PlanStore::REPEAT_INTERVAL);
    Esp32BaseWeb::sendChunk(">interval</option>");
}

const char* modeLabel(SettingsStore::ExecutionMode mode) {
    return mode == SettingsStore::MODE_SEQUENTIAL ? "顺序浇水" : "同时浇水";
}

const char* repeatLabel(PlanStore::RepeatMode mode) {
    return mode == PlanStore::REPEAT_INTERVAL ? "每隔 N 天" : "每周";
}

const char* roadStateLabel(WateringSession::RoadState state) {
    switch (state) {
        case WateringSession::ROAD_IDLE:
            return "关闭";
        case WateringSession::ROAD_PENDING:
            return "等待";
        case WateringSession::ROAD_RUNNING:
            return "浇水中";
        case WateringSession::ROAD_DONE:
            return "完成";
        case WateringSession::ROAD_STOPPED:
            return "已停止";
        case WateringSession::ROAD_ERROR:
            return "异常";
        default:
            return "未知";
    }
}

void writeCnModeOptions(SettingsStore::ExecutionMode selected, bool includeDefault = false) {
    if (includeDefault) {
        Esp32BaseWeb::sendChunk("<option value=''>使用默认模式</option>");
    }
    Esp32BaseWeb::sendChunk("<option value='simultaneous'");
    writeSelected(selected == SettingsStore::MODE_SIMULTANEOUS);
    Esp32BaseWeb::sendChunk(">同时浇水</option><option value='sequential'");
    writeSelected(selected == SettingsStore::MODE_SEQUENTIAL);
    Esp32BaseWeb::sendChunk(">顺序浇水</option>");
}

void writeCnRepeatOptions(PlanStore::RepeatMode selected) {
    Esp32BaseWeb::sendChunk("<option value='weekly'");
    writeSelected(selected == PlanStore::REPEAT_WEEKLY);
    Esp32BaseWeb::sendChunk(">每周</option><option value='interval'");
    writeSelected(selected == PlanStore::REPEAT_INTERVAL);
    Esp32BaseWeb::sendChunk(">每隔 N 天</option>");
}

void writeAppStyle() {
    Esp32BaseWeb::sendChunk(
        "<style>"
        "body{max-width:none;margin:0;background:#f5f7fb;color:#152033;padding:0;font-size:14px}"
        "body>nav{background:#fff;border-bottom:1px solid #d9e1ec;padding:8px 18px;margin:0;display:flex;flex-wrap:wrap;gap:7px;align-items:center}"
        "body>nav a{font-size:.92rem;border-radius:6px;background:#e8f0fa;color:#0f5f97;padding:6px 10px;margin:0}"
        "body>nav a.brand{background:#e8f0fa;color:#0f5f97}"
        "body>nav a.current{background:#0f766e;color:#fff}"
        ".irr{max-width:1180px;margin:20px auto 28px;padding:0 18px}"
        ".irr h2{font-size:1.6rem;margin:0 0 14px;color:#111827}"
        ".irr h3{font-size:1.08rem;margin:16px 0 9px;color:#111827}"
        ".grid{display:grid;grid-template-columns:repeat(12,1fr);gap:12px;align-items:start}"
        ".panel{grid-column:span 12;background:#fff;border:1px solid #d6dee8;border-radius:8px;padding:12px;min-width:0}"
        ".panel h3{margin-top:0}"
        ".span-4{grid-column:span 4}.span-6{grid-column:span 6}.span-8{grid-column:span 8}.span-12{grid-column:span 12}"
        ".irr table{width:100%;border-collapse:separate;border-spacing:0;background:#fff;border:1px solid #d6dee8;border-radius:8px;overflow:hidden;margin:8px 0 16px}"
        ".panel table{margin-bottom:0}"
        ".irr th,.irr td{padding:8px 9px;border-bottom:1px solid #e4eaf1;text-align:left;vertical-align:middle}"
        ".irr tr:last-child td{border-bottom:0}"
        ".irr th{background:#eef4fb;color:#4b5f78;font-weight:700}"
        ".irr input,.irr select{font-size:.95rem;max-width:320px}"
        ".irr a,.irr button,.irr input[type=submit],.irr input[type=button]{border-radius:6px;padding:6px 10px;background:#0f766e;color:#fff}"
        ".irr input[type=submit][style*='background:#c33']{background:#b42318!important}"
        ".irr p{line-height:1.45;color:#526173;margin:8px 0 12px}"
        ".irr form{margin:0 0 10px}"
        ".irr td form{margin:0}"
        ".roadcard{border:1px solid #d6dee8;border-radius:8px;padding:10px;margin:8px 0;background:#fbfdff}"
        ".roadcard strong{display:block;margin-bottom:8px;font-size:1rem}"
        ".fieldgrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 12px;margin:8px 0}"
        ".field{display:grid;grid-template-columns:130px minmax(0,1fr);align-items:center;gap:8px;border:1px solid #e4eaf1;border-radius:8px;padding:8px;background:#fbfdff}"
        ".field label,.field span{color:#4b5f78;font-weight:700}"
        ".field input,.field select{width:100%;box-sizing:border-box;margin:0}"
        ".field.full{grid-column:1/-1}"
        ".kv{display:flex;justify-content:space-between;gap:10px;padding:5px 0;border-bottom:1px solid #e7edf5}"
        ".kv:last-child{border-bottom:0}"
        ".kv span{color:#5a6b80}.kv b{font-weight:700;text-align:right}"
        ".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:9px}"
        ".badge{display:inline-block;border-radius:999px;background:#eef2f6;color:#4b5a6a;padding:3px 8px;font-weight:700}"
        ".config-form{grid-column:span 12;display:grid;grid-template-columns:repeat(12,1fr);gap:12px;align-items:start}"
        "@media(max-width:980px){.span-4,.span-6,.span-8{grid-column:span 12}.config-form>.span-4,.config-form>.span-6{grid-column:span 12}}"
        "@media(max-width:760px){body{font-size:13px}.irr{margin-top:14px;padding:0 10px}.irr h2{font-size:1.45rem}.irr table{display:block;overflow-x:auto;white-space:nowrap}.irr input,.irr select{max-width:100%}.fieldgrid{grid-template-columns:1fr}.field{grid-template-columns:1fr}}"
        "</style><script>(function(){var p=location.pathname;document.querySelectorAll('body>nav a').forEach(function(a){if(a.pathname===p)a.classList.add('current')});document.addEventListener('submit',function(e){var f=e.target;if(!f||String(f.method).toLowerCase()!=='post')return;var msg=f.getAttribute('data-confirm')||'确认执行此操作？';if(!confirm(msg)){e.preventDefault();}})})();</script>");
}

void sendMethodNotAllowed(const char* allowed) {
    beginJsonStream(405);
    Esp32BaseWeb::sendChunk("{\"ok\":false,\"error\":\"method_not_allowed\",\"allowed\":\"");
    Esp32BaseWeb::writeJsonEscaped(allowed);
    Esp32BaseWeb::sendChunk("\",\"method\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32BaseWeb::currentMethodName());
    Esp32BaseWeb::sendChunk("\"}");
    endJsonStream();
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

bool readOptionalUIntParam(const char* name, uint16_t* value, bool* present) {
    *present = Esp32BaseWeb::hasParam(name);
    if (!*present) {
        return true;
    }
    return readUIntParam(name, value);
}

bool readOptionalDurationParam(const char* secondsName, const char* minutesName, uint16_t* seconds, bool* present) {
    *present = false;
    uint16_t value = 0;
    if (Esp32BaseWeb::hasParam(minutesName)) {
        *present = true;
        if (!readUIntParam(minutesName, &value) || value > 240) {
            return false;
        }
        *seconds = static_cast<uint16_t>(value * 60U);
        return true;
    }
    if (Esp32BaseWeb::hasParam(secondsName)) {
        *present = true;
        return readUIntParam(secondsName, seconds);
    }
    return true;
}

bool readModeParam(const char* name, SettingsStore::ExecutionMode* mode) {
    char text[20] = "";
    return Esp32BaseWeb::getParam(name, text, sizeof(text)) && SettingsStore::parseExecutionMode(text, mode);
}

bool readBoolParam(const char* name, bool* value) {
    char text[8] = "";
    if (!Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0 || strcmp(text, "locked") == 0) {
        *value = true;
        return true;
    }
    if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0 || strcmp(text, "unlocked") == 0) {
        *value = false;
        return true;
    }
    return false;
}

bool readOptionalBoolParam(const char* name, bool* value, bool* present) {
    *present = Esp32BaseWeb::hasParam(name);
    if (!*present) {
        return true;
    }
    return readBoolParam(name, value);
}

uint8_t selectedPlanIndex() {
    uint16_t value = 0;
    if (!Esp32BaseWeb::hasParam("edit") || !readUIntParam("edit", &value) || value >= PlanStore::MaxPlans) {
        return 0;
    }
    return static_cast<uint8_t>(value);
}

uint32_t currentYmd() {
    if (!Esp32BaseNtp::isTimeSynced()) {
        return 0;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    tm local = {};
    if (!localtime_r(&now, &local)) {
        return 0;
    }
    return static_cast<uint32_t>(local.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(local.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(local.tm_mday);
}

uint32_t makeYmd(const tm& value) {
    return static_cast<uint32_t>(value.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(value.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(value.tm_mday);
}

bool localDateFromOffset(uint8_t offsetDays, tm* out) {
    if (!out || !Esp32BaseNtp::isTimeSynced()) {
        return false;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    const time_t target = now + static_cast<time_t>(offsetDays) * 86400L;
    return localtime_r(&target, out) != nullptr;
}

uint32_t daysBetweenYmd(uint32_t fromYmd, uint32_t toYmd) {
    tm from = {};
    from.tm_year = static_cast<int>(fromYmd / 10000UL) - 1900;
    from.tm_mon = static_cast<int>((fromYmd / 100UL) % 100UL) - 1;
    from.tm_mday = static_cast<int>(fromYmd % 100UL);
    tm to = {};
    to.tm_year = static_cast<int>(toYmd / 10000UL) - 1900;
    to.tm_mon = static_cast<int>((toYmd / 100UL) % 100UL) - 1;
    to.tm_mday = static_cast<int>(toYmd % 100UL);
    const time_t fromTime = mktime(&from);
    const time_t toTime = mktime(&to);
    if (fromTime <= 0 || toTime <= fromTime) {
        return 0;
    }
    return static_cast<uint32_t>((toTime - fromTime) / 86400L);
}

bool planMatchesDate(const PlanStore::Plan& plan, const tm& date, uint32_t ymd) {
    if (!plan.enabled || plan.skipYmd == ymd || plan.lastRunYmd == ymd) {
        return false;
    }
    if (plan.repeatMode == PlanStore::REPEAT_WEEKLY) {
        return (plan.weekMask & (1U << static_cast<uint8_t>(date.tm_wday))) != 0;
    }
    if (plan.lastRunYmd == 0) {
        return true;
    }
    return daysBetweenYmd(plan.lastRunYmd, ymd) >= plan.intervalDays;
}

uint32_t flowRateLiterPerMinuteX100(uint8_t road) {
    const SettingsStore::Settings& settings = SettingsStore::current();
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return 0;
    }
    const SettingsStore::RoadConfig& roadConfig = settings.roads[road - 1];
    if (roadConfig.pulsePerLiter == 0) {
        return 0;
    }
    const uint64_t pulseRateX1000 = FlowMeter::pulseRatePerMinuteX1000(road);
    const uint64_t value = pulseRateX1000 * static_cast<uint64_t>(roadConfig.calibrationX1000) * 100ULL;
    const uint64_t denominator = static_cast<uint64_t>(roadConfig.pulsePerLiter) * 1000ULL * 1000ULL;
    return static_cast<uint32_t>(value / denominator);
}

bool wantsPageRedirect() {
    char text[8] = "";
    return Esp32BaseWeb::getParam("return", text, sizeof(text)) && strcmp(text, "page") == 0;
}

void redirectToPage() {
    char path[32] = "";
    if (Esp32BaseWeb::getParam("return_path", path, sizeof(path)) && strncmp(path, "/irrigation", 11) == 0) {
        Esp32BaseWeb::redirectSeeOther(path);
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation");
}

void writeSettingsJson() {
    const SettingsStore::Settings& settings = SettingsStore::current();
    Esp32BaseWeb::sendChunk("\"settings\":{\"enabled_roads\":");
    writeUInt(settings.enabledRoads);
    Esp32BaseWeb::sendChunk(",\"default_mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(SettingsStore::executionModeName(settings.defaultMode));
    Esp32BaseWeb::sendChunk("\",\"quick_duration_sec\":{\"r1\":");
    writeUInt(settings.quickDurationSec[0]);
    Esp32BaseWeb::sendChunk(",\"r2\":");
    writeUInt(settings.quickDurationSec[1]);
    Esp32BaseWeb::sendChunk("},\"flow_no_pulse_timeout_s\":");
    writeUInt(settings.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk(",\"idle_leak_window_s\":");
    writeUInt(settings.idleLeakWindowSec);
    Esp32BaseWeb::sendChunk(",\"idle_leak_pulse_threshold\":");
    writeUInt(settings.idleLeakPulseThreshold);
    Esp32BaseWeb::sendChunk(",\"keypad_locked\":");
    writeBool(settings.keypadLocked);
    Esp32BaseWeb::sendChunk(",\"roads\":{\"r1\":{\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(settings.roads[0].name);
    Esp32BaseWeb::sendChunk("\",\"pulse_per_liter\":");
    writeUInt(settings.roads[0].pulsePerLiter);
    Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
    writeUInt(settings.roads[0].calibrationX1000);
    Esp32BaseWeb::sendChunk(",\"calibration_factor\":");
    writeFixedX1000(settings.roads[0].calibrationX1000);
    Esp32BaseWeb::sendChunk("},\"r2\":{\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(settings.roads[1].name);
    Esp32BaseWeb::sendChunk("\",\"pulse_per_liter\":");
    writeUInt(settings.roads[1].pulsePerLiter);
    Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
    writeUInt(settings.roads[1].calibrationX1000);
    Esp32BaseWeb::sendChunk(",\"calibration_factor\":");
    writeFixedX1000(settings.roads[1].calibrationX1000);
    Esp32BaseWeb::sendChunk("}}");
    Esp32BaseWeb::sendChunk("}");
}

void writeRoadJson(uint8_t road) {
    const WateringSession::RoadStatus& status = WateringSession::roadStatus(road);
    Esp32BaseWeb::sendChunk("{\"state\":\"");
    Esp32BaseWeb::writeJsonEscaped(WateringSession::roadStateName(status.state));
    Esp32BaseWeb::sendChunk("\",\"target_sec\":");
    writeUInt(status.targetSec);
    Esp32BaseWeb::sendChunk(",\"pulse_count\":");
    writeUInt(FlowMeter::pulseCount(road));
    Esp32BaseWeb::sendChunk(",\"session_pulses\":");
    const uint32_t sessionPulses = status.lastPulseCount >= status.startedPulseCount ? status.lastPulseCount - status.startedPulseCount : 0;
    writeUInt(sessionPulses);
    Esp32BaseWeb::sendChunk(",\"estimated_ml\":");
    writeUInt(SettingsStore::estimateMilliliters(road, sessionPulses));
    Esp32BaseWeb::sendChunk(",\"flow_l_min\":");
    writeFixed2FromX100(flowRateLiterPerMinuteX100(road));
    Esp32BaseWeb::sendChunk(",\"last_pulse_ms\":");
    writeUInt(status.lastPulseMs);
    Esp32BaseWeb::sendChunk(",\"started_ms\":");
    writeUInt(status.startedMs);
    Esp32BaseWeb::sendChunk(",\"ended_ms\":");
    writeUInt(status.endedMs);
    Esp32BaseWeb::sendChunk(",\"valve_open\":");
    writeBool(ValveController::isOpen(road));
    Esp32BaseWeb::sendChunk("}");
}

void writeWateringJson() {
    Esp32BaseWeb::sendChunk("\"watering\":{\"active\":");
    writeBool(WateringSession::isActive());
    Esp32BaseWeb::sendChunk(",\"mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(SettingsStore::executionModeName(WateringSession::mode()));
    Esp32BaseWeb::sendChunk("\",\"last_stop_reason\":\"");
    Esp32BaseWeb::writeJsonEscaped(WateringSession::stopReasonName(WateringSession::lastStopReason()));
    Esp32BaseWeb::sendChunk("\",\"roads\":{\"r1\":");
    writeRoadJson(1);
    Esp32BaseWeb::sendChunk(",\"r2\":");
    writeRoadJson(2);
    Esp32BaseWeb::sendChunk("}}");
}

void writePlanJson(uint8_t index, const PlanStore::Plan& plan) {
    Esp32BaseWeb::sendChunk("{\"index\":");
    writeUInt(index);
    Esp32BaseWeb::sendChunk(",\"enabled\":");
    writeBool(plan.enabled);
    Esp32BaseWeb::sendChunk(",\"minute_of_day\":");
    writeUInt(plan.minuteOfDay);
    Esp32BaseWeb::sendChunk(",\"start_hour\":");
    writeUInt(plan.minuteOfDay / 60);
    Esp32BaseWeb::sendChunk(",\"start_minute\":");
    writeUInt(plan.minuteOfDay % 60);
    Esp32BaseWeb::sendChunk(",\"road_sec\":{\"r1\":");
    writeUInt(plan.roadSec[0]);
    Esp32BaseWeb::sendChunk(",\"r2\":");
    writeUInt(plan.roadSec[1]);
    Esp32BaseWeb::sendChunk("},\"mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(SettingsStore::executionModeName(plan.mode));
    Esp32BaseWeb::sendChunk("\",\"repeat_mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(PlanStore::repeatModeName(plan.repeatMode));
    Esp32BaseWeb::sendChunk("\",\"week_mask\":");
    writeUInt(plan.weekMask);
    Esp32BaseWeb::sendChunk(",\"interval_days\":");
    writeUInt(plan.intervalDays);
    Esp32BaseWeb::sendChunk(",\"skip_ymd\":");
    writeUInt(plan.skipYmd);
    Esp32BaseWeb::sendChunk(",\"last_run_ymd\":");
    writeUInt(plan.lastRunYmd);
    Esp32BaseWeb::sendChunk("}");
}

void writePlanTitle(uint8_t index) {
    Esp32BaseWeb::sendChunk("计划 ");
    writeUIntText(static_cast<uint32_t>(index + 1));
}

void writeMinuteOfDayText(uint16_t minuteOfDay) {
    char text[8];
    snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(minuteOfDay / 60), static_cast<unsigned>(minuteOfDay % 60));
    Esp32BaseWeb::sendChunk(text);
}

void writePlanPreviewItemJson(uint8_t index, const PlanStore::Plan& plan, const char* dayLabel) {
    Esp32BaseWeb::sendChunk("{\"index\":");
    writeUInt(index);
    Esp32BaseWeb::sendChunk(",\"title\":\"计划 ");
    writeUInt(index + 1);
    Esp32BaseWeb::sendChunk("\",\"day\":\"");
    Esp32BaseWeb::writeJsonEscaped(dayLabel);
    Esp32BaseWeb::sendChunk("\",\"minute_of_day\":");
    writeUInt(plan.minuteOfDay);
    Esp32BaseWeb::sendChunk(",\"time\":\"");
    char text[8];
    snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(plan.minuteOfDay / 60), static_cast<unsigned>(plan.minuteOfDay % 60));
    Esp32BaseWeb::writeJsonEscaped(text);
    Esp32BaseWeb::sendChunk("\",\"mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(SettingsStore::executionModeName(plan.mode));
    Esp32BaseWeb::sendChunk("\",\"road_sec\":{\"r1\":");
    writeUInt(plan.roadSec[0]);
    Esp32BaseWeb::sendChunk(",\"r2\":");
    writeUInt(plan.roadSec[1]);
    Esp32BaseWeb::sendChunk("}}");
}

void writePlanPreviewArrayJson(uint8_t dayOffset, bool todayRemainingOnly) {
    tm date = {};
    if (!localDateFromOffset(dayOffset, &date)) {
        Esp32BaseWeb::sendChunk("[]");
        return;
    }
    const uint32_t ymd = makeYmd(date);
    const uint16_t currentMinute = static_cast<uint16_t>(date.tm_hour * 60 + date.tm_min);
    bool first = true;
    Esp32BaseWeb::sendChunk("[");
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        if (!planMatchesDate(plan, date, ymd)) {
            continue;
        }
        if (todayRemainingOnly && plan.minuteOfDay < currentMinute) {
            continue;
        }
        if (!first) {
            Esp32BaseWeb::sendChunk(",");
        }
        first = false;
        writePlanPreviewItemJson(i, plan, dayOffset == 0 ? "today" : "tomorrow");
    }
    Esp32BaseWeb::sendChunk("]");
}

void writePlansPreviewJson() {
    Esp32BaseWeb::sendChunk("\"preview\":{\"today_remaining\":");
    writePlanPreviewArrayJson(0, true);
    Esp32BaseWeb::sendChunk(",\"tomorrow\":");
    writePlanPreviewArrayJson(1, false);
    Esp32BaseWeb::sendChunk(",\"next\":");
    bool found = false;
    for (uint8_t day = 0; day < 31 && !found; ++day) {
        tm date = {};
        if (!localDateFromOffset(day, &date)) {
            break;
        }
        const uint32_t ymd = makeYmd(date);
        const uint16_t currentMinute = static_cast<uint16_t>(date.tm_hour * 60 + date.tm_min);
        uint8_t bestIndex = PlanStore::MaxPlans;
        uint16_t bestMinute = 1440;
        for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
            const PlanStore::Plan& plan = PlanStore::get(i);
            if (!planMatchesDate(plan, date, ymd)) {
                continue;
            }
            if (day == 0 && plan.minuteOfDay < currentMinute) {
                continue;
            }
            if (plan.minuteOfDay < bestMinute) {
                bestMinute = plan.minuteOfDay;
                bestIndex = i;
            }
        }
        if (bestIndex < PlanStore::MaxPlans) {
            writePlanPreviewItemJson(bestIndex, PlanStore::get(bestIndex), day == 0 ? "today" : (day == 1 ? "tomorrow" : "future"));
            found = true;
        }
    }
    if (!found) {
        Esp32BaseWeb::sendChunk("null");
    }
    Esp32BaseWeb::sendChunk("}");
}

void writePlanPreviewRows(uint8_t dayOffset, bool todayRemainingOnly) {
    tm date = {};
    if (!localDateFromOffset(dayOffset, &date)) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='5'>时间未同步</td></tr>");
        return;
    }
    const uint32_t ymd = makeYmd(date);
    const uint16_t currentMinute = static_cast<uint16_t>(date.tm_hour * 60 + date.tm_min);
    bool any = false;
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        const PlanStore::Plan& plan = PlanStore::get(i);
        if (!planMatchesDate(plan, date, ymd) || (todayRemainingOnly && plan.minuteOfDay < currentMinute)) {
            continue;
        }
        any = true;
        Esp32BaseWeb::sendChunk("<tr><td>");
        writeMinuteOfDayText(plan.minuteOfDay);
        Esp32BaseWeb::sendChunk("</td><td>");
        writePlanTitle(i);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(SettingsStore::executionModeName(plan.mode));
        Esp32BaseWeb::sendChunk("</td><td>");
        writeMinutesFromSeconds(plan.roadSec[0]);
        Esp32BaseWeb::sendChunk(" 分钟</td><td>");
        if (SettingsStore::isRoadEnabled(2)) {
            writeMinutesFromSeconds(plan.roadSec[1]);
            Esp32BaseWeb::sendChunk(" 分钟");
        } else {
            Esp32BaseWeb::sendChunk("未启用");
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='5'>无计划</td></tr>");
    }
}

void writeRecordRoadJson(const RecordStore::RoadRecord& road) {
    Esp32BaseWeb::sendChunk("{\"state\":\"");
    Esp32BaseWeb::writeJsonEscaped(WateringSession::roadStateName(static_cast<WateringSession::RoadState>(road.state)));
    Esp32BaseWeb::sendChunk("\",\"target_sec\":");
    writeUInt(road.targetSec);
    Esp32BaseWeb::sendChunk(",\"pulse_per_liter\":");
    writeUInt(road.pulsePerLiter);
    Esp32BaseWeb::sendChunk(",\"calibration_x1000\":");
    writeUInt(road.calibrationX1000);
    Esp32BaseWeb::sendChunk(",\"started_ms\":");
    writeUInt(road.startedMs);
    Esp32BaseWeb::sendChunk(",\"ended_ms\":");
    writeUInt(road.endedMs);
    Esp32BaseWeb::sendChunk(",\"pulse_start\":");
    writeUInt(road.startedPulseCount);
    Esp32BaseWeb::sendChunk(",\"pulse_end\":");
    writeUInt(road.endedPulseCount);
    Esp32BaseWeb::sendChunk(",\"pulses\":");
    writeUInt(road.endedPulseCount >= road.startedPulseCount ? road.endedPulseCount - road.startedPulseCount : 0);
    Esp32BaseWeb::sendChunk(",\"estimated_ml\":");
    writeUInt(road.estimatedMilliliters);
    Esp32BaseWeb::sendChunk("}");
}

void writeRecordJson(const RecordStore::Record& record) {
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(record.id);
    Esp32BaseWeb::sendChunk(",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(RecordStore::sourceName(static_cast<RecordStore::Source>(record.source)));
    Esp32BaseWeb::sendChunk("\",\"mode\":\"");
    Esp32BaseWeb::writeJsonEscaped(SettingsStore::executionModeName(static_cast<SettingsStore::ExecutionMode>(record.mode)));
    Esp32BaseWeb::sendChunk("\",\"stop_reason\":\"");
    Esp32BaseWeb::writeJsonEscaped(WateringSession::stopReasonName(static_cast<WateringSession::StopReason>(record.stopReason)));
    Esp32BaseWeb::sendChunk("\",\"session_started_ms\":");
    writeUInt(record.sessionStartedMs);
    Esp32BaseWeb::sendChunk(",\"session_ended_ms\":");
    writeUInt(record.sessionEndedMs);
    Esp32BaseWeb::sendChunk(",\"enabled_roads\":");
    writeUInt(record.enabledRoads);
    Esp32BaseWeb::sendChunk(",\"flow_no_pulse_timeout_s\":");
    writeUInt(record.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk(",\"roads\":{\"r1\":");
    writeRecordRoadJson(record.roads[0]);
    Esp32BaseWeb::sendChunk(",\"r2\":");
    writeRecordRoadJson(record.roads[1]);
    Esp32BaseWeb::sendChunk("}}");
}

void writeRecordJsonCallback(const RecordStore::Record& record, void* user) {
    bool* first = static_cast<bool*>(user);
    if (!*first) {
        Esp32BaseWeb::sendChunk(",");
    }
    *first = false;
    writeRecordJson(record);
}

void writeEventJson(const EventStore::Event& event) {
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(event.id);
    Esp32BaseWeb::sendChunk(",\"type\":\"");
    Esp32BaseWeb::writeJsonEscaped(EventStore::typeName(static_cast<EventStore::Type>(event.type)));
    Esp32BaseWeb::sendChunk("\",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(EventStore::sourceName(static_cast<EventStore::Source>(event.source)));
    Esp32BaseWeb::sendChunk("\",\"uptime_ms\":");
    writeUInt(event.uptimeMs);
    Esp32BaseWeb::sendChunk(",\"epoch\":");
    writeUInt(event.epoch);
    Esp32BaseWeb::sendChunk(",\"road\":");
    writeUInt(event.road);
    Esp32BaseWeb::sendChunk(",\"code\":");
    writeUInt(event.code);
    char number[16];
    Esp32BaseWeb::sendChunk(",\"value1\":");
    snprintf(number, sizeof(number), "%ld", static_cast<long>(event.value1));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk(",\"value2\":");
    snprintf(number, sizeof(number), "%ld", static_cast<long>(event.value2));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk(",\"text\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.text);
    Esp32BaseWeb::sendChunk("\"}");
}

void writeEventJsonCallback(const EventStore::Event& event, void* user) {
    bool* first = static_cast<bool*>(user);
    if (!*first) {
        Esp32BaseWeb::sendChunk(",");
    }
    *first = false;
    writeEventJson(event);
}

void writeCsvNumber(uint32_t value) {
    char number[16];
    snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(number);
}

void writeCsvInt(int32_t value) {
    char number[16];
    snprintf(number, sizeof(number), "%ld", static_cast<long>(value));
    Esp32BaseWeb::sendChunk(number);
}

void writeCsvComma() {
    Esp32BaseWeb::sendChunk(",");
}

void writeCsvNewline() {
    Esp32BaseWeb::sendChunk("\r\n");
}

void writeRecordCsvRoad(const RecordStore::Record& record, uint8_t road) {
    const RecordStore::RoadRecord& roadRecord = record.roads[road - 1];
    const uint32_t pulses = roadRecord.endedPulseCount >= roadRecord.startedPulseCount
        ? roadRecord.endedPulseCount - roadRecord.startedPulseCount
        : 0;
    const uint32_t actualSec = roadRecord.endedMs >= roadRecord.startedMs && roadRecord.startedMs > 0
        ? (roadRecord.endedMs - roadRecord.startedMs) / 1000UL
        : 0;

    writeCsvNumber(record.id);
    writeCsvComma();
    Esp32BaseWeb::writeCsvEscaped(RecordStore::sourceName(static_cast<RecordStore::Source>(record.source)));
    writeCsvComma();
    Esp32BaseWeb::writeCsvEscaped(SettingsStore::executionModeName(static_cast<SettingsStore::ExecutionMode>(record.mode)));
    writeCsvComma();
    Esp32BaseWeb::writeCsvEscaped(WateringSession::stopReasonName(static_cast<WateringSession::StopReason>(record.stopReason)));
    writeCsvComma();
    writeCsvNumber(record.sessionStartedMs);
    writeCsvComma();
    writeCsvNumber(record.sessionEndedMs);
    writeCsvComma();
    writeCsvNumber(record.enabledRoads);
    writeCsvComma();
    writeCsvNumber(record.flowNoPulseTimeoutSec);
    writeCsvComma();
    writeCsvNumber(road);
    writeCsvComma();
    Esp32BaseWeb::writeCsvEscaped(WateringSession::roadStateName(static_cast<WateringSession::RoadState>(roadRecord.state)));
    writeCsvComma();
    writeCsvNumber(roadRecord.targetSec);
    writeCsvComma();
    writeCsvNumber(actualSec);
    writeCsvComma();
    writeCsvNumber(roadRecord.startedMs);
    writeCsvComma();
    writeCsvNumber(roadRecord.endedMs);
    writeCsvComma();
    writeCsvNumber(roadRecord.startedPulseCount);
    writeCsvComma();
    writeCsvNumber(roadRecord.endedPulseCount);
    writeCsvComma();
    writeCsvNumber(pulses);
    writeCsvComma();
    writeCsvNumber(roadRecord.pulsePerLiter);
    writeCsvComma();
    writeCsvNumber(roadRecord.calibrationX1000);
    writeCsvComma();
    writeCsvNumber(roadRecord.estimatedMilliliters);
    writeCsvNewline();
}

void writeRecordCsvCallback(const RecordStore::Record& record, void*) {
    writeRecordCsvRoad(record, 1);
    writeRecordCsvRoad(record, 2);
}

void writeEventCsvCallback(const EventStore::Event& event, void*) {
    writeCsvNumber(event.id);
    writeCsvComma();
    Esp32BaseWeb::writeCsvEscaped(EventStore::typeName(static_cast<EventStore::Type>(event.type)));
    writeCsvComma();
    Esp32BaseWeb::writeCsvEscaped(EventStore::sourceName(static_cast<EventStore::Source>(event.source)));
    writeCsvComma();
    writeCsvNumber(event.uptimeMs);
    writeCsvComma();
    writeCsvNumber(event.epoch);
    writeCsvComma();
    writeCsvNumber(event.road);
    writeCsvComma();
    writeCsvNumber(event.code);
    writeCsvComma();
    writeCsvInt(event.value1);
    writeCsvComma();
    writeCsvInt(event.value2);
    writeCsvComma();
    Esp32BaseWeb::writeCsvEscaped(event.text);
    writeCsvNewline();
}

void writeRecordHtmlRoad(const RecordStore::Record& record, uint8_t road) {
    const RecordStore::RoadRecord& roadRecord = record.roads[road - 1];
    const uint32_t pulses = roadRecord.endedPulseCount >= roadRecord.startedPulseCount
        ? roadRecord.endedPulseCount - roadRecord.startedPulseCount
        : 0;
    const uint32_t actualSec = roadRecord.endedMs >= roadRecord.startedMs && roadRecord.startedMs > 0
        ? (roadRecord.endedMs - roadRecord.startedMs) / 1000UL
        : 0;
    Esp32BaseWeb::sendChunk("<tr><td>");
    writeUIntText(record.id);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(RecordStore::sourceName(static_cast<RecordStore::Source>(record.source)));
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(SettingsStore::executionModeName(static_cast<SettingsStore::ExecutionMode>(record.mode)));
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(WateringSession::stopReasonName(static_cast<WateringSession::StopReason>(record.stopReason)));
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUIntText(road);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(roadStateLabel(static_cast<WateringSession::RoadState>(roadRecord.state)));
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUIntText(roadRecord.targetSec);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUIntText(actualSec);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUIntText(pulses);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUIntText(roadRecord.estimatedMilliliters);
    Esp32BaseWeb::sendChunk("</td></tr>");
}

void writeRecordHtmlCallback(const RecordStore::Record& record, void*) {
    writeRecordHtmlRoad(record, 1);
    writeRecordHtmlRoad(record, 2);
}

void writeEventHtmlCallback(const EventStore::Event& event, void*) {
    Esp32BaseWeb::sendChunk("<tr><td>");
    writeUIntText(event.id);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(EventStore::typeName(static_cast<EventStore::Type>(event.type)));
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(EventStore::sourceName(static_cast<EventStore::Source>(event.source)));
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUIntText(event.road);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeUIntText(event.code);
    Esp32BaseWeb::sendChunk("</td><td>");
    char number[16];
    snprintf(number, sizeof(number), "%ld", static_cast<long>(event.value1));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("</td><td>");
    snprintf(number, sizeof(number), "%ld", static_cast<long>(event.value2));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(event.text);
    Esp32BaseWeb::sendChunk("</td></tr>");
}

void writePlanForm(uint8_t index, const PlanStore::Plan& plan) {
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/plans' data-confirm='确认保存这个浇花计划？'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/plans'><input type='hidden' name='index' value='");
    writeUIntText(index);
    Esp32BaseWeb::sendChunk("'><div class='fieldgrid'><div class='field'><label>启用状态</label><select name='enabled'><option value='1'");
    writeSelected(plan.enabled);
    Esp32BaseWeb::sendChunk(">启用</option><option value='0'");
    writeSelected(!plan.enabled);
    Esp32BaseWeb::sendChunk(">停用</option></select></div><div class='field'><label>小时</label><input name='hour' type='number' min='0' max='23' value='");
    writeUIntText(plan.minuteOfDay / 60);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>分钟</label><input name='minute' type='number' min='0' max='59' value='");
    writeUIntText(plan.minuteOfDay % 60);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>第 1 路时长</label><input name='r1_min' type='number' min='0' max='240' value='");
    writeMinutesFromSeconds(plan.roadSec[0]);
    Esp32BaseWeb::sendChunk("' placeholder='分钟'></div><div class='field'><label>第 2 路时长</label><input name='r2_min' type='number' min='0' max='240' value='");
    writeMinutesFromSeconds(plan.roadSec[1]);
    Esp32BaseWeb::sendChunk("' placeholder='分钟'></div><div class='field'><label>执行模式</label><select name='mode'>");
    writeCnModeOptions(plan.mode);
    Esp32BaseWeb::sendChunk("</select></div><div class='field'><label>周期</label><select name='repeat_mode'>");
    writeCnRepeatOptions(plan.repeatMode);
    Esp32BaseWeb::sendChunk("</select></div><div class='field'><label>每周掩码</label><input name='week_mask' type='number' min='0' max='127' value='");
    writeUIntText(plan.weekMask);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>间隔天数</label><input name='interval_days' type='number' min='1' max='30' value='");
    writeUIntText(plan.intervalDays);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><span>跳过日期</span><b>");
    writeUIntText(plan.skipYmd);
    Esp32BaseWeb::sendChunk("</b></div></div><div class='actions'><input type='submit' value='保存计划'><a href='/irrigation/plans'>返回计划列表</a></div></form><form method='post' action='/api/v1/plans' data-confirm='确认跳过今日此计划？' style='display:inline'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/plans'><input type='hidden' name='index' value='");
    writeUIntText(index);
    Esp32BaseWeb::sendChunk("'><input type='hidden' name='action' value='skip_today'><input type='submit' value='跳过今日'></form><form method='post' action='/api/v1/plans' data-confirm='确认取消跳过此计划？' style='display:inline'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/plans'><input type='hidden' name='index' value='");
    writeUIntText(index);
    Esp32BaseWeb::sendChunk("'><input type='hidden' name='action' value='clear_skip'><input type='submit' value='取消跳过'></form>");
}

void writePlanListRow(uint8_t index, const PlanStore::Plan& plan) {
    Esp32BaseWeb::sendChunk("<tr><td>");
    Esp32BaseWeb::sendChunk(plan.enabled ? "是" : "否");
    Esp32BaseWeb::sendChunk("</td><td>");
    writePlanTitle(index);
    Esp32BaseWeb::sendChunk("</td><td>");
    writeMinuteOfDayText(plan.minuteOfDay);
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(repeatLabel(plan.repeatMode));
    Esp32BaseWeb::sendChunk("</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(modeLabel(plan.mode));
    Esp32BaseWeb::sendChunk("</td><td>");
    writeMinutesFromSeconds(plan.roadSec[0]);
    Esp32BaseWeb::sendChunk(" 分钟</td><td>");
    if (SettingsStore::isRoadEnabled(2)) {
        writeMinutesFromSeconds(plan.roadSec[1]);
        Esp32BaseWeb::sendChunk(" 分钟");
    } else {
        Esp32BaseWeb::sendChunk("未启用");
    }
    Esp32BaseWeb::sendChunk("</td><td><a href='/irrigation/plan?edit=");
    writeUIntText(index);
    Esp32BaseWeb::sendChunk("'>编辑</a></td></tr>");
}

void writeHomeRoadCard(uint8_t road, const SettingsStore::Settings& settings) {
    const WateringSession::RoadStatus& status = WateringSession::roadStatus(road);
    const bool enabled = SettingsStore::isRoadEnabled(road);
    uint32_t remainingSec = 0;
    if (status.state == WateringSession::ROAD_RUNNING && status.targetSec > 0) {
        const uint32_t elapsed = (Esp32BaseSystem::uptimeMs() - status.startedMs) / 1000UL;
        remainingSec = elapsed < status.targetSec ? status.targetSec - elapsed : 0;
    }
    const uint32_t pulses = status.lastPulseCount >= status.startedPulseCount ? status.lastPulseCount - status.startedPulseCount : 0;

    Esp32BaseWeb::sendChunk("<div class='roadcard'><strong>第 ");
    writeUIntText(road);
    Esp32BaseWeb::sendChunk(" 路：");
    Esp32BaseWeb::writeHtmlEscaped(enabled ? roadStateLabel(status.state) : "未启用");
    Esp32BaseWeb::sendChunk("</strong><div class='kv'><span>默认时长</span><b>");
    writeMinutesFromSeconds(settings.quickDurationSec[road - 1]);
    Esp32BaseWeb::sendChunk(" 分钟</b></div><div class='kv'><span>剩余时间</span><b>");
    writeMinutesFromSeconds(remainingSec);
    Esp32BaseWeb::sendChunk(" 分钟</b></div><div class='kv'><span>原始脉冲</span><b>");
    writeUIntText(pulses);
    Esp32BaseWeb::sendChunk("</b></div><div class='kv'><span>估算水量</span><b>");
    writeLitersFromMl(SettingsStore::estimateMilliliters(road, pulses));
    Esp32BaseWeb::sendChunk("</b></div><div class='kv'><span>当前流速</span><b>");
    writeFixed2FromX100(flowRateLiterPerMinuteX100(road));
    Esp32BaseWeb::sendChunk(" L/min</b></div><div class='actions'>");
    if (enabled) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' data-confirm='确认停止这一路浇水？'><input type='hidden' name='return' value='page'><input type='hidden' name='road' value='");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("'><input type='submit' value='停止第 ");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk(" 路'></form>");
    } else {
        Esp32BaseWeb::sendChunk("<span class='badge'>未启用</span>");
    }
    Esp32BaseWeb::sendChunk("</div></div>");
}

void handleIrrigationPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }

    Esp32BaseWeb::sendHeader("灌溉首页");
    writeAppStyle();
    Esp32BaseWeb::sendChunk("<div class='irr'>");
    Esp32BaseWeb::sendChunk("<h2>灌溉首页</h2>");

    const SettingsStore::Settings& settings = SettingsStore::current();
    char number[40];
    char timeText[24] = "未同步";
    (void)Esp32BaseNtp::formatTime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S");
    char ipText[24] = "-";
#if ESP32BASE_ENABLE_WIFI
    (void)Esp32BaseWiFi::ip(ipText, sizeof(ipText));
#endif

    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-4'><h3>设备状态</h3><table><tr><th>项目</th><th>值</th></tr>");

#if ESP32BASE_ENABLE_WIFI
    Esp32BaseWeb::sendChunk("<tr><td>WiFi 名称</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(Esp32BaseWiFi::ssid());
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>信号质量</td><td>");
    snprintf(number, sizeof(number), "%ld dBm", static_cast<long>(Esp32BaseWiFi::rssi()));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>IP 地址</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(ipText);
    Esp32BaseWeb::sendChunk("</td></tr>");
#endif

    Esp32BaseWeb::sendChunk("<tr><td>当前时间</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(timeText);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>启用路数</td><td>");
    writeUIntText(settings.enabledRoads);
    Esp32BaseWeb::sendChunk(" 路</td></tr><tr><td>默认模式</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(modeLabel(settings.defaultMode));
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>剩余内存</td><td>");
    writeBytesHuman(Esp32BaseSystem::freeHeap());
    Esp32BaseWeb::sendChunk("</td></tr></table></div>");

    Esp32BaseWeb::sendChunk("<div class='panel span-4'><h3>阀门状态</h3>");
    for (uint8_t road = 1; road <= 2; ++road) {
        writeHomeRoadCard(road, settings);
    }
    Esp32BaseWeb::sendChunk("</div>");

    Esp32BaseWeb::sendChunk("<div class='panel span-4'><h3>异常与事件提示</h3><p>");
    if (LeakMonitor::hasAlert()) {
        Esp32BaseWeb::sendChunk("存在当前异常，请确认现场已处理后再解除提示。</p><form method='post' action='/api/v1/alerts/clear' data-confirm='确认现场已处理并解除异常提示？'><input type='hidden' name='return' value='page'><input type='submit' value='解除异常提示'></form>");
    } else {
        Esp32BaseWeb::sendChunk("无当前异常。历史事件可在记录页查看。</p>");
    }
    Esp32BaseWeb::sendChunk("</div>");

    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h3>后续计划</h3><h3>今日剩余</h3><table><tr><th>时间</th><th>计划</th><th>模式</th><th>第 1 路</th><th>第 2 路</th></tr>");
    writePlanPreviewRows(0, true);
    Esp32BaseWeb::sendChunk("</table><h3>明日</h3><table><tr><th>时间</th><th>计划</th><th>模式</th><th>第 1 路</th><th>第 2 路</th></tr>");
    writePlanPreviewRows(1, false);
    Esp32BaseWeb::sendChunk("</table></div></section></div>");
    Esp32BaseWeb::sendFooter();
}

void handleManualPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    const SettingsStore::Settings& settings = SettingsStore::current();
    Esp32BaseWeb::sendHeader("手动浇水");
    writeAppStyle();
    Esp32BaseWeb::sendChunk("<div class='irr'>");
    Esp32BaseWeb::sendChunk("<h2>手动浇水</h2>");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-4'><h3>当前状态</h3><table><tr><th>项目</th><th>值</th></tr><tr><td>第 1 路</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(roadStateLabel(WateringSession::roadStatus(1).state));
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>第 2 路</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(SettingsStore::isRoadEnabled(2) ? roadStateLabel(WateringSession::roadStatus(2).state) : "未启用");
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>执行模式</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(modeLabel(settings.defaultMode));
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>无脉冲超时</td><td>");
    writeUIntText(settings.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk(" 秒</td></tr></table></div>");

    Esp32BaseWeb::sendChunk("<div class='panel span-8'><h3>启动浇水</h3><form method='post' action='/api/v1/water/start' data-confirm='确认按当前设置开始浇水？'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/manual'><div class='fieldgrid'><div class='field'><label>执行模式</label><select name='mode'>");
    writeCnModeOptions(settings.defaultMode);
    Esp32BaseWeb::sendChunk("</select></div><div class='field'><label>第 1 路</label><select name='r1_enabled'><option value='1'>浇水</option><option value='0'>不浇水</option></select></div><div class='field'><label>第 1 路时长</label><input name='r1_min' type='number' min='0' max='240' value='");
    writeMinutesFromSeconds(settings.quickDurationSec[0]);
    Esp32BaseWeb::sendChunk("' placeholder='分钟'></div><div class='field'><label>第 2 路</label><select name='r2_enabled'");
    if (!SettingsStore::isRoadEnabled(2)) {
        Esp32BaseWeb::sendChunk(" disabled");
    }
    Esp32BaseWeb::sendChunk("><option value='");
    Esp32BaseWeb::sendChunk(SettingsStore::isRoadEnabled(2) ? "1'>浇水" : "0'>未启用");
    Esp32BaseWeb::sendChunk("</option><option value='0'>不浇水</option></select></div><div class='field'><label>第 2 路时长</label><input name='r2_min' type='number' min='0' max='240' value='");
    writeMinutesFromSeconds(settings.quickDurationSec[1]);
    Esp32BaseWeb::sendChunk("'");
    if (!SettingsStore::isRoadEnabled(2)) {
        Esp32BaseWeb::sendChunk(" disabled");
    }
    Esp32BaseWeb::sendChunk(" placeholder='分钟'></div></div><div class='actions'><input type='submit' value='开始浇水'></div></form>");
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' data-confirm='确认停止全部浇水？'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/manual'><input type='hidden' name='road' value='0'><input type='submit' value='停止全部' style='background:#c33'></form></div>");

    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h3>运行时展示</h3><table><tr><th>路</th><th>状态</th><th>目标时长</th><th>剩余时间</th><th>原始脉冲</th><th>估算水量</th><th>流速</th><th>操作</th></tr>");
    for (uint8_t road = 1; road <= 2; ++road) {
        const WateringSession::RoadStatus& status = WateringSession::roadStatus(road);
        const bool enabled = SettingsStore::isRoadEnabled(road);
        uint32_t remainingSec = 0;
        if (status.state == WateringSession::ROAD_RUNNING && status.targetSec > 0) {
            const uint32_t elapsed = (Esp32BaseSystem::uptimeMs() - status.startedMs) / 1000UL;
            remainingSec = elapsed < status.targetSec ? status.targetSec - elapsed : 0;
        }
        const uint32_t pulses = status.lastPulseCount >= status.startedPulseCount ? status.lastPulseCount - status.startedPulseCount : 0;
        Esp32BaseWeb::sendChunk("<tr><td>第 ");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk(" 路</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(enabled ? roadStateLabel(status.state) : "未启用");
        Esp32BaseWeb::sendChunk("</td><td>");
        writeMinutesFromSeconds(status.targetSec);
        Esp32BaseWeb::sendChunk(" 分钟</td><td>");
        writeMinutesFromSeconds(remainingSec);
        Esp32BaseWeb::sendChunk(" 分钟</td><td>");
        writeUIntText(pulses);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeLitersFromMl(SettingsStore::estimateMilliliters(road, pulses));
        Esp32BaseWeb::sendChunk("</td><td>");
        writeFixed2FromX100(flowRateLiterPerMinuteX100(road));
        Esp32BaseWeb::sendChunk(" L/min</td><td>");
        if (enabled) {
            Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/water/stop' data-confirm='确认停止这一路浇水？'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/manual'><input type='hidden' name='road' value='");
            writeUIntText(road);
            Esp32BaseWeb::sendChunk("'><input type='submit' value='停止第 ");
            writeUIntText(road);
            Esp32BaseWeb::sendChunk(" 路'></form>");
        } else {
            Esp32BaseWeb::sendChunk("未启用");
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</table></div></section></div>");
    Esp32BaseWeb::sendFooter();
}

void handleSettingsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    const SettingsStore::Settings& settings = SettingsStore::current();
    Esp32BaseWeb::sendHeader("设备配置");
    writeAppStyle();
    Esp32BaseWeb::sendChunk("<div class='irr'>");
    Esp32BaseWeb::sendChunk("<h2>设备配置</h2>");
    Esp32BaseWeb::sendChunk("<section class='grid'><form class='config-form' method='post' action='/api/v1/config' data-confirm='确认保存设备配置？'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/settings'><div class='panel span-4'><h3>基础设置</h3><div class='fieldgrid'><div class='field'><label>启用路数</label><input name='enabled_roads' type='number' min='1' max='2' value='");
    writeUIntText(settings.enabledRoads);
    Esp32BaseWeb::sendChunk("'></div><div class='field'><label>默认模式</label><select name='default_mode'>");
    writeCnModeOptions(settings.defaultMode);
    Esp32BaseWeb::sendChunk("</select></div></div></div>");

    for (uint8_t road = 1; road <= 2; ++road) {
        const bool enabled = SettingsStore::isRoadEnabled(road);
        const SettingsStore::RoadConfig& roadConfig = settings.roads[road - 1];
        Esp32BaseWeb::sendChunk("<div class='panel span-4'><h3>第 ");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk(" 路配置</h3><div class='fieldgrid'><div class='field'><label>名称</label><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_name' maxlength='11' value='");
        Esp32BaseWeb::writeHtmlEscaped(roadConfig.name);
        Esp32BaseWeb::sendChunk("'");
        if (!enabled) {
            Esp32BaseWeb::sendChunk(" disabled");
        }
        Esp32BaseWeb::sendChunk("></div><div class='field'><label>默认时长</label><input name='quick_r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_min' type='number' min='1' max='240' value='");
        writeMinutesFromSeconds(settings.quickDurationSec[road - 1]);
        Esp32BaseWeb::sendChunk("'");
        if (!enabled) {
            Esp32BaseWeb::sendChunk(" disabled");
        }
        Esp32BaseWeb::sendChunk(" placeholder='分钟'></div><div class='field'><label>每升脉冲</label><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_pulse_per_liter' type='number' min='1' max='10000' value='");
        writeUIntText(roadConfig.pulsePerLiter);
        Esp32BaseWeb::sendChunk("'");
        if (!enabled) {
            Esp32BaseWeb::sendChunk(" disabled");
        }
        Esp32BaseWeb::sendChunk("></div><div class='field'><label>校准 x1000</label><input name='r");
        writeUIntText(road);
        Esp32BaseWeb::sendChunk("_calibration_x1000' type='number' min='100' max='10000' value='");
        writeUIntText(roadConfig.calibrationX1000);
        Esp32BaseWeb::sendChunk("'");
        if (!enabled) {
            Esp32BaseWeb::sendChunk(" disabled");
        }
        Esp32BaseWeb::sendChunk("></div></div></div>");
    }

    Esp32BaseWeb::sendChunk("<div class='panel span-6'><h3>水流检测</h3><div class='fieldgrid'><div class='field'><label>无脉冲超时</label><input name='flow_no_pulse_timeout_s' type='number' min='1' max='60' value='");
    writeUIntText(settings.flowNoPulseTimeoutSec);
    Esp32BaseWeb::sendChunk("' placeholder='秒'></div><div class='field'><label>漏水窗口</label><input name='idle_leak_window_s' type='number' min='1' max='60' value='");
    writeUIntText(settings.idleLeakWindowSec);
    Esp32BaseWeb::sendChunk("' placeholder='秒'></div><div class='field'><label>漏水脉冲阈值</label><input name='idle_leak_pulse_threshold' type='number' min='1' max='100' value='");
    writeUIntText(settings.idleLeakPulseThreshold);
    Esp32BaseWeb::sendChunk("'></div></div></div><div class='panel span-6'><h3>交互与安全</h3><div class='fieldgrid'><div class='field'><label>按键锁定</label><select name='keypad_locked'><option value='0'");
    writeSelected(!settings.keypadLocked);
    Esp32BaseWeb::sendChunk(">未锁定</option><option value='1'");
    writeSelected(settings.keypadLocked);
    Esp32BaseWeb::sendChunk(">已锁定</option></select></div></div><div class='actions'><input type='submit' value='保存配置'></div></div></form>");

    Esp32BaseWeb::sendChunk("<div class='panel span-6'><h3>登录与访问</h3><table><tr><td>当前账号</td><td>");
    Esp32BaseWeb::writeHtmlEscaped(Esp32BaseWeb::authUser());
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>修改账号密码</td><td><a href='/esp32base/auth'>进入认证设置</a></td></tr></table><p>登录账号和密码由 Esp32Base 内置认证页面管理；本项目只设置默认认证，不保存 Web Auth 密码。</p></div>");
    Esp32BaseWeb::sendChunk("<div class='panel span-6'><h3>维护</h3><form method='post' action='/api/v1/factory-reset' data-confirm='确认恢复出厂设置？此操作会重置设备配置。'><input type='hidden' name='return' value='page'><input type='hidden' name='return_path' value='/irrigation/settings'><div class='fieldgrid'><div class='field'><label>确认文本</label><input name='confirm' maxlength='5' placeholder='RESET'></div><div class='field'><label>记录处理</label><select name='clear_records'><option value='0'>保留记录</option><option value='1'>清空记录</option></select></div></div><input type='submit' value='恢复出厂' style='background:#c33'></form></div></section></div>");
    Esp32BaseWeb::sendFooter();
}

void handlePlansPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("浇花计划");
    writeAppStyle();
    Esp32BaseWeb::sendChunk("<div class='irr'>");
    Esp32BaseWeb::sendChunk("<h2>浇花计划</h2>");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-6'><h3>今日剩余计划</h3><table><tr><th>时间</th><th>计划</th><th>模式</th><th>第 1 路</th><th>第 2 路</th></tr>");
    writePlanPreviewRows(0, true);
    Esp32BaseWeb::sendChunk("</table></div><div class='panel span-6'><h3>明日计划</h3><table><tr><th>时间</th><th>计划</th><th>模式</th><th>第 1 路</th><th>第 2 路</th></tr>");
    writePlanPreviewRows(1, false);
    Esp32BaseWeb::sendChunk("</table></div><div class='panel span-12'><h3>计划列表</h3><table><tr><th>启用</th><th>名称</th><th>时间</th><th>周期</th><th>模式</th><th>第 1 路</th><th>第 2 路</th><th>操作</th></tr>");
    for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
        writePlanListRow(i, PlanStore::get(i));
    }
    Esp32BaseWeb::sendChunk("</table></div></section></div>");
    Esp32BaseWeb::sendFooter();
}

void handlePlanEditPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    const uint8_t editIndex = selectedPlanIndex();
    Esp32BaseWeb::sendHeader("编辑计划");
    writeAppStyle();
    Esp32BaseWeb::sendChunk("<div class='irr'><h2>编辑计划</h2><section class='grid'><div class='panel span-8'><h3>");
    writePlanTitle(editIndex);
    Esp32BaseWeb::sendChunk("</h3>");
    writePlanForm(editIndex, PlanStore::get(editIndex));
    Esp32BaseWeb::sendChunk("</div></section></div>");
    Esp32BaseWeb::sendFooter();
}

void handleDataPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("数据记录");
    writeAppStyle();
    Esp32BaseWeb::sendChunk("<div class='irr'>");
    Esp32BaseWeb::sendChunk("<h2>数据记录</h2>");
    Esp32BaseWeb::sendChunk("<section class='grid'><div class='panel span-12'><h3 id='records'>浇水记录</h3><p><a href='/api/v1/records'>JSON</a> <a href='/api/v1/records.csv'>CSV 导出</a></p><table><tr><th>ID</th><th>来源</th><th>模式</th><th>原因</th><th>路</th><th>状态</th><th>目标秒</th><th>实际秒</th><th>原始脉冲</th><th>ml</th></tr>");
    (void)RecordStore::readLatest(0, 10, writeRecordHtmlCallback, nullptr);
    Esp32BaseWeb::sendChunk("</table></div>");
    Esp32BaseWeb::sendChunk("<div class='panel span-12'><h3 id='events'>事件记录</h3><p><a href='/api/v1/events'>JSON</a> <a href='/api/v1/events.csv'>CSV 导出</a></p><table><tr><th>ID</th><th>类型</th><th>来源</th><th>路</th><th>代码</th><th>V1</th><th>V2</th><th>说明</th></tr>");
    (void)EventStore::readLatest(0, 20, writeEventHtmlCallback, nullptr);
    Esp32BaseWeb::sendChunk("</table></div></section></div>");
    Esp32BaseWeb::sendFooter();
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }

    char number[32];
    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"firmware\":{\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32Base::firmwareName());
    Esp32BaseWeb::sendChunk("\",\"version\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32Base::firmwareVersion());
    Esp32BaseWeb::sendChunk("\"},\"base\":{\"ready\":");
    writeBool(Esp32Base::isReady());
    Esp32BaseWeb::sendChunk(",\"profile\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32Base::profileName());
    Esp32BaseWeb::sendChunk("\"},\"wifi\":{\"connected\":");
#if ESP32BASE_ENABLE_WIFI
    writeBool(Esp32BaseWiFi::isConnected());
    Esp32BaseWeb::sendChunk(",\"state\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32BaseWiFi::stateName());
    Esp32BaseWeb::sendChunk("\",\"ssid\":\"");
    Esp32BaseWeb::writeJsonEscaped(Esp32BaseWiFi::ssid());
    Esp32BaseWeb::sendChunk("\",\"rssi\":");
    snprintf(number, sizeof(number), "%ld", static_cast<long>(Esp32BaseWiFi::rssi()));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk(",\"ip\":\"");
    char ipText[24] = "";
    (void)Esp32BaseWiFi::ip(ipText, sizeof(ipText));
    Esp32BaseWeb::writeJsonEscaped(ipText);
#else
    writeBool(false);
    Esp32BaseWeb::sendChunk(",\"state\":\"disabled");
#endif
    Esp32BaseWeb::sendChunk("\"},\"time\":{\"synced\":");
    writeBool(Esp32BaseNtp::isTimeSynced());
    Esp32BaseWeb::sendChunk(",\"current\":\"");
    char timeText[24] = "";
    if (!Esp32BaseNtp::formatTime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S")) {
        timeText[0] = '\0';
    }
    Esp32BaseWeb::writeJsonEscaped(timeText);
    Esp32BaseWeb::sendChunk("\"},\"system\":{\"uptime_ms\":");
    snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(Esp32BaseSystem::uptimeMs()));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk(",\"free_heap\":");
    snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(Esp32BaseSystem::freeHeap()));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("},\"valves\":{\"r1_open\":");
    writeBool(ValveController::isOpen(ValveController::Road1));
    Esp32BaseWeb::sendChunk(",\"r2_open\":");
    writeBool(ValveController::isOpen(ValveController::Road2));
    Esp32BaseWeb::sendChunk("},\"keypad\":{\"locked\":");
    writeBool(SafetyManager::isLocked());
    Esp32BaseWeb::sendChunk(",\"factory_reset_requested\":");
    writeBool(SafetyManager::factoryResetRequested());
    Esp32BaseWeb::sendChunk(",\"factory_reset_pending\":");
    writeBool(MaintenanceService::factoryResetPending());
    Esp32BaseWeb::sendChunk("},\"alerts\":{\"leak\":");
    writeBool(LeakMonitor::hasAlert());
    Esp32BaseWeb::sendChunk(",\"r1_leak\":");
    writeBool(LeakMonitor::roadAlert(1));
    Esp32BaseWeb::sendChunk(",\"r2_leak\":");
    writeBool(LeakMonitor::roadAlert(2));
    Esp32BaseWeb::sendChunk("},\"records\":{\"count\":");
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
    endJsonStream();
}

void handleConfigApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }

    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        beginJsonStream(200);
        Esp32BaseWeb::sendChunk("{");
        writeSettingsJson();
        Esp32BaseWeb::sendChunk("}");
        endJsonStream();
        return;
    }

    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("GET,POST");
        return;
    }

    SettingsStore::Settings next = SettingsStore::current();
    uint16_t value = 0;
    bool present = false;
    bool boolValue = false;
    SettingsStore::ExecutionMode mode = next.defaultMode;

    if (!readOptionalUIntParam("enabled_roads", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_enabled_roads\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > IrrigationPins::MaxRoads) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_enabled_roads\"}");
            return;
        }
        next.enabledRoads = static_cast<uint8_t>(value);
    }
    if (Esp32BaseWeb::hasParam("default_mode") && !readModeParam("default_mode", &mode)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_default_mode\"}");
        return;
    }
    if (Esp32BaseWeb::hasParam("default_mode")) {
        next.defaultMode = mode;
    }
    if (!readOptionalDurationParam("quick_r1_sec", "quick_r1_min", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_quick_r1_sec\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 14400) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_quick_r1_sec\"}");
            return;
        }
        next.quickDurationSec[0] = value;
    }
    if (!readOptionalDurationParam("quick_r2_sec", "quick_r2_min", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_quick_r2_sec\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 14400) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_quick_r2_sec\"}");
            return;
        }
        next.quickDurationSec[1] = value;
    }
    if (!readOptionalUIntParam("flow_no_pulse_timeout_s", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_flow_no_pulse_timeout_s\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 60) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_flow_no_pulse_timeout_s\"}");
            return;
        }
        next.flowNoPulseTimeoutSec = static_cast<uint8_t>(value);
    }
    if (!readOptionalUIntParam("idle_leak_window_s", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_idle_leak_window_s\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 60) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_idle_leak_window_s\"}");
            return;
        }
        next.idleLeakWindowSec = static_cast<uint8_t>(value);
    }
    if (!readOptionalUIntParam("idle_leak_pulse_threshold", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_idle_leak_pulse_threshold\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 100) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_idle_leak_pulse_threshold\"}");
            return;
        }
        next.idleLeakPulseThreshold = static_cast<uint8_t>(value);
    }
    if (Esp32BaseWeb::hasParam("keypad_locked") && !readBoolParam("keypad_locked", &boolValue)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_keypad_locked\"}");
        return;
    }
    if (Esp32BaseWeb::hasParam("keypad_locked")) {
        next.keypadLocked = boolValue;
    }
    char textValue[12] = "";
    if (Esp32BaseWeb::hasParam("r1_name")) {
        if (!Esp32BaseWeb::getParam("r1_name", textValue, sizeof(textValue)) || textValue[0] == '\0') {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_name\"}");
            return;
        }
        strlcpy(next.roads[0].name, textValue, sizeof(next.roads[0].name));
    }
    if (Esp32BaseWeb::hasParam("r2_name")) {
        if (!Esp32BaseWeb::getParam("r2_name", textValue, sizeof(textValue)) || textValue[0] == '\0') {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_name\"}");
            return;
        }
        strlcpy(next.roads[1].name, textValue, sizeof(next.roads[1].name));
    }
    if (!readOptionalUIntParam("r1_pulse_per_liter", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_pulse_per_liter\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 10000) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_pulse_per_liter\"}");
            return;
        }
        next.roads[0].pulsePerLiter = value;
    }
    if (!readOptionalUIntParam("r2_pulse_per_liter", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_pulse_per_liter\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 10000) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_pulse_per_liter\"}");
            return;
        }
        next.roads[1].pulsePerLiter = value;
    }
    if (!readOptionalUIntParam("r1_calibration_x1000", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_calibration_x1000\"}");
        return;
    }
    if (present) {
        if (value < 100 || value > 10000) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_calibration_x1000\"}");
            return;
        }
        next.roads[0].calibrationX1000 = value;
    }
    if (!readOptionalUIntParam("r2_calibration_x1000", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_calibration_x1000\"}");
        return;
    }
    if (present) {
        if (value < 100 || value > 10000) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_calibration_x1000\"}");
            return;
        }
        next.roads[1].calibrationX1000 = value;
    }

    const bool ok = SettingsStore::setEnabledRoads(next.enabledRoads) &&
                    SettingsStore::setDefaultExecutionMode(next.defaultMode) &&
                    SettingsStore::setQuickDurationSec(1, next.quickDurationSec[0]) &&
                    SettingsStore::setQuickDurationSec(2, next.quickDurationSec[1]) &&
                    SettingsStore::setFlowNoPulseTimeoutSec(next.flowNoPulseTimeoutSec) &&
                    SettingsStore::setIdleLeakWindowSec(next.idleLeakWindowSec) &&
                    SettingsStore::setIdleLeakPulseThreshold(next.idleLeakPulseThreshold) &&
                    SettingsStore::setRoadName(1, next.roads[0].name) &&
                    SettingsStore::setRoadName(2, next.roads[1].name) &&
                    SettingsStore::setRoadPulsePerLiter(1, next.roads[0].pulsePerLiter) &&
                    SettingsStore::setRoadPulsePerLiter(2, next.roads[1].pulsePerLiter) &&
                    SettingsStore::setRoadCalibrationX1000(1, next.roads[0].calibrationX1000) &&
                    SettingsStore::setRoadCalibrationX1000(2, next.roads[1].calibrationX1000) &&
                    SafetyManager::setLocked(next.keypadLocked);
    if (!ok) {
        Esp32BaseWeb::sendJson(500, "{\"ok\":false,\"error\":\"save_failed\"}");
        return;
    }
    (void)EventStore::append(EventStore::TYPE_CONFIG_CHANGED,
                             EventStore::SOURCE_WEB,
                             0,
                             0,
                             next.enabledRoads,
                             next.flowNoPulseTimeoutSec,
                             "config saved");
    if (wantsPageRedirect()) {
        redirectToPage();
        return;
    }

    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,");
    writeSettingsJson();
    Esp32BaseWeb::sendChunk("}");
    endJsonStream();
}

void handleWaterStartApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }

    uint16_t road1Sec = 0;
    uint16_t road2Sec = 0;
    bool present = false;
    if (!readOptionalDurationParam("r1_sec", "r1_min", &road1Sec, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_duration\"}");
        return;
    }
    bool boolValue = false;
    if (Esp32BaseWeb::hasParam("r1_enabled")) {
        if (!readBoolParam("r1_enabled", &boolValue)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_enabled\"}");
            return;
        }
        if (!boolValue) {
            road1Sec = 0;
        }
    }
    if (!readOptionalDurationParam("r2_sec", "r2_min", &road2Sec, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_duration\"}");
        return;
    }
    if (Esp32BaseWeb::hasParam("r2_enabled")) {
        if (!readBoolParam("r2_enabled", &boolValue)) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_enabled\"}");
            return;
        }
        if (!boolValue) {
            road2Sec = 0;
        }
    }

    SettingsStore::ExecutionMode mode = SettingsStore::defaultExecutionMode();
    if (Esp32BaseWeb::hasParam("mode") && !readModeParam("mode", &mode)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_mode\"}");
        return;
    }

    if (!WateringSession::startManual(road1Sec, road2Sec, mode, RecordStore::SOURCE_WEB, "web manual")) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_watering_request\"}");
        return;
    }
    if (wantsPageRedirect()) {
        redirectToPage();
        return;
    }

    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,");
    writeWateringJson();
    Esp32BaseWeb::sendChunk("}");
    endJsonStream();
}

void handleWaterStopApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }

    uint16_t road = 0;
    if (Esp32BaseWeb::hasParam("road") && !readUIntParam("road", &road)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_road\"}");
        return;
    }
    if (road == 0) {
        WateringSession::stopAll(WateringSession::REASON_MANUAL_STOP, "web stop all");
    } else if (road == 1 || road == 2) {
        WateringSession::stopRoad(static_cast<uint8_t>(road), WateringSession::REASON_MANUAL_STOP, "web stop road");
    } else {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_road\"}");
        return;
    }
    if (wantsPageRedirect()) {
        redirectToPage();
        return;
    }

    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,");
    writeWateringJson();
    Esp32BaseWeb::sendChunk("}");
    endJsonStream();
}

void handleAlertsClearApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }

    LeakMonitor::clearAlerts();
    if (wantsPageRedirect()) {
        redirectToPage();
        return;
    }
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

void handleFactoryResetApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("POST");
        return;
    }

    char confirm[8] = "";
    if (!Esp32BaseWeb::getParam("confirm", confirm, sizeof(confirm)) || strcmp(confirm, "RESET") != 0) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"confirmation_required\"}");
        return;
    }

    bool clearRecords = false;
    if (Esp32BaseWeb::hasParam("clear_records") && !readBoolParam("clear_records", &clearRecords)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_clear_records\"}");
        return;
    }

    if (!MaintenanceService::requestFactoryReset(clearRecords)) {
        Esp32BaseWeb::sendJson(409, "{\"ok\":false,\"error\":\"factory_reset_already_pending\"}");
        return;
    }
    if (wantsPageRedirect()) {
        redirectToPage();
        return;
    }

    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"factory_reset_pending\":true,\"clear_records\":");
    writeBool(clearRecords);
    Esp32BaseWeb::sendChunk("}");
    endJsonStream();
}

void handleRecordsApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }

    uint16_t offset = 0;
    uint16_t limitRaw = 20;
    bool present = false;
    if (!readOptionalUIntParam("offset", &offset, &present) ||
        !readOptionalUIntParam("limit", &limitRaw, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_pagination\"}");
        return;
    }
    if (limitRaw == 0 || limitRaw > 50) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_limit\"}");
        return;
    }

    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"count\":");
    writeUInt(RecordStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(RecordStore::capacity());
    Esp32BaseWeb::sendChunk(",\"offset\":");
    writeUInt(offset);
    Esp32BaseWeb::sendChunk(",\"limit\":");
    writeUInt(limitRaw);
    Esp32BaseWeb::sendChunk(",\"records\":[");
    bool first = true;
    (void)RecordStore::readLatest(offset, static_cast<uint8_t>(limitRaw), writeRecordJsonCallback, &first);
    Esp32BaseWeb::sendChunk("]}");
    endJsonStream();
}

void handleRecordsCsvApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }

    uint16_t offset = 0;
    uint16_t limitRaw = 50;
    bool present = false;
    if (!readOptionalUIntParam("offset", &offset, &present) ||
        !readOptionalUIntParam("limit", &limitRaw, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_pagination\"}");
        return;
    }
    if (limitRaw == 0 || limitRaw > RecordStore::capacity()) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_limit\"}");
        return;
    }

    if (!Esp32BaseWeb::beginCsv(200, "irrigation-records.csv")) {
        return;
    }
    Esp32BaseWeb::sendChunk("session_id,source,mode,stop_reason,session_started_ms,session_ended_ms,enabled_roads,flow_no_pulse_timeout_s,road,state,target_sec,actual_sec,road_started_ms,road_ended_ms,pulse_start,pulse_end,pulses,pulse_per_liter,calibration_x1000,estimated_ml\r\n");
    (void)RecordStore::readLatest(offset, static_cast<uint8_t>(limitRaw), writeRecordCsvCallback, nullptr);
    Esp32BaseWeb::endResponse();
}

void handlePlansApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }

    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        beginJsonStream(200);
        Esp32BaseWeb::sendChunk("{\"count\":");
        writeUInt(PlanStore::MaxPlans);
        Esp32BaseWeb::sendChunk(",\"plans\":[");
        for (uint8_t i = 0; i < PlanStore::MaxPlans; ++i) {
            if (i > 0) {
                Esp32BaseWeb::sendChunk(",");
            }
            writePlanJson(i, PlanStore::get(i));
        }
        Esp32BaseWeb::sendChunk("],");
        writePlansPreviewJson();
        Esp32BaseWeb::sendChunk("}");
        endJsonStream();
        return;
    }

    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        sendMethodNotAllowed("GET,POST");
        return;
    }

    uint16_t indexRaw = 0;
    bool present = false;
    if (!readOptionalUIntParam("index", &indexRaw, &present) || !present || indexRaw >= PlanStore::MaxPlans) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_index\"}");
        return;
    }
    const uint8_t index = static_cast<uint8_t>(indexRaw);

    char action[16] = "";
    if (Esp32BaseWeb::getParam("action", action, sizeof(action))) {
        if (strcmp(action, "skip_today") == 0) {
            const uint32_t today = currentYmd();
            if (today == 0 || !PlanStore::setSkipYmd(index, today)) {
                Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"skip_today_failed\"}");
                return;
            }
            (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED,
                                     EventStore::SOURCE_WEB,
                                     0,
                                     index,
                                     static_cast<int32_t>(today),
                                     0,
                                     "skip today");
        } else if (strcmp(action, "clear_skip") == 0) {
            if (!PlanStore::clearSkipYmd(index)) {
                Esp32BaseWeb::sendJson(500, "{\"ok\":false,\"error\":\"clear_skip_failed\"}");
                return;
            }
            (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED,
                                     EventStore::SOURCE_WEB,
                                     0,
                                     index,
                                     0,
                                     0,
                                     "clear skip");
        } else {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_action\"}");
            return;
        }
        if (wantsPageRedirect()) {
            redirectToPage();
            return;
        }
        beginJsonStream(200);
        Esp32BaseWeb::sendChunk("{\"ok\":true,\"plan\":");
        writePlanJson(index, PlanStore::get(index));
        Esp32BaseWeb::sendChunk("}");
        endJsonStream();
        return;
    }

    PlanStore::Plan plan = PlanStore::get(index);
    bool boolValue = false;
    if (!readOptionalBoolParam("enabled", &boolValue, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_enabled\"}");
        return;
    }
    if (present) {
        plan.enabled = boolValue;
    }

    uint16_t value = 0;
    uint16_t hour = plan.minuteOfDay / 60;
    uint16_t minute = plan.minuteOfDay % 60;
    if (!readOptionalUIntParam("hour", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_hour\"}");
        return;
    }
    if (present) {
        if (value > 23) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_hour\"}");
            return;
        }
        hour = value;
    }
    if (!readOptionalUIntParam("minute", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_minute\"}");
        return;
    }
    if (present) {
        if (value > 59) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_minute\"}");
            return;
        }
        minute = value;
    }
    plan.minuteOfDay = static_cast<uint16_t>(hour * 60 + minute);

    if (!readOptionalDurationParam("r1_sec", "r1_min", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_sec\"}");
        return;
    }
    if (present) {
        if (value > 14400) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r1_sec\"}");
            return;
        }
        plan.roadSec[0] = value;
    }
    if (!readOptionalDurationParam("r2_sec", "r2_min", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_sec\"}");
        return;
    }
    if (present) {
        if (value > 14400) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_r2_sec\"}");
            return;
        }
        plan.roadSec[1] = value;
    }

    SettingsStore::ExecutionMode mode = plan.mode;
    if (Esp32BaseWeb::hasParam("mode") && !readModeParam("mode", &mode)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_mode\"}");
        return;
    }
    plan.mode = mode;

    PlanStore::RepeatMode repeatMode = plan.repeatMode;
    char repeatText[20] = "";
    if (Esp32BaseWeb::getParam("repeat_mode", repeatText, sizeof(repeatText)) &&
        !PlanStore::parseRepeatMode(repeatText, &repeatMode)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_repeat_mode\"}");
        return;
    }
    plan.repeatMode = repeatMode;

    if (!readOptionalUIntParam("week_mask", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_week_mask\"}");
        return;
    }
    if (present) {
        if (value > 127) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_week_mask\"}");
            return;
        }
        plan.weekMask = static_cast<uint8_t>(value);
    }
    if (!readOptionalUIntParam("interval_days", &value, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_interval_days\"}");
        return;
    }
    if (present) {
        if (value < 1 || value > 30) {
            Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_interval_days\"}");
            return;
        }
        plan.intervalDays = static_cast<uint8_t>(value);
    }

    if (!PlanStore::set(index, plan)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_plan\"}");
        return;
    }
    (void)EventStore::append(EventStore::TYPE_PLAN_CHANGED,
                             EventStore::SOURCE_WEB,
                             0,
                             index,
                             plan.roadSec[0],
                             plan.roadSec[1],
                             "plan saved");
    if (wantsPageRedirect()) {
        redirectToPage();
        return;
    }

    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"plan\":");
    writePlanJson(index, PlanStore::get(index));
    Esp32BaseWeb::sendChunk("}");
    endJsonStream();
}

void handleEventsApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }

    uint16_t offset = 0;
    uint16_t limitRaw = 20;
    bool present = false;
    if (!readOptionalUIntParam("offset", &offset, &present) ||
        !readOptionalUIntParam("limit", &limitRaw, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_pagination\"}");
        return;
    }
    if (limitRaw == 0 || limitRaw > 50) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_limit\"}");
        return;
    }

    beginJsonStream(200);
    Esp32BaseWeb::sendChunk("{\"count\":");
    writeUInt(EventStore::count());
    Esp32BaseWeb::sendChunk(",\"capacity\":");
    writeUInt(EventStore::capacity());
    Esp32BaseWeb::sendChunk(",\"offset\":");
    writeUInt(offset);
    Esp32BaseWeb::sendChunk(",\"limit\":");
    writeUInt(limitRaw);
    Esp32BaseWeb::sendChunk(",\"events\":[");
    bool first = true;
    (void)EventStore::readLatest(offset, static_cast<uint8_t>(limitRaw), writeEventJsonCallback, &first);
    Esp32BaseWeb::sendChunk("]}");
    endJsonStream();
}

void handleEventsCsvApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        sendMethodNotAllowed("GET");
        return;
    }

    uint16_t offset = 0;
    uint16_t limitRaw = 50;
    bool present = false;
    if (!readOptionalUIntParam("offset", &offset, &present) ||
        !readOptionalUIntParam("limit", &limitRaw, &present)) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_pagination\"}");
        return;
    }
    if (limitRaw == 0 || limitRaw > EventStore::capacity()) {
        Esp32BaseWeb::sendJson(400, "{\"ok\":false,\"error\":\"invalid_limit\"}");
        return;
    }

    if (!Esp32BaseWeb::beginCsv(200, "irrigation-events.csv")) {
        return;
    }
    Esp32BaseWeb::sendChunk("event_id,type,source,uptime_ms,epoch,road,code,value1,value2,text\r\n");
    (void)EventStore::readLatest(offset, static_cast<uint8_t>(limitRaw), writeEventCsvCallback, nullptr);
    Esp32BaseWeb::endResponse();
}

}

namespace IrrigationWeb {

void begin() {
    const bool pageOk = Esp32BaseWeb::addPage("/irrigation", "首页", handleIrrigationPage);
    const bool manualPageOk = Esp32BaseWeb::addPage("/irrigation/manual", "手动", handleManualPage);
    const bool plansPageOk = Esp32BaseWeb::addPage("/irrigation/plans", "计划", handlePlansPage);
    const bool settingsPageOk = Esp32BaseWeb::addPage("/irrigation/settings", "配置", handleSettingsPage);
    const bool dataPageOk = Esp32BaseWeb::addPage("/irrigation/data", "记录", handleDataPage);
    const bool planEditOk = Esp32BaseWeb::addRoute("/irrigation/plan", Esp32BaseWeb::METHOD_GET, handlePlanEditPage);
    const bool statusNavOk = Esp32BaseWeb::addNavItem("/api/v1/status", "状态 API");
    const bool statusOk = Esp32BaseWeb::addApi("/api/v1/status", handleStatusApi);
    const bool configOk = Esp32BaseWeb::addApi("/api/v1/config", handleConfigApi);
    const bool startOk = Esp32BaseWeb::addApi("/api/v1/water/start", handleWaterStartApi);
    const bool stopOk = Esp32BaseWeb::addApi("/api/v1/water/stop", handleWaterStopApi);
    const bool recordsOk = Esp32BaseWeb::addApi("/api/v1/records", handleRecordsApi);
    const bool recordsCsvOk = Esp32BaseWeb::addApi("/api/v1/records.csv", handleRecordsCsvApi);
    const bool eventsOk = Esp32BaseWeb::addApi("/api/v1/events", handleEventsApi);
    const bool eventsCsvOk = Esp32BaseWeb::addApi("/api/v1/events.csv", handleEventsCsvApi);
    const bool plansOk = Esp32BaseWeb::addApi("/api/v1/plans", handlePlansApi);
    const bool alertsOk = Esp32BaseWeb::addApi("/api/v1/alerts/clear", handleAlertsClearApi);
    const bool resetOk = Esp32BaseWeb::addApi("/api/v1/factory-reset", handleFactoryResetApi);
    ESP32BASE_LOG_I("irrigation.web", "routes page=%s manualPage=%s settingsPage=%s plansPage=%s dataPage=%s planEdit=%s statusNav=%s status=%s config=%s start=%s stop=%s records=%s recordsCsv=%s events=%s eventsCsv=%s plans=%s alerts=%s reset=%s firmware=%s",
                    pageOk ? "ok" : "fail",
                    manualPageOk ? "ok" : "fail",
                    settingsPageOk ? "ok" : "fail",
                    plansPageOk ? "ok" : "fail",
                    dataPageOk ? "ok" : "fail",
                    planEditOk ? "ok" : "fail",
                    statusNavOk ? "ok" : "fail",
                    statusOk ? "ok" : "fail",
                    configOk ? "ok" : "fail",
                    startOk ? "ok" : "fail",
                    stopOk ? "ok" : "fail",
                    recordsOk ? "ok" : "fail",
                    recordsCsvOk ? "ok" : "fail",
                    eventsOk ? "ok" : "fail",
                    eventsCsvOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    alertsOk ? "ok" : "fail",
                    resetOk ? "ok" : "fail",
                    IrrigationVersion::FirmwareName);
}

}
