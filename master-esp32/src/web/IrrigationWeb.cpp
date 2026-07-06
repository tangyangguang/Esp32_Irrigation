#include "IrrigationWeb.h"

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

const char* calibrationModeName(CalibrationMode mode) {
    switch (mode) {
        case CalibrationMode::None: return "none";
        case CalibrationMode::FlowMeterVolume: return "flow_meter_volume";
        case CalibrationMode::ZoneStandardFlow: return "zone_standard_flow";
    }
    return "unknown";
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
        snprintf(out, len, "None");
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
    char value[16];
    snprintf(key, sizeof(key), "z%u", zoneId);
    if (!Esp32BaseWeb::getParam(key, value, sizeof(value))) {
        out = 0;
        return true;
    }
    char* end = nullptr;
    const unsigned long minutes = strtoul(value, &end, 10);
    if (end == value || minutes > 120UL) {
        return false;
    }
    out = static_cast<uint32_t>(minutes) * 60UL;
    return true;
}

bool readZoneParam(uint8_t& zoneId) {
    char value[8];
    if (!Esp32BaseWeb::getParam("zone", value, sizeof(value))) {
        return false;
    }
    const unsigned long parsed = strtoul(value, nullptr, 10);
    if (parsed < 1 || parsed > kMaxZones) {
        return false;
    }
    zoneId = static_cast<uint8_t>(parsed);
    return true;
}

