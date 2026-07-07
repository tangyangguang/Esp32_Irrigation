#include "IrrigationWeb.h"

#include <ArduinoJson.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BoardHardware.h"
#include "CalibrationService.h"
#include "ConfigStore.h"
#include "HistoryService.h"
#include "IrrigationConfig.h"
#include "PlanService.h"
#include "RunController.h"
#include "ZoneService.h"

namespace Irrigation {

namespace {

char g_historyViewBuffer[3072];

void sendIrrigationHeadExtra() {
    Esp32BaseWeb::sendChunk(
        "<style>"
        ":root{--ir-accent:#0f7b61;--ir-accent-strong:#08634e;--ir-soft:#edf7f3;--ir-ink:#1d2935;--ir-muted:#637083;--ir-line:#d9e2e7;--ir-bg:#f5f7f8;--ir-card:#fff}"
        "body{background:var(--ir-bg);color:var(--ir-ink)}"
        ".topbar{border-bottom:1px solid #dce5e8;background:rgba(255,255,255,.94);backdrop-filter:blur(10px)}"
        "nav a{border-radius:7px;font-weight:650}nav a.active{background:var(--ir-soft);color:var(--ir-accent-strong)}nav a.brand{font-size:0}nav a.brand:after{content:'首页';font-size:14px}"
        ".page{max-width:1180px}.pagehead{border:1px solid var(--ir-line);background:linear-gradient(135deg,#fff 0%,#f4faf7 100%);border-radius:8px;padding:16px 18px;box-shadow:0 1px 2px rgba(16,24,40,.04)}"
        ".pagehead h1{font-size:22px;letter-spacing:0}.pagehead p{font-size:13px;color:var(--ir-muted)}"
        ".panel{border:1px solid var(--ir-line);border-radius:8px;background:var(--ir-card);box-shadow:0 1px 2px rgba(16,24,40,.04)}"
        ".panel h2{font-size:16px;margin-bottom:10px}.metrics{gap:10px}.metric{border:1px solid var(--ir-line);border-radius:8px;background:#fff;padding:12px}.metric b{font-size:20px}"
        ".actions{gap:8px;justify-content:flex-end}button,input[type=submit],input[type=button],.btnlink{border-radius:7px;font-weight:700;min-height:34px;padding:0 14px}"
        "input:not([type]),input[type=text],input[type=number],input[type=time],select{border:1px solid #cbd6de;border-radius:7px;background:#fff;color:var(--ir-ink);min-height:36px;padding:6px 9px;box-sizing:border-box}"
        "input[type=number]{font-variant-numeric:tabular-nums}select{min-width:180px}.tablewrap{border:1px solid var(--ir-line);border-radius:8px;background:#fff}table{font-size:13px}th{color:var(--ir-muted);font-weight:700}"
        ".ir-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.ir-grid.ir-three{grid-template-columns:repeat(3,minmax(0,1fr))}"
        ".ir-card{border:1px solid var(--ir-line);border-radius:8px;background:#fff;padding:13px;min-width:0}.ir-card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-bottom:10px}.ir-card-title{font-size:15px;font-weight:800;color:var(--ir-ink)}.ir-card-note{margin:3px 0 0;color:var(--ir-muted);font-size:12px;line-height:1.45}"
        ".ir-empty{border:1px dashed #cbd6de;border-radius:8px;background:#fbfcfd;color:var(--ir-muted);padding:14px;text-align:center}"
        ".ir-form{display:block;max-width:none;margin:0}.ir-card.ir-form{display:block}.ir-section{margin:0 0 14px}.ir-section:last-child{margin-bottom:0}.ir-section-title{font-size:15px;font-weight:800;margin:0 0 2px}.ir-section-note{margin:0 0 8px;color:var(--ir-muted);font-size:12px}"
        ".ir-setting-list{display:grid;gap:8px}.ir-setting{display:grid;grid-template-columns:minmax(0,1fr) minmax(190px,260px);gap:12px;align-items:center;border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:11px 12px}.ir-setting-title{display:block;font-weight:800;color:var(--ir-ink)}.ir-setting-desc{display:block;margin-top:3px;color:var(--ir-muted);font-size:12px;line-height:1.45}.ir-control{display:flex;align-items:center;justify-content:flex-end;gap:7px;min-width:0}.ir-control input[type=number],.ir-control input[type=time],.ir-control input:not([type]),.ir-control input[type=text]{width:100%}.ir-unit{flex:0 0 auto;color:var(--ir-muted);font-size:12px;font-weight:700;white-space:nowrap}"
        ".ir-toggle{position:relative;display:inline-flex;align-items:center;cursor:pointer;user-select:none}.ir-toggle input{position:absolute;opacity:0;pointer-events:none}.ir-toggle span{position:relative;display:inline-flex;align-items:center;justify-content:flex-end;width:74px;height:32px;padding:0 10px;border-radius:999px;background:#eef2f4;color:#667085;font-size:12px;font-weight:800;transition:.16s;box-sizing:border-box}.ir-toggle span:before{content:'';position:absolute;left:4px;top:4px;width:24px;height:24px;border-radius:50%;background:#fff;box-shadow:0 1px 3px rgba(16,24,40,.24);transition:.16s}.ir-toggle span:after{content:'禁用'}.ir-toggle input:checked+span{justify-content:flex-start;background:#dff4e8;color:#087443}.ir-toggle input:checked+span:before{transform:translateX(42px)}.ir-toggle input:checked+span:after{content:'启用'}"
        ".ir-field{display:grid;gap:4px;margin:0 0 10px}.ir-field label{font-weight:800;margin:0}.ir-help{color:var(--ir-muted);font-size:12px;line-height:1.45}.ir-input-unit{display:flex;align-items:center;gap:7px}.ir-input-unit input{flex:1 1 auto;min-width:0}.ir-input-unit .ir-unit{padding-right:2px}"
        ".ir-run-list,.ir-zone-grid,.ir-plan-list{display:grid;gap:12px}.ir-run-row{display:grid;grid-template-columns:minmax(0,1fr) minmax(150px,220px);gap:10px;align-items:center;border:1px solid #e4ebef;border-radius:8px;padding:11px;background:#fbfcfd}.ir-run-row b{display:block}.ir-badge{display:inline-flex;align-items:center;min-height:22px;padding:0 8px;border-radius:999px;background:var(--ir-soft);color:var(--ir-accent-strong);font-size:12px;font-weight:800}"
        ".ir-time-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.ir-time-item{display:grid;grid-template-columns:auto minmax(0,1fr);gap:8px;align-items:center;border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:8px}.ir-time-item input[type=time]{width:100%}.ir-zone-duration{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.ir-danger-line{display:flex;justify-content:flex-end;margin-top:8px}"
        "@media(max-width:820px){.ir-grid,.ir-grid.ir-three,.ir-time-grid,.ir-zone-duration{grid-template-columns:1fr}.ir-setting,.ir-run-row{grid-template-columns:1fr}.ir-control{justify-content:flex-start}.actions{justify-content:flex-start}select{width:100%;min-width:0}}"
        "</style>");
}

const char* runStateName(RunState state) {
    switch (state) {
        case RunState::Idle: return "idle";
        case RunState::Precheck: return "precheck";
        case RunState::OpenValve: return "open_valve";
        case RunState::PumpSignalOn: return "pump_signal_on";
        case RunState::PumpStartDelay: return "pump_start_delay";
        case RunState::FlowGrace: return "flow_grace";
        case RunState::Running: return "running";
        case RunState::PumpSignalOff: return "pump_signal_off";
        case RunState::PumpStopDelay: return "pump_stop_delay";
        case RunState::CloseValve: return "close_valve";
        case RunState::AdvanceStep: return "advance_step";
        case RunState::Finished: return "finished";
    }
    return "unknown";
}

const char* runStateLabel(RunState state) {
    switch (state) {
        case RunState::Idle: return "空闲";
        case RunState::Precheck: return "运行前检查";
        case RunState::OpenValve: return "打开水路";
        case RunState::PumpSignalOn: return "启动自吸泵信号";
        case RunState::PumpStartDelay: return "自吸泵启动延时";
        case RunState::FlowGrace: return "等待流量稳定";
        case RunState::Running: return "浇水中";
        case RunState::PumpSignalOff: return "关闭自吸泵信号";
        case RunState::PumpStopDelay: return "自吸泵停止延时";
        case RunState::CloseValve: return "关闭水路";
        case RunState::AdvanceStep: return "切换下一水路";
        case RunState::Finished: return "已结束";
    }
    return "未知";
}

const char* runResultName(RunResult result) {
    switch (result) {
        case RunResult::None: return "none";
        case RunResult::Completed: return "completed";
        case RunResult::UserStopped: return "user_stopped";
        case RunResult::FaultStopped: return "fault_stopped";
        case RunResult::Skipped: return "skipped";
    }
    return "unknown";
}

const char* runReasonLabel(RunReason reason) {
    switch (reason) {
        case RunReason::None: return "无";
        case RunReason::ManualRequest: return "手动启动";
        case RunReason::PlanStartTime: return "计划时间启动";
        case RunReason::RunPlanNow: return "立即执行计划";
        case RunReason::CalibrationRequest: return "校准启动";
        case RunReason::UserStop: return "用户停止";
        case RunReason::NoEffectiveStep: return "没有有效浇水水路";
        case RunReason::ControllerBusy: return "控制器正在运行";
        case RunReason::PlanDisabled: return "计划未启用";
        case RunReason::ZoneDisabled: return "水路未启用";
        case RunReason::InvalidDuration: return "时长无效";
        case RunReason::ConfigInvalid: return "配置无效";
        case RunReason::FlowNotCalibrated: return "流量计未校准";
        case RunReason::TimeInvalid: return "系统时间无效";
        case RunReason::NoFlow: return "未检测到流量";
        case RunReason::LowLevel: return "低液位保护";
        case RunReason::RebootRecoveredSafe: return "重启后安全恢复";
    }
    return "未知原因";
}

const char* runSourceLabelFromString(const char* source) {
    if (source == nullptr) {
        return "未知";
    }
    if (strcmp(source, "manual") == 0) {
        return "手动浇水";
    }
    if (strcmp(source, "plan") == 0) {
        return "自动计划";
    }
    if (strcmp(source, "run_plan_now") == 0) {
        return "立即执行计划";
    }
    if (strcmp(source, "calibration") == 0) {
        return "校准";
    }
    return "未知";
}

const char* runResultLabelFromString(const char* result) {
    if (result == nullptr) {
        return "未知";
    }
    if (strcmp(result, "none") == 0) {
        return "无";
    }
    if (strcmp(result, "completed") == 0) {
        return "已完成";
    }
    if (strcmp(result, "user_stopped") == 0) {
        return "用户停止";
    }
    if (strcmp(result, "fault_stopped") == 0) {
        return "故障停止";
    }
    if (strcmp(result, "skipped") == 0) {
        return "已跳过";
    }
    return "未知";
}

const char* runReasonLabelFromString(const char* reason) {
    if (reason == nullptr) {
        return "未知";
    }
    if (strcmp(reason, "none") == 0) return "无";
    if (strcmp(reason, "manual_request") == 0) return "手动启动";
    if (strcmp(reason, "plan_start_time") == 0) return "计划时间启动";
    if (strcmp(reason, "run_plan_now") == 0) return "立即执行计划";
    if (strcmp(reason, "calibration_request") == 0) return "校准启动";
    if (strcmp(reason, "user_stop") == 0) return "用户停止";
    if (strcmp(reason, "no_effective_step") == 0) return "没有有效浇水水路";
    if (strcmp(reason, "controller_busy") == 0) return "控制器正在运行";
    if (strcmp(reason, "plan_disabled") == 0) return "计划未启用";
    if (strcmp(reason, "zone_disabled") == 0) return "水路未启用";
    if (strcmp(reason, "invalid_duration") == 0) return "时长无效";
    if (strcmp(reason, "config_invalid") == 0) return "配置无效";
    if (strcmp(reason, "flow_not_calibrated") == 0) return "流量计未校准";
    if (strcmp(reason, "time_invalid") == 0) return "系统时间无效";
    if (strcmp(reason, "no_flow") == 0) return "未检测到流量";
    if (strcmp(reason, "low_level") == 0) return "低液位保护";
    if (strcmp(reason, "reboot_recovered_safe") == 0) return "重启后安全恢复";
    return "未知原因";
}

const char* calibrationModeName(CalibrationMode mode) {
    switch (mode) {
        case CalibrationMode::None: return "none";
        case CalibrationMode::FlowMeterVolume: return "flow_meter_volume";
        case CalibrationMode::ZoneStandardFlow: return "zone_standard_flow";
    }
    return "unknown";
}

const char* calibrationModeLabel(CalibrationMode mode) {
    switch (mode) {
        case CalibrationMode::None: return "无";
        case CalibrationMode::FlowMeterVolume: return "流量计水量校准";
        case CalibrationMode::ZoneStandardFlow: return "水路标准流量校准";
    }
    return "未知";
}

void sendJsonStringField(const char* name, const char* value, bool comma = true) {
    Esp32BaseWeb::sendChunk("\"");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("\":\"");
    Esp32BaseWeb::writeJsonEscaped(value != nullptr ? value : "");
    Esp32BaseWeb::sendChunk(comma ? "\"," : "\"");
}

bool parseBoolParam(const char* name) {
    char value[8];
    if (!Esp32BaseWeb::getParam(name, value, sizeof(value))) {
        return false;
    }
    return strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "true") == 0;
}

bool parseU32Param(const char* name, uint32_t minValue, uint32_t maxValue, uint32_t fallback, uint32_t& out) {
    char value[16];
    if (!Esp32BaseWeb::getParam(name, value, sizeof(value)) || value[0] == '\0') {
        out = fallback;
        return true;
    }
    char* end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed < minValue || parsed > maxValue) {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

bool parseU8Param(const char* name, uint8_t minValue, uint8_t maxValue, uint8_t& out) {
    uint32_t parsed = 0;
    if (!parseU32Param(name, minValue, maxValue, 0, parsed)) {
        return false;
    }
    out = static_cast<uint8_t>(parsed);
    return true;
}

bool parseMinuteParam(const char* name, bool enabled, uint16_t& out) {
    char value[8];
    if (!Esp32BaseWeb::getParam(name, value, sizeof(value)) || value[0] == '\0') {
        out = kInvalidMinuteOfDay;
        return !enabled;
    }
    if (strlen(value) != 5 || value[2] != ':' ||
        value[0] < '0' || value[0] > '9' ||
        value[1] < '0' || value[1] > '9' ||
        value[3] < '0' || value[3] > '9' ||
        value[4] < '0' || value[4] > '9') {
        return false;
    }
    const uint8_t hour = static_cast<uint8_t>((value[0] - '0') * 10 + (value[1] - '0'));
    const uint8_t minute = static_cast<uint8_t>((value[3] - '0') * 10 + (value[4] - '0'));
    if (hour > 23 || minute > 59) {
        return false;
    }
    out = static_cast<uint16_t>(hour) * 60U + minute;
    return true;
}

void minuteToText(uint16_t minuteOfDay, char* out, size_t len) {
    if (!isValidMinuteOfDay(minuteOfDay)) {
        snprintf(out, len, "");
        return;
    }
    snprintf(out, len, "%02u:%02u", minuteOfDay / 60U, minuteOfDay % 60U);
}

void epochToText(uint32_t epoch, char* out, size_t len) {
    if (epoch == 0 || !Esp32BaseTime::formatEpoch(epoch, out, len, "%Y-%m-%d %H:%M")) {
        snprintf(out, len, "无");
    }
}

void sendEscapedValue(const char* value) {
    Esp32BaseWeb::writeHtmlEscaped(value != nullptr ? value : "");
}

void sendChecked(bool checked) {
    if (checked) {
        Esp32BaseWeb::sendChunk(" checked");
    }
}

void sendToggleInput(const char* name, bool checked) {
    Esp32BaseWeb::sendChunk("<label class='ir-toggle'><input type='checkbox' name='");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("' value='1'");
    sendChecked(checked);
    Esp32BaseWeb::sendChunk("><span></span></label>");
}

void sendTextInput(const char* name, const char* value, uint8_t maxLength) {
    char buf[96];
    snprintf(buf, sizeof(buf), "<input name='%s' maxlength='%u' value='", name, maxLength);
    Esp32BaseWeb::sendChunk(buf);
    sendEscapedValue(value);
    Esp32BaseWeb::sendChunk("'>");
}

void sendNumberInput(const char* name, uint32_t value, uint32_t minValue, uint32_t maxValue) {
    char buf[128];
    snprintf(buf, sizeof(buf), "<input type='number' name='%s' min='%lu' max='%lu' value='%lu'>",
             name,
             static_cast<unsigned long>(minValue),
             static_cast<unsigned long>(maxValue),
             static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(buf);
}

void sendSettingToggle(const char* title, const char* desc, const char* name, bool checked) {
    Esp32BaseWeb::sendChunk("<div class='ir-setting'><div><span class='ir-setting-title'>");
    Esp32BaseWeb::sendChunk(title);
    Esp32BaseWeb::sendChunk("</span><span class='ir-setting-desc'>");
    Esp32BaseWeb::sendChunk(desc);
    Esp32BaseWeb::sendChunk("</span></div><div class='ir-control'>");
    sendToggleInput(name, checked);
    Esp32BaseWeb::sendChunk("</div></div>");
}

void sendSettingNumber(const char* title,
                       const char* desc,
                       const char* unit,
                       const char* name,
                       uint32_t value,
                       uint32_t minValue,
                       uint32_t maxValue) {
    Esp32BaseWeb::sendChunk("<div class='ir-setting'><div><span class='ir-setting-title'>");
    Esp32BaseWeb::sendChunk(title);
    Esp32BaseWeb::sendChunk("</span><span class='ir-setting-desc'>");
    Esp32BaseWeb::sendChunk(desc);
    Esp32BaseWeb::sendChunk("</span></div><div class='ir-control'>");
    sendNumberInput(name, value, minValue, maxValue);
    Esp32BaseWeb::sendChunk("<span class='ir-unit'>");
    Esp32BaseWeb::sendChunk(unit);
    Esp32BaseWeb::sendChunk("</span></div></div>");
}

void sendSectionStart(const char* title, const char* note) {
    Esp32BaseWeb::sendChunk("<div class='ir-section'><h3 class='ir-section-title'>");
    Esp32BaseWeb::sendChunk(title);
    Esp32BaseWeb::sendChunk("</h3><p class='ir-section-note'>");
    Esp32BaseWeb::sendChunk(note);
    Esp32BaseWeb::sendChunk("</p><div class='ir-setting-list'>");
}

void sendSectionEnd() {
    Esp32BaseWeb::sendChunk("</div></div>");
}

uint32_t maxZoneDurationMinutes() {
    const uint32_t seconds = ConfigStore::config().valve.maxZoneDurationSec;
    const uint32_t minutes = (seconds + 59UL) / 60UL;
    return minutes > 0 ? minutes : 1;
}

void sendStatusJson() {
    const StatusSnapshot status = RunController::statusSnapshot();
    const WateringRun& run = RunController::currentRun();

    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("{");
    Esp32BaseWeb::sendChunk("\"configValid\":");
    Esp32BaseWeb::sendChunk(status.configValid ? "true," : "false,");
    Esp32BaseWeb::sendChunk("\"busy\":");
    Esp32BaseWeb::sendChunk(status.busy ? "true," : "false,");
    sendJsonStringField("runState", runStateName(status.runState));
    sendJsonStringField("runResult", runResultName(status.runResult));
    Esp32BaseWeb::sendChunk("\"activeZoneId\":");
    char number[16];
    snprintf(number, sizeof(number), "%u,", status.activeZoneId);
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("\"enabledZoneCount\":");
    snprintf(number, sizeof(number), "%u,", status.enabledZoneCount);
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("\"enabledPlanCount\":");
    snprintf(number, sizeof(number), "%u,", status.enabledPlanCount);
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("\"currentFlowMlPerMin\":");
    snprintf(number, sizeof(number), "%lu,", static_cast<unsigned long>(status.currentFlowMlPerMin));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("\"currentRunVolumeMl\":");
    snprintf(number, sizeof(number), "%lu,", static_cast<unsigned long>(status.currentRunVolumeMl));
    Esp32BaseWeb::sendChunk(number);
    Esp32BaseWeb::sendChunk("\"nextRunEpoch\":");
    snprintf(number, sizeof(number), "%lu,", static_cast<unsigned long>(status.nextRunEpoch));
    Esp32BaseWeb::sendChunk(number);
    sendJsonStringField("reason", runReasonToString(run.reason), false);
    Esp32BaseWeb::sendChunk("}");
    Esp32BaseWeb::endJson();
}

void handleStatusApi() {
    sendStatusJson();
}

void handleZonesApi() {
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("{\"zones\":[");
    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (i > 0) {
            Esp32BaseWeb::sendChunk(",");
        }
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"id\":%u,\"enabled\":%s,\"defaultDurationSec\":%lu,\"standardFlowMlPerMin\":%lu,\"name\":\"",
                 zone.id,
                 zone.enabled ? "true" : "false",
                 static_cast<unsigned long>(zone.defaultDurationSec),
                 static_cast<unsigned long>(zone.standardFlowMlPerMin));
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeJsonEscaped(zone.name);
        Esp32BaseWeb::sendChunk("\"}");
    }
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endJson();
}

void handlePlansApi() {
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("{\"plans\":[");
    const IrrigationConfig& config = ConfigStore::config();
    bool firstPlan = true;
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (!plan.used) {
            continue;
        }
        if (!firstPlan) {
            Esp32BaseWeb::sendChunk(",");
        }
        firstPlan = false;

        char buf[96];
        snprintf(buf, sizeof(buf), "{\"id\":%u,\"enabled\":%s,\"name\":\"", plan.id, plan.enabled ? "true" : "false");
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeJsonEscaped(plan.name);
        Esp32BaseWeb::sendChunk("\",\"startTimes\":[");
        bool firstStart = true;
        for (uint8_t j = 0; j < kMaxPlanStartTimes; ++j) {
            if (!plan.startTimes[j].enabled || !isValidMinuteOfDay(plan.startTimes[j].minuteOfDay)) {
                continue;
            }
            if (!firstStart) {
                Esp32BaseWeb::sendChunk(",");
            }
            firstStart = false;
            snprintf(buf, sizeof(buf), "%u", plan.startTimes[j].minuteOfDay);
            Esp32BaseWeb::sendChunk(buf);
        }
        Esp32BaseWeb::sendChunk("]}");
    }
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endJson();
}

