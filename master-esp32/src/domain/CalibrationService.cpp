#include "CalibrationService.h"

#include <Esp32Base.h>
#include <stdio.h>
#include <string.h>

#include "ConfigStore.h"
#include "EventService.h"
#include "FlowSafetyService.h"
#include "IrrigationConfig.h"
#include "RunController.h"
#include "ZoneService.h"

namespace Irrigation {

namespace {

CalibrationSnapshot g_session;
char g_lastError[40] = "ok";

void clearSession() {
    memset(&g_session, 0, sizeof(g_session));
    g_session.mode = CalibrationMode::None;
}

uint32_t computePulsesPerLiter(uint32_t pulses, uint32_t measuredMl) {
    if (pulses == 0 || measuredMl == 0) {
        return 0;
    }
    const uint64_t numerator = static_cast<uint64_t>(pulses) * 1000ULL + measuredMl / 2ULL;
    return static_cast<uint32_t>(numerator / measuredMl);
}

uint32_t computeFlowMlPerMin(uint32_t pulses, uint32_t pulsesPerLiter, uint32_t durationSec) {
    if (pulses == 0 || pulsesPerLiter == 0 || durationSec == 0) {
        return 0;
    }
    const uint64_t numerator = static_cast<uint64_t>(pulses) * 60000ULL;
    const uint64_t denominator = static_cast<uint64_t>(pulsesPerLiter) * durationSec;
    return static_cast<uint32_t>(numerator / denominator);
}

const char* modeName(CalibrationMode mode) {
    switch (mode) {
        case CalibrationMode::None: return "none";
        case CalibrationMode::FlowMeterVolume: return "flow_meter_volume";
        case CalibrationMode::ZoneStandardFlow: return "zone_standard_flow";
    }
    return "unknown";
}

bool validateZoneAndDuration(uint8_t zoneId, uint32_t durationSec, RunReason& reason, const char** error) {
    const uint8_t index = zoneIndexFromId(zoneId);
    if (index >= kMaxZones) {
        reason = RunReason::ZoneDisabled;
        *error = "zone_id_invalid";
        return false;
    }

    const IrrigationConfig& config = ConfigStore::config();
    if (!config.zones[index].enabled) {
        reason = RunReason::ZoneDisabled;
        *error = "zone_disabled";
        return false;
    }
    if (durationSec == 0 || durationSec > config.valve.maxZoneDurationSec) {
        reason = RunReason::InvalidDuration;
        *error = "duration_invalid";
        return false;
    }
    return true;
}

} // namespace

void CalibrationService::begin() {
    clearSession();
    setLastError("ok");
}

void CalibrationService::handle() {
    if (g_session.mode == CalibrationMode::None || !g_session.running) {
        return;
    }

    const WateringRun& run = RunController::currentRun();
    if (run.id != g_session.runId || run.source != RunSource::Calibration) {
        if (!RunController::busy()) {
            g_session.running = false;
            g_session.resultReady = false;
            setLastError("calibration_run_lost");
        }
        return;
    }

    g_session.pulses = FlowSafetyService::currentStepPulses();
    if (RunController::busy()) {
        return;
    }

    g_session.running = false;
    g_session.resultReady = true;
    if (g_session.mode == CalibrationMode::ZoneStandardFlow && run.result == RunResult::Completed) {
        g_session.suggestedFlowMlPerMin = computeFlowMlPerMin(g_session.pulses,
                                                               ConfigStore::config().flow.pulsesPerLiter,
                                                               g_session.durationSec);
    }
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO,
                         "calibration",
                         "finished",
                         modeName(g_session.mode),
                         "run",
                         0,
                         g_session.runId,
                         g_session.zoneId,
                         g_session.pulses,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
    setLastError("ok");
}

bool CalibrationService::startVolumeCalibration(uint8_t zoneId, uint32_t durationSec, RunReason& reason) {
    return startSession(CalibrationMode::FlowMeterVolume, zoneId, durationSec, reason);
}

bool CalibrationService::startStandardFlowCalibration(uint8_t zoneId, uint32_t durationSec, RunReason& reason) {
    return startSession(CalibrationMode::ZoneStandardFlow, zoneId, durationSec, reason);
}

bool CalibrationService::stop() {
    if (g_session.mode == CalibrationMode::None || !g_session.running) {
        setLastError("calibration_not_running");
        return false;
    }
    if (!RunController::stop(RunReason::UserStop)) {
        setLastError(RunController::lastError());
        return false;
    }
    setLastError("ok");
    return true;
}

void CalibrationService::clearResult() {
    if (g_session.running) {
        setLastError("calibration_running");
        return;
    }
    clearSession();
    setLastError("ok");
}

bool CalibrationService::savePulsesPerLiter(uint32_t measuredMl) {
    if (g_session.mode != CalibrationMode::FlowMeterVolume || !g_session.resultReady) {
        setLastError("volume_result_missing");
        return false;
    }
    const uint32_t pulsesPerLiter = computePulsesPerLiter(g_session.pulses, measuredMl);
    if (pulsesPerLiter == 0 || pulsesPerLiter > 100000UL) {
        setLastError("pulses_per_liter_invalid");
        return false;
    }

    IrrigationConfig next = ConfigStore::config();
    next.flow.pulsesPerLiter = pulsesPerLiter;
    if (!ConfigStore::save(next)) {
        setLastError(ConfigStore::lastError());
        return false;
    }

    g_session.measuredMl = measuredMl;
    g_session.computedPulsesPerLiter = pulsesPerLiter;
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO,
                         "calibration",
                         "saved",
                         "flow_meter_volume",
                         "flow",
                         0,
                         pulsesPerLiter,
                         measuredMl,
                         g_session.pulses,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
    clearSession();
    setLastError("ok");
    return true;
}

bool CalibrationService::saveZoneStandardFlow(uint8_t zoneId, uint32_t flowMlPerMin) {
    if (flowMlPerMin == 0 || flowMlPerMin > 100000UL) {
        setLastError("standard_flow_invalid");
        return false;
    }

    const ZoneConfig* current = ZoneService::find(zoneId);
    if (current == nullptr) {
        setLastError(ZoneService::lastError());
        return false;
    }

    ZoneConfig zone = *current;
    zone.standardFlowMlPerMin = flowMlPerMin;
    if (!ZoneService::saveZone(zone)) {
        setLastError(ZoneService::lastError());
        return false;
    }

    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO,
                         "calibration",
                         "saved",
                         "zone_standard_flow",
                         "zone",
                         0,
                         zoneId,
                         flowMlPerMin,
                         0,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2);
    if (g_session.mode == CalibrationMode::ZoneStandardFlow && g_session.zoneId == zoneId && !g_session.running) {
        clearSession();
    }
    setLastError("ok");
    return true;
}

