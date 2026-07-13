#include "RunController.h"

#include <Esp32Base.h>
#include <stdio.h>
#include <string.h>

#include "BoardHardware.h"
#include "ConfigStore.h"
#include "EventService.h"
#include "FlowSafetyService.h"
#include "HistoryService.h"
#include "IrrigationConfig.h"
#include "PlanService.h"
#include "Scheduler.h"

namespace Irrigation {

namespace {

WateringRun g_run;
uint32_t g_nextRunId = 1;
char g_lastError[40] = "ok";

void setLastError(const char* error) {
    snprintf(g_lastError, sizeof(g_lastError), "%s", error != nullptr ? error : "unknown");
    g_lastError[sizeof(g_lastError) - 1] = '\0';
}

uint32_t elapsed(uint32_t nowMs, uint32_t sinceMs) {
    return nowMs - sinceMs;
}

uint8_t activeZoneId() {
    if (g_run.currentStep >= g_run.stepCount) {
        return 0;
    }
    return g_run.steps[g_run.currentStep].zoneId;
}

uint32_t activeTargetDurationSec() {
    if (g_run.currentStep >= g_run.stepCount) {
        return 0;
    }
    return g_run.steps[g_run.currentStep].targetDurationSec;
}

void clearRun() {
    memset(&g_run, 0, sizeof(g_run));
    g_run.state = RunState::Idle;
    g_run.result = RunResult::None;
    g_run.reason = RunReason::None;
}

bool addStep(uint8_t zoneId, uint32_t durationSec) {
    if (g_run.stepCount >= kMaxRunSteps) {
        return false;
    }
    g_run.steps[g_run.stepCount].zoneId = zoneId;
    g_run.steps[g_run.stepCount].targetDurationSec = durationSec;
    ++g_run.stepCount;
    return true;
}

bool buildManualSteps(const uint32_t zoneDurationSec[kMaxZones], RunReason& reason) {
    const IrrigationConfig& config = ConfigStore::config();
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const uint32_t duration = zoneDurationSec[i];
        if (!config.zones[i].enabled || duration == 0) {
            continue;
        }
        if (duration > config.valve.maxZoneDurationSec) {
            reason = RunReason::InvalidDuration;
            setLastError("manual_duration_too_long");
            return false;
        }
        addStep(config.zones[i].id, duration);
    }

    if (g_run.stepCount == 0) {
        reason = RunReason::NoEffectiveStep;
        setLastError("no_effective_step");
        return false;
    }

    return true;
}

void safeAllOff() {
    BoardHardware::setPumpSignal(false);
    BoardHardware::closeAllValves();
}

uint32_t currentEpoch() {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    return time.synced ? time.epochSec : 0;
}

Esp32BaseAppEventLog::Level levelForResult(RunResult result) {
    return result == RunResult::FaultStopped ? Esp32BaseAppEventLog::LEVEL_ERROR :
           result == RunResult::Skipped ? Esp32BaseAppEventLog::LEVEL_WARN :
           Esp32BaseAppEventLog::LEVEL_INFO;
}

const char* typeForResult(RunResult result) {
    switch (result) {
        case RunResult::Completed: return "completed";
        case RunResult::UserStopped: return "stopped";
        case RunResult::FaultStopped: return "fault_stopped";
        case RunResult::Skipped: return "skipped";
        case RunResult::None: return "finished";
    }
    return "finished";
}

} // namespace

void RunController::begin() {
    clearRun();
    setLastError("ok");
}