void handleHistoryApi() {
    if (!HistoryService::readRecent(g_historyViewBuffer, sizeof(g_historyViewBuffer))) {
        Esp32BaseWeb::sendText(500, HistoryService::lastError());
        return;
    }
    Esp32BaseWeb::sendText(200, g_historyViewBuffer);
}

void handleCalibrationApi() {
    const CalibrationSnapshot snapshot = CalibrationService::snapshot();
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("{");
    sendJsonStringField("mode", calibrationModeName(snapshot.mode));
    Esp32BaseWeb::sendChunk("\"running\":");
    Esp32BaseWeb::sendChunk(snapshot.running ? "true," : "false,");
    Esp32BaseWeb::sendChunk("\"resultReady\":");
    Esp32BaseWeb::sendChunk(snapshot.resultReady ? "true," : "false,");
    char buf[96];
    snprintf(buf, sizeof(buf),
             "\"runId\":%lu,\"zoneId\":%u,\"durationSec\":%lu,\"pulses\":%lu,\"computedPulsesPerLiter\":%lu,\"suggestedFlowMlPerMin\":%lu}",
             static_cast<unsigned long>(snapshot.runId),
             snapshot.zoneId,
             static_cast<unsigned long>(snapshot.durationSec),
             static_cast<unsigned long>(snapshot.pulses),
             static_cast<unsigned long>(snapshot.computedPulsesPerLiter),
             static_cast<unsigned long>(snapshot.suggestedFlowMlPerMin));
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::endJson();
}

