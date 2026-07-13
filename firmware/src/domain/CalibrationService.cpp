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
VolumeCalibrationSample g_volumeSamples[kCalibrationMaxVolumeSamples] = {};
uint8_t g_volumeSampleCount = 0;
uint32_t g_standardFlowSamples[kCalibrationStableFlowSamples] = {};
uint8_t g_standardFlowSampleCount = 0;
uint8_t g_standardFlowNext = 0;
uint32_t g_lastStandardFlowSampleMs = 0;
char g_lastError[40] = "ok";

void clearSession() {
    memset(&g_session, 0, sizeof(g_session));
    g_session.mode = CalibrationMode::None;
}

void resetStandardFlowSamples() {
    for (uint8_t i = 0; i < kCalibrationStableFlowSamples; ++i) {
        g_standardFlowSamples[i] = 0;
    }
    g_standardFlowSampleCount = 0;
    g_standardFlowNext = 0;
    g_lastStandardFlowSampleMs = 0;
    g_session.standardFlowStable = false;
    g_session.standardFlowSampleCount = 0;
    g_session.standardFlowAverageMlPerMin = 0;
    g_session.standardFlowMinMlPerMin = 0;
    g_session.standardFlowMaxMlPerMin = 0;
    g_session.suggestedFlowMlPerMin = 0;
}

void copyVolumeSamplesToSnapshot() {
    g_session.volumeSampleCount = g_volumeSampleCount;
    for (uint8_t i = 0; i < kCalibrationMaxVolumeSamples; ++i) {
        g_session.volumeSamples[i] = g_volumeSamples[i];
    }
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
    if (durationSec == 0 ||
        durationSec > kCalibrationMaxDurationSec ||
        durationSec > config.valve.maxZoneDurationSec) {
        reason = RunReason::InvalidDuration;
        *error = "duration_invalid";
        return false;
    }
    return true;
}

void updateStandardFlowSuggestion() {
    if (g_standardFlowSampleCount == 0) {
        return;
    }

    uint32_t minFlow = 0;
    uint32_t maxFlow = 0;
    uint64_t total = 0;
    for (uint8_t i = 0; i < g_standardFlowSampleCount; ++i) {
        const uint32_t flow = g_standardFlowSamples[i];
        if (flow == 0) {
            continue;
        }
        if (minFlow == 0 || flow < minFlow) {
            minFlow = flow;
        }
        if (flow > maxFlow) {
            maxFlow = flow;
        }
        total += flow;
    }
    if (minFlow == 0 || total == 0) {
        return;
    }

    const uint32_t average = static_cast<uint32_t>(total / g_standardFlowSampleCount);
    g_session.standardFlowSampleCount = g_standardFlowSampleCount;
    g_session.standardFlowAverageMlPerMin = average;
    g_session.standardFlowMinMlPerMin = minFlow;
    g_session.standardFlowMaxMlPerMin = maxFlow;
    if (g_standardFlowSampleCount >= kCalibrationStableFlowSamples &&
        average > 0 &&
        maxFlow - minFlow <= (average / 10UL)) {
        g_session.standardFlowStable = true;
        g_session.suggestedFlowMlPerMin = average;
    }
}

void addStandardFlowSample(uint32_t flowMlPerMin) {
    if (flowMlPerMin == 0) {
        return;
    }
    const uint32_t nowMs = millis();
    if (g_lastStandardFlowSampleMs != 0 && nowMs - g_lastStandardFlowSampleMs < 1000UL) {
        return;
    }
    g_standardFlowSamples[g_standardFlowNext] = flowMlPerMin;
    g_standardFlowNext = static_cast<uint8_t>((g_standardFlowNext + 1U) % kCalibrationStableFlowSamples);
    if (g_standardFlowSampleCount < kCalibrationStableFlowSamples) {
        ++g_standardFlowSampleCount;
    }
    g_lastStandardFlowSampleMs = nowMs;
    updateStandardFlowSuggestion();
}