bool readDurationMinutesParam(uint32_t& durationSec) {
    uint32_t minutes = 0;
    if (!parseU32Param("durationMin", 1, 120, 5, minutes)) {
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
            Esp32BaseWeb::sendText(400, "invalid duration");
            return;
        }
    }

    RunReason reason = RunReason::None;
    if (!RunController::startManual(durations, reason)) {
        Esp32BaseWeb::sendText(409, runReasonToString(reason));
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
        Esp32BaseWeb::sendText(400, "missing action");
        return;
    }

    if (strcmp(action, "start_volume") == 0 || strcmp(action, "start_standard") == 0) {
        uint8_t zoneId = 0;
        uint32_t durationSec = 0;
        if (!readZoneParam(zoneId) || !readDurationMinutesParam(durationSec)) {
            Esp32BaseWeb::sendText(400, "invalid calibration request");
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
            Esp32BaseWeb::sendText(400, "invalid measured volume");
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
            Esp32BaseWeb::sendText(400, "invalid standard flow");
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

    Esp32BaseWeb::sendText(400, "unknown action");
}

void handlePlanNowPost() {
    if (!Esp32BaseWeb::checkPostAllowed("plan_now")) {
        return;
    }
    char value[8];
    if (!Esp32BaseWeb::getParam("plan", value, sizeof(value))) {
        Esp32BaseWeb::sendText(400, "missing plan");
        return;
    }
    const uint8_t planId = static_cast<uint8_t>(strtoul(value, nullptr, 10));
    RunReason reason = RunReason::None;
    if (!RunController::startPlanNow(planId, reason)) {
        Esp32BaseWeb::sendText(409, runReasonToString(reason));
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
        Esp32BaseWeb::sendText(409, "zones locked while running");
        return;
    }

    char value[32];
    if (!Esp32BaseWeb::getParam("id", value, sizeof(value))) {
        Esp32BaseWeb::sendText(400, "missing zone id");
        return;
    }
    const uint8_t zoneId = static_cast<uint8_t>(strtoul(value, nullptr, 10));
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
    if (!parseU32Param("defaultMinutes", 0, 120, zone.defaultDurationSec / 60UL, minutes)) {
        Esp32BaseWeb::sendText(400, "invalid default duration");
        return;
    }
    zone.defaultDurationSec = minutes * 60UL;

    uint32_t flow = 0;
    if (!parseU32Param("standardFlow", 0, 100000, zone.standardFlowMlPerMin, flow)) {
        Esp32BaseWeb::sendText(400, "invalid standard flow");
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
    char name[kPlanNameLength] = "New plan";
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
    char value[8];
    if (!Esp32BaseWeb::getParam("id", value, sizeof(value))) {
        Esp32BaseWeb::sendText(400, "missing plan id");
        return;
    }
    const uint8_t planId = static_cast<uint8_t>(strtoul(value, nullptr, 10));
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
    if (!Esp32BaseWeb::getParam("id", value, sizeof(value))) {
        Esp32BaseWeb::sendText(400, "missing plan id");
        return;
    }
    const uint8_t planId = static_cast<uint8_t>(strtoul(value, nullptr, 10));
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
            Esp32BaseWeb::sendText(400, "invalid start time");
            return;
        }
        plan.startTimes[i].enabled = enabled;
        plan.startTimes[i].minuteOfDay = minuteOfDay;
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "z%u", i + 1);
        uint32_t minutes = 0;
        if (!parseU32Param(key, 0, 120, plan.zoneDurationSec[i] / 60UL, minutes)) {
            Esp32BaseWeb::sendText(400, "invalid zone duration");
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
        Esp32BaseWeb::sendText(409, "settings locked while running");
        return;
    }

    IrrigationConfig next = ConfigStore::config();
    next.supply.pumpEnabled = parseBoolParam("pumpEnabled");
    next.supply.lowLevelEnabled = parseBoolParam("lowLevelEnabled");
    next.supply.lowLevelContactType = parseBoolParam("lowLevelNormallyClosed") ? ContactType::NormallyClosed : ContactType::NormallyOpen;

    uint32_t value = 0;
    if (!parseU32Param("pumpStartDelayMs", 0, 10000, next.supply.pumpStartDelayMs, value)) {
        Esp32BaseWeb::sendText(400, "invalid pump start delay");
        return;
    }
    next.supply.pumpStartDelayMs = value;

    if (!parseU32Param("pumpStopDelayMs", 0, 10000, next.supply.pumpStopDelayMs, value)) {
        Esp32BaseWeb::sendText(400, "invalid pump stop delay");
        return;
    }
    next.supply.pumpStopDelayMs = value;

    if (!parseU32Param("lowLevelDebounceMs", 0, 10000, next.supply.lowLevelDebounceMs, value)) {
        Esp32BaseWeb::sendText(400, "invalid low-level debounce");
        return;
    }
    next.supply.lowLevelDebounceMs = value;

    if (!parseU32Param("pulsesPerLiter", 0, 100000, next.flow.pulsesPerLiter, value)) {
        Esp32BaseWeb::sendText(400, "invalid pulses per liter");
        return;
    }
    next.flow.pulsesPerLiter = value;

    if (!parseU32Param("startupGraceSec", 0, 120, next.flow.startupGraceSec, value)) {
        Esp32BaseWeb::sendText(400, "invalid startup grace");
        return;
    }
    next.flow.startupGraceSec = value;

    if (!parseU32Param("noFlowConfirmSec", 1, 600, next.flow.noFlowConfirmSec, value)) {
        Esp32BaseWeb::sendText(400, "invalid no-flow confirm");
        return;
    }
    next.flow.noFlowConfirmSec = value;

    if (!parseU32Param("leakWindowSec", 1, 600, next.flow.leakWindowSec, value)) {
        Esp32BaseWeb::sendText(400, "invalid leak window");
        return;
    }
    next.flow.leakWindowSec = value;

    if (!parseU32Param("leakPulseThreshold", 1, 1000, next.flow.leakPulseThreshold, value)) {
        Esp32BaseWeb::sendText(400, "invalid leak pulse threshold");
        return;
    }
    next.flow.leakPulseThreshold = value;

    if (!parseU32Param("lowFlowPercent", 1, 100, next.flow.lowFlowPercent, value)) {
        Esp32BaseWeb::sendText(400, "invalid low-flow percent");
        return;
    }
    next.flow.lowFlowPercent = static_cast<uint8_t>(value);

    if (!parseU32Param("highFlowPercent", 100, 1000, next.flow.highFlowPercent, value)) {
        Esp32BaseWeb::sendText(400, "invalid high-flow percent");
        return;
    }
    next.flow.highFlowPercent = static_cast<uint16_t>(value);

    if (!parseU32Param("pullInMs", 50, 3000, next.valve.pullInMs, value)) {
        Esp32BaseWeb::sendText(400, "invalid valve pull-in");
        return;
    }
    next.valve.pullInMs = value;

    if (!parseU32Param("holdPercent", 1, 100, next.valve.holdPercent, value)) {
        Esp32BaseWeb::sendText(400, "invalid valve hold percent");
        return;
    }
    next.valve.holdPercent = static_cast<uint8_t>(value);

    if (!parseU32Param("maxZoneDurationMin", 1, 360, next.valve.maxZoneDurationSec / 60UL, value)) {
        Esp32BaseWeb::sendText(400, "invalid max Zone duration");
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
    Esp32BaseWeb::beginPanel("Manual watering");
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run/start'><table><thead><tr><th>Zone</th><th>Status</th><th>Minutes</th></tr></thead><tbody>");
    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (!zone.enabled) {
            continue;
        }
        char row[192];
        snprintf(row, sizeof(row),
                 "<tr><td>Zone %u</td><td>Enabled</td><td><input name='z%u' type='number' min='0' max='120' value='%lu'></td></tr>",
                 zone.id,
                 zone.id,
                 static_cast<unsigned long>(zone.defaultDurationSec / 60UL));
        Esp32BaseWeb::sendChunk(row);
    }
    Esp32BaseWeb::sendChunk("</tbody></table><div class='actions'><input type='submit' value='Start manual run'></div></form>");
    Esp32BaseWeb::endPanel();
}