bool readDurationParam(uint8_t zoneId, uint32_t& out) {
    char key[8];
    snprintf(key, sizeof(key), "z%u", zoneId);
    uint32_t minutes = 0;
    if (!parseU32Param(key, 0, maxZoneDurationMinutes(), 0, minutes)) {
        return false;
    }
    out = minutes * 60UL;
    return true;
}

bool readZoneParam(uint8_t& zoneId) {
    return parseU8Param("zone", 1, kMaxZones, zoneId);
}

bool readDurationMinutesParam(uint32_t& durationSec) {
    uint32_t minutes = 0;
    if (!parseU32Param("durationMin", 1, maxZoneDurationMinutes(), 5, minutes)) {
        return false;
    }
    durationSec = minutes * 60UL;
    return true;
}

void handleManualStartPost() {
    if (!Esp32BaseWeb::checkPostAllowed("manual_start")) {
        return;
    }

    uint32_t durations[kMaxZones] = {};
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (!readDurationParam(i + 1, durations[i])) {
            Esp32BaseWeb::sendText(400, "浇水时长无效");
            return;
        }
    }

    RunReason reason = RunReason::None;
    if (!RunController::startManual(durations, reason)) {
        Esp32BaseWeb::sendText(409, runReasonLabel(reason));
        return;
    }

    Esp32BaseWeb::redirectSeeOther("/irrigation/run");
}