bool recomputeVolumeFit() {
    copyVolumeSamplesToSnapshot();
    g_session.volumeFitReady = false;
    g_session.volumeFitAcceptable = false;
    g_session.fittedPulsesPerLiter = 0;
    g_session.fittedStartupOffsetPulses = 0;
    g_session.fitMaxErrorPermille = 0;
    g_session.fitWaterSpreadMl = 0;

    if (g_volumeSampleCount < 2) {
        return false;
    }

    uint32_t minMl = g_volumeSamples[0].measuredMl;
    uint32_t maxMl = g_volumeSamples[0].measuredMl;
    for (uint8_t i = 1; i < g_volumeSampleCount; ++i) {
        if (g_volumeSamples[i].measuredMl < minMl) {
            minMl = g_volumeSamples[i].measuredMl;
        }
        if (g_volumeSamples[i].measuredMl > maxMl) {
            maxMl = g_volumeSamples[i].measuredMl;
        }
    }
    g_session.fitWaterSpreadMl = maxMl - minMl;

    double slopePulsesPerMl = 0.0;
    double offsetPulses = 0.0;
    if (g_volumeSampleCount == 2) {
        const VolumeCalibrationSample& a = g_volumeSamples[0];
        const VolumeCalibrationSample& b = g_volumeSamples[1];
        const int32_t deltaMl = static_cast<int32_t>(b.measuredMl) - static_cast<int32_t>(a.measuredMl);
        const int32_t deltaPulses = static_cast<int32_t>(b.pulses) - static_cast<int32_t>(a.pulses);
        if (deltaMl == 0) {
            return false;
        }
        slopePulsesPerMl = static_cast<double>(deltaPulses) / static_cast<double>(deltaMl);
        if (slopePulsesPerMl <= 0.0) {
            return false;
        }
        offsetPulses = static_cast<double>(a.pulses) - slopePulsesPerMl * static_cast<double>(a.measuredMl);
    } else {
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXX = 0.0;
        double sumXY = 0.0;
        for (uint8_t i = 0; i < g_volumeSampleCount; ++i) {
            const double x = static_cast<double>(g_volumeSamples[i].measuredMl);
            const double y = static_cast<double>(g_volumeSamples[i].pulses);
            sumX += x;
            sumY += y;
            sumXX += x * x;
            sumXY += x * y;
        }
        const double n = static_cast<double>(g_volumeSampleCount);
        const double denominator = n * sumXX - sumX * sumX;
        if (denominator == 0.0) {
            return false;
        }
        slopePulsesPerMl = (n * sumXY - sumX * sumY) / denominator;
        offsetPulses = (sumY - slopePulsesPerMl * sumX) / n;
    }

    if (slopePulsesPerMl <= 0.0) {
        return false;
    }

    const uint32_t pulsesPerLiter = static_cast<uint32_t>(slopePulsesPerMl * 1000.0 + 0.5);
    if (pulsesPerLiter == 0 || pulsesPerLiter > 100000UL) {
        return false;
    }

    uint32_t maxErrorPermille = 0;
    for (uint8_t i = 0; i < g_volumeSampleCount; ++i) {
        const double predicted = slopePulsesPerMl * static_cast<double>(g_volumeSamples[i].measuredMl) + offsetPulses;
        double error = predicted - static_cast<double>(g_volumeSamples[i].pulses);
        if (error < 0.0) {
            error = -error;
        }
        const double base = g_volumeSamples[i].pulses > 0 ? static_cast<double>(g_volumeSamples[i].pulses) : 1.0;
        const uint32_t permille = static_cast<uint32_t>((error * 1000.0) / base + 0.5);
        if (permille > maxErrorPermille) {
            maxErrorPermille = permille;
        }
    }

    g_session.volumeFitReady = true;
    g_session.fittedPulsesPerLiter = pulsesPerLiter;
    g_session.fittedStartupOffsetPulses = static_cast<int32_t>(offsetPulses >= 0.0 ? offsetPulses + 0.5 : offsetPulses - 0.5);
    g_session.fitMaxErrorPermille = maxErrorPermille;
    g_session.volumeFitAcceptable = g_session.fitWaterSpreadMl >= 2000UL &&
                                    (g_volumeSampleCount < 3 || maxErrorPermille <= 80UL);
    return true;
}

} // namespace

void CalibrationService::begin() {
    clearSession();
    clearVolumeSamples();
    setLastError("ok");
}

