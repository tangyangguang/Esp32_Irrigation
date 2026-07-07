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

char g_historyViewBuffer[24576];

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
        ".ir-form{display:block;max-width:none;margin:0}.ir-card.ir-form{display:block}.ir-section{margin:0 0 14px;border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:13px}.ir-section:last-child{margin-bottom:0}.ir-section-title{font-size:15px;font-weight:800;margin:0 0 2px}.ir-section-note{margin:0 0 12px;color:var(--ir-muted);font-size:12px}"
        ".ir-setting-list{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px}.ir-setting{border:1px solid #e4ebef;border-radius:8px;background:#fff;padding:12px;min-width:0}.ir-setting-title{display:block;font-weight:800;color:var(--ir-ink)}.ir-setting-desc{display:block;margin-top:4px;color:var(--ir-muted);font-size:12px;line-height:1.45}.ir-control{display:flex;align-items:center;justify-content:flex-start;gap:7px;min-width:0;margin-top:10px}.ir-control input[type=number],.ir-control input[type=time],.ir-control input:not([type]),.ir-control input[type=text]{width:100%;max-width:210px}.ir-unit{flex:0 0 auto;color:var(--ir-muted);font-size:12px;font-weight:700;white-space:nowrap}"
        ".ir-toggle{position:relative;display:inline-flex;align-items:center;cursor:pointer;user-select:none}.ir-toggle input{position:absolute;opacity:0;pointer-events:none}.ir-toggle span{position:relative;display:inline-flex;align-items:center;justify-content:flex-end;width:74px;height:32px;padding:0 10px;border-radius:999px;background:#eef2f4;color:#667085;font-size:12px;font-weight:800;transition:.16s;box-sizing:border-box}.ir-toggle span:before{content:'';position:absolute;left:4px;top:4px;width:24px;height:24px;border-radius:50%;background:#fff;box-shadow:0 1px 3px rgba(16,24,40,.24);transition:.16s}.ir-toggle span:after{content:'禁用'}.ir-toggle input:checked+span{justify-content:flex-start;background:#dff4e8;color:#087443}.ir-toggle input:checked+span:before{transform:translateX(42px)}.ir-toggle input:checked+span:after{content:'启用'}"
        ".ir-choice{display:inline-grid;grid-auto-flow:column;grid-auto-columns:1fr;align-items:center;height:40px;box-sizing:border-box;border:1px solid #cbd6de;border-radius:8px;background:#f4f7f8;padding:3px;gap:3px;min-width:176px;vertical-align:middle}.ir-choice label{position:relative;display:flex;align-items:center;justify-content:center;height:32px;box-sizing:border-box;margin:0;padding:0 12px;border-radius:6px;font-size:12px;font-weight:800;line-height:1;color:#667085;cursor:pointer;white-space:nowrap}.ir-choice input{position:absolute;opacity:0;pointer-events:none}.ir-choice label:has(input:checked){background:#fff;color:var(--ir-accent-strong);box-shadow:0 1px 2px rgba(16,24,40,.12)}"
        ".ir-field{display:grid;gap:4px;margin:0 0 10px}.ir-field label{font-weight:800;margin:0}.ir-help{color:var(--ir-muted);font-size:12px;line-height:1.45}.ir-input-unit{display:flex;align-items:center;gap:7px}.ir-input-unit input{flex:1 1 auto;min-width:0}.ir-input-unit .ir-unit{padding-right:2px}"
        ".ir-run-list{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}.ir-run-row{border:1px solid #e4ebef;border-radius:8px;padding:12px;background:#fbfcfd;min-width:0}.ir-run-row b{display:block}.ir-run-row .ir-help{display:block;margin-top:4px}.ir-run-row .ir-input-unit{margin-top:12px}.ir-run-row input[type=number]{max-width:110px}.ir-badge{display:inline-flex;align-items:center;min-height:22px;padding:0 8px;border-radius:999px;background:var(--ir-soft);color:var(--ir-accent-strong);font-size:12px;font-weight:800}"
        ".ir-toolbar{display:flex;justify-content:space-between;align-items:center;gap:10px;margin:0 0 10px}.ir-toolbar p{margin:0;color:var(--ir-muted);font-size:13px}.ir-list-main b{display:block}.ir-list-main small,.ir-list-cell small{display:block;color:var(--ir-muted);font-size:12px;margin-top:2px}.ir-list-cell{font-size:13px}.ir-pill{display:inline-flex;align-items:center;min-height:24px;padding:0 9px;border-radius:999px;font-size:12px;font-weight:800;white-space:nowrap;background:#eef2f4;color:#667085}.ir-pill.on{background:#dff4e8;color:#087443}.ir-pill.warn{background:#fff4d6;color:#935b00}.ir-kv{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.ir-kv div{border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:10px}.ir-kv b{display:block;font-size:18px}.ir-kv span{display:block;margin-top:2px;color:var(--ir-muted);font-size:12px}"
        ".ir-data-table{width:100%;border-collapse:separate;border-spacing:0}.ir-data-table th,.ir-data-table td{padding:11px 12px;border-bottom:1px solid #e4ebef;vertical-align:middle;text-align:left}.ir-data-table th{font-size:12px;background:#f8fafb;color:var(--ir-muted);white-space:nowrap}.ir-data-table tbody tr:last-child td{border-bottom:0}.ir-data-table tbody tr:hover td{background:#fbfcfd}.ir-data-table .num{text-align:right;font-variant-numeric:tabular-nums}.ir-table-title{font-weight:800;color:var(--ir-ink)}.ir-table-note{display:block;margin-top:3px;color:var(--ir-muted);font-size:12px}.ir-compact-list{display:flex;flex-wrap:wrap;gap:5px}.ir-compact-item{display:inline-flex;align-items:center;min-height:22px;padding:0 7px;border-radius:6px;background:#eef6f3;color:#236052;font-size:12px;font-weight:700;white-space:nowrap}"
        ".ir-time-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.ir-time-item{display:grid;grid-template-columns:auto minmax(0,1fr);gap:8px;align-items:center;border:1px solid #e4ebef;border-radius:8px;background:#fff;padding:8px}.ir-time-item input[type=time]{width:100%}.ir-zone-duration{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:8px}.ir-zone-duration .ir-field{border:1px solid #e4ebef;border-radius:8px;background:#fff;padding:10px;margin:0}.ir-zone-duration input[type=number]{max-width:100px}.ir-danger-line{display:flex;justify-content:flex-end;margin-top:8px}"
        ".ir-calib-flow{display:grid;grid-template-columns:minmax(0,.9fr) minmax(0,1.1fr);gap:12px}.ir-calib-card{border:1px solid #e4ebef;border-radius:8px;background:#fff;padding:13px}.ir-calib-card h3{margin:0 0 4px;font-size:15px}.ir-calib-card p{margin:0 0 12px;color:var(--ir-muted);font-size:12px;line-height:1.45}.ir-calib-steps{display:grid;gap:8px}.ir-calib-step{display:grid;grid-template-columns:28px minmax(0,1fr);gap:9px;align-items:start}.ir-step-no{display:flex;align-items:center;justify-content:center;width:24px;height:24px;border-radius:50%;background:var(--ir-soft);color:var(--ir-accent-strong);font-size:12px;font-weight:900}.ir-calib-result{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin-bottom:12px}.ir-calib-result div{border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:10px}.ir-calib-result b{display:block;font-size:18px}.ir-calib-result span{display:block;margin-top:2px;color:var(--ir-muted);font-size:12px}"
        ".ir-status-panel{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:12px;align-items:start}.ir-status-title{font-size:17px;font-weight:850;margin:0 0 4px}.ir-status-note{margin:0;color:var(--ir-muted);font-size:12px;line-height:1.45}.ir-status-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-top:12px}.ir-status-grid div{border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:10px}.ir-status-grid b{display:block;font-size:18px}.ir-status-grid span{display:block;margin-top:2px;color:var(--ir-muted);font-size:12px}.ir-plan-now{display:grid;grid-template-columns:minmax(240px,.75fr) minmax(0,1.25fr);gap:12px;align-items:start}.ir-plan-now .ir-field{margin:0}.ir-plan-preview{border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:12px;min-width:0}.ir-plan-preview[hidden]{display:none}.ir-plan-preview h3{font-size:15px;margin:0 0 4px}.ir-plan-preview p{margin:0 0 9px;color:var(--ir-muted);font-size:12px}.ir-plan-summary{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:9px}.ir-zone-seq{display:grid;gap:6px}.ir-zone-seq div{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center;border:1px solid #e4ebef;border-radius:7px;background:#fff;padding:8px}.ir-event-summary{min-width:220px}.ir-event-values{display:flex;flex-wrap:wrap;gap:4px}.ir-detail-section{border:1px solid #e4ebef;border-radius:8px;background:#fbfcfd;padding:12px;margin:0 0 12px}.ir-detail-section h2{font-size:15px;margin:0 0 10px}.ir-detail-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.ir-detail-row{border:1px solid #e4ebef;border-radius:7px;background:#fff;padding:9px;min-width:0}.ir-detail-row span{display:block;color:var(--ir-muted);font-size:12px;margin-bottom:3px}.ir-detail-row b{display:block;font-size:13px;overflow-wrap:anywhere}.ir-detail-row.full{grid-column:1/-1}"
        "@media(max-width:820px){.ir-grid,.ir-grid.ir-three,.ir-time-grid,.ir-zone-duration,.ir-kv,.ir-setting-list,.ir-run-list,.ir-calib-flow,.ir-calib-result,.ir-status-panel,.ir-status-grid,.ir-plan-now,.ir-detail-grid{grid-template-columns:1fr}.actions,.ir-toolbar{justify-content:flex-start;align-items:flex-start}.ir-toolbar{flex-direction:column}select{width:100%;min-width:0}.tablewrap{overflow-x:auto}.ir-data-table{min-width:680px}}"
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
        case RunState::PumpStartDelay: return "启动稳定时间";
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