void handleCalibrationPost() {
    if (!Esp32BaseWeb::checkPostAllowed("calibration")) {
        return;
    }

    char action[24];
    if (!Esp32BaseWeb::getParam("action", action, sizeof(action))) {
        Esp32BaseWeb::sendText(400, "缺少操作");
        return;
    }

    if (strcmp(action, "start_volume") == 0 || strcmp(action, "start_standard") == 0) {
        uint8_t zoneId = 0;
        uint32_t durationSec = 0;
        if (!readZoneParam(zoneId) || !readDurationMinutesParam(durationSec)) {
            Esp32BaseWeb::sendText(400, "校准请求无效");
            return;
        }

        RunReason reason = RunReason::None;
        const bool ok = strcmp(action, "start_volume") == 0 ?
            CalibrationService::startVolumeCalibration(zoneId, durationSec, reason) :
            CalibrationService::startStandardFlowCalibration(zoneId, durationSec, reason);
        if (!ok) {
            Esp32BaseWeb::sendText(409, CalibrationService::lastError());
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/irrigation/calibration");
        return;
    }

    if (strcmp(action, "stop") == 0) {
        if (!CalibrationService::stop()) {
            Esp32BaseWeb::sendText(409, CalibrationService::lastError());
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/irrigation/calibration");
        return;
    }

    if (strcmp(action, "save_volume") == 0) {
        uint32_t measuredMl = 0;
        if (!parseU32Param("measuredMl", 1, 100000, 0, measuredMl)) {
            Esp32BaseWeb::sendText(400, "实测水量无效");
            return;
        }
        if (!CalibrationService::savePulsesPerLiter(measuredMl)) {
            Esp32BaseWeb::sendText(400, CalibrationService::lastError());
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/irrigation/calibration");
        return;
    }

    if (strcmp(action, "save_standard") == 0) {
        uint8_t zoneId = 0;
        uint32_t flow = 0;
        if (!readZoneParam(zoneId) || !parseU32Param("standardFlow", 1, 100000, 0, flow)) {
            Esp32BaseWeb::sendText(400, "标准流量无效");
            return;
        }
        if (!CalibrationService::saveZoneStandardFlow(zoneId, flow)) {
            Esp32BaseWeb::sendText(400, CalibrationService::lastError());
            return;
        }
        Esp32BaseWeb::redirectSeeOther("/irrigation/calibration");
        return;
    }

    if (strcmp(action, "clear") == 0) {
        CalibrationService::clearResult();
        Esp32BaseWeb::redirectSeeOther("/irrigation/calibration");
        return;
    }

    Esp32BaseWeb::sendText(400, "未知操作");
}

void handlePlanNowPost() {
    if (!Esp32BaseWeb::checkPostAllowed("plan_now")) {
        return;
    }
    char value[8];
    if (!Esp32BaseWeb::getParam("plan", value, sizeof(value))) {
        Esp32BaseWeb::sendText(400, "缺少计划");
        return;
    }
    uint8_t planId = 0;
    if (!parseU8Param("plan", 1, kMaxPlans, planId)) {
        Esp32BaseWeb::sendText(400, "计划无效");
        return;
    }
    RunReason reason = RunReason::None;
    if (!RunController::startPlanNow(planId, reason)) {
        Esp32BaseWeb::sendText(409, runReasonLabel(reason));
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation/run");
}

void handleStopPost() {
    if (!Esp32BaseWeb::checkPostAllowed("manual_stop")) {
        return;
    }
    RunController::stop();
    Esp32BaseWeb::redirectSeeOther("/irrigation/run");
}

void handleZoneSavePost() {
    if (!Esp32BaseWeb::checkPostAllowed("zone_save")) {
        return;
    }
    if (RunController::busy()) {
        Esp32BaseWeb::sendText(409, "运行中不能修改水路");
        return;
    }

    char value[32];
    uint8_t zoneId = 0;
    if (!parseU8Param("id", 1, kMaxZones, zoneId)) {
        Esp32BaseWeb::sendText(400, "水路编号无效");
        return;
    }
    const ZoneConfig* current = ZoneService::find(zoneId);
    if (current == nullptr) {
        Esp32BaseWeb::sendText(404, ZoneService::lastError());
        return;
    }

    ZoneConfig zone = *current;
    zone.enabled = parseBoolParam("enabled");
    if (Esp32BaseWeb::getParam("name", value, sizeof(value))) {
        snprintf(zone.name, sizeof(zone.name), "%s", value);
        zone.name[sizeof(zone.name) - 1] = '\0';
    }

    uint32_t minutes = 0;
    if (!parseU32Param("defaultMinutes", 0, maxZoneDurationMinutes(), zone.defaultDurationSec / 60UL, minutes)) {
        Esp32BaseWeb::sendText(400, "默认时长无效");
        return;
    }
    zone.defaultDurationSec = minutes * 60UL;

    uint32_t flow = 0;
    if (!parseU32Param("standardFlow", 0, 100000, zone.standardFlowMlPerMin, flow)) {
        Esp32BaseWeb::sendText(400, "标准流量无效");
        return;
    }
    zone.standardFlowMlPerMin = flow;

    if (!ZoneService::saveZone(zone)) {
        Esp32BaseWeb::sendText(400, ZoneService::lastError());
        return;
    }

    Esp32BaseWeb::redirectSeeOther("/irrigation/zones");
}

void handlePlanCreatePost() {
    if (!Esp32BaseWeb::checkPostAllowed("plan_create")) {
        return;
    }
    char name[kPlanNameLength] = "新计划";
    Esp32BaseWeb::getParam("name", name, sizeof(name));
    uint8_t planId = 0;
    if (!PlanService::createPlan(name, planId)) {
        Esp32BaseWeb::sendText(400, PlanService::lastError());
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation/plans");
}

void handlePlanDeletePost() {
    if (!Esp32BaseWeb::checkPostAllowed("plan_delete")) {
        return;
    }
    uint8_t planId = 0;
    if (!parseU8Param("id", 1, kMaxPlans, planId)) {
        Esp32BaseWeb::sendText(400, "计划编号无效");
        return;
    }
    if (!PlanService::deletePlan(planId)) {
        Esp32BaseWeb::sendText(400, PlanService::lastError());
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation/plans");
}

void handlePlanSavePost() {
    if (!Esp32BaseWeb::checkPostAllowed("plan_save")) {
        return;
    }

    char value[32];
    uint8_t planId = 0;
    if (!parseU8Param("id", 1, kMaxPlans, planId)) {
        Esp32BaseWeb::sendText(400, "计划编号无效");
        return;
    }
    const WateringPlan* current = PlanService::find(planId);
    if (current == nullptr) {
        Esp32BaseWeb::sendText(404, PlanService::lastError());
        return;
    }

    WateringPlan plan = *current;
    plan.enabled = parseBoolParam("enabled");
    if (Esp32BaseWeb::getParam("name", value, sizeof(value))) {
        snprintf(plan.name, sizeof(plan.name), "%s", value);
        plan.name[sizeof(plan.name) - 1] = '\0';
    }

    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "se%u", i);
        const bool enabled = parseBoolParam(key);
        snprintf(key, sizeof(key), "st%u", i);
        uint16_t minuteOfDay = kInvalidMinuteOfDay;
        if (!parseMinuteParam(key, enabled, minuteOfDay)) {
            Esp32BaseWeb::sendText(400, "启动时间无效");
            return;
        }
        plan.startTimes[i].enabled = enabled;
        plan.startTimes[i].minuteOfDay = minuteOfDay;
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "z%u", i + 1);
        uint32_t minutes = 0;
        if (!parseU32Param(key, 0, maxZoneDurationMinutes(), plan.zoneDurationSec[i] / 60UL, minutes)) {
            Esp32BaseWeb::sendText(400, "水路时长无效");
            return;
        }
        plan.zoneDurationSec[i] = minutes * 60UL;
    }

    if (!PlanService::savePlan(plan)) {
        Esp32BaseWeb::sendText(400, PlanService::lastError());
        return;
    }

    Esp32BaseWeb::redirectSeeOther("/irrigation/plans");
}

void handleSettingsSavePost() {
    if (!Esp32BaseWeb::checkPostAllowed("settings_save")) {
        return;
    }
    if (RunController::busy()) {
        Esp32BaseWeb::sendText(409, "运行中不能修改设置");
        return;
    }

    IrrigationConfig next = ConfigStore::config();
    next.supply.pumpEnabled = parseBoolParam("pumpEnabled");
    next.supply.lowLevelEnabled = parseBoolParam("lowLevelEnabled");
    next.supply.lowLevelContactType = parseBoolParam("lowLevelNormallyClosed") ? ContactType::NormallyClosed : ContactType::NormallyOpen;

    uint32_t value = 0;
    if (!parseU32Param("pumpStartDelayMs", 0, 10000, next.supply.pumpStartDelayMs, value)) {
        Esp32BaseWeb::sendText(400, "自吸泵启动延时无效");
        return;
    }
    next.supply.pumpStartDelayMs = value;

    if (!parseU32Param("pumpStopDelayMs", 0, 10000, next.supply.pumpStopDelayMs, value)) {
        Esp32BaseWeb::sendText(400, "自吸泵停止延时无效");
        return;
    }
    next.supply.pumpStopDelayMs = value;

    if (!parseU32Param("lowLevelDebounceMs", 0, 10000, next.supply.lowLevelDebounceMs, value)) {
        Esp32BaseWeb::sendText(400, "低液位消抖时间无效");
        return;
    }
    next.supply.lowLevelDebounceMs = value;

    if (!parseU32Param("pulsesPerLiter", 0, 100000, next.flow.pulsesPerLiter, value)) {
        Esp32BaseWeb::sendText(400, "每升脉冲数无效");
        return;
    }
    next.flow.pulsesPerLiter = value;

    if (!parseU32Param("startupGraceSec", 0, 120, next.flow.startupGraceSec, value)) {
        Esp32BaseWeb::sendText(400, "启动流量宽限时间无效");
        return;
    }
    next.flow.startupGraceSec = value;

    if (!parseU32Param("noFlowConfirmSec", 1, 600, next.flow.noFlowConfirmSec, value)) {
        Esp32BaseWeb::sendText(400, "无流量确认时间无效");
        return;
    }
    next.flow.noFlowConfirmSec = value;

    if (!parseU32Param("leakWindowSec", 1, 600, next.flow.leakWindowSec, value)) {
        Esp32BaseWeb::sendText(400, "待机漏水检测窗口无效");
        return;
    }
    next.flow.leakWindowSec = value;

    if (!parseU32Param("leakPulseThreshold", 1, 1000, next.flow.leakPulseThreshold, value)) {
        Esp32BaseWeb::sendText(400, "待机漏水脉冲阈值无效");
        return;
    }
    next.flow.leakPulseThreshold = value;

    if (!parseU32Param("lowFlowPercent", 1, 100, next.flow.lowFlowPercent, value)) {
        Esp32BaseWeb::sendText(400, "低流量百分比无效");
        return;
    }
    next.flow.lowFlowPercent = static_cast<uint8_t>(value);

    if (!parseU32Param("highFlowPercent", 100, 1000, next.flow.highFlowPercent, value)) {
        Esp32BaseWeb::sendText(400, "高流量百分比无效");
        return;
    }
    next.flow.highFlowPercent = static_cast<uint16_t>(value);

    if (!parseU32Param("pullInMs", 50, 3000, next.valve.pullInMs, value)) {
        Esp32BaseWeb::sendText(400, "电磁阀吸合时间无效");
        return;
    }
    next.valve.pullInMs = value;

    if (!parseU32Param("holdPercent", 1, 100, next.valve.holdPercent, value)) {
        Esp32BaseWeb::sendText(400, "电磁阀保持占空比无效");
        return;
    }
    next.valve.holdPercent = static_cast<uint8_t>(value);

    if (!parseU32Param("maxZoneDurationMin", 1, 360, next.valve.maxZoneDurationSec / 60UL, value)) {
        Esp32BaseWeb::sendText(400, "单路最大浇水时长无效");
        return;
    }
    next.valve.maxZoneDurationSec = value * 60UL;

    if (!ConfigStore::save(next)) {
        Esp32BaseWeb::sendText(400, ConfigStore::lastError());
        return;
    }
    BoardHardware::configure(ConfigStore::config().valve, ConfigStore::config().supply);
    Esp32BaseWeb::redirectSeeOther("/irrigation/settings");
}

void sendRunPanel() {
    Esp32BaseWeb::beginPanel("手动浇水");
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/run/start'><div class='ir-run-list'>");
    const IrrigationConfig& config = ConfigStore::config();
    const uint32_t maxMinutes = maxZoneDurationMinutes();
    bool any = false;
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (!zone.enabled) {
            continue;
        }
        any = true;
        char row[224];
        snprintf(row, sizeof(row), "<div class='ir-run-row'><div><b>水路 %u ", zone.id);
        Esp32BaseWeb::sendChunk(row);
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</b><span class='ir-help'>本次手动任务会按水路编号顺序执行，填 0 表示跳过。</span></div><div class='ir-input-unit'>");
        char key[8];
        snprintf(key, sizeof(key), "z%u", zone.id);
        sendNumberInput(key, zone.defaultDurationSec / 60UL, 0, maxMinutes);
        Esp32BaseWeb::sendChunk("<span class='ir-unit'>分钟</span></div></div>");
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<div class='ir-empty'>还没有启用水路，请先到水路页面启用实际接线的水路。</div>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='submit' value='启动手动浇水'");
    if (!any) {
        Esp32BaseWeb::sendChunk(" disabled");
    }
    Esp32BaseWeb::sendChunk("></div></form>");
    Esp32BaseWeb::endPanel();
}

void sendPlanNowPanel() {
    const IrrigationConfig& config = ConfigStore::config();
    bool hasPlan = false;
    const bool flowReady = config.flow.pulsesPerLiter > 0;
    Esp32BaseWeb::beginPanel("立即执行计划");
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/run/plan-now'><div class='ir-field'><label>选择计划</label><span class='ir-help'>立即按所选计划的水路时长顺序执行，不等待计划启动时间。</span><select name='plan'>");
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (!plan.used || !plan.enabled) {
            continue;
        }
        hasPlan = true;
        char buf[80];
        snprintf(buf, sizeof(buf), "<option value='%u'>", plan.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeHtmlEscaped(plan.name);
        Esp32BaseWeb::sendChunk("</option>");
    }
    Esp32BaseWeb::sendChunk("</select></div><div class='actions'><input type='submit' value='立即执行所选计划'");
    if (!hasPlan || !flowReady) {
        Esp32BaseWeb::sendChunk(" disabled");
    }
    Esp32BaseWeb::sendChunk("></div></form>");
    if (!hasPlan) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "没有已启用的计划", "先启用一个计划，才能使用立即执行。");
    } else if (!flowReady) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "流量计未校准", "执行自动计划前需要先设置每升脉冲数。");
    }
    Esp32BaseWeb::endPanel();
}