void CalibrationService::handle() {
    if (g_session.mode == CalibrationMode::None || !g_session.running) {
        copyVolumeSamplesToSnapshot();
        recomputeVolumeFit();
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
    if (g_session.mode == CalibrationMode::ZoneStandardFlow) {
        addStandardFlowSample(FlowSafetyService::currentFlowMlPerMin());
    }
    if (RunController::busy()) {
        return;
    }

    g_session.running = false;
    g_session.resultReady = true;
    updateStandardFlowSuggestion();
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

bool CalibrationService::startVolumeCalibration(uint8_t zoneId, uint32_t maxDurationSec, RunReason& reason) {
    if (g_volumeSampleCount >= kCalibrationMaxVolumeSamples) {
        reason = RunReason::ConfigInvalid;
        setLastError("volume_samples_full");
        return false;
    }
    return startSession(CalibrationMode::FlowMeterVolume, zoneId, maxDurationSec, reason);
}

bool CalibrationService::startStandardFlowCalibration(uint8_t zoneId, uint32_t maxDurationSec, RunReason& reason) {
    if (ConfigStore::config().flow.pulsesPerLiter == 0) {
        reason = RunReason::FlowNotCalibrated;
        setLastError("flow_not_calibrated");
        return false;
    }
    return startSession(CalibrationMode::ZoneStandardFlow, zoneId, maxDurationSec, reason);
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
    recomputeVolumeFit();
    setLastError("ok");
}

bool CalibrationService::addVolumeSample(uint32_t measuredMl) {
    if (g_session.mode != CalibrationMode::FlowMeterVolume || !g_session.resultReady) {
        setLastError("volume_result_missing");
        return false;
    }
    if (g_volumeSampleCount >= kCalibrationMaxVolumeSamples) {
        setLastError("volume_samples_full");
        return false;
    }
    if (g_session.pulses == 0 || measuredMl == 0) {
        setLastError("volume_sample_invalid");
        return false;
    }

    VolumeCalibrationSample& sample = g_volumeSamples[g_volumeSampleCount++];
    sample.used = true;
    sample.runId = g_session.runId;
    sample.zoneId = g_session.zoneId;
    sample.pulses = g_session.pulses;
    sample.measuredMl = measuredMl;
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO,
                         "calibration",
                         "sample_saved",
                         "flow_meter_volume",
                         "flow",
                         0,
                         g_volumeSampleCount,
                         measuredMl,
                         g_session.pulses,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
    clearSession();
    recomputeVolumeFit();
    setLastError("ok");
    return true;
}

bool CalibrationService::saveFittedPulsesPerLiter() {
    if (!recomputeVolumeFit() || !g_session.volumeFitReady || !g_session.volumeFitAcceptable) {
        setLastError("volume_fit_not_acceptable");
        return false;
    }

    IrrigationConfig next = ConfigStore::config();
    next.flow.pulsesPerLiter = g_session.fittedPulsesPerLiter;
    if (!ConfigStore::save(next)) {
        setLastError(ConfigStore::lastError());
        return false;
    }

    const uint32_t savedPulsesPerLiter = g_session.fittedPulsesPerLiter;
    const uint32_t sampleCount = g_volumeSampleCount;
    EventService::append(Esp32BaseAppEventLog::LEVEL_INFO,
                         "calibration",
                         "saved",
                         "flow_meter_volume",
                         "flow",
                         0,
                         savedPulsesPerLiter,
                         sampleCount,
                         g_session.fitMaxErrorPermille,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
    clearSession();
    clearVolumeSamples();
    setLastError("ok");
    return true;
}

void CalibrationService::clearVolumeSamples() {
    for (uint8_t i = 0; i < kCalibrationMaxVolumeSamples; ++i) {
        memset(&g_volumeSamples[i], 0, sizeof(g_volumeSamples[i]));
    }
    g_volumeSampleCount = 0;
    copyVolumeSamplesToSnapshot();
    g_session.volumeFitReady = false;
    g_session.volumeFitAcceptable = false;
    g_session.fittedPulsesPerLiter = 0;
    g_session.fittedStartupOffsetPulses = 0;
    g_session.fitMaxErrorPermille = 0;
    g_session.fitWaterSpreadMl = 0;
    setLastError("ok");
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
    copyVolumeSamplesToSnapshot();
    recomputeVolumeFit();
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
    resetStandardFlowSamples();
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