const char* runResultLabel(RunResult result) {
    switch (result) {
        case RunResult::None: return "无";
        case RunResult::Completed: return "已完成";
        case RunResult::UserStopped: return "用户停止";
        case RunResult::FaultStopped: return "故障停止";
        case RunResult::Skipped: return "已跳过";
    }
    return "未知";
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

const char* eventLevelLabel(uint8_t level) {
    switch (level) {
        case Esp32BaseAppEventLog::LEVEL_INFO: return "信息";
        case Esp32BaseAppEventLog::LEVEL_WARN: return "警告";
        case Esp32BaseAppEventLog::LEVEL_ERROR: return "错误";
    }
    return "未知";
}

const char* eventSourceLabel(const char* source) {
    if (source == nullptr) return "未知";
    if (strcmp(source, "system") == 0) return "系统";
    if (strcmp(source, "run") == 0) return "运行";
    if (strcmp(source, "plan") == 0) return "计划";
    if (strcmp(source, "calibration") == 0) return "校准";
    if (strcmp(source, "flow") == 0) return "流量";
    if (strcmp(source, "zone") == 0) return "水路";
    return source;
}

const char* eventTypeLabel(const char* type) {
    if (type == nullptr) return "未知";
    if (strcmp(type, "startup") == 0) return "启动";
    if (strcmp(type, "started") == 0) return "已启动";
    if (strcmp(type, "finished") == 0) return "已结束";
    if (strcmp(type, "completed") == 0) return "已完成";
    if (strcmp(type, "user_stopped") == 0) return "用户停止";
    if (strcmp(type, "fault_stopped") == 0) return "故障停止";
    if (strcmp(type, "saved") == 0) return "已保存";
    if (strcmp(type, "triggered") == 0) return "已触发";
    if (strcmp(type, "skipped") == 0) return "已跳过";
    if (strcmp(type, "leak") == 0) return "疑似漏水";
    if (strcmp(type, "low_flow") == 0) return "低流量";
    if (strcmp(type, "high_flow") == 0) return "高流量";
    return type;
}

const char* eventReasonLabel(const char* reason) {
    if (reason == nullptr || reason[0] == '\0') return "无";
    if (strcmp(reason, "boot") == 0) return "开机";
    if (strcmp(reason, "request") == 0) return "用户请求";
    if (strcmp(reason, "start_time") == 0) return "启动时间";
    if (strcmp(reason, "idle_flow") == 0) return "待机流量";
    if (strcmp(reason, "flow_meter_volume") == 0) return "流量计水量校准";
    if (strcmp(reason, "zone_standard_flow") == 0) return "水路标准流量校准";
    const char* runReason = runReasonLabelFromString(reason);
    return strcmp(runReason, "未知原因") == 0 ? reason : runReason;
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

bool parseFlowRateParam(const char* name, uint32_t minMlPerMin, uint32_t maxMlPerMin, uint32_t fallbackMlPerMin, uint32_t& out) {
    char value[24];
    if (!Esp32BaseWeb::getParam(name, value, sizeof(value)) || value[0] == '\0') {
        out = fallbackMlPerMin;
        return true;
    }

    uint32_t wholeLiters = 0;
    uint32_t fractionalMl = 0;
    uint8_t fractionalDigits = 0;
    bool sawDigit = false;
    bool inFraction = false;
    for (size_t i = 0; value[i] != '\0'; ++i) {
        const char c = value[i];
        if (c == '.') {
            if (inFraction) {
                return false;
            }
            inFraction = true;
            continue;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        sawDigit = true;
        if (inFraction) {
            if (fractionalDigits >= 3) {
                return false;
            }
            fractionalMl = fractionalMl * 10UL + static_cast<uint32_t>(c - '0');
            ++fractionalDigits;
        } else {
            wholeLiters = wholeLiters * 10UL + static_cast<uint32_t>(c - '0');
            if (wholeLiters > 100000UL) {
                return false;
            }
        }
    }
    if (!sawDigit) {
        return false;
    }
    while (fractionalDigits < 3) {
        fractionalMl *= 10UL;
        ++fractionalDigits;
    }

    const uint64_t mlPerMin = static_cast<uint64_t>(wholeLiters) * 1000ULL + fractionalMl;
    if (mlPerMin < minMlPerMin || mlPerMin > maxMlPerMin) {
        return false;
    }
    out = static_cast<uint32_t>(mlPerMin);
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

void flowRateToText(uint32_t mlPerMin, char* out, size_t len) {
    const uint32_t whole = mlPerMin / 1000UL;
    const uint32_t fractional = mlPerMin % 1000UL;
    if (fractional == 0) {
        snprintf(out, len, "%lu", static_cast<unsigned long>(whole));
        return;
    }
    char text[24];
    snprintf(text, sizeof(text), "%lu.%03lu",
             static_cast<unsigned long>(whole),
             static_cast<unsigned long>(fractional));
    size_t n = strlen(text);
    while (n > 0 && text[n - 1] == '0') {
        text[--n] = '\0';
    }
    snprintf(out, len, "%s", text);
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
    char buf[192];
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

void sendFlowRateInput(const char* name, uint32_t mlPerMin, uint32_t minMlPerMin, uint32_t maxMlPerMin) {
    char value[24];
    char minValue[24];
    char maxValue[24];
    flowRateToText(mlPerMin, value, sizeof(value));
    flowRateToText(minMlPerMin, minValue, sizeof(minValue));
    flowRateToText(maxMlPerMin, maxValue, sizeof(maxValue));
    char buf[192];
    snprintf(buf, sizeof(buf), "<input type='number' name='%s' min='%s' max='%s' step='0.001' value='%s'>",
             name,
             minValue,
             maxValue,
             value);
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

void sendSettingFlowRate(const char* title,
                         const char* desc,
                         const char* name,
                         uint32_t value,
                         uint32_t minValue,
                         uint32_t maxValue) {
    Esp32BaseWeb::sendChunk("<div class='ir-setting'><div><span class='ir-setting-title'>");
    Esp32BaseWeb::sendChunk(title);
    Esp32BaseWeb::sendChunk("</span><span class='ir-setting-desc'>");
    Esp32BaseWeb::sendChunk(desc);
    Esp32BaseWeb::sendChunk("</span></div><div class='ir-control'>");
    sendFlowRateInput(name, value, minValue, maxValue);
    Esp32BaseWeb::sendChunk("<span class='ir-unit'>L/min</span></div></div>");
}

void sendContactTypeChoice(ContactType type) {
    Esp32BaseWeb::sendChunk("<div class='ir-setting'><div><span class='ir-setting-title'>低液位触点类型</span><span class='ir-setting-desc'>按实际缺水传感器接线选择：常开 NO 或常闭 NC。</span></div><div class='ir-control'><div class='ir-choice'>");
    Esp32BaseWeb::sendChunk("<label><input type='radio' name='lowLevelContactType' value='normally_open'");
    sendChecked(type == ContactType::NormallyOpen);
    Esp32BaseWeb::sendChunk(">常开 NO</label><label><input type='radio' name='lowLevelContactType' value='normally_closed'");
    sendChecked(type == ContactType::NormallyClosed);
    Esp32BaseWeb::sendChunk(">常闭 NC</label></div></div></div>");
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

uint32_t maxZoneDurationMinutes();

void sendEnabledPill(bool enabled) {
    Esp32BaseWeb::sendChunk(enabled ? "<span class='ir-pill on'>启用</span>" : "<span class='ir-pill'>禁用</span>");
}

uint32_t durationMinutesRounded(uint32_t seconds) {
    return (seconds + 59UL) / 60UL;
}

uint32_t msToSecondsRounded(uint32_t milliseconds) {
    return (milliseconds + 999UL) / 1000UL;
}

const char* runSourceLabel(RunSource source) {
    switch (source) {
        case RunSource::Manual: return "手动浇水";
        case RunSource::Plan: return "自动计划";
        case RunSource::RunPlanNow: return "立即执行计划";
        case RunSource::Calibration: return "校准";
    }
    return "未知";
}

const ZoneConfig* findZoneById(const IrrigationConfig& config, uint8_t zoneId) {
    const uint8_t index = zoneIndexFromId(zoneId);
    if (index >= kMaxZones) {
        return nullptr;
    }
    return &config.zones[index];
}

const char* zoneNameById(const IrrigationConfig& config, uint8_t zoneId) {
    const ZoneConfig* zone = findZoneById(config, zoneId);
    return zone != nullptr ? zone->name : "未知水路";
}

const char* planNameById(const IrrigationConfig& config, uint8_t planId) {
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (plan.used && plan.id == planId) {
            return plan.name;
        }
    }
    return "未知计划";
}

void durationToText(uint32_t seconds, char* out, size_t len) {
    const uint32_t minutes = seconds / 60UL;
    const uint32_t remainSec = seconds % 60UL;
    if (minutes > 0 && remainSec > 0) {
        snprintf(out, len, "%lu 分 %lu 秒",
                 static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(remainSec));
    } else if (minutes > 0) {
        snprintf(out, len, "%lu 分钟", static_cast<unsigned long>(minutes));
    } else {
        snprintf(out, len, "%lu 秒", static_cast<unsigned long>(remainSec));
    }
}

uint8_t planEnabledZoneCount(const IrrigationConfig& config, const WateringPlan& plan) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (config.zones[i].enabled && plan.zoneDurationSec[i] > 0) {
            ++count;
        }
    }
    return count;
}

uint32_t planTotalDurationSec(const IrrigationConfig& config, const WateringPlan& plan) {
    uint32_t total = 0;
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (config.zones[i].enabled) {
            total += plan.zoneDurationSec[i];
        }
    }
    return total;
}

uint8_t zonePlanUseCount(const IrrigationConfig& config, uint8_t zoneId) {
    uint8_t count = 0;
    const uint8_t zoneIndex = zoneIndexFromId(zoneId);
    if (zoneIndex >= kMaxZones) {
        return 0;
    }
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (plan.used && plan.zoneDurationSec[zoneIndex] > 0) {
            ++count;
        }
    }
    return count;
}

uint8_t firstEnabledZoneId(const IrrigationConfig& config) {
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (config.zones[i].enabled) {
            return config.zones[i].id;
        }
    }
    return 0;
}

void sendPlanStartSummary(const WateringPlan& plan) {
    bool any = false;
    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        if (!plan.startTimes[i].enabled || !isValidMinuteOfDay(plan.startTimes[i].minuteOfDay)) {
            continue;
        }
        if (any) {
            Esp32BaseWeb::sendChunk(" / ");
        }
        char text[8];
        minuteToText(plan.startTimes[i].minuteOfDay, text, sizeof(text));
        Esp32BaseWeb::sendChunk(text);
        any = true;
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("未设置");
    }
}