void handleDashboardPage() {
    const StatusSnapshot status = RunController::statusSnapshot();
    char value[32];

    Esp32BaseWeb::sendHeader("智能浇水");
    Esp32BaseWeb::sendPageTitle("智能浇水", "本地 12V 六路浇水控制器");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("运行状态", runStateLabel(status.runState), status.busy ? "正在运行" : "空闲");
    snprintf(value, sizeof(value), "%u / %u", status.enabledZoneCount, kMaxZones);
    Esp32BaseWeb::sendMetric("已启用水路", value, "硬件最多 6 路");
    snprintf(value, sizeof(value), "%u / %u", status.enabledPlanCount, kMaxPlans);
    Esp32BaseWeb::sendMetric("已启用计划", value, "每日计划");
    epochToText(status.nextRunEpoch, value, sizeof(value));
    Esp32BaseWeb::sendMetric("下次运行", value, status.nextRunEpoch == 0 ? "没有已启用的计划" : "本地时间");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::beginPanel("常用操作");
    Esp32BaseWeb::sendInfoRowCompactLink("手动浇水", "为已启用水路分别设置时长，然后按顺序浇水。", nullptr, "/irrigation/run", "打开", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("浇水计划", "每日计划，可启用、禁用，并支持多个启动时间。", nullptr, "/irrigation/plans", "打开", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("水路配置", "启用实际接线的水路，设置名称和默认时长。", nullptr, "/irrigation/zones", "打开", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("流量校准", "校准流量计脉冲数和各水路标准流量。", nullptr, "/irrigation/calibration", "打开", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("系统设置", "配置水源、流量保护和电磁阀驱动参数。", nullptr, "/irrigation/settings", "打开", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("运行历史", "查看最近浇水记录和停止原因。", nullptr, "/irrigation/history", "打开", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("状态接口", "查看机器可读的当前状态。", nullptr, "/api/status", "打开", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleRunPage() {
    const StatusSnapshot status = RunController::statusSnapshot();
    Esp32BaseWeb::sendHeader("运行");
    Esp32BaseWeb::sendPageTitle("运行", "手动顺序浇水和立即执行计划");
    char value[32];
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("运行状态", runStateLabel(status.runState), status.busy ? "正在运行" : "空闲");
    snprintf(value, sizeof(value), "%u", status.activeZoneId);
    Esp32BaseWeb::sendMetric("当前水路", value, status.activeZoneId == 0 ? "无" : "已打开");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(status.currentFlowMlPerMin));
    Esp32BaseWeb::sendMetric("流量 ml/min", value, "估算值");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(status.currentRunVolumeMl));
    Esp32BaseWeb::sendMetric("累计水量 ml", value, "当前步骤");
    Esp32BaseWeb::endMetricGrid();
    if (status.busy) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "正在运行", runStateLabel(status.runState));
        Esp32BaseWeb::beginPanel("停止运行");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run/stop'><div class='actions'><input class='danger' type='submit' value='停止浇水'></div></form>");
        Esp32BaseWeb::endPanel();
    } else {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "空闲", "当前没有正在执行的浇水任务。");
    }
    sendRunPanel();
    sendPlanNowPanel();
    Esp32BaseWeb::sendFooter();
}

void handleZonesPage() {
    Esp32BaseWeb::sendHeader("水路");
    Esp32BaseWeb::sendPageTitle("水路", "只启用实际接线的固定水路");
    Esp32BaseWeb::beginPanel("水路配置");

    const IrrigationConfig& config = ConfigStore::config();
    const uint32_t maxMinutes = maxZoneDurationMinutes();
    Esp32BaseWeb::sendChunk("<div class='ir-zone-grid ir-grid'>");
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        char buf[512];
        snprintf(buf, sizeof(buf), "<form class='ir-card ir-form' method='post' action='/irrigation/zones/save'><input type='hidden' name='id' value='%u'>", zone.id);
        Esp32BaseWeb::sendChunk(buf);
        snprintf(buf, sizeof(buf), "<div class='ir-card-head'><div><div class='ir-card-title'>水路 %u</div><p class='ir-card-note'>固定输出端口，只启用实际接线的水路。</p></div>", zone.id);
        Esp32BaseWeb::sendChunk(buf);
        sendToggleInput("enabled", zone.enabled);
        Esp32BaseWeb::sendChunk("</div>");
        Esp32BaseWeb::sendChunk("<div class='ir-field'><label>名称</label><span class='ir-help'>用于页面、计划和运行历史展示。</span>");
        sendTextInput("name", zone.name, sizeof(zone.name) - 1);
        Esp32BaseWeb::sendChunk("</div><div class='ir-field'><label>默认时长</label><span class='ir-help'>手动浇水页面默认带出的时长，自动计划可单独设置。</span><div class='ir-input-unit'>");
        sendNumberInput("defaultMinutes", zone.defaultDurationSec / 60UL, 0, maxMinutes);
        Esp32BaseWeb::sendChunk("<span class='ir-unit'>分钟</span></div></div><div class='ir-field'><label>标准流量</label><span class='ir-help'>用于低流量和高流量判断，校准后会自动写入。</span><div class='ir-input-unit'>");
        sendNumberInput("standardFlow", zone.standardFlowMlPerMin, 0, 100000);
        Esp32BaseWeb::sendChunk("<span class='ir-unit'>ml/min</span></div></div><div class='actions'><input type='submit' value='保存水路'></div></form>");
    }
    Esp32BaseWeb::sendChunk("</div>");

    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void sendPlanStartFields(const WateringPlan& plan) {
    Esp32BaseWeb::sendChunk("<div class='ir-section'><h3 class='ir-section-title'>启动时间</h3><p class='ir-section-note'>同一个计划可设置多个每日启动时间，水路时长保持一致。</p><div class='ir-time-grid'>");
    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        char timeText[8];
        minuteToText(plan.startTimes[i].minuteOfDay, timeText, sizeof(timeText));
        char buf[96];
        Esp32BaseWeb::sendChunk("<div class='ir-time-item'>");
        snprintf(buf, sizeof(buf), "se%u", i);
        sendToggleInput(buf, plan.startTimes[i].enabled);
        snprintf(buf, sizeof(buf), "<div class='ir-field'><label>时间 %u</label><input type='time' name='st%u' value='", i + 1, i);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::sendChunk(timeText);
        Esp32BaseWeb::sendChunk("'></div></div>");
    }
    Esp32BaseWeb::sendChunk("</div></div>");
}

void sendPlanZoneDurationFields(const WateringPlan& plan) {
    Esp32BaseWeb::sendChunk("<div class='ir-section'><h3 class='ir-section-title'>已启用水路时长</h3><p class='ir-section-note'>每个启用水路填写本计划执行时长，填 0 表示该计划跳过该水路。</p><div class='ir-zone-duration'>");
    const IrrigationConfig& config = ConfigStore::config();
    const uint32_t maxMinutes = maxZoneDurationMinutes();
    bool any = false;
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (!zone.enabled) {
            continue;
        }
        any = true;
        char buf[128];
        snprintf(buf, sizeof(buf), "<div class='ir-field'><label>水路 %u ", zone.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</label><div class='ir-input-unit'>");
        char key[8];
        snprintf(key, sizeof(key), "z%u", zone.id);
        sendNumberInput(key, plan.zoneDurationSec[i] / 60UL, 0, maxMinutes);
        Esp32BaseWeb::sendChunk("<span class='ir-unit'>分钟</span></div></div>");
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<div class='ir-empty'>还没有启用水路，计划保存后也不会执行有效浇水步骤。</div>");
    }
    Esp32BaseWeb::sendChunk("</div></div>");
}

void handlePlansPage() {
    Esp32BaseWeb::sendHeader("计划");
    Esp32BaseWeb::sendPageTitle("计划", "每日计划，最多 8 个计划，每个计划最多 4 个启动时间");

    Esp32BaseWeb::beginPanel("新建计划");
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/plans/create'><div class='ir-field'><label>计划名称</label><span class='ir-help'>创建后默认不启用，确认启动时间和水路时长后再打开。</span>");
    sendTextInput("name", "新计划", kPlanNameLength - 1);
    Esp32BaseWeb::sendChunk("</div><div class='actions'><input type='submit' value='创建未启用计划'></div></form>");
    Esp32BaseWeb::endPanel();

    const IrrigationConfig& config = ConfigStore::config();
    Esp32BaseWeb::sendChunk("<div class='ir-plan-list'>");
    bool anyPlan = false;
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (!plan.used) {
            continue;
        }
        anyPlan = true;
        Esp32BaseWeb::sendChunk("<section class='panel'>");
        char buf[512];
        snprintf(buf, sizeof(buf), "<form class='ir-form' method='post' action='/irrigation/plans/save'><input type='hidden' name='id' value='%u'><div class='ir-card-head'><div><div class='ir-card-title'>",
                 plan.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeHtmlEscaped(plan.name);
        Esp32BaseWeb::sendChunk("</div><p class='ir-card-note'>每日计划，可启用、禁用，也可在运行页立即执行。</p></div>");
        sendToggleInput("enabled", plan.enabled);
        Esp32BaseWeb::sendChunk("</div><div class='ir-field'><label>名称</label><span class='ir-help'>用于区分不同季节、上午/下午或不同浇水策略。</span>");
        sendTextInput("name", plan.name, sizeof(plan.name) - 1);
        Esp32BaseWeb::sendChunk("</div>");
        sendPlanStartFields(plan);
        sendPlanZoneDurationFields(plan);
        Esp32BaseWeb::sendChunk("<div class='actions'><input type='submit' value='保存计划'></div></form>");
        snprintf(buf, sizeof(buf), "<form method='post' action='/irrigation/plans/delete' onsubmit=\"return confirm('确认删除这个计划？')\"><input type='hidden' name='id' value='%u'><div class='ir-danger-line'><input class='danger' type='submit' value='删除计划'></div></form>",
                 plan.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::sendChunk("</section>");
    }
    if (!anyPlan) {
        Esp32BaseWeb::sendChunk("<div class='ir-empty'>还没有计划。先创建一个计划，再配置启动时间和各水路时长。</div>");
    }
    Esp32BaseWeb::sendChunk("</div>");

    Esp32BaseWeb::sendFooter();
}

void handleSettingsPage() {
    const IrrigationConfig& config = ConfigStore::config();
    Esp32BaseWeb::sendHeader("设置");
    Esp32BaseWeb::sendPageTitle("设置", "水源、流量保护和电磁阀驱动");
    if (RunController::busy()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "设置已锁定", "请先停止当前浇水任务，再保存全局设置。");
    }

    Esp32BaseWeb::beginPanel("全局设置");
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/settings/save'>");

    sendSectionStart("水源与自吸泵", "自吸泵是可选能力；低液位保护只在启用自吸泵时参与防干抽。");
    sendSettingToggle("自吸泵联动",
                      "启用后，浇水运行时输出干接点控制信号给外部自吸泵继电器模块。",
                      "pumpEnabled",
                      config.supply.pumpEnabled);
    sendSettingNumber("自吸泵启动延时",
                      "打开水路和自吸泵控制信号后，等待水压建立再进入流量判断。",
                      "ms",
                      "pumpStartDelayMs",
                      config.supply.pumpStartDelayMs,
                      0,
                      10000);
    sendSettingNumber("自吸泵停止延时",
                      "浇水结束时保留的停止缓冲时间，用于降低频繁切换带来的冲击。",
                      "ms",
                      "pumpStopDelayMs",
                      config.supply.pumpStopDelayMs,
                      0,
                      10000);
    sendSettingToggle("低液位保护",
                      "启用后，当低液位输入触发时停止自吸泵，避免水箱缺水干抽。",
                      "lowLevelEnabled",
                      config.supply.lowLevelEnabled);
    sendSettingToggle("低液位触点类型",
                      "启用表示常闭触点，禁用表示常开触点；按实际缺水传感器接线选择。",
                      "lowLevelNormallyClosed",
                      config.supply.lowLevelContactType == ContactType::NormallyClosed);
    sendSettingNumber("低液位消抖时间",
                      "输入信号需要持续稳定这么久才会被认为有效，避免触点抖动误触发。",
                      "ms",
                      "lowLevelDebounceMs",
                      config.supply.lowLevelDebounceMs,
                      0,
                      10000);
    sendSectionEnd();

    sendSectionStart("流量检测", "流量计为必配输入，用于校准、无流量保护、漏水检测和异常流量判断。");
    sendSettingNumber("流量计每升脉冲数",
                      "流量计每流过 1 升水产生的脉冲数量；未校准时自动计划不能执行。",
                      "脉冲/L",
                      "pulsesPerLiter",
                      config.flow.pulsesPerLiter,
                      0,
                      100000);
    sendSettingNumber("启动流量宽限时间",
                      "水路刚打开后暂不判断无流量，给管路充水和流量稳定留出时间。",
                      "秒",
                      "startupGraceSec",
                      config.flow.startupGraceSec,
                      0,
                      120);
    sendSettingNumber("无流量确认时间",
                      "超过宽限期后，连续这么久没有有效流量才停止运行。",
                      "秒",
                      "noFlowConfirmSec",
                      config.flow.noFlowConfirmSec,
                      1,
                      600);
    sendSettingNumber("待机漏水检测窗口",
                      "系统空闲时累计流量脉冲的观察窗口，用于发现疑似漏水。",
                      "秒",
                      "leakWindowSec",
                      config.flow.leakWindowSec,
                      1,
                      600);
    sendSettingNumber("待机漏水脉冲阈值",
                      "空闲观察窗口内达到该脉冲数，即认为存在异常流动。",
                      "脉冲",
                      "leakPulseThreshold",
                      config.flow.leakPulseThreshold,
                      1,
                      1000);
    sendSettingNumber("低流量阈值",
                      "运行流量低于水路标准流量的该比例时，判定为低流量异常。",
                      "%",
                      "lowFlowPercent",
                      config.flow.lowFlowPercent,
                      1,
                      100);
    sendSettingNumber("高流量阈值",
                      "运行流量高于水路标准流量的该比例时，判定为高流量异常。",
                      "%",
                      "highFlowPercent",
                      config.flow.highFlowPercent,
                      100,
                      1000);
    sendSectionEnd();

    sendSectionStart("电磁阀驱动", "6 路 12V DC 电磁阀为固定输出，驱动参数影响吸合可靠性和发热。");
    sendSettingNumber("电磁阀吸合时间",
                      "打开电磁阀时全功率吸合的持续时间，过短可能打不开，过长会增加发热。",
                      "ms",
                      "pullInMs",
                      config.valve.pullInMs,
                      50,
                      3000);
    sendSettingNumber("电磁阀保持占空比",
                      "电磁阀吸合后使用的保持功率比例，用于降低线圈发热。",
                      "%",
                      "holdPercent",
                      config.valve.holdPercent,
                      1,
                      100);
    sendSettingNumber("单路最大时长",
                      "任何手动任务或计划中，单个水路允许设置的最大运行时间。",
                      "分钟",
                      "maxZoneDurationMin",
                      config.valve.maxZoneDurationSec / 60UL,
                      1,
                      360);
    sendSectionEnd();

    Esp32BaseWeb::sendChunk("<div class='actions'><input type='submit' value='保存设置'");
    if (RunController::busy()) {
        Esp32BaseWeb::sendChunk(" disabled");
    }
    Esp32BaseWeb::sendChunk("></div></form>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void sendEnabledZoneSelect(uint8_t selectedZoneId) {
    Esp32BaseWeb::sendChunk("<select name='zone'>");
    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (!zone.enabled) {
            continue;
        }
        char buf[80];
        snprintf(buf, sizeof(buf), "<option value='%u'%s>水路 %u ",
                 zone.id,
                 zone.id == selectedZoneId ? " selected" : "",
                 zone.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</option>");
    }
    Esp32BaseWeb::sendChunk("</select>");
}

void sendCalibrationStartForm(const char* title, const char* action, uint32_t defaultMinutes) {
    Esp32BaseWeb::beginPanel(title);
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='");
    Esp32BaseWeb::sendChunk(action);
    Esp32BaseWeb::sendChunk("'><div class='ir-grid'><div class='ir-field'><label>水路</label><span class='ir-help'>选择一个已启用水路进行校准。</span>");
    sendEnabledZoneSelect(1);
    Esp32BaseWeb::sendChunk("</div><div class='ir-field'><label>运行时长</label><span class='ir-help'>校准会打开所选水路并累计流量计脉冲。</span><div class='ir-input-unit'>");
    sendNumberInput("durationMin", defaultMinutes, 1, maxZoneDurationMinutes());
    Esp32BaseWeb::sendChunk("<span class='ir-unit'>分钟</span></div></div></div><div class='actions'><input type='submit' value='启动校准'></div></form>");
    Esp32BaseWeb::endPanel();
}

void sendCalibrationResultPanel(const CalibrationSnapshot& snapshot) {
    if (!snapshot.resultReady) {
        return;
    }

    char buf[768];
    Esp32BaseWeb::beginPanel("校准结果");
    snprintf(buf, sizeof(buf), "<div class='ir-grid ir-three'><div class='ir-card'><div class='ir-card-title'>模式</div><p class='ir-card-note'>%s</p></div><div class='ir-card'><div class='ir-card-title'>水路</div><p class='ir-card-note'>%u</p></div><div class='ir-card'><div class='ir-card-title'>脉冲数</div><p class='ir-card-note'>%lu</p></div></div>",
             calibrationModeLabel(snapshot.mode),
             snapshot.zoneId,
             static_cast<unsigned long>(snapshot.pulses));
    Esp32BaseWeb::sendChunk(buf);

    if (snapshot.mode == CalibrationMode::FlowMeterVolume) {
        Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='save_volume'><div class='ir-field'><label>实测水量</label><span class='ir-help'>填写本次实际接到的水量，系统据此计算每升脉冲数。</span><div class='ir-input-unit'><input type='number' name='measuredMl' min='1' max='100000' value='1000'><span class='ir-unit'>ml</span></div></div><div class='actions'><input type='submit' value='保存每升脉冲数'></div></form>");
    } else if (snapshot.mode == CalibrationMode::ZoneStandardFlow) {
        snprintf(buf, sizeof(buf), "<form class='ir-form' method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='save_standard'><input type='hidden' name='zone' value='%u'><div class='ir-field'><label>标准流量</label><span class='ir-help'>保存后用于该水路的低流量和高流量判断。</span><div class='ir-input-unit'><input type='number' name='standardFlow' min='1' max='100000' value='%lu'><span class='ir-unit'>ml/min</span></div></div><div class='actions'><input type='submit' value='保存水路标准流量'></div></form>",
                 snapshot.zoneId,
                 static_cast<unsigned long>(snapshot.suggestedFlowMlPerMin));
        Esp32BaseWeb::sendChunk(buf);
        if (snapshot.suggestedFlowMlPerMin == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "需要手动填写", "需要一次已完成的运行记录和每升脉冲数，才能自动建议标准流量。");
        }
    }

    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='clear'><div class='actions'><input type='submit' value='丢弃结果'></div></form>");
    Esp32BaseWeb::endPanel();
}

void handleCalibrationPage() {
    const CalibrationSnapshot snapshot = CalibrationService::snapshot();
    const IrrigationConfig& config = ConfigStore::config();

    Esp32BaseWeb::sendHeader("校准");
    Esp32BaseWeb::sendPageTitle("校准", "流量计和水路标准流量");

    Esp32BaseWeb::beginMetricGrid();
    char value[32];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(config.flow.pulsesPerLiter));
    Esp32BaseWeb::sendMetric("每升脉冲数", value, config.flow.pulsesPerLiter == 0 ? "未校准" : "已保存");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.pulses));
    Esp32BaseWeb::sendMetric("当前脉冲数", value, calibrationModeLabel(snapshot.mode));
    snprintf(value, sizeof(value), "%u", snapshot.zoneId);
    Esp32BaseWeb::sendMetric("校准水路", value, snapshot.running ? "正在运行" : "空闲");
    Esp32BaseWeb::endMetricGrid();

    if (snapshot.running) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "正在校准", "接够水后可以手动停止，也可以等待设定时长结束。");
        Esp32BaseWeb::beginPanel("停止校准");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='stop'><div class='actions'><input class='danger' type='submit' value='停止校准'></div></form>");
        Esp32BaseWeb::endPanel();
    } else {
        sendCalibrationResultPanel(snapshot);
        sendCalibrationStartForm("流量计水量校准", "start_volume", 5);
        sendCalibrationStartForm("水路标准流量校准", "start_standard", 2);
    }

    Esp32BaseWeb::sendFooter();
}