void sendPlanNowPanel() {
    const IrrigationConfig& config = ConfigStore::config();
    bool hasPlan = false;
    Esp32BaseWeb::beginPanel("Run plan now");
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run/plan-now'><select name='plan'>");
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
    Esp32BaseWeb::sendChunk("</select><div class='actions'><input type='submit' value='Run selected plan now'");
    if (!hasPlan) {
        Esp32BaseWeb::sendChunk(" disabled");
    }
    Esp32BaseWeb::sendChunk("></div></form>");
    if (!hasPlan) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "No enabled plans", "Enable a plan before using this shortcut.");
    }
    Esp32BaseWeb::endPanel();
}

void handleDashboardPage() {
    const StatusSnapshot status = RunController::statusSnapshot();
    char value[32];

    Esp32BaseWeb::sendHeader("Irrigation");
    Esp32BaseWeb::sendPageTitle("Irrigation", "Local 12V Zone controller");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("State", runStateName(status.runState), status.busy ? "Running" : "Idle");
    snprintf(value, sizeof(value), "%u / %u", status.enabledZoneCount, kMaxZones);
    Esp32BaseWeb::sendMetric("Enabled Zones", value, "Hardware max 6");
    snprintf(value, sizeof(value), "%u / %u", status.enabledPlanCount, kMaxPlans);
    Esp32BaseWeb::sendMetric("Enabled Plans", value, "Daily plans");
    epochToText(status.nextRunEpoch, value, sizeof(value));
    Esp32BaseWeb::sendMetric("Next Run", value, status.nextRunEpoch == 0 ? "No enabled schedule" : "Local time");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::beginPanel("Actions");
    Esp32BaseWeb::sendInfoRowCompactLink("Manual run", "Set enabled Zone durations and start a sequential run.", nullptr, "/irrigation/run", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("Plans", "Daily schedules with enable and disable control.", nullptr, "/irrigation/plans", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("Zones", "Enable, name and tune each fixed hardware Zone.", nullptr, "/irrigation/zones", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("Calibration", "Calibrate flow pulses and Zone standard flow.", nullptr, "/irrigation/calibration", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("Settings", "Supply, flow and valve driver settings.", nullptr, "/irrigation/settings", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("History", "Recent run records and stop reasons.", nullptr, "/irrigation/history", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("Status API", "Machine-readable current state.", nullptr, "/api/status", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleRunPage() {
    const StatusSnapshot status = RunController::statusSnapshot();
    Esp32BaseWeb::sendHeader("Run");
    Esp32BaseWeb::sendPageTitle("Run", "Manual sequential watering");
    char value[32];
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("State", runStateName(status.runState), status.busy ? "Running" : "Idle");
    snprintf(value, sizeof(value), "%u", status.activeZoneId);
    Esp32BaseWeb::sendMetric("Active Zone", value, status.activeZoneId == 0 ? "None" : "Open");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(status.currentFlowMlPerMin));
    Esp32BaseWeb::sendMetric("Flow ml/min", value, "Estimated");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(status.currentRunVolumeMl));
    Esp32BaseWeb::sendMetric("Volume ml", value, "Current step");
    Esp32BaseWeb::endMetricGrid();
    if (status.busy) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "Run active", runStateName(status.runState));
        Esp32BaseWeb::beginPanel("Stop");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run/stop'><div class='actions'><input class='danger' type='submit' value='Stop run'></div></form>");
        Esp32BaseWeb::endPanel();
    } else {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Idle", "No active watering run.");
    }
    sendRunPanel();
    sendPlanNowPanel();
    Esp32BaseWeb::sendFooter();
}

void handleZonesPage() {
    Esp32BaseWeb::sendHeader("Zones");
    Esp32BaseWeb::sendPageTitle("Zones", "Enable only the wired irrigation Zones");
    Esp32BaseWeb::beginPanel("Zone configuration");
    Esp32BaseWeb::sendChunk("<table><thead><tr><th>Zone</th><th>Name</th><th>Enabled</th><th>Default minutes</th><th>Standard flow ml/min</th><th>Save</th></tr></thead><tbody>");

    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        char buf[256];
        snprintf(buf, sizeof(buf), "<tr><form method='post' action='/irrigation/zones/save'><td>Zone %u<input type='hidden' name='id' value='%u'></td><td><input name='name' maxlength='23' value='",
                 zone.id, zone.id);
        Esp32BaseWeb::sendChunk(buf);
        sendEscapedValue(zone.name);
        snprintf(buf, sizeof(buf), "'></td><td><input type='checkbox' name='enabled' value='1'%s></td><td><input type='number' min='0' max='120' name='defaultMinutes' value='%lu'></td><td><input type='number' min='0' max='100000' name='standardFlow' value='%lu'></td><td><input type='submit' value='Save'></td></form></tr>",
                 zone.enabled ? " checked" : "",
                 static_cast<unsigned long>(zone.defaultDurationSec / 60UL),
                 static_cast<unsigned long>(zone.standardFlowMlPerMin));
        Esp32BaseWeb::sendChunk(buf);
    }

    Esp32BaseWeb::sendChunk("</tbody></table>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void sendPlanStartFields(const WateringPlan& plan) {
    Esp32BaseWeb::sendChunk("<fieldset><legend>Start times</legend>");
    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        char timeText[8];
        minuteToText(plan.startTimes[i].minuteOfDay, timeText, sizeof(timeText));
        char buf[192];
        snprintf(buf, sizeof(buf), "<label><input type='checkbox' name='se%u' value='1'%s> Slot %u</label> <input type='time' name='st%u' value='",
                 i,
                 plan.startTimes[i].enabled ? " checked" : "",
                 i + 1,
                 i);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::sendChunk(timeText);
        Esp32BaseWeb::sendChunk("'><br>");
    }
    Esp32BaseWeb::sendChunk("</fieldset>");
}

void sendPlanZoneDurationFields(const WateringPlan& plan) {
    Esp32BaseWeb::sendChunk("<fieldset><legend>Enabled Zone durations</legend><table><thead><tr><th>Zone</th><th>Minutes</th></tr></thead><tbody>");
    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (!zone.enabled) {
            continue;
        }
        char buf[160];
        snprintf(buf, sizeof(buf), "<tr><td>Zone %u ", zone.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        snprintf(buf, sizeof(buf), "</td><td><input type='number' min='0' max='120' name='z%u' value='%lu'></td></tr>",
                 zone.id,
                 static_cast<unsigned long>(plan.zoneDurationSec[i] / 60UL));
        Esp32BaseWeb::sendChunk(buf);
    }
    Esp32BaseWeb::sendChunk("</tbody></table></fieldset>");
}

void handlePlansPage() {
    Esp32BaseWeb::sendHeader("Plans");
    Esp32BaseWeb::sendPageTitle("Plans", "Daily schedules, up to 8 plans and 4 start times each");

    Esp32BaseWeb::beginPanel("Create plan");
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/plans/create'><input name='name' maxlength='31' value='New plan'><div class='actions'><input type='submit' value='Create disabled plan'></div></form>");
    Esp32BaseWeb::endPanel();

    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        if (!plan.used) {
            continue;
        }
        Esp32BaseWeb::beginPanel(plan.name);
        char buf[160];
        snprintf(buf, sizeof(buf), "<form method='post' action='/irrigation/plans/save'><input type='hidden' name='id' value='%u'><label>Name</label><input name='name' maxlength='31' value='",
                 plan.id);
        Esp32BaseWeb::sendChunk(buf);
        sendEscapedValue(plan.name);
        Esp32BaseWeb::sendChunk("'><label><input type='checkbox' name='enabled' value='1'");
        sendChecked(plan.enabled);
        Esp32BaseWeb::sendChunk("> Enabled for daily schedule and run-now</label>");
        sendPlanStartFields(plan);
        sendPlanZoneDurationFields(plan);
        Esp32BaseWeb::sendChunk("<div class='actions'><input type='submit' value='Save plan'></div></form>");
        snprintf(buf, sizeof(buf), "<form method='post' action='/irrigation/plans/delete' onsubmit=\"return confirm('Delete plan?')\"><input type='hidden' name='id' value='%u'><div class='actions'><input class='danger' type='submit' value='Delete plan'></div></form>",
                 plan.id);
        Esp32BaseWeb::sendChunk(buf);
        Esp32BaseWeb::endPanel();
    }

    Esp32BaseWeb::sendFooter();
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

void handleSettingsPage() {
    const IrrigationConfig& config = ConfigStore::config();
    Esp32BaseWeb::sendHeader("Settings");
    Esp32BaseWeb::sendPageTitle("Settings", "Supply, flow safety and valve driver");
    if (RunController::busy()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "Settings locked", "Stop the current run before saving global settings.");
    }

    Esp32BaseWeb::beginPanel("Global settings");
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/settings/save'><table><tbody>");

    Esp32BaseWeb::sendChunk("<tr><td>Self-priming pump signal</td><td><input type='checkbox' name='pumpEnabled' value='1'");
    sendChecked(config.supply.pumpEnabled);
    Esp32BaseWeb::sendChunk("></td></tr>");
    Esp32BaseWeb::sendChunk("<tr><td>Pump start delay ms</td><td>");
    sendNumberInput("pumpStartDelayMs", config.supply.pumpStartDelayMs, 0, 10000);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Pump stop delay ms</td><td>");
    sendNumberInput("pumpStopDelayMs", config.supply.pumpStopDelayMs, 0, 10000);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Low-level sensor</td><td><input type='checkbox' name='lowLevelEnabled' value='1'");
    sendChecked(config.supply.lowLevelEnabled);
    Esp32BaseWeb::sendChunk("></td></tr><tr><td>Low-level normally closed</td><td><input type='checkbox' name='lowLevelNormallyClosed' value='1'");
    sendChecked(config.supply.lowLevelContactType == ContactType::NormallyClosed);
    Esp32BaseWeb::sendChunk("></td></tr><tr><td>Low-level debounce ms</td><td>");
    sendNumberInput("lowLevelDebounceMs", config.supply.lowLevelDebounceMs, 0, 10000);

    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Flow pulses per liter</td><td>");
    sendNumberInput("pulsesPerLiter", config.flow.pulsesPerLiter, 0, 100000);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Startup flow grace sec</td><td>");
    sendNumberInput("startupGraceSec", config.flow.startupGraceSec, 0, 120);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>No-flow confirm sec</td><td>");
    sendNumberInput("noFlowConfirmSec", config.flow.noFlowConfirmSec, 1, 600);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Standby leak window sec</td><td>");
    sendNumberInput("leakWindowSec", config.flow.leakWindowSec, 1, 600);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Standby leak pulses</td><td>");
    sendNumberInput("leakPulseThreshold", config.flow.leakPulseThreshold, 1, 1000);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Low-flow percent</td><td>");
    sendNumberInput("lowFlowPercent", config.flow.lowFlowPercent, 1, 100);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>High-flow percent</td><td>");
    sendNumberInput("highFlowPercent", config.flow.highFlowPercent, 100, 1000);

    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Valve pull-in ms</td><td>");
    sendNumberInput("pullInMs", config.valve.pullInMs, 50, 3000);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Valve hold percent</td><td>");
    sendNumberInput("holdPercent", config.valve.holdPercent, 1, 100);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Max Zone duration min</td><td>");
    sendNumberInput("maxZoneDurationMin", config.valve.maxZoneDurationSec / 60UL, 1, 360);
    Esp32BaseWeb::sendChunk("</td></tr>");

    Esp32BaseWeb::sendChunk("</tbody></table><div class='actions'><input type='submit' value='Save settings'");
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
        snprintf(buf, sizeof(buf), "<option value='%u'%s>Zone %u ",
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
    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='");
    Esp32BaseWeb::sendChunk(action);
    Esp32BaseWeb::sendChunk("'><table><tbody><tr><td>Zone</td><td>");
    sendEnabledZoneSelect(1);
    Esp32BaseWeb::sendChunk("</td></tr><tr><td>Duration minutes</td><td>");
    sendNumberInput("durationMin", defaultMinutes, 1, 120);
    Esp32BaseWeb::sendChunk("</td></tr></tbody></table><div class='actions'><input type='submit' value='Start calibration'></div></form>");
    Esp32BaseWeb::endPanel();
}

void sendCalibrationResultPanel(const CalibrationSnapshot& snapshot) {
    if (!snapshot.resultReady) {
        return;
    }

    char buf[192];
    Esp32BaseWeb::beginPanel("Calibration result");
    snprintf(buf, sizeof(buf), "<table><tbody><tr><td>Mode</td><td>%s</td></tr><tr><td>Zone</td><td>%u</td></tr><tr><td>Pulses</td><td>%lu</td></tr></tbody></table>",
             calibrationModeName(snapshot.mode),
             snapshot.zoneId,
             static_cast<unsigned long>(snapshot.pulses));
    Esp32BaseWeb::sendChunk(buf);

    if (snapshot.mode == CalibrationMode::FlowMeterVolume) {
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='save_volume'><label>Measured water ml</label><input type='number' name='measuredMl' min='1' max='100000' value='1000'><div class='actions'><input type='submit' value='Save pulses per liter'></div></form>");
    } else if (snapshot.mode == CalibrationMode::ZoneStandardFlow) {
        snprintf(buf, sizeof(buf), "<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='save_standard'><input type='hidden' name='zone' value='%u'><label>Standard flow ml/min</label><input type='number' name='standardFlow' min='1' max='100000' value='%lu'><div class='actions'><input type='submit' value='Save Zone standard flow'></div></form>",
                 snapshot.zoneId,
                 static_cast<unsigned long>(snapshot.suggestedFlowMlPerMin));
        Esp32BaseWeb::sendChunk(buf);
        if (snapshot.suggestedFlowMlPerMin == 0) {
            Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "Manual value required", "A completed run and flow pulses per liter are required for an automatic suggestion.");
        }
    }

    Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='clear'><div class='actions'><input type='submit' value='Discard result'></div></form>");
    Esp32BaseWeb::endPanel();
}

void handleCalibrationPage() {
    const CalibrationSnapshot snapshot = CalibrationService::snapshot();
    const IrrigationConfig& config = ConfigStore::config();

    Esp32BaseWeb::sendHeader("Calibration");
    Esp32BaseWeb::sendPageTitle("Calibration", "Flow meter and Zone standard flow");

    Esp32BaseWeb::beginMetricGrid();
    char value[32];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(config.flow.pulsesPerLiter));
    Esp32BaseWeb::sendMetric("Pulses per liter", value, config.flow.pulsesPerLiter == 0 ? "Not calibrated" : "Saved");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.pulses));
    Esp32BaseWeb::sendMetric("Current pulses", value, calibrationModeName(snapshot.mode));
    snprintf(value, sizeof(value), "%u", snapshot.zoneId);
    Esp32BaseWeb::sendMetric("Calibration Zone", value, snapshot.running ? "Running" : "Idle");
    Esp32BaseWeb::endMetricGrid();

    if (snapshot.running) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "Calibration running", "Stop after collecting enough water or wait for the duration to end.");
        Esp32BaseWeb::beginPanel("Stop calibration");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/calibration/action'><input type='hidden' name='action' value='stop'><div class='actions'><input class='danger' type='submit' value='Stop calibration'></div></form>");
        Esp32BaseWeb::endPanel();
    } else {
        sendCalibrationResultPanel(snapshot);
        sendCalibrationStartForm("Flow meter volume calibration", "start_volume", 5);
        sendCalibrationStartForm("Zone standard flow calibration", "start_standard", 2);
    }

    Esp32BaseWeb::sendFooter();
}