void sendPlanZoneSummary(const IrrigationConfig& config, const WateringPlan& plan) {
    bool any = false;
    Esp32BaseWeb::sendChunk("<div class='ir-compact-list'>");
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (!zone.enabled || plan.zoneDurationSec[i] == 0) {
            continue;
        }
        any = true;
        Esp32BaseWeb::sendChunk("<span class='ir-compact-item'>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk(" ");
        char value[24];
        snprintf(value, sizeof(value), "%lu 分钟", static_cast<unsigned long>(durationMinutesRounded(plan.zoneDurationSec[i])));
        Esp32BaseWeb::sendChunk(value);
        Esp32BaseWeb::sendChunk("</span>");
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<span class='ir-table-note'>未配置有效水路时长</span>");
    }
    Esp32BaseWeb::sendChunk("</div>");
}

void sendPlanStepList(const IrrigationConfig& config, const WateringPlan& plan) {
    bool any = false;
    Esp32BaseWeb::sendChunk("<div class='ir-zone-seq'>");
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (!zone.enabled || plan.zoneDurationSec[i] == 0) {
            continue;
        }
        any = true;
        char duration[24];
        durationToText(plan.zoneDurationSec[i], duration, sizeof(duration));
        Esp32BaseWeb::sendChunk("<div><span>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</span><b>");
        Esp32BaseWeb::writeHtmlEscaped(duration);
        Esp32BaseWeb::sendChunk("</b></div>");
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<div><span>没有有效水路</span><b>不会执行</b></div>");
    }
    Esp32BaseWeb::sendChunk("</div>");
}

bool readRequestZoneId(uint8_t& zoneId) {
    return parseU8Param("id", 1, kMaxZones, zoneId);
}

bool readRequestPlanId(uint8_t& planId) {
    return parseU8Param("id", 1, kMaxPlans, planId);
}

bool readPagingParam(const char* name, uint32_t minValue, uint32_t maxValue, uint32_t fallback, uint32_t& out) {
    return parseU32Param(name, minValue, maxValue, fallback, out);
}

bool readPlanFieldsFromRequest(WateringPlan& plan) {
    char value[kPlanNameLength];
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
            return false;
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
            return false;
        }
        plan.zoneDurationSec[i] = minutes * 60UL;
    }
    return true;
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
    Esp32BaseWeb::endJson();
}

void handleStatusApi() {
    sendStatusJson();
}

void handleZonesApi() {
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("\"zones\":[");
    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (i > 0) {
            Esp32BaseWeb::sendChunk(",");
        }
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"id\":%u,\"enabled\":%s,\"standardFlowMlPerMin\":%lu,\"name\":\"",
                 zone.id,
                 zone.enabled ? "true" : "false",
                 static_cast<unsigned long>(zone.standardFlowMlPerMin));
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeJsonEscaped(zone.name);
        Esp32BaseWeb::sendChunk("\"}");
    }
    Esp32BaseWeb::sendChunk("]");
    Esp32BaseWeb::endJson();
}