void sendHistoryTable(char* historyText) {
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table><thead><tr><th>编号</th><th>来源</th><th>结果</th><th>原因</th><th>开始时间</th><th>结束时间</th><th>步骤数</th></tr></thead><tbody>");

    bool any = false;
    char* save = nullptr;
    for (char* line = strtok_r(historyText, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
        while (*line == ' ' || *line == '\r' || *line == '\t') {
            ++line;
        }
        if (*line != '{') {
            continue;
        }

        StaticJsonDocument<1536> doc;
        if (deserializeJson(doc, line) != DeserializationError::Ok) {
            continue;
        }

        char started[24];
        char finished[24];
        epochToText(doc["startedAt"] | 0, started, sizeof(started));
        epochToText(doc["finishedAt"] | 0, finished, sizeof(finished));

        char row[448];
        snprintf(row,
                 sizeof(row),
                 "<tr><td>%lu</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%u</td></tr>",
                 static_cast<unsigned long>(doc["id"] | 0),
                 runSourceLabelFromString(doc["source"] | ""),
                 runResultLabelFromString(doc["result"] | ""),
                 runReasonLabelFromString(doc["reason"] | ""),
                 started,
                 finished,
                 static_cast<unsigned>(doc["stepCount"] | 0));
        Esp32BaseWeb::sendChunk(row);
        any = true;
    }

    if (!any) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>暂无可显示的历史记录</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
}

void handleHistoryPage() {
    Esp32BaseWeb::sendHeader("历史");
    Esp32BaseWeb::sendPageTitle("历史", "最近运行记录");
    Esp32BaseWeb::beginPanel("最近运行");
    if (!HistoryService::readRecent(g_historyViewBuffer, sizeof(g_historyViewBuffer))) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "历史不可用", "读取运行历史失败。");
    } else if (g_historyViewBuffer[0] == '\0') {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "暂无运行历史", "浇水完成、停止或故障后，会在这里显示运行记录。");
    } else {
        sendHistoryTable(g_historyViewBuffer);
    }
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

} // namespace