CalibrationSnapshot CalibrationService::snapshot() {
    handle();
    if (g_session.running) {
        g_session.pulses = FlowSafetyService::currentStepPulses();
    }
    return g_session;
}

const char* CalibrationService::lastError() {
    return g_lastError;
}

bool CalibrationService::startSession(CalibrationMode mode, uint8_t zoneId, uint32_t durationSec, RunReason& reason) {
    if (g_session.running || RunController::busy()) {
        reason = RunReason::ControllerBusy;
        setLastError("controller_busy");
        return false;
    }

    const char* error = nullptr;
    if (!validateZoneAndDuration(zoneId, durationSec, reason, &error)) {
        setLastError(error);
        return false;
    }

    if (!RunController::startCalibration(zoneId, durationSec, reason)) {
        setLastError(RunController::lastError());
        return false;
    }

    clearSession();
    g_session.mode = mode;
    g_session.running = true;
    g_session.resultReady = false;
    g_session.runId = RunController::currentRun().id;
    g_session.zoneId = zoneId;
    g_session.durationSec = durationSec;
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO,
                         "calibration",
                         "started",
                         modeName(mode),
                         "zone",
                         0,
                         g_session.runId,
                         zoneId,
                         durationSec,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
    setLastError("ok");
    return true;
}

void CalibrationService::setLastError(const char* error) {
    snprintf(g_lastError, sizeof(g_lastError), "%s", error != nullptr ? error : "unknown");
    g_lastError[sizeof(g_lastError) - 1] = '\0';
}

} // namespace Irrigation