void handlePlansApi() {
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("\"plans\":[");
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
    Esp32BaseWeb::sendChunk("]");
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
    sendJsonStringField("mode", calibrationModeName(snapshot.mode));
    Esp32BaseWeb::sendChunk("\"running\":");
    Esp32BaseWeb::sendChunk(snapshot.running ? "true," : "false,");
    Esp32BaseWeb::sendChunk("\"resultReady\":");
    Esp32BaseWeb::sendChunk(snapshot.resultReady ? "true," : "false,");
    char buf[192];
    snprintf(buf, sizeof(buf),
             "\"runId\":%lu,\"zoneId\":%u,\"durationSec\":%lu,\"pulses\":%lu,\"computedPulsesPerLiter\":%lu,\"suggestedFlowMlPerMin\":%lu",
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
        if (!readZoneParam(zoneId) || !parseFlowRateParam("standardFlow", 1, 100000, 0, flow)) {
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

    uint32_t flow = 0;
    if (!parseFlowRateParam("standardFlow", 0, 100000, zone.standardFlowMlPerMin, flow)) {
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

    WateringPlan plan = {};
    plan.id = 1;
    plan.used = true;
    snprintf(plan.name, sizeof(plan.name), "新计划");
    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        plan.startTimes[i].enabled = false;
        plan.startTimes[i].minuteOfDay = kInvalidMinuteOfDay;
    }
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        plan.zoneDurationSec[i] = 0;
    }
    if (!readPlanFieldsFromRequest(plan)) {
        return;
    }

    uint8_t planId = 0;
    if (!PlanService::createPlan(plan.name, planId)) {
        Esp32BaseWeb::sendText(400, PlanService::lastError());
        return;
    }
    plan.id = planId;
    if (!PlanService::savePlan(plan)) {
        PlanService::deletePlan(planId);
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
    if (!readPlanFieldsFromRequest(plan)) {
        return;
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
    char contactType[24];
    if (Esp32BaseWeb::getParam("lowLevelContactType", contactType, sizeof(contactType)) &&
        strcmp(contactType, "normally_closed") == 0) {
        next.supply.lowLevelContactType = ContactType::NormallyClosed;
    } else {
        next.supply.lowLevelContactType = ContactType::NormallyOpen;
    }
    next.supply.pumpStartDelayMs = 0;

    uint32_t value = 0;
    if (!parseU32Param("pumpStopDelaySec", 0, 10, msToSecondsRounded(next.supply.pumpStopDelayMs), value)) {
        Esp32BaseWeb::sendText(400, "停泵后关阀延时无效");
        return;
    }
    next.supply.pumpStopDelayMs = value * 1000UL;

    if (!parseU32Param("lowLevelDebounceSec", 0, 10, msToSecondsRounded(next.supply.lowLevelDebounceMs), value)) {
        Esp32BaseWeb::sendText(400, "低液位消抖时间无效");
        return;
    }
    next.supply.lowLevelDebounceMs = value * 1000UL;

    if (!parseU32Param("pulsesPerLiter", 0, 100000, next.flow.pulsesPerLiter, value)) {
        Esp32BaseWeb::sendText(400, "每升脉冲数无效");
        return;
    }
    next.flow.pulsesPerLiter = value;

    if (!parseU32Param("startupGraceSec", 0, 120, next.flow.startupGraceSec, value)) {
        Esp32BaseWeb::sendText(400, "启动稳定时间无效");
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

    if (!parseU32Param("lowHighFlowConfirmSec", 1, 600, next.flow.lowHighFlowConfirmSec, value)) {
        Esp32BaseWeb::sendText(400, "异常流量确认时间无效");
        return;
    }
    next.flow.lowHighFlowConfirmSec = value;

    if (!parseU32Param("pullInSec", 1, 60, msToSecondsRounded(next.valve.pullInMs), value)) {
        Esp32BaseWeb::sendText(400, "电磁阀吸合时间无效");
        return;
    }
    next.valve.pullInMs = value * 1000UL;

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
        Esp32BaseWeb::sendChunk("<div class='ir-run-row'><div><b>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</b><span class='ir-help'>本次手动任务会按水路编号顺序执行，填 0 表示跳过。</span></div><div class='ir-input-unit'>");
        char key[8];
        snprintf(key, sizeof(key), "z%u", zone.id);
        sendNumberInput(key, 0, 0, maxMinutes);
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
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/run/plan-now'><div class='ir-plan-now'><div class='ir-card'><div class='ir-field'><label>选择计划</label><span class='ir-help'>立即按所选计划的水路时长顺序执行，不等待计划启动时间。</span><select id='ir-plan-now-select' name='plan'>");
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
    Esp32BaseWeb::sendChunk("></div></div><div>");
    bool firstPreview = true;
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (!plan.used || !plan.enabled) {
            continue;
        }
        char buf[192];
        snprintf(buf, sizeof(buf), "<div class='ir-plan-preview' data-plan-preview='%u'%s><h3>",
                 plan.id,
                 firstPreview ? "" : " hidden");
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeHtmlEscaped(plan.name);
        Esp32BaseWeb::sendChunk("</h3><p>只预览本计划内容，不能在运行页修改。</p><div class='ir-plan-summary'><span class='ir-pill on'>每日</span><span class='ir-pill'>");
        sendPlanStartSummary(plan);
        Esp32BaseWeb::sendChunk("</span>");
        snprintf(buf, sizeof(buf), "<span class='ir-pill'>%u 个水路</span><span class='ir-pill'>%lu 分钟</span></div>",
                 planEnabledZoneCount(config, plan),
                 static_cast<unsigned long>(durationMinutesRounded(planTotalDurationSec(config, plan))));
        Esp32BaseWeb::sendChunk(buf);
        sendPlanStepList(config, plan);
        Esp32BaseWeb::sendChunk("</div>");
        firstPreview = false;
    }
    if (!hasPlan) {
        Esp32BaseWeb::sendChunk("<div class='ir-empty'>没有已启用的计划。</div>");
    }
    Esp32BaseWeb::sendChunk("</div></div></form>");
    if (hasPlan) {
        Esp32BaseWeb::sendChunk("<script>(function(){var s=document.getElementById('ir-plan-now-select');if(!s)return;function u(){document.querySelectorAll('[data-plan-preview]').forEach(function(c){c.hidden=c.getAttribute('data-plan-preview')!==s.value;});}s.addEventListener('change',u);u();})();</script>");
    }
    if (!hasPlan) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "没有已启用的计划", "先启用一个计划，才能使用立即执行。");
    } else if (!flowReady) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "流量计未校准", "执行自动计划前需要先设置每升脉冲数。");
    }
    Esp32BaseWeb::endPanel();
}

void sendRunStatusPanel(const char* title, bool includeStop) {
    const StatusSnapshot status = RunController::statusSnapshot();
    const IrrigationConfig& config = ConfigStore::config();
    const WateringRun& run = RunController::currentRun();
    char flow[24];
    char volume[24];
    flowRateToText(status.currentFlowMlPerMin, flow, sizeof(flow));
    snprintf(volume, sizeof(volume), "%lu ml", static_cast<unsigned long>(status.currentRunVolumeMl));

    Esp32BaseWeb::beginPanel(title);
    Esp32BaseWeb::sendChunk("<div class='ir-status-panel'><div><p class='ir-status-title'>");
    Esp32BaseWeb::sendChunk(status.busy ? runStateLabel(status.runState) : "空闲");
    Esp32BaseWeb::sendChunk("</p><p class='ir-status-note'>");
    if (status.busy) {
        Esp32BaseWeb::sendChunk(runSourceLabel(run.source));
        if (run.planId > 0) {
            Esp32BaseWeb::sendChunk(" · ");
            Esp32BaseWeb::writeHtmlEscaped(planNameById(config, run.planId));
        }
    } else {
        Esp32BaseWeb::sendChunk("当前没有正在执行的浇水任务。");
    }
    Esp32BaseWeb::sendChunk("</p></div>");
    if (includeStop && status.busy) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run/stop'><input class='danger' type='submit' value='停止浇水'></form>");
    }
    Esp32BaseWeb::sendChunk("</div><div class='ir-status-grid'>");

    char value[48];
    if (status.busy && run.currentStep < run.stepCount) {
        snprintf(value, sizeof(value), "%u / %u",
                 static_cast<unsigned>(run.currentStep + 1U),
                 static_cast<unsigned>(run.stepCount));
    } else {
        snprintf(value, sizeof(value), "无");
    }
    Esp32BaseWeb::sendChunk("<div><b>");
    Esp32BaseWeb::writeHtmlEscaped(value);
    Esp32BaseWeb::sendChunk("</b><span>步骤进度</span></div><div><b>");
    if (status.activeZoneId > 0) {
        Esp32BaseWeb::writeHtmlEscaped(zoneNameById(config, status.activeZoneId));
    } else {
        Esp32BaseWeb::sendChunk("无");
    }
    Esp32BaseWeb::sendChunk("</b><span>当前水路</span></div><div><b>");
    Esp32BaseWeb::writeHtmlEscaped(flow);
    Esp32BaseWeb::sendChunk("</b><span>当前流量 L/min</span></div><div><b>");
    Esp32BaseWeb::writeHtmlEscaped(volume);
    Esp32BaseWeb::sendChunk("</b><span>当前步骤累计水量</span></div></div>");
    Esp32BaseWeb::endPanel();
}

void sendLastWateringSummary() {
    uint32_t total = 0;
    if (!HistoryService::readPage(1, 1, g_historyViewBuffer, sizeof(g_historyViewBuffer), total) || total == 0) {
        Esp32BaseWeb::sendChunk("<div><b>无</b><span>最近浇水</span></div>");
        return;
    }
    char* line = g_historyViewBuffer;
    while (*line == ' ' || *line == '\r' || *line == '\n' || *line == '\t') {
        ++line;
    }
    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        Esp32BaseWeb::sendChunk("<div><b>不可用</b><span>最近浇水</span></div>");
        return;
    }
    char finished[24];
    epochToText(doc["finishedAt"] | 0, finished, sizeof(finished));
    Esp32BaseWeb::sendChunk("<div><b>");
    Esp32BaseWeb::sendChunk(runResultLabelFromString(doc["result"] | ""));
    Esp32BaseWeb::sendChunk("</b><span>");
    Esp32BaseWeb::writeHtmlEscaped(finished);
    Esp32BaseWeb::sendChunk("</span></div>");
}

void handleDashboardPage() {
    const StatusSnapshot status = RunController::statusSnapshot();
    char value[32];

    Esp32BaseWeb::sendHeader("智能浇水");
    Esp32BaseWeb::sendPageTitle("智能浇水", "本地 12V 六路浇水控制器");

    if (status.busy) {
        sendRunStatusPanel("正在运行", true);
    }

    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("运行状态", runStateLabel(status.runState), status.busy ? "正在运行" : "空闲");
    epochToText(status.nextRunEpoch, value, sizeof(value));
    Esp32BaseWeb::sendMetric("下次运行", value, status.nextRunEpoch == 0 ? "没有已启用的计划" : "本地时间");
    Esp32BaseWeb::sendMetric("流量计", ConfigStore::config().flow.pulsesPerLiter > 0 ? "已校准" : "未校准", "自动计划依赖");
    Esp32BaseWeb::sendMetric("供水方式", ConfigStore::config().supply.pumpEnabled ? "外部自吸泵" : "有压水源", ConfigStore::config().supply.lowLevelEnabled ? "缺水保护启用" : "缺水保护禁用");
    Esp32BaseWeb::endMetricGrid();

    Esp32BaseWeb::beginPanel("系统健康");
    Esp32BaseWeb::sendChunk("<div class='ir-kv'>");
    sendLastWateringSummary();
    Esp32BaseWeb::sendChunk("<div><b>");
    Esp32BaseWeb::sendChunk(status.configValid ? "正常" : "异常");
    Esp32BaseWeb::sendChunk("</b><span>配置状态</span></div><div><b>");
    Esp32BaseWeb::sendChunk(ConfigStore::config().supply.pumpEnabled ? "启用" : "禁用");
    Esp32BaseWeb::sendChunk("</b><span>外部自吸泵供水</span></div><div><b>");
    Esp32BaseWeb::sendChunk(ConfigStore::config().supply.lowLevelEnabled ? "启用" : "禁用");
    Esp32BaseWeb::sendChunk("</b><span>低液位保护</span></div></div>");
    if (ConfigStore::config().flow.pulsesPerLiter == 0) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "流量计未校准", "自动计划执行前需要先设置每升脉冲数。");
    }
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleRunPage() {
    const StatusSnapshot status = RunController::statusSnapshot();
    Esp32BaseWeb::sendHeader("运行");
    Esp32BaseWeb::sendPageTitle("运行", "手动顺序浇水和立即执行计划");
    if (status.busy) {
        sendRunStatusPanel("当前运行", true);
    } else {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "空闲", "当前没有正在执行的浇水任务。");
    }
    sendRunPanel();
    sendPlanNowPanel();
    Esp32BaseWeb::sendFooter();
}