void handleHistoryPage() {
    Esp32BaseWeb::sendHeader("History");
    Esp32BaseWeb::sendPageTitle("History", "Recent run records");
    Esp32BaseWeb::beginPanel("Recent runs");
    if (!HistoryService::readRecent(g_historyViewBuffer, sizeof(g_historyViewBuffer))) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "History unavailable", HistoryService::lastError());
    } else if (g_historyViewBuffer[0] == '\0') {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "No run history", "Run records will appear after watering completes, stops or faults.");
    } else {
        Esp32BaseWeb::sendChunk("<pre>");
        Esp32BaseWeb::writeHtmlEscaped(g_historyViewBuffer);
        Esp32BaseWeb::sendChunk("</pre>");
    }
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

} // namespace

void IrrigationWeb::registerRoutes() {
    Esp32BaseWeb::addPage("/irrigation", "Irrigation", handleDashboardPage);
    Esp32BaseWeb::addPage("/irrigation/run", "Run", handleRunPage);
    Esp32BaseWeb::addPage("/irrigation/zones", "Zones", handleZonesPage);
    Esp32BaseWeb::addPage("/irrigation/plans", "Plans", handlePlansPage);
    Esp32BaseWeb::addPage("/irrigation/calibration", "Calibration", handleCalibrationPage);
    Esp32BaseWeb::addPage("/irrigation/settings", "Settings", handleSettingsPage);
    Esp32BaseWeb::addPage("/irrigation/history", "History", handleHistoryPage);
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
    Esp32BaseWeb::addNavItem("/irrigation", "Irrigation");
}

} // namespace Irrigation
