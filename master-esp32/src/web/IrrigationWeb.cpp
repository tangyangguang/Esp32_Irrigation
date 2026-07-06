#include "IrrigationWeb.h"

#include <Esp32Base.h>
#include <stdio.h>
#include <stdlib.h>

#include "ConfigStore.h"
#include "IrrigationConfig.h"
#include "PlanService.h"
#include "RunController.h"
#include "ZoneService.h"

namespace Irrigation {

namespace {

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

void sendJsonStringField(const char* name, const char* value, bool comma = true) {
    Esp32BaseWeb::sendChunk("\"");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("\":\"");
    Esp32BaseWeb::writeJsonEscaped(value != nullptr ? value : "");
    Esp32BaseWeb::sendChunk(comma ? "\"," : "\"");
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

void handleStopPost() {
    if (!Esp32BaseWeb::checkPostAllowed("manual_stop")) {
        return;
    }
    RunController::stop();
    Esp32BaseWeb::redirectSeeOther("/irrigation/run");
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
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::beginPanel("Actions");
    Esp32BaseWeb::sendInfoRowCompactLink("Manual run", "Set enabled Zone durations and start a sequential run.", nullptr, "/irrigation/run", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::sendInfoRowCompactLink("Status API", "Machine-readable current state.", nullptr, "/api/status", "Open", Esp32BaseWeb::UI_INFO);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleRunPage() {
    const StatusSnapshot status = RunController::statusSnapshot();
    Esp32BaseWeb::sendHeader("Run");
    Esp32BaseWeb::sendPageTitle("Run", "Manual sequential watering");
    if (status.busy) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "Run active", runStateName(status.runState));
        Esp32BaseWeb::beginPanel("Stop");
        Esp32BaseWeb::sendChunk("<form method='post' action='/irrigation/run/stop'><div class='actions'><input class='danger' type='submit' value='Stop run'></div></form>");
        Esp32BaseWeb::endPanel();
    } else {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "Idle", "No active watering run.");
    }
    sendRunPanel();
    Esp32BaseWeb::sendFooter();
}

} // namespace

void IrrigationWeb::registerRoutes() {
    Esp32BaseWeb::addPage("/irrigation", "Irrigation", handleDashboardPage);
    Esp32BaseWeb::addPage("/irrigation/run", "Run", handleRunPage);
    Esp32BaseWeb::addRoute("/irrigation/run/start", Esp32BaseWeb::METHOD_POST, handleManualStartPost);
    Esp32BaseWeb::addRoute("/irrigation/run/stop", Esp32BaseWeb::METHOD_POST, handleStopPost);
    Esp32BaseWeb::addApi("/api/status", handleStatusApi);
    Esp32BaseWeb::addApi("/api/zones", handleZonesApi);
    Esp32BaseWeb::addApi("/api/plans", handlePlansApi);
    Esp32BaseWeb::addNavItem("/irrigation", "Irrigation");
}

} // namespace Irrigation