void handleZonesPage() {
    Esp32BaseWeb::sendHeader("水路");
    Esp32BaseWeb::sendPageTitle("水路", "固定 6 路输出，启用实际接线的水路");
    Esp32BaseWeb::beginPanel("水路列表");

    const IrrigationConfig& config = ConfigStore::config();
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='ir-data-table'><thead><tr><th>水路</th><th>状态</th><th class='num'>标准流量</th><th class='num'>已配置计划</th><th>说明</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        char buf[256];
        char flowText[24];
        flowRateToText(zone.standardFlowMlPerMin, flowText, sizeof(flowText));
        snprintf(buf, sizeof(buf), "<tr><td><span class='ir-table-title'>水路 %u</span><span class='ir-table-note'>", zone.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</span></td><td>");
        sendEnabledPill(zone.enabled);
        snprintf(buf,
                 sizeof(buf),
                 "</td><td class='num'>%s L/min</td><td class='num'>%u 个</td><td><span class='ir-table-note'>浇水时长在手动任务或计划中设置</span></td><td>",
                 flowText,
                 zonePlanUseCount(config, zone.id));
        Esp32BaseWeb::sendChunk(buf);
        snprintf(buf, sizeof(buf), "<a class='btnlink info' href='/irrigation/zones/edit?id=%u'>配置</a>", zone.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");

    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleZoneEditPage() {
    uint8_t zoneId = 0;
    if (!readRequestZoneId(zoneId)) {
        Esp32BaseWeb::sendText(400, "水路编号无效");
        return;
    }
    const ZoneConfig* zone = ZoneService::find(zoneId);
    if (zone == nullptr) {
        Esp32BaseWeb::sendText(404, ZoneService::lastError());
        return;
    }

    char title[32];
    snprintf(title, sizeof(title), "水路 %u", zone->id);
    Esp32BaseWeb::sendHeader(title);
    Esp32BaseWeb::sendPageTitle(title, "配置水路基础信息");
    Esp32BaseWeb::beginPanel("水路配置");
    char buf[128];
    snprintf(buf, sizeof(buf), "<form class='ir-form' method='post' action='/irrigation/zones/save'><input type='hidden' name='id' value='%u'>", zone->id);
    Esp32BaseWeb::sendChunk(buf);
    Esp32BaseWeb::sendChunk("<div class='ir-setting-list'>");
    sendSettingToggle("启用水路", "只有启用的水路才会出现在手动浇水和计划配置中。", "enabled", zone->enabled);
    Esp32BaseWeb::sendChunk("<div class='ir-setting'><div><span class='ir-setting-title'>水路名称</span><span class='ir-setting-desc'>用于页面、计划和运行历史展示。</span></div><div class='ir-control'>");
    sendTextInput("name", zone->name, sizeof(zone->name) - 1);
    Esp32BaseWeb::sendChunk("</div></div>");
    sendSettingFlowRate("标准流量", "用于低流量和高流量判断，校准后可以自动写入。", "standardFlow", zone->standardFlowMlPerMin, 0, 100000);
    Esp32BaseWeb::sendChunk("</div><div class='actions'><a class='btnlink secondary' href='/irrigation/zones'>返回列表</a><input type='submit' value='保存水路'></div></form>");
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
        Esp32BaseWeb::sendChunk("<div class='ir-field'><label>");
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

void sendPlanEditForm(const WateringPlan& plan, const char* action, bool isCreate) {
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='");
    Esp32BaseWeb::sendChunk(action);
    Esp32BaseWeb::sendChunk("'>");
    if (!isCreate) {
        char id[64];
        snprintf(id, sizeof(id), "<input type='hidden' name='id' value='%u'>", plan.id);
        Esp32BaseWeb::sendChunk(id);
    }

    Esp32BaseWeb::sendChunk("<div class='ir-setting-list'>");
    sendSettingToggle("启用计划", "启用后按每日启动时间自动执行，也可以在运行页立即执行。", "enabled", plan.enabled);
    Esp32BaseWeb::sendChunk("<div class='ir-setting'><div><span class='ir-setting-title'>计划名称</span><span class='ir-setting-desc'>用于区分不同季节、上午/下午或不同浇水策略。</span></div><div class='ir-control'>");
    sendTextInput("name", plan.name, sizeof(plan.name) - 1);
    Esp32BaseWeb::sendChunk("</div></div><div class='ir-setting'><div><span class='ir-setting-title'>重复方式</span><span class='ir-setting-desc'>当前产品只支持每日计划，不做复杂重复策略。</span></div><div class='ir-control'><span class='ir-pill on'>每日</span></div></div></div>");
    sendPlanStartFields(plan);
    sendPlanZoneDurationFields(plan);
    Esp32BaseWeb::sendChunk("<div class='actions'>");
    Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/plans'>返回列表</a>");
    Esp32BaseWeb::sendChunk(isCreate ? "<input type='submit' value='创建计划'>" : "<input type='submit' value='保存计划'>");
    Esp32BaseWeb::sendChunk("</div></form>");
}

void handlePlansPage() {
    Esp32BaseWeb::sendHeader("计划");
    Esp32BaseWeb::sendPageTitle("计划", "每日计划，最多 8 个计划，每个计划最多 4 个启动时间");

    const IrrigationConfig& config = ConfigStore::config();
    Esp32BaseWeb::beginPanel("计划列表");
    Esp32BaseWeb::sendChunk("<div class='ir-toolbar'><p>计划只支持每日重复；启动时间和各水路时长在计划详情中配置。</p><a class='btnlink info' href='/irrigation/plans/new'>创建计划</a></div>");
    const bool anyPlan = usedPlanCount(config) > 0;
    if (anyPlan) {
        Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='ir-data-table'><thead><tr><th>计划</th><th>状态</th><th>启动时间</th><th>水路时长</th><th class='num'>总时长</th><th>操作</th></tr></thead><tbody>");
    }
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (!plan.used) {
            continue;
        }
        char buf[256];
        Esp32BaseWeb::sendChunk("<tr><td><span class='ir-table-title'>");
        Esp32BaseWeb::writeHtmlEscaped(plan.name);
        Esp32BaseWeb::sendChunk("</span><span class='ir-table-note'>每日计划</span></td><td>");
        sendEnabledPill(plan.enabled);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendPlanStartSummary(plan);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendPlanZoneSummary(config, plan);
        snprintf(buf,
                 sizeof(buf),
                 "</td><td class='num'>%lu 分钟<span class='ir-table-note'>%u 个水路</span></td><td>",
                 static_cast<unsigned long>(durationMinutesRounded(planTotalDurationSec(config, plan))),
                 planEnabledZoneCount(config, plan));
        Esp32BaseWeb::sendChunk(buf);
        snprintf(buf, sizeof(buf), "<a class='btnlink info' href='/irrigation/plans/edit?id=%u'>配置</a>", plan.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    if (anyPlan) {
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
    } else {
        Esp32BaseWeb::sendChunk("<div class='ir-empty'>暂无计划。创建计划后，再配置每日启动时间和各水路时长。</div>");
    }
    Esp32BaseWeb::endPanel();

    Esp32BaseWeb::sendFooter();
}

void handlePlanNewPage() {
    WateringPlan plan = {};
    plan.id = 0;
    plan.used = true;
    plan.enabled = false;
    snprintf(plan.name, sizeof(plan.name), "新计划");
    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        plan.startTimes[i].enabled = i == 0;
        plan.startTimes[i].minuteOfDay = i == 0 ? 7U * 60U : kInvalidMinuteOfDay;
    }
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        plan.zoneDurationSec[i] = 0;
    }

    Esp32BaseWeb::sendHeader("创建计划");
    Esp32BaseWeb::sendPageTitle("创建计划", "每日启动时间和各水路时长");
    Esp32BaseWeb::beginPanel("计划信息");
    sendPlanEditForm(plan, "/irrigation/plans/create", true);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handlePlanEditPage() {
    uint8_t planId = 0;
    if (!readRequestPlanId(planId)) {
        Esp32BaseWeb::sendText(400, "计划编号无效");
        return;
    }
    const WateringPlan* plan = PlanService::find(planId);
    if (plan == nullptr) {
        Esp32BaseWeb::sendText(404, PlanService::lastError());
        return;
    }

    Esp32BaseWeb::sendHeader(plan->name);
    Esp32BaseWeb::sendPageTitle(plan->name, "每日启动时间和各水路时长");
    Esp32BaseWeb::beginPanel("计划信息");
    sendPlanEditForm(*plan, "/irrigation/plans/save", false);
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans/delete' onsubmit=\"return confirm('确认删除这个计划？')\"><input type='hidden' name='id' value='");
    char id[8];
    snprintf(id, sizeof(id), "%u", plan->id);
    Esp32BaseWeb::sendChunk(id);
    Esp32BaseWeb::sendChunk("'><div class='ir-danger-line'><input class='danger' type='submit' value='删除计划'></div></form>");
    Esp32BaseWeb::endPanel();
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

    sendSectionStart("供水方式与缺水保护", "默认按有压水源工作；启用外部自吸泵供水时，低液位保护才参与防干抽。");
    sendSettingToggle("使用外部自吸泵供水",
                      "启用后，浇水时输出干接点控制信号给外部自吸泵继电器模块；禁用时仅打开水路阀，适用于已有水压的水源。",
                      "pumpEnabled",
                      config.supply.pumpEnabled);
    sendSettingNumber("停泵后关阀延时",
                      "仅外部自吸泵供水时生效。先关闭自吸泵控制信号，水路阀继续打开这段时间后再关闭。",
                      "秒",
                      "pumpStopDelaySec",
                      msToSecondsRounded(config.supply.pumpStopDelayMs),
                      0,
                      10);
    sendSettingToggle("自吸泵缺水保护",
                      "仅外部自吸泵供水时生效。检测到水箱缺水时，禁止启动或停止正在运行的自吸泵。",
                      "lowLevelEnabled",
                      config.supply.lowLevelEnabled);
    sendContactTypeChoice(config.supply.lowLevelContactType);
    sendSettingNumber("低液位消抖时间",
                      "缺水输入需要持续稳定这么久才会生效，避免触点抖动导致误停。",
                      "秒",
                      "lowLevelDebounceSec",
                      msToSecondsRounded(config.supply.lowLevelDebounceMs),
                      0,
                      10);
    sendSectionEnd();

    sendSectionStart("流量检测", "流量计为必配输入，用于校准、无流量保护、漏水检测和异常流量判断。");
    sendSettingNumber("流量计每升脉冲数",
                      "流量计每流过 1 升水产生的脉冲数量；未校准时自动计划不能执行。",
                      "脉冲/L",
                      "pulsesPerLiter",
                      config.flow.pulsesPerLiter,
                      0,
                      100000);
    sendSettingNumber("启动稳定时间",
                      "打开水路后等待水压和流量计脉冲稳定；这段时间内不触发无流量、低流量或高流量判断。",
                      "秒",
                      "startupGraceSec",
                      config.flow.startupGraceSec,
                      0,
                      120);
    sendSettingNumber("无流量确认时间",
                      "启动稳定时间结束后，如果连续这么久没有流量计脉冲，就停止本次浇水。",
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
                      "运行流量低于水路标准流量的该比例，并持续达到确认时间后，记录低流量提示，不自动停机。",
                      "%",
                      "lowFlowPercent",
                      config.flow.lowFlowPercent,
                      1,
                      100);
    sendSettingNumber("高流量阈值",
                      "运行流量高于水路标准流量的该比例，并持续达到确认时间后，记录高流量提示，不自动停机。",
                      "%",
                      "highFlowPercent",
                      config.flow.highFlowPercent,
                      100,
                      1000);
    sendSettingNumber("异常流量确认时间",
                      "低流量或高流量需要连续持续这么久，才记录异常提示。",
                      "秒",
                      "lowHighFlowConfirmSec",
                      config.flow.lowHighFlowConfirmSec,
                      1,
                      600);
    sendSectionEnd();

    sendSectionStart("电磁阀驱动", "6 路 12V DC 电磁阀为固定输出，驱动参数影响吸合可靠性和发热。");
    sendSettingNumber("全功率吸合时间",
                      "打开电磁阀时先 100% 输出这段时间，确保阀可靠吸合；之后切换到保持占空比。",
                      "秒",
                      "pullInSec",
                      msToSecondsRounded(config.valve.pullInMs),
                      1,
                      60);
    sendSettingNumber("保持占空比",
                      "吸合时间结束后，用该占空比维持电磁阀打开，降低线圈发热。",
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
    const IrrigationConfig& config = ConfigStore::config();
    if (selectedZoneId == 0 || zoneIndexFromId(selectedZoneId) >= kMaxZones || !config.zones[zoneIndexFromId(selectedZoneId)].enabled) {
        selectedZoneId = firstEnabledZoneId(config);
    }
    Esp32BaseWeb::sendChunk("<select name='zone'>");
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

void sendCalibrationStartCard(const char* title,
                              const char* note,
                              const char* action,
                              uint32_t defaultMinutes,
                              const char* buttonText) {
    const IrrigationConfig& config = ConfigStore::config();
    const bool hasEnabledZone = firstEnabledZoneId(config) != 0;
    Esp32BaseWeb::sendChunk("<div class='ir-calib-card'><h3>");
    Esp32BaseWeb::sendChunk(title);
    Esp32BaseWeb::sendChunk("</h3><p>");
    Esp32BaseWeb::sendChunk(note);
    Esp32BaseWeb::sendChunk("</p>");
    if (!hasEnabledZone) {
        Esp32BaseWeb::sendChunk("<div class='ir-empty'>还没有启用水路，不能启动校准。</div></div>");
        return;
    }
    Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='");
    Esp32BaseWeb::sendChunk(action);
    Esp32BaseWeb::sendChunk("'><div class='ir-grid'><div class='ir-field'><label>校准水路</label><span class='ir-help'>选择一个已启用水路，系统会打开该水路并累计流量计脉冲。</span>");
    sendEnabledZoneSelect(firstEnabledZoneId(config));
    Esp32BaseWeb::sendChunk("</div><div class='ir-field'><label>运行时长</label><span class='ir-help'>校准会打开所选水路并累计流量计脉冲。</span><div class='ir-input-unit'>");
    sendNumberInput("durationMin", defaultMinutes, 1, maxZoneDurationMinutes());
    Esp32BaseWeb::sendChunk("<span class='ir-unit'>分钟</span></div></div></div><div class='actions'><input type='submit' value='");
    Esp32BaseWeb::sendChunk(buttonText);
    Esp32BaseWeb::sendChunk("'></div></form></div>");
}

void sendCalibrationStartPanel() {
    const IrrigationConfig& config = ConfigStore::config();
    Esp32BaseWeb::beginPanel("开始校准");
    Esp32BaseWeb::sendChunk("<div class='ir-calib-flow'>");
    sendCalibrationStartCard("流量计水量校准",
                             "先让某一路运行一段时间，用量杯或水桶记录实际出水量；结束后填写实测水量，系统计算每升脉冲数。",
                             "start_volume",
                             5,
                             "启动流量计校准");
    sendCalibrationStartCard("水路标准流量校准",
                             config.flow.pulsesPerLiter == 0 ?
                                 "建议先完成流量计水量校准；没有每升脉冲数时，系统无法自动建议标准流量，只能后续手动填写。" :
                                 "用于得到某一路正常工作时的标准流量，保存后作为低流量和高流量判断基准。",
                             "start_standard",
                             2,
                             "启动水路流量校准");
    Esp32BaseWeb::sendChunk("</div>");
    Esp32BaseWeb::endPanel();
}

void sendCalibrationResultPanel(const CalibrationSnapshot& snapshot) {
    if (!snapshot.resultReady) {
        return;
    }

    char buf[768];
    Esp32BaseWeb::beginPanel("校准结果");
    snprintf(buf,
             sizeof(buf),
             "<div class='ir-calib-result'><div><b>%s</b><span>校准类型</span></div><div><b>水路 %u</b><span>本次校准水路</span></div><div><b>%lu</b><span>累计脉冲数</span></div></div>",
             calibrationModeLabel(snapshot.mode),
             snapshot.zoneId,
             static_cast<unsigned long>(snapshot.pulses));
    Esp32BaseWeb::sendChunk(buf);

    if (snapshot.mode == CalibrationMode::FlowMeterVolume) {
        Esp32BaseWeb::sendChunk("<form class='ir-form' method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='save_volume'><div class='ir-field'><label>实测水量</label><span class='ir-help'>填写本次实际接到的水量，系统据此计算每升脉冲数。</span><div class='ir-input-unit'><input type='number' name='measuredMl' min='1' max='100000' value='1000'><span class='ir-unit'>ml</span></div></div><div class='actions'><input type='submit' value='保存每升脉冲数'></div></form>");
    } else if (snapshot.mode == CalibrationMode::ZoneStandardFlow) {
        char flowText[24];
        flowRateToText(snapshot.suggestedFlowMlPerMin, flowText, sizeof(flowText));
        snprintf(buf, sizeof(buf), "<form class='ir-form' method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='save_standard'><input type='hidden' name='zone' value='%u'><div class='ir-field'><label>标准流量</label><span class='ir-help'>保存后用于该水路的低流量和高流量判断。</span><div class='ir-input-unit'><input type='number' name='standardFlow' min='0.001' max='100' step='0.001' value='%s'><span class='ir-unit'>L/min</span></div></div><div class='actions'><input type='submit' value='保存水路标准流量'></div></form>",
                 snapshot.zoneId,
                 flowText);
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
        Esp32BaseWeb::sendChunk("<div class='ir-calib-result'><div><b>");
        Esp32BaseWeb::sendChunk(calibrationModeLabel(snapshot.mode));
        Esp32BaseWeb::sendChunk("</b><span>当前模式</span></div><div><b>");
        snprintf(value, sizeof(value), "水路 %u", snapshot.zoneId);
        Esp32BaseWeb::sendChunk(value);
        Esp32BaseWeb::sendChunk("</b><span>正在出水</span></div><div><b>");
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.pulses));
        Esp32BaseWeb::sendChunk(value);
        Esp32BaseWeb::sendChunk("</b><span>已累计脉冲</span></div></div>");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='stop'><div class='actions'><input class='danger' type='submit' value='停止校准'></div></form>");
        Esp32BaseWeb::endPanel();
    } else {
        sendCalibrationResultPanel(snapshot);
        Esp32BaseWeb::beginPanel("校准流程");
        Esp32BaseWeb::sendChunk("<div class='ir-calib-steps'><div class='ir-calib-step'><span class='ir-step-no'>1</span><div><b>校准流量计</b><span class='ir-help'>运行任意已启用水路，接取实际水量，保存每升脉冲数。</span></div></div><div class='ir-calib-step'><span class='ir-step-no'>2</span><div><b>校准水路标准流量</b><span class='ir-help'>每个水路单独运行，保存正常出水时的 L/min，供异常流量判断使用。</span></div></div><div class='ir-calib-step'><span class='ir-step-no'>3</span><div><b>回到水路页复核</b><span class='ir-help'>确认各水路标准流量已写入；浇水时长仍在手动任务或计划中设置。</span></div></div></div>");
        Esp32BaseWeb::endPanel();
        sendCalibrationStartPanel();
    }

    Esp32BaseWeb::sendFooter();
}

void eventTimeToText(const Esp32BaseAppEventRecord& event, char* out, size_t len) {
    if (event.epochSec != 0 && Esp32BaseTime::formatEpoch(event.epochSec, out, len, "%Y-%m-%d %H:%M")) {
        return;
    }
    snprintf(out, len, "启动后 %lus", static_cast<unsigned long>(event.uptimeSec));
}

void writeEventObjectText(const char* value, size_t maxLen) {
    char text[64];
    size_t n = 0;
    while (n < maxLen && value[n] != '\0' && n < sizeof(text) - 1) {
        text[n] = value[n];
        ++n;
    }
    text[n] = '\0';
    if (strcmp(text, "controller") == 0) {
        Esp32BaseWeb::sendChunk("控制器");
    } else if (strcmp(text, "run") == 0) {
        Esp32BaseWeb::sendChunk("浇水任务");
    } else if (strcmp(text, "zone") == 0) {
        Esp32BaseWeb::sendChunk("水路");
    } else if (strcmp(text, "flow") == 0) {
        Esp32BaseWeb::sendChunk("流量计");
    } else if (strncmp(text, "plan:", 5) == 0) {
        Esp32BaseWeb::sendChunk("计划 ");
        Esp32BaseWeb::writeHtmlEscaped(text + 5);
    } else if (text[0] == '\0') {
        Esp32BaseWeb::sendChunk("无");
    } else {
        Esp32BaseWeb::writeHtmlEscaped(text);
    }
}

void sendEventValues(const Esp32BaseAppEventRecord& event) {
    bool any = false;
    Esp32BaseWeb::sendChunk("<div class='ir-event-values'>");
    if (event.valueMask & Esp32BaseAppEventLog::VALUE1) {
        char buf[32];
        snprintf(buf, sizeof(buf), "<span class='ir-compact-item'>v1 %ld</span>", static_cast<long>(event.value1));
        Esp32BaseWeb::sendChunk(buf);
        any = true;
    }
    if (event.valueMask & Esp32BaseAppEventLog::VALUE2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "<span class='ir-compact-item'>v2 %ld</span>", static_cast<long>(event.value2));
        Esp32BaseWeb::sendChunk(buf);
        any = true;
    }
    if (event.valueMask & Esp32BaseAppEventLog::VALUE3) {
        char buf[32];
        snprintf(buf, sizeof(buf), "<span class='ir-compact-item'>v3 %ld</span>", static_cast<long>(event.value3));
        Esp32BaseWeb::sendChunk(buf);
        any = true;
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<span class='ir-table-note'>无</span>");
    }
    Esp32BaseWeb::sendChunk("</div>");
}

void sendEventTableRow(const Esp32BaseAppEventRecord& event, void* user) {
    bool* any = static_cast<bool*>(user);
    if (any != nullptr) {
        *any = true;
    }
    char timeText[24];
    eventTimeToText(event, timeText, sizeof(timeText));
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "<tr><td class='num'>%lu</td><td>%s</td><td><div class='ir-event-summary'><b>%s</b><span class='ir-table-note'>%s · %s</span></div></td><td>",
             static_cast<unsigned long>(event.id),
             eventLevelLabel(event.level),
             eventSourceLabel(event.source),
             eventTypeLabel(event.type),
             eventReasonLabel(event.reason));
    Esp32BaseWeb::sendChunk(buf);
    writeEventObjectText(event.object, sizeof(event.object));
    Esp32BaseWeb::sendChunk("</td><td>");
    sendEventValues(event);
    snprintf(buf, sizeof(buf), "</td><td>%s</td><td><a class='btnlink info' href='/irrigation/events/detail?id=%lu'>详情</a></td></tr>",
             timeText,
             static_cast<unsigned long>(event.id));
    Esp32BaseWeb::sendChunk(buf);
}

const char* boolLabel(bool value) {
    return value ? "是" : "否";
}

void sendDetailTextRow(const char* name, const char* value, bool full = false) {
    Esp32BaseWeb::sendChunk(full ? "<div class='ir-detail-row full'><span>" : "<div class='ir-detail-row'><span>");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("</span><b>");
    Esp32BaseWeb::writeHtmlEscaped(value != nullptr && value[0] != '\0' ? value : "无");
    Esp32BaseWeb::sendChunk("</b></div>");
}

void sendDetailUintRow(const char* name, uint32_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(value));
    sendDetailTextRow(name, buf);
}

void sendDetailIntRow(const char* name, int32_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", static_cast<long>(value));
    sendDetailTextRow(name, buf);
}

void sendDetailHexRow(const char* name, uint32_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(value));
    sendDetailTextRow(name, buf);
}

