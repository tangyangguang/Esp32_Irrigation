#include "domain/FlowCalibration.h"

#include <Arduino.h>
#include <string.h>

#include "domain/FlowMeter.h"
#include "domain/ZoneManager.h"
#include "storage/FlowConfigStore.h"

namespace {

FlowCalibration::State g_state = FlowCalibration::State::IDLE;
uint8_t g_activeZoneId = 0;
uint8_t g_activeFlowId = 0;
uint32_t g_startedMs = 0;
uint32_t g_startedPulses = 0;
uint32_t g_maxCaptureMs = 0;
FlowCalibration::Sample g_samples[FlowCalibration::MaxSamples] = {};
uint8_t g_sampleCount = 0;
FlowCalibration::Sample g_pending = {};
FlowCalibration::Sample g_emptySample = {};
FlowCalibration::Recommendation g_recommendation = {};
char g_lastError[64] = {};

void setError(const char* error) {
    strlcpy(g_lastError, error ? error : "", sizeof(g_lastError));
}

void resetActive() {
    g_state = FlowCalibration::State::IDLE;
    g_activeZoneId = 0;
    g_activeFlowId = 0;
    g_startedMs = 0;
    g_startedPulses = 0;
    g_maxCaptureMs = 0;
}

bool finishCapture() {
    if (g_state != FlowCalibration::State::CAPTURING || g_activeZoneId == 0 || g_activeFlowId == 0) {
        setError("calibration_not_capturing");
        return false;
    }
    const uint32_t nowMs = millis();
    const uint32_t pulses = FlowMeter::pulseCount(g_activeFlowId);
    g_pending = {};
    g_pending.exists = true;
    g_pending.valid = pulses >= g_startedPulses && nowMs > g_startedMs;
    g_pending.zoneId = g_activeZoneId;
    g_pending.startedMs = g_startedMs;
    g_pending.endedMs = nowMs;
    g_pending.durationMs = nowMs - g_startedMs;
    g_pending.totalPulses = pulses >= g_startedPulses ? pulses - g_startedPulses : 0;
    if (!g_pending.valid || g_pending.totalPulses == 0) {
        g_pending.valid = false;
        strlcpy(g_pending.invalidReason, "no_pulses", sizeof(g_pending.invalidReason));
    }
    g_state = FlowCalibration::State::WAITING_ACTUAL;
    return true;
}

bool recomputeRecommendation() {
    uint64_t totalActualMl = 0;
    uint64_t totalPulses = 0;
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < g_sampleCount; ++i) {
        const FlowCalibration::Sample& sample = g_samples[i];
        if (!sample.exists || !sample.valid || sample.actualMl == 0 || sample.totalPulses == 0) {
            continue;
        }
        totalActualMl += sample.actualMl;
        totalPulses += sample.totalPulses;
        ++validCount;
    }
    if (validCount == 0 || totalPulses == 0) {
        g_recommendation = {};
        setError("no_valid_samples");
        return false;
    }
    const uint64_t k = (totalActualMl * 60000ULL) / totalPulses;
    g_recommendation = {};
    g_recommendation.valid = k > 0 && k <= 10000000ULL;
    g_recommendation.flowId = g_activeFlowId != 0 ? g_activeFlowId : ZoneManager::config(g_samples[0].zoneId).flowId;
    g_recommendation.sampleCount = validCount;
    g_recommendation.stableDetectedCount = validCount;
    g_recommendation.calibration = FlowConfigStore::get(g_recommendation.flowId).activeCalibration;
    g_recommendation.calibration.source = validCount == 1 ? Irrigation::ParameterSource::SINGLE_POINT
                                                          : Irrigation::ParameterSource::MULTI_POINT;
    g_recommendation.calibration.kUlPerMinPerHz = static_cast<int32_t>(k);
    g_recommendation.calibration.offsetMilliHz = 0;
    g_recommendation.calibration.updatedAt = ZoneManager::trustedEpoch();
    setError(g_recommendation.valid ? "" : "calibration_out_of_range");
    return g_recommendation.valid;
}

}