void IrrigationWeb::registerRoutes() {
    Esp32BaseWeb::setHeadExtraCallback(sendIrrigationHeadExtra);
    Esp32BaseWeb::addPage("/irrigation", "首页", handleDashboardPage);
    Esp32BaseWeb::addPage("/irrigation/run", "运行", handleRunPage);
    Esp32BaseWeb::addPage("/irrigation/zones", "水路", handleZonesPage);
    Esp32BaseWeb::addPage("/irrigation/plans", "计划", handlePlansPage);
    Esp32BaseWeb::addPage("/irrigation/calibration", "校准", handleCalibrationPage);
    Esp32BaseWeb::addPage("/irrigation/settings", "设置", handleSettingsPage);
    Esp32BaseWeb::addPage("/irrigation/history", "历史", handleHistoryPage);
    Esp32BaseWeb::addRoute("/irrigation/run/start", Esp32BaseWeb::METHOD_POST, handleManualStartPost);
    Esp32BaseWeb::addRoute("/irrigation/run/plan-now", Esp32BaseWeb::METHOD_POST, handlePlanNowPost);
    Esp32BaseWeb::addRoute("/irrigation/run/stop", Esp32BaseWeb::METHOD_POST, handleStopPost);
    Esp32BaseWeb::addRoute("/irrigation/zones/save", Esp32BaseWeb::METHOD_POST, handleZoneSavePost);
    Esp32BaseWeb::addRoute("/irrigation/plans/create", Esp32BaseWeb::METHOD_POST, handlePlanCreatePost);
    Esp32BaseWeb::addRoute("/irrigation/plans/save", Esp32BaseWeb::METHOD_POST, handlePlanSavePost);
    Esp32BaseWeb::addRoute("/irrigation/plans/delete", Esp32BaseWeb::METHOD_POST, handlePlanDeletePost);
    Esp32BaseWeb::addRoute("/irrigation/calibration/action", Esp32BaseWeb::METHOD_POST, handleCalibrationPost);
    Esp32BaseWeb::addRoute("/irrigation/settings/save", Esp32BaseWeb::METHOD_POST, handleSettingsSavePost);
    Esp32BaseWeb::addApi("/api/status", handleStatusApi);
    Esp32BaseWeb::addApi("/api/run/current", handleStatusApi);
    Esp32BaseWeb::addApi("/api/zones", handleZonesApi);
    Esp32BaseWeb::addApi("/api/plans", handlePlansApi);
    Esp32BaseWeb::addApi("/api/calibration", handleCalibrationApi);
    Esp32BaseWeb::addApi("/api/history", handleHistoryApi);
    Esp32BaseWeb::addNavItem("/irrigation", "首页");
}

} // namespace Irrigation