void sendDetailBoolRow(const char* name, bool value) {
    sendDetailTextRow(name, boolLabel(value));
}

struct EventFindState {
    uint32_t id;
    bool found;
    Esp32BaseAppEventLog::StoreRecord item;
};

void findEventStoreRecord(const Esp32BaseAppEventLog::StoreRecord& item, void* user) {
    EventFindState* state = static_cast<EventFindState*>(user);
    if (state == nullptr || state->found) {
        return;
    }
    if (item.record.id == state->id) {
        state->item = item;
        state->found = true;
    }
}

void sendEventDetailSectionStart(const char* title) {
    Esp32BaseWeb::sendChunk("<div class='ir-detail-section'><h2>");
    Esp32BaseWeb::sendChunk(title);
    Esp32BaseWeb::sendChunk("</h2><div class='ir-detail-grid'>");
}

void sendEventDetailSectionEnd() {
    Esp32BaseWeb::sendChunk("</div></div>");
}

void sendEventTextDetailRow(const char* name, const char* value, size_t maxLen, bool full = false) {
    char text[80];
    size_t n = 0;
    while (n < maxLen && value[n] != '\0' && n < sizeof(text) - 1) {
        text[n] = value[n];
        ++n;
    }
    text[n] = '\0';
    sendDetailTextRow(name, text, full);
}