namespace FlowCalibration {

void begin() {
    resetActive();
    g_sampleCount = 0;
    g_recommendation = {};
    setError("");
}

void handle(const Irrigation::SystemConfig& config) {
    (void)config;
    if (g_state == State::CAPTURING && g_maxCaptureMs != 0 && millis() - g_startedMs >= g_maxCaptureMs) {
        (void)ZoneManager::stopZone(g_activeZoneId, Irrigation::StopSource::LOCAL_BUTTON);
        (void)finishCapture();
    }
}

bool active() {
    return g_state != State::IDLE;
}

State state() {
    return g_state;
}

uint8_t activeZoneId() {
    return g_activeZoneId;
}

void status(StatusSnapshot* out) {
    if (!out) {
        return;
    }
    *out = {};
    out->state = g_state;
    out->activeZoneId = g_activeZoneId;
    out->startedMs = g_startedMs;
    out->maxCaptureMs = g_maxCaptureMs;
    out->sampleCapacity = MaxSamples;
    if (g_state == State::CAPTURING) {
        const uint32_t nowMs = millis();
        out->elapsedMs = nowMs - g_startedMs;
        out->remainingMs = g_maxCaptureMs > out->elapsedMs ? g_maxCaptureMs - out->elapsedMs : 0;
        const uint32_t pulses = FlowMeter::pulseCount(g_activeFlowId);
        out->currentPulses = pulses >= g_startedPulses ? pulses - g_startedPulses : 0;
    }
    out->pendingExists = g_pending.exists;
    out->pendingZoneId = g_pending.zoneId;
    out->pendingDurationMs = g_pending.durationMs;
    out->pendingTotalPulses = g_pending.totalPulses;
}

const char* lastError() {
    return g_lastError;
}

bool start(uint8_t zoneId, const Irrigation::SystemConfig& config) {
    if (!Irrigation::validZoneId(zoneId) || active()) {
        setError("calibration_unavailable");
        return false;
    }
    const Irrigation::ZoneConfig& zone = ZoneManager::config(zoneId);
    if (!zone.enabled || !FlowConfigStore::get(zone.flowId).enabled) {
        setError("zone_or_flow_disabled");
        return false;
    }
    const uint32_t nowMs = millis();
    g_activeZoneId = zoneId;
    g_activeFlowId = zone.flowId;
    g_startedMs = nowMs;
    g_startedPulses = FlowMeter::pulseCount(g_activeFlowId);
    g_maxCaptureMs = config.maxWateringDurationSec * 1000UL;
    g_pending = {};
    if (!ZoneManager::startManual(zoneId, config.maxWateringDurationSec, Irrigation::StartSource::WEB_PAGE)) {
        resetActive();
        setError("calibration_start_rejected");
        return false;
    }
    g_state = State::CAPTURING;
    setError("");
    return true;
}

bool stop() {
    if (g_state != State::CAPTURING) {
        setError("calibration_not_capturing");
        return false;
    }
    (void)ZoneManager::stopZone(g_activeZoneId, Irrigation::StopSource::WEB_PAGE);
    return finishCapture();
}

bool abort(const char* reason) {
    (void)reason;
    if (g_activeZoneId != 0) {
        (void)ZoneManager::stopZone(g_activeZoneId, Irrigation::StopSource::WEB_PAGE);
    }
    resetActive();
    setError("");
    return true;
}

bool submitActualMilliliters(uint32_t actualMl) {
    if (g_state != State::WAITING_ACTUAL || !g_pending.exists || !g_pending.valid || actualMl == 0) {
        setError("invalid_calibration_sample");
        return false;
    }
    if (g_sampleCount >= MaxSamples) {
        for (uint8_t i = 1; i < MaxSamples; ++i) {
            g_samples[i - 1] = g_samples[i];
        }
        g_sampleCount = MaxSamples - 1;
    }
    g_pending.actualMl = actualMl;
    g_samples[g_sampleCount++] = g_pending;
    g_pending = {};
    resetActive();
    return recomputeRecommendation();
}

bool updateActualMilliliters(uint8_t sampleIndex, uint32_t actualMl) {
    if (sampleIndex >= g_sampleCount || actualMl == 0) {
        setError("invalid_sample_index");
        return false;
    }
    g_samples[sampleIndex].actualMl = actualMl;
    return recomputeRecommendation();
}

bool clear() {
    resetActive();
    g_sampleCount = 0;
    g_pending = {};
    g_recommendation = {};
    setError("");
    return true;
}

uint8_t sampleCount() {
    return g_sampleCount;
}

uint8_t validSampleCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < g_sampleCount; ++i) {
        if (g_samples[i].exists && g_samples[i].valid && g_samples[i].actualMl != 0) {
            ++count;
        }
    }
    return count;
}

const Sample& sample(uint8_t index) {
    return index < g_sampleCount ? g_samples[index] : g_emptySample;
}

uint16_t samplePulseDelta(uint8_t sampleIndex, uint16_t pulseIndex) {
    (void)sampleIndex;
    (void)pulseIndex;
    return 0;
}

uint16_t sampleWindowPulse(uint8_t sampleIndex, uint16_t pointIndex) {
    (void)sampleIndex;
    (void)pointIndex;
    return 0;
}

bool computeRecommendation(Recommendation* out) {
    if (!g_recommendation.valid && !recomputeRecommendation()) {
        return false;
    }
    if (out) {
        *out = g_recommendation;
    }
    return true;
}

const Recommendation& recommendation() {
    return g_recommendation;
}

bool saveCandidate() {
    if (!g_recommendation.valid) {
        setError("no_recommendation");
        return false;
    }
    if (!FlowConfigStore::savePendingCalibration(g_recommendation.flowId, g_recommendation.calibration)) {
        setError("save_pending_failed");
        return false;
    }
    setError("");
    return true;
}

}