void RunController::handle(uint32_t nowMs) {
    BoardHardware::handle(nowMs);

    switch (g_run.state) {
        case RunState::Idle:
            FlowSafetyService::handleIdle(nowMs);
            return;

        case RunState::Precheck:
            if (g_run.stepCount == 0) {
                finish(RunResult::Skipped, RunReason::NoEffectiveStep, nowMs);
                return;
            }
            enterState(RunState::OpenValve, nowMs);
            return;

        case RunState::OpenValve:
            if (!BoardHardware::openValve(activeZoneId(), nowMs)) {
                finish(RunResult::FaultStopped, RunReason::ConfigInvalid, nowMs);
                return;
            }
            FlowSafetyService::beginStep(nowMs, activeZoneId());
            if (ConfigStore::config().supply.pumpEnabled) {
                enterState(RunState::PumpSignalOn, nowMs);
            } else {
                enterState(RunState::FlowGrace, nowMs);
            }
            return;

        case RunState::PumpSignalOn:
            BoardHardware::setPumpSignal(true);
            enterState(RunState::FlowGrace, nowMs);
            return;

        case RunState::PumpStartDelay:
            enterState(RunState::FlowGrace, nowMs);
            return;

        case RunState::FlowGrace:
            if (FlowSafetyService::checkFlowGrace(nowMs, g_run.reason)) {
                finish(RunResult::FaultStopped, g_run.reason, nowMs);
                return;
            }
            if (elapsed(nowMs, g_run.stateEnteredMs) >= ConfigStore::config().flow.startupGraceSec * 1000UL) {
                enterState(RunState::Running, nowMs);
            }
            return;

        case RunState::Running:
            if (FlowSafetyService::checkRunning(nowMs, g_run.reason)) {
                finish(RunResult::FaultStopped, g_run.reason, nowMs);
                return;
            }
            if (elapsed(nowMs, g_run.stateEnteredMs) >= activeTargetDurationSec() * 1000UL) {
                if (ConfigStore::config().supply.pumpEnabled) {
                    enterState(RunState::PumpSignalOff, nowMs);
                } else {
                    enterState(RunState::CloseValve, nowMs);
                }
            }
            return;

        case RunState::PumpSignalOff:
            BoardHardware::setPumpSignal(false);
            enterState(RunState::PumpStopDelay, nowMs);
            return;

        case RunState::PumpStopDelay:
            if (elapsed(nowMs, g_run.stateEnteredMs) >= ConfigStore::config().supply.pumpStopDelayMs) {
                enterState(RunState::CloseValve, nowMs);
            }
            return;

        case RunState::CloseValve:
            BoardHardware::closeValve(activeZoneId());
            enterState(RunState::AdvanceStep, nowMs);
            return;

        case RunState::AdvanceStep:
            ++g_run.currentStep;
            if (g_run.currentStep < g_run.stepCount) {
                enterState(RunState::OpenValve, nowMs);
            } else {
                finish(RunResult::Completed, RunReason::None, nowMs);
            }
            return;

        case RunState::Finished:
            enterState(RunState::Idle, nowMs);
            return;
    }
}

bool RunController::startManual(const uint32_t zoneDurationSec[kMaxZones], RunReason& reason) {
    if (busy()) {
        reason = RunReason::ControllerBusy;
        setLastError("controller_busy");
        return false;
    }

    clearRun();
    g_run.id = g_nextRunId++;
    g_run.source = RunSource::Manual;
    g_run.reason = RunReason::ManualRequest;
    g_run.startedAtEpoch = currentEpoch();

    if (!buildManualSteps(zoneDurationSec, reason)) {
        clearRun();
        return false;
    }

    enterState(RunState::Precheck, millis());
    reason = RunReason::ManualRequest;
    setLastError("ok");
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO, "run", "started", "manual", "run", 0, g_run.id, 0, 0, Esp32BaseAppEventLog::VALUE1);
    ESP32BASE_LOG_I("run", "manual_started id=%u steps=%u", static_cast<unsigned>(g_run.id), g_run.stepCount);
    return true;
}

bool RunController::startPlanNow(uint8_t planId, RunReason& reason) {
    if (busy()) {
        reason = RunReason::ControllerBusy;
        setLastError("controller_busy");
        return false;
    }
    if (ConfigStore::config().flow.pulsesPerLiter == 0) {
        reason = RunReason::FlowNotCalibrated;
        setLastError("flow_not_calibrated");
        return false;
    }

    clearRun();
    g_run.id = g_nextRunId++;
    g_run.source = RunSource::RunPlanNow;
    g_run.planId = planId;
    g_run.reason = RunReason::RunPlanNow;
    g_run.startedAtEpoch = currentEpoch();

    if (!PlanService::buildSteps(planId, g_run.steps, kMaxRunSteps, g_run.stepCount, reason)) {
        clearRun();
        setLastError(PlanService::lastError());
        return false;
    }

    enterState(RunState::Precheck, millis());
    reason = RunReason::RunPlanNow;
    setLastError("ok");
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO, "run", "started", "run_plan_now", "run", 0, g_run.id, planId, 0, Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2);
    ESP32BASE_LOG_I("run", "plan_now_started id=%u plan=%u steps=%u",
                    static_cast<unsigned>(g_run.id),
                    planId,
                    g_run.stepCount);
    return true;
}