void sendEventDetail(const Esp32BaseAppEventLog::StoreRecord& item) {
    const Esp32BaseAppEventRecord& event = item.record;
    char timeText[32];
    eventTimeToText(event, timeText, sizeof(timeText));
    char levelText[32];
    snprintf(levelText, sizeof(levelText), "%s (%u)", eventLevelLabel(event.level), static_cast<unsigned>(event.level));
    char sourceText[80];
    snprintf(sourceText, sizeof(sourceText), "%s", eventSourceLabel(event.source));
    char typeText[80];
    snprintf(typeText, sizeof(typeText), "%s", eventTypeLabel(event.type));
    char reasonText[80];
    snprintf(reasonText, sizeof(reasonText), "%s", eventReasonLabel(event.reason));

    sendEventDetailSectionStart("基本信息");
    sendDetailUintRow("事件 ID", event.id);
    sendDetailTextRow("级别", levelText);
    sendDetailTextRow("来源", sourceText);
    sendDetailTextRow("事件类型", typeText);
    sendDetailTextRow("原因", reasonText);
    Esp32BaseWeb::sendChunk("<div class='ir-detail-row'><span>对象</span><b>");
    writeEventObjectText(event.object, sizeof(event.object));
    Esp32BaseWeb::sendChunk("</b></div>");
    sendDetailUintRow("代码 code", event.code);
    sendEventTextDetailRow("文本 text", event.text, sizeof(event.text), true);
    sendEventDetailSectionEnd();

    sendEventDetailSectionStart("时间信息");
    sendDetailTextRow("显示时间", timeText);
    sendDetailUintRow("epochSec", event.epochSec);
    sendDetailUintRow("bootId", event.bootId);
    sendDetailUintRow("uptimeSec", event.uptimeSec);
    sendEventDetailSectionEnd();

    sendEventDetailSectionStart("数值信息");
    sendDetailIntRow("value1", event.value1);
    sendDetailIntRow("value2", event.value2);
    sendDetailIntRow("value3", event.value3);
    sendDetailUintRow("valueMask", event.valueMask);
    sendEventDetailSectionEnd();

    sendEventDetailSectionStart("原始字段");
    sendDetailHexRow("magic", event.magic);
    sendEventTextDetailRow("source", event.source, sizeof(event.source));
    sendEventTextDetailRow("type", event.type, sizeof(event.type));
    sendEventTextDetailRow("reason", event.reason, sizeof(event.reason));
    sendEventTextDetailRow("object", event.object, sizeof(event.object), true);
    sendDetailUintRow("flags", event.flags);
    sendDetailUintRow("reserved", event.reserved);
    sendDetailUintRow("crc16", event.crc16);
    sendEventDetailSectionEnd();

    sendEventDetailSectionStart("存储信息");
    sendDetailUintRow("slot", item.slot);
    sendDetailUintRow("index", item.index);
    sendDetailUintRow("offset", item.offset);
    sendDetailTextRow("状态", Esp32BaseAppEventLog::storeRecordStatusName(item.status));
    sendDetailUintRow("storedCrc16", item.storedCrc16);
    sendDetailUintRow("calculatedCrc16", item.calculatedCrc16);
    sendDetailBoolRow("readOk", item.readOk);
    sendDetailBoolRow("magicOk", item.magicOk);
    sendDetailBoolRow("levelOk", item.levelOk);
    sendDetailBoolRow("crcOk", item.crcOk);
    sendDetailBoolRow("committed", item.committed);
    sendEventDetailSectionEnd();
}

void handleEventDetailPage() {
    uint32_t id = 0;
    if (!parseU32Param("id", 1, 1000000000UL, 0, id)) {
        Esp32BaseWeb::sendText(400, "事件编号无效");
        return;
    }
    Esp32BaseAppEventLog::StoreInfo info;
    if (!Esp32BaseAppEventLog::readStoreInfo(info)) {
        Esp32BaseWeb::sendText(500, Esp32BaseAppEventLog::lastError());
        return;
    }
    EventFindState state = {};
    state.id = id;
    Esp32BaseAppEventLog::readStoreRecords(0, info.count, findEventStoreRecord, &state);
    if (!state.found) {
        Esp32BaseWeb::sendText(404, "事件不存在");
        return;
    }

    Esp32BaseWeb::sendHeader("事件详情");
    Esp32BaseWeb::sendPageTitle("事件详情", "事件记录的完整字段");
    Esp32BaseWeb::beginPanel("事件记录");
    sendEventDetail(state.item);
    Esp32BaseWeb::sendChunk("<div class='actions'><a class='btnlink secondary' href='/irrigation/events'>返回事件</a></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void sendEventsTable(uint32_t page, uint32_t perPage, uint32_t total) {
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='ir-data-table'><thead><tr><th class='num'>编号</th><th>级别</th><th>事件摘要</th><th>对象</th><th>关键数值</th><th>时间</th><th>操作</th></tr></thead><tbody>");
    bool any = false;
    const uint32_t offset = (page - 1U) * perPage;
    if (offset <= 65535UL && perPage <= 65535UL) {
        Esp32BaseAppEventLog::readLatest(static_cast<uint16_t>(offset), static_cast<uint16_t>(perPage), sendEventTableRow, &any);
    }
    if (!any) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>暂无可显示的事件</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");

    Esp32BaseWeb::Pagination pagination = {"/irrigation/events", "", page, perPage, total};
    Esp32BaseWeb::sendPagination(pagination);
}

void sendHistoryTable(char* historyText) {
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='ir-data-table'><thead><tr><th class='num'>编号</th><th>来源</th><th>结果</th><th>原因</th><th>开始时间</th><th>结束时间</th><th class='num'>步骤数</th></tr></thead><tbody>");

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
                 "<tr><td class='num'>%lu</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td class='num'>%u</td></tr>",
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
    Esp32BaseWeb::sendHeader("浇水历史");
    Esp32BaseWeb::sendPageTitle("浇水历史", "手动浇水和自动计划的运行记录");
    uint32_t page = 1;
    uint32_t perPage = 10;
    readPagingParam("page", 1, 65535, 1, page);
    readPagingParam("per", 10, 50, 10, perPage);

    Esp32BaseWeb::beginPanel("浇水记录");
    uint32_t total = 0;
    if (!HistoryService::readPage(page, perPage, g_historyViewBuffer, sizeof(g_historyViewBuffer), total)) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "历史不可用", "读取浇水历史失败。");
    } else if (total == 0) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "暂无浇水历史", "手动浇水或自动计划完成、停止、故障后，会在这里显示记录。");
    } else {
        sendHistoryTable(g_historyViewBuffer);
        Esp32BaseWeb::Pagination pagination = {"/irrigation/history", "", page, perPage, total};
        Esp32BaseWeb::sendPagination(pagination);
    }
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleEventsPage() {
    Esp32BaseWeb::sendHeader("事件");
    Esp32BaseWeb::sendPageTitle("事件", "系统、计划、校准和保护事件");
    uint32_t page = 1;
    uint32_t perPage = 10;
    readPagingParam("page", 1, 65535, 1, page);
    readPagingParam("per", 10, 50, 10, perPage);

    Esp32BaseAppEventLog::StoreInfo info;
    const bool ok = Esp32BaseAppEventLog::readStoreInfo(info);
    Esp32BaseWeb::beginPanel("事件记录");
    if (!ok) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "事件不可用", Esp32BaseAppEventLog::lastError());
    } else if (info.validCount == 0) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "暂无事件", "系统运行、计划触发、校准保存或保护动作会记录到这里。");
    } else {
        sendEventsTable(page, perPage, info.validCount);
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
    Esp32BaseWeb::addPage("/irrigation/history", "浇水历史", handleHistoryPage);
    Esp32BaseWeb::addPage("/irrigation/events", "事件", handleEventsPage);
    Esp32BaseWeb::addRoute("/irrigation/events/detail", Esp32BaseWeb::METHOD_GET, handleEventDetailPage);
    Esp32BaseWeb::addRoute("/irrigation/run/start", Esp32BaseWeb::METHOD_POST, handleManualStartPost);
    Esp32BaseWeb::addRoute("/irrigation/run/plan-now", Esp32BaseWeb::METHOD_POST, handlePlanNowPost);
    Esp32BaseWeb::addRoute("/irrigation/run/stop", Esp32BaseWeb::METHOD_POST, handleStopPost);
    Esp32BaseWeb::addRoute("/irrigation/zones/edit", Esp32BaseWeb::METHOD_GET, handleZoneEditPage);
    Esp32BaseWeb::addRoute("/irrigation/zones/save", Esp32BaseWeb::METHOD_POST, handleZoneSavePost);
    Esp32BaseWeb::addRoute("/irrigation/plans/new", Esp32BaseWeb::METHOD_GET, handlePlanNewPage);
    Esp32BaseWeb::addRoute("/irrigation/plans/edit", Esp32BaseWeb::METHOD_GET, handlePlanEditPage);
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