bool RunController::startPlan(uint8_t planId, RunReason& reason) {
    if (busy()) {
        reason = RunReason::ControllerBusy;
        setLastError("controller_busy");
        return false;
    }
    if (ConfigStore::config().flow.pulsesPerLiter == 0) {
        reason = RunReason::FlowNotCalibrated;
        setLastError("flow_not_calibrated");
        return false;
    }

    clearRun();
    g_run.id = g_nextRunId++;
    g_run.source = RunSource::Plan;
    g_run.planId = planId;
    g_run.reason = RunReason::PlanStartTime;
    g_run.startedAtEpoch = currentEpoch();

    if (!PlanService::buildSteps(planId, g_run.steps, kMaxRunSteps, g_run.stepCount, reason)) {
        clearRun();
        setLastError(PlanService::lastError());
        return false;
    }

    enterState(RunState::Precheck, millis());
    reason = RunReason::PlanStartTime;
    setLastError("ok");
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO, "run", "started", "plan", "run", 0, g_run.id, planId, 0, Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2);
    ESP32BASE_LOG_I("run", "plan_started id=%u plan=%u steps=%u",
                    static_cast<unsigned>(g_run.id),
                    planId,
                    g_run.stepCount);
    return true;
}

bool RunController::startCalibration(uint8_t zoneId, uint32_t durationSec, RunReason& reason) {
    if (busy()) {
        reason = RunReason::ControllerBusy;
        setLastError("controller_busy");
        return false;
    }

    const uint8_t index = zoneIndexFromId(zoneId);
    if (index >= kMaxZones) {
        reason = RunReason::ZoneDisabled;
        setLastError("zone_id_invalid");
        return false;
    }

    const IrrigationConfig& config = ConfigStore::config();
    if (!config.zones[index].enabled) {
        reason = RunReason::ZoneDisabled;
        setLastError("zone_disabled");
        return false;
    }
    if (durationSec == 0 || durationSec > config.valve.maxZoneDurationSec) {
        reason = RunReason::InvalidDuration;
        setLastError("calibration_duration_invalid");
        return false;
    }

    clearRun();
    g_run.id = g_nextRunId++;
    g_run.source = RunSource::Calibration;
    g_run.reason = RunReason::CalibrationRequest;
    g_run.startedAtEpoch = currentEpoch();
    addStep(zoneId, durationSec);

    enterState(RunState::Precheck, millis());
    reason = RunReason::CalibrationRequest;
    setLastError("ok");
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO, "calibration", "started", "request", "run", 0, g_run.id, zoneId, 0, Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2);
    ESP32BASE_LOG_I("run", "calibration_started id=%u zone=%u duration=%lu",
                    static_cast<unsigned>(g_run.id),
                    zoneId,
                    static_cast<unsigned long>(durationSec));
    return true;
}

bool RunController::stop(RunReason reason) {
    if (!busy()) {
        setLastError("not_busy");
        return false;
    }

    finish(RunResult::UserStopped, reason, millis());
    return true;
}

bool RunController::busy() {
    return g_run.state != RunState::Idle && g_run.state != RunState::Finished;
}

StatusSnapshot RunController::statusSnapshot() {
    StatusSnapshot snapshot = {};
    snapshot.configValid = ConfigStore::configValid();
    snapshot.busy = busy();
    snapshot.runState = g_run.state;
    snapshot.runResult = g_run.result;
    snapshot.activeZoneId = busy() ? activeZoneId() : 0;
    snapshot.currentFlowMlPerMin = busy() ? FlowSafetyService::currentFlowMlPerMin() : 0;
    snapshot.currentRunVolumeMl = busy() ? FlowSafetyService::currentStepVolumeMl() : 0;
    snapshot.todayVolumeMl = 0;
    snapshot.enabledZoneCount = enabledZoneCount(ConfigStore::config());
    snapshot.enabledPlanCount = enabledPlanCount(ConfigStore::config());
    snapshot.nextRunEpoch = Scheduler::nextRunEpoch();
    return snapshot;
}

const WateringRun& RunController::currentRun() {
    return g_run;
}

const char* RunController::lastError() {
    return g_lastError;
}

void RunController::enterState(RunState state, uint32_t nowMs) {
    g_run.state = state;
    g_run.stateEnteredMs = nowMs;
}

void RunController::finish(RunResult result, RunReason reason, uint32_t nowMs) {
    safeAllOff();
    g_run.result = result;
    g_run.reason = reason;
    g_run.finishedAtEpoch = currentEpoch();
    enterState(RunState::Finished, nowMs);
    if (!HistoryService::appendRun(g_run)) {
        ESP32BASE_LOG_W("run", "history_append_failed error=%s", HistoryService::lastError());
    }
    EventService::append(levelForResult(result),
                         "run",
                         typeForResult(result),
                         runReasonToString(reason),
                         "run",
                         0,
                         g_run.id,
                         g_run.planId,
                         static_cast<int32_t>(result),
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
    setLastError("ok");
    ESP32BASE_LOG_I("run", "finished id=%u result=%u reason=%s",
                    static_cast<unsigned>(g_run.id),
                    static_cast<unsigned>(result),
                    runReasonToString(reason));
}

} // namespace Irrigation
