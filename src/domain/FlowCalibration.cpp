#include "domain/FlowCalibration.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>

#include "domain/FlowMeter.h"
#include "domain/ValveController.h"
#include "domain/ZoneManager.h"
#include "storage/ZoneConfigStore.h"

namespace {

static constexpr uint32_t stableWindowMs = 2000UL;
static constexpr uint32_t stableStepMs = 200UL;
static constexpr uint8_t stableWindowStepCount = stableWindowMs / stableStepMs;
static constexpr uint8_t stableConsecutiveWindows = 5;
static constexpr uint8_t stableRateTolerancePercent = 10;
static constexpr uint32_t minStableStartMs = 1000UL;

FlowCalibration::State g_state = FlowCalibration::State::IDLE;
uint8_t g_activeZoneId = 0;
uint32_t g_startedMs = 0;
uint32_t g_startedPulseCount = 0;
uint32_t g_maxCaptureMs = 0;
uint8_t g_sampleCapacity = FlowCalibration::MaxSamples;
uint16_t* g_activeWindowPulses = nullptr;
uint16_t g_activeWindowCapacity = 0;
uint16_t g_activeWindowCount = 0;
uint32_t g_nextWindowSampleMs = 0;
uint32_t g_windowPulseHistory[stableWindowStepCount + 1] = {};
uint8_t g_windowPulseHistoryPos = 0;
uint8_t g_windowPulseHistoryFilled = 0;
FlowCalibration::Sample g_samples[FlowCalibration::MaxSamples] = {};
uint8_t g_sampleCount = 0;
FlowCalibration::Sample g_pending = {};
uint16_t g_pendingDeltas[FlowCalibration::MaxDetailPulses] = {};
FlowCalibration::Recommendation g_recommendation = {};
char g_lastError[32] = "";
FlowCalibration::Sample g_emptySample = {};

void setError(const char* error) {
    strlcpy(g_lastError, error ? error : "", sizeof(g_lastError));
}

uint16_t clampDetailPulseLimit(uint16_t value) {
    return value > FlowCalibration::MaxDetailPulses ? FlowCalibration::MaxDetailPulses : value;
}

bool sampleSlotAvailable() {
    return g_sampleCount < FlowCalibration::MaxSamples && g_sampleCount < g_sampleCapacity;
}

bool sampleWorkAreaMatches(uint8_t zoneId) {
    for (uint8_t i = 0; i < g_sampleCount; ++i) {
        if (g_samples[i].exists && g_samples[i].zoneId != zoneId) {
            return false;
        }
    }
    return true;
}

uint8_t clampSampleCapacity(uint8_t value) {
    if (value < 2) {
        return 2;
    }
    if (value > FlowCalibration::MaxSamples) {
        return FlowCalibration::MaxSamples;
    }
    return value;
}

void freeSampleData(FlowCalibration::Sample* sample) {
    if (!sample) {
        return;
    }
    if (sample->detailPulseDeltas) {
        free(sample->detailPulseDeltas);
    }
    if (sample->windowPulses) {
        free(sample->windowPulses);
    }
    *sample = {};
}

void releaseActiveWindow() {
    if (g_activeWindowPulses) {
        free(g_activeWindowPulses);
    }
    g_activeWindowPulses = nullptr;
    g_activeWindowCapacity = 0;
    g_activeWindowCount = 0;
    g_nextWindowSampleMs = 0;
    memset(g_windowPulseHistory, 0, sizeof(g_windowPulseHistory));
    g_windowPulseHistoryPos = 0;
    g_windowPulseHistoryFilled = 0;
}

bool beginWindowCapture(const Irrigation::SystemConfig& config) {
    releaseActiveWindow();
    const uint32_t points = (static_cast<uint32_t>(config.calibrationMaxCaptureMin) * 60000UL + stableStepMs - 1UL) / stableStepMs;
    if (points == 0 || points > 65535UL) {
        return false;
    }
    g_activeWindowPulses = static_cast<uint16_t*>(malloc(static_cast<size_t>(points) * sizeof(uint16_t)));
    if (!g_activeWindowPulses) {
        return false;
    }
    g_activeWindowCapacity = static_cast<uint16_t>(points);
    g_activeWindowCount = 0;
    g_nextWindowSampleMs = g_startedMs + stableStepMs;
    memset(g_windowPulseHistory, 0, sizeof(g_windowPulseHistory));
    g_windowPulseHistoryPos = 0;
    g_windowPulseHistoryFilled = 0;
    return true;
}

void recordWindowPoint(uint32_t cumulativePulses) {
    if (!g_activeWindowPulses || g_activeWindowCount >= g_activeWindowCapacity) {
        return;
    }
    uint32_t windowStartPulses = 0;
    if (g_windowPulseHistoryFilled >= stableWindowStepCount) {
        const uint8_t historyIndex = static_cast<uint8_t>((g_windowPulseHistoryPos + 1) % (stableWindowStepCount + 1));
        windowStartPulses = g_windowPulseHistory[historyIndex];
    }
    uint32_t windowPulses = cumulativePulses >= windowStartPulses ? cumulativePulses - windowStartPulses : 0;
    if (windowPulses > 65535UL) {
        windowPulses = 65535UL;
    }
    g_activeWindowPulses[g_activeWindowCount++] = static_cast<uint16_t>(windowPulses);
    g_windowPulseHistory[g_windowPulseHistoryPos] = cumulativePulses;
    g_windowPulseHistoryPos = static_cast<uint8_t>((g_windowPulseHistoryPos + 1) % (stableWindowStepCount + 1));
    if (g_windowPulseHistoryFilled < stableWindowStepCount + 1) {
        ++g_windowPulseHistoryFilled;
    }
}

void updateWindowCapture(uint32_t nowMs) {
    if (!g_activeWindowPulses || g_nextWindowSampleMs == 0) {
        return;
    }
    while (g_activeWindowCount < g_activeWindowCapacity &&
           static_cast<int32_t>(nowMs - g_nextWindowSampleMs) >= 0) {
        const uint32_t pulses = FlowMeter::pulseCount(g_activeZoneId);
        const uint32_t cumulative = pulses >= g_startedPulseCount ? pulses - g_startedPulseCount : 0;
        recordWindowPoint(cumulative);
        g_nextWindowSampleMs += stableStepMs;
    }
}

void shrinkActiveWindowForPending() {
    g_pending.windowPulses = g_activeWindowPulses;
    g_pending.windowPulseCount = g_activeWindowCount;
    if (g_pending.windowPulses && g_pending.windowPulseCount == 0) {
        free(g_pending.windowPulses);
        g_pending.windowPulses = nullptr;
    } else if (g_pending.windowPulses && g_pending.windowPulseCount < g_activeWindowCapacity) {
        void* shrunk = realloc(g_pending.windowPulses, static_cast<size_t>(g_pending.windowPulseCount) * sizeof(uint16_t));
        if (shrunk) {
            g_pending.windowPulses = static_cast<uint16_t*>(shrunk);
        }
    }
    g_activeWindowPulses = nullptr;
    g_activeWindowCapacity = 0;
    g_activeWindowCount = 0;
}

uint32_t pulseTimeAt(const uint16_t* deltas, uint16_t count, uint16_t index) {
    uint32_t t = 0;
    for (uint16_t i = 0; i <= index && i < count; ++i) {
        t += deltas[i];
    }
    return t;
}

uint16_t countPulsesBefore(const uint16_t* deltas, uint16_t count, uint32_t timeMs) {
    uint32_t t = 0;
    uint16_t pulses = 0;
    for (uint16_t i = 0; i < count; ++i) {
        t += deltas[i];
        if (t < timeMs) {
            ++pulses;
        }
    }
    return pulses;
}

uint16_t countPulsesInWindow(const uint16_t* deltas, uint16_t count, uint32_t startMs, uint32_t endMs) {
    uint32_t t = 0;
    uint16_t pulses = 0;
    for (uint16_t i = 0; i < count; ++i) {
        t += deltas[i];
        if (t >= startMs && t < endMs) {
            ++pulses;
        }
    }
    return pulses;
}

void analyzeStablePoint(FlowCalibration::Sample* sample) {
    if (!sample) {
        return;
    }
    sample->stableDetected = false;
    sample->stableStartMs = 0;
    sample->startupPulseInSample = 0;
    sample->stableRatePerMinuteX1000 = 0;
    sample->rateVariationPermille = 0;
    const uint16_t* deltas = sample->detailPulseDeltas;
    if (!deltas || sample->detailCapturedPulses < 2) {
        return;
    }
    const uint32_t lastPulseMs = pulseTimeAt(deltas, sample->detailCapturedPulses, sample->detailCapturedPulses - 1);
    const uint32_t requiredSpan = stableWindowMs + static_cast<uint32_t>(stableConsecutiveWindows - 1) * stableStepMs;
    if (lastPulseMs < minStableStartMs + requiredSpan) {
        return;
    }

    for (uint32_t startMs = minStableStartMs; startMs + requiredSpan <= lastPulseMs; startMs += stableStepMs) {
        uint16_t minRate = 65535;
        uint16_t maxRate = 0;
        uint32_t sum = 0;
        bool usable = true;
        for (uint8_t i = 0; i < stableConsecutiveWindows; ++i) {
            const uint32_t windowStart = startMs + static_cast<uint32_t>(i) * stableStepMs;
            const uint16_t pulses = countPulsesInWindow(deltas, sample->detailCapturedPulses, windowStart, windowStart + stableWindowMs);
            if (pulses == 0) {
                usable = false;
                break;
            }
            if (pulses < minRate) minRate = pulses;
            if (pulses > maxRate) maxRate = pulses;
            sum += pulses;
        }
        if (!usable || sum == 0) {
            continue;
        }
        const uint32_t spread = static_cast<uint32_t>(maxRate - minRate);
        if (spread * 100UL * stableConsecutiveWindows > sum * stableRateTolerancePercent) {
            continue;
        }
        sample->stableDetected = true;
        sample->stableStartMs = startMs;
        sample->startupPulseInSample = countPulsesBefore(deltas, sample->detailCapturedPulses, startMs);
        sample->stableRatePerMinuteX1000 = static_cast<uint32_t>((static_cast<uint64_t>(sum) * 60000ULL * 1000ULL) /
                                                                 (stableWindowMs * stableConsecutiveWindows));
        sample->rateVariationPermille = static_cast<uint16_t>((spread * 1000UL * stableConsecutiveWindows) / sum);
        return;
    }
}

uint32_t estimateMl(uint32_t pulses, uint16_t startupPulseLimit, uint16_t startupEstimatedMl, uint16_t stablePulsePerLiter) {
    if (stablePulsePerLiter == 0) {
        return 0;
    }
    if (startupPulseLimit == 0) {
        return static_cast<uint32_t>((static_cast<uint64_t>(pulses) * 1000ULL) / stablePulsePerLiter);
    }
    const uint32_t startupPulses = pulses < startupPulseLimit ? pulses : startupPulseLimit;
    const uint32_t stablePulses = pulses > startupPulseLimit ? pulses - startupPulseLimit : 0;
    const uint64_t startupMl = (static_cast<uint64_t>(startupPulses) * startupEstimatedMl) / startupPulseLimit;
    const uint64_t stableMl = (static_cast<uint64_t>(stablePulses) * 1000ULL) / stablePulsePerLiter;
    return static_cast<uint32_t>(startupMl + stableMl);
}

uint8_t collectValidSamples(const FlowCalibration::Sample** out) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < g_sampleCount; ++i) {
        if (g_samples[i].exists && g_samples[i].valid && g_samples[i].actualMl > 0 && g_samples[i].totalPulses > 0) {
            out[count++] = &g_samples[i];
        }
    }
    return count;
}

void fillRecommendationDiagnostics(FlowCalibration::Recommendation* out, const FlowCalibration::Sample** samples, uint8_t sampleCount) {
    if (!out || !samples || sampleCount == 0) {
        return;
    }
    uint32_t minMl = samples[0]->actualMl;
    uint32_t maxMl = samples[0]->actualMl;
    uint32_t minPulses = samples[0]->totalPulses;
    uint32_t maxPulses = samples[0]->totalPulses;
    uint8_t stableCount = 0;
    for (uint8_t i = 0; i < sampleCount; ++i) {
        if (samples[i]->actualMl < minMl) minMl = samples[i]->actualMl;
        if (samples[i]->actualMl > maxMl) maxMl = samples[i]->actualMl;
        if (samples[i]->totalPulses < minPulses) minPulses = samples[i]->totalPulses;
        if (samples[i]->totalPulses > maxPulses) maxPulses = samples[i]->totalPulses;
        if (samples[i]->stableDetected) {
            ++stableCount;
        }
    }
    out->stableDetectedCount = stableCount;
    out->volumeSpanPermille = maxMl > 0 ? static_cast<uint16_t>(((maxMl - minMl) * 1000UL) / maxMl) : 0;
    out->pulseSpanPermille = maxPulses > 0 ? static_cast<uint16_t>(((maxPulses - minPulses) * 1000UL) / maxPulses) : 0;
}

uint16_t medianDetectedStartup() {
    uint16_t values[FlowCalibration::MaxSamples] = {};
    uint8_t count = 0;
    for (uint8_t i = 0; i < g_sampleCount; ++i) {
        if (g_samples[i].exists && g_samples[i].valid && g_samples[i].stableDetected) {
            values[count++] = g_samples[i].startupPulseInSample;
        }
    }
    if (count == 0) {
        return 0;
    }
    for (uint8_t i = 0; i < count; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < count; ++j) {
            if (values[j] < values[i]) {
                const uint16_t tmp = values[i];
                values[i] = values[j];
                values[j] = tmp;
            }
        }
    }
    return values[count / 2];
}

bool scoreCandidate(uint16_t candidate,
                    const FlowCalibration::Sample** samples,
                    uint8_t sampleCount,
                    FlowCalibration::Recommendation* out) {
    double startupEstimatedMl = 0.0;
    double mlPerStablePulse = 0.0;
    if (sampleCount == 1) {
        const FlowCalibration::Sample* sample = samples[0];
        const double mlPerPulse = static_cast<double>(sample->actualMl) / static_cast<double>(sample->totalPulses);
        mlPerStablePulse = mlPerPulse;
        startupEstimatedMl = static_cast<double>(candidate) * mlPerPulse;
    } else if (candidate == 0) {
        double sx2 = 0.0;
        double sxy = 0.0;
        for (uint8_t i = 0; i < sampleCount; ++i) {
            const double x = static_cast<double>(samples[i]->totalPulses);
            sx2 += x * x;
            sxy += x * static_cast<double>(samples[i]->actualMl);
        }
        if (sx2 <= 0.0) {
            return false;
        }
        mlPerStablePulse = sxy / sx2;
    } else {
        double s11 = 0.0;
        double s12 = 0.0;
        double s22 = 0.0;
        double sy1 = 0.0;
        double sy2 = 0.0;
        for (uint8_t i = 0; i < sampleCount; ++i) {
            const double x1 = static_cast<double>(samples[i]->totalPulses < candidate ? samples[i]->totalPulses : candidate) /
                              static_cast<double>(candidate);
            const double x2 = samples[i]->totalPulses > candidate ? static_cast<double>(samples[i]->totalPulses - candidate) : 0.0;
            const double y = static_cast<double>(samples[i]->actualMl);
            s11 += x1 * x1;
            s12 += x1 * x2;
            s22 += x2 * x2;
            sy1 += x1 * y;
            sy2 += x2 * y;
        }
        const double det = s11 * s22 - s12 * s12;
        if (det <= 0.000001) {
            return false;
        }
        startupEstimatedMl = (sy1 * s22 - sy2 * s12) / det;
        mlPerStablePulse = (s11 * sy2 - s12 * sy1) / det;
    }

    if (startupEstimatedMl < 0.0 || mlPerStablePulse <= 0.0) {
        return false;
    }
    const uint16_t startupMl = static_cast<uint16_t>(startupEstimatedMl > 10000.0 ? 10000 : startupEstimatedMl + 0.5);
    const double stablePplRaw = 1000.0 / mlPerStablePulse;
    const uint16_t stablePpl = static_cast<uint16_t>(stablePplRaw < 1.0 ? 1 : (stablePplRaw > 10000.0 ? 10000 : stablePplRaw + 0.5));

    out->valid = true;
    out->flow.startupPulseLimit = candidate;
    out->flow.startupEstimatedMl = candidate == 0 ? 0 : startupMl;
    out->flow.stablePulsePerLiter = stablePpl;
    out->sampleCount = sampleCount;
    uint32_t sumErrorPermille = 0;
    uint16_t maxErrorPermille = 0;
    for (uint8_t i = 0; i < sampleCount; ++i) {
        const uint32_t estimated = estimateMl(samples[i]->totalPulses,
                                              out->flow.startupPulseLimit,
                                              out->flow.startupEstimatedMl,
                                              out->flow.stablePulsePerLiter);
        const int32_t errorMl = static_cast<int32_t>(estimated) - static_cast<int32_t>(samples[i]->actualMl);
        const uint32_t absError = errorMl < 0 ? static_cast<uint32_t>(-errorMl) : static_cast<uint32_t>(errorMl);
        const uint16_t errorPermille = static_cast<uint16_t>((absError * 1000UL) / samples[i]->actualMl);
        out->errors[i].sampleIndex = i;
        out->errors[i].actualMl = samples[i]->actualMl;
        out->errors[i].estimatedMl = estimated;
        out->errors[i].errorMl = errorMl;
        out->errors[i].errorPermille = errorPermille;
        sumErrorPermille += errorPermille;
        if (errorPermille > maxErrorPermille) {
            maxErrorPermille = errorPermille;
        }
    }
    out->averageErrorPermille = static_cast<uint16_t>(sumErrorPermille / sampleCount);
    out->maxErrorPermille = maxErrorPermille;
    fillRecommendationDiagnostics(out, samples, sampleCount);
    return true;
}

void finishCapture(bool validStop, const char* invalidReason) {
    FlowMeter::CaptureResult capture = {};
    (void)ValveController::off(g_activeZoneId, "flow calibration finish");
    updateWindowCapture(millis());
    if (!FlowMeter::endCapture(g_pendingDeltas, FlowCalibration::MaxDetailPulses, &capture)) {
        setError("capture_end_failed");
        g_state = FlowCalibration::State::IDLE;
        releaseActiveWindow();
        return;
    }
    freeSampleData(&g_pending);
    g_pending.exists = true;
    g_pending.valid = validStop;
    g_pending.zoneId = g_activeZoneId;
    g_pending.startedMs = g_startedMs;
    g_pending.endedMs = millis();
    g_pending.durationMs = g_pending.endedMs - g_startedMs;
    const uint32_t endedPulseCount = FlowMeter::pulseCount(g_activeZoneId);
    g_pending.totalPulses = endedPulseCount >= g_startedPulseCount ? endedPulseCount - g_startedPulseCount : 0;
    g_pending.detailCapturedPulses = capture.capturedPulses;
    strlcpy(g_pending.detailCaptureEndedReason, capture.endedReason, sizeof(g_pending.detailCaptureEndedReason));
    if (g_pending.detailCapturedPulses > 0) {
        g_pending.detailPulseDeltas = static_cast<uint16_t*>(malloc(static_cast<size_t>(g_pending.detailCapturedPulses) * sizeof(uint16_t)));
        if (!g_pending.detailPulseDeltas) {
            freeSampleData(&g_pending);
            releaseActiveWindow();
            setError("memory_low");
            g_state = FlowCalibration::State::IDLE;
            return;
        }
        memcpy(g_pending.detailPulseDeltas, g_pendingDeltas, static_cast<size_t>(g_pending.detailCapturedPulses) * sizeof(uint16_t));
    }
    shrinkActiveWindowForPending();
    if (!validStop) {
        strlcpy(g_pending.invalidReason, invalidReason ? invalidReason : "invalid_stop", sizeof(g_pending.invalidReason));
        if (sampleSlotAvailable()) {
            freeSampleData(&g_samples[g_sampleCount]);
            g_samples[g_sampleCount] = g_pending;
            g_pending = {};
            ++g_sampleCount;
        } else {
            freeSampleData(&g_pending);
        }
        g_state = FlowCalibration::State::IDLE;
    } else {
        g_state = FlowCalibration::State::WAITING_ACTUAL;
    }
}

}

namespace FlowCalibration {

void begin() {
    clear();
}

void handle(const Irrigation::SystemConfig& config) {
    if (g_state != State::CAPTURING) {
        return;
    }
    const uint32_t now = millis();
    updateWindowCapture(now);
    if (now - g_startedMs >= static_cast<uint32_t>(config.calibrationMaxCaptureMin) * 60000UL) {
        finishCapture(false, "capture_timeout");
    }
}

bool active() {
    return g_state == State::CAPTURING || g_state == State::WAITING_ACTUAL;
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
    out->sampleCapacity = g_sampleCapacity;
    if (g_state == State::CAPTURING) {
        const uint32_t now = millis();
        out->elapsedMs = now - g_startedMs;
        out->remainingMs = out->elapsedMs >= g_maxCaptureMs ? 0 : g_maxCaptureMs - out->elapsedMs;
        const uint32_t pulses = FlowMeter::pulseCount(g_activeZoneId);
        out->currentPulses = pulses >= g_startedPulseCount ? pulses - g_startedPulseCount : 0;
        return;
    }
    if (g_state == State::WAITING_ACTUAL && g_pending.exists) {
        out->pendingExists = true;
        out->pendingZoneId = g_pending.zoneId;
        out->pendingDurationMs = g_pending.durationMs;
        out->pendingTotalPulses = g_pending.totalPulses;
        out->pendingDetailCapturedPulses = g_pending.detailCapturedPulses;
        out->activeZoneId = g_pending.zoneId;
        out->elapsedMs = g_pending.durationMs;
        out->currentPulses = g_pending.totalPulses;
    }
}

const char* lastError() {
    return g_lastError;
}

bool start(uint8_t zoneId, const Irrigation::SystemConfig& config) {
    if (g_state != State::IDLE) {
        setError("calibration_busy");
        return false;
    }
    if (!Irrigation::validZoneId(zoneId) || !ZoneConfigStore::get(zoneId).enabled) {
        setError("invalid_zone");
        return false;
    }
    if (ZoneManager::isBusy() || ZoneManager::leakAlertActive()) {
        setError("zone_busy");
        return false;
    }
    if (!sampleWorkAreaMatches(zoneId)) {
        setError("sample_zone_mismatch");
        return false;
    }
    g_sampleCapacity = clampSampleCapacity(config.calibrationSampleTarget);
    if (!sampleSlotAvailable()) {
        setError("sample_full");
        return false;
    }
    const uint16_t detailLimit = clampDetailPulseLimit(config.calibrationDetailPulseLimit);
    g_activeZoneId = zoneId;
    g_startedMs = millis();
    g_startedPulseCount = FlowMeter::pulseCount(zoneId);
    g_maxCaptureMs = static_cast<uint32_t>(config.calibrationMaxCaptureMin) * 60000UL;
    if (!beginWindowCapture(config)) {
        setError("memory_low");
        return false;
    }
    if (!FlowMeter::beginCapture(zoneId, static_cast<uint32_t>(config.calibrationDetailCaptureSec) * 1000UL, detailLimit)) {
        releaseActiveWindow();
        setError("capture_start_failed");
        return false;
    }
    if (!ValveController::setRoad(zoneId, true, "flow calibration start")) {
        FlowMeter::CaptureResult ignored = {};
        (void)FlowMeter::endCapture(g_pendingDeltas, FlowCalibration::MaxDetailPulses, &ignored);
        releaseActiveWindow();
        setError("valve_start_failed");
        return false;
    }
    g_state = State::CAPTURING;
    setError("");
    return true;
}

bool stop() {
    if (g_state != State::CAPTURING) {
        setError("not_capturing");
        return false;
    }
    finishCapture(true, nullptr);
    return g_state == State::WAITING_ACTUAL;
}

bool abort(const char* reason) {
    if (g_state == State::IDLE) {
        return false;
    }
    const char* abortReason = reason && reason[0] ? reason : "aborted";
    if (g_state == State::CAPTURING) {
        finishCapture(false, abortReason);
    } else {
        (void)ValveController::off(g_activeZoneId, "flow calibration abort");
        freeSampleData(&g_pending);
        releaseActiveWindow();
        g_state = State::IDLE;
    }
    setError(abortReason);
    return true;
}

bool submitActualMilliliters(uint32_t actualMl) {
    if (g_state != State::WAITING_ACTUAL) {
        setError("not_waiting_actual");
        return false;
    }
    if (actualMl == 0 || g_sampleCount >= FlowCalibration::MaxSamples) {
        setError("invalid_actual_ml");
        return false;
    }
    g_pending.actualMl = actualMl;
    g_pending.valid = g_pending.totalPulses > 0;
    if (!g_pending.valid) {
        strlcpy(g_pending.invalidReason, "no_pulses", sizeof(g_pending.invalidReason));
    } else {
        analyzeStablePoint(&g_pending);
    }
    freeSampleData(&g_samples[g_sampleCount]);
    g_samples[g_sampleCount] = g_pending;
    ++g_sampleCount;
    g_recommendation = {};
    g_pending = {};
    g_state = State::IDLE;
    setError("");
    return true;
}

bool updateActualMilliliters(uint8_t sampleIndex, uint32_t actualMl) {
    if (sampleIndex >= g_sampleCount || actualMl == 0) {
        setError("invalid_actual_ml");
        return false;
    }
    Sample& target = g_samples[sampleIndex];
    if (!target.exists || target.totalPulses == 0) {
        setError("invalid_sample");
        return false;
    }
    target.actualMl = actualMl;
    target.valid = true;
    target.invalidReason[0] = '\0';
    analyzeStablePoint(&target);
    g_recommendation = {};
    setError("");
    return true;
}

bool clear() {
    (void)ValveController::off(g_activeZoneId, "flow calibration clear");
    if (g_state == State::CAPTURING) {
        FlowMeter::CaptureResult ignored = {};
        (void)FlowMeter::endCapture(g_pendingDeltas, FlowCalibration::MaxDetailPulses, &ignored);
    }
    for (uint8_t i = 0; i < FlowCalibration::MaxSamples; ++i) {
        freeSampleData(&g_samples[i]);
    }
    freeSampleData(&g_pending);
    releaseActiveWindow();
    g_state = State::IDLE;
    g_activeZoneId = 0;
    g_startedMs = 0;
    g_startedPulseCount = 0;
    g_maxCaptureMs = 0;
    memset(g_pendingDeltas, 0, sizeof(g_pendingDeltas));
    g_recommendation = {};
    g_sampleCount = 0;
    setError("");
    return true;
}

uint8_t sampleCount() {
    return g_sampleCount;
}

uint8_t validSampleCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < g_sampleCount; ++i) {
        if (g_samples[i].valid) {
            ++count;
        }
    }
    return count;
}

const Sample& sample(uint8_t index) {
    if (index >= g_sampleCount) {
        return g_emptySample;
    }
    return g_samples[index];
}

uint16_t samplePulseDelta(uint8_t sampleIndex, uint16_t pulseIndex) {
    if (sampleIndex >= g_sampleCount || pulseIndex >= g_samples[sampleIndex].detailCapturedPulses || !g_samples[sampleIndex].detailPulseDeltas) {
        return 0;
    }
    return g_samples[sampleIndex].detailPulseDeltas[pulseIndex];
}

uint16_t sampleWindowPulse(uint8_t sampleIndex, uint16_t pointIndex) {
    if (sampleIndex >= g_sampleCount || pointIndex >= g_samples[sampleIndex].windowPulseCount || !g_samples[sampleIndex].windowPulses) {
        return 0;
    }
    return g_samples[sampleIndex].windowPulses[pointIndex];
}

bool computeRecommendation(Recommendation* out) {
    const Sample* samples[MaxSamples] = {};
    const uint8_t validCount = collectValidSamples(samples);
    if (validCount == 0) {
        setError("no_valid_samples");
        return false;
    }
    for (uint8_t i = 0; i < g_sampleCount; ++i) {
        if (g_samples[i].exists && g_samples[i].zoneId != samples[0]->zoneId) {
            setError("sample_zone_mismatch");
            return false;
        }
    }
    Recommendation best = {};
    double bestScore = 999999999.0;
    const uint16_t median = medianDetectedStartup();
    uint16_t beginCandidate = 0;
    uint16_t endCandidate = 0;
    if (median > 0) {
        beginCandidate = static_cast<uint16_t>((static_cast<uint32_t>(median) * 70UL) / 100UL);
        endCandidate = static_cast<uint16_t>((static_cast<uint32_t>(median) * 130UL) / 100UL);
        if (endCandidate <= beginCandidate) {
            endCandidate = static_cast<uint16_t>(beginCandidate + 1);
        }
    }
    for (uint16_t candidate = beginCandidate; candidate <= endCandidate; ++candidate) {
        Recommendation current = {};
        if (!scoreCandidate(candidate, samples, validCount, &current)) {
            continue;
        }
        double score = static_cast<double>(current.averageErrorPermille) + static_cast<double>(current.maxErrorPermille) * 0.25;
        if (!best.valid || score < bestScore) {
            best = current;
            bestScore = score;
        }
        if (candidate == 65535) {
            break;
        }
    }
    if (!best.valid) {
        setError("fit_failed");
        return false;
    }
    best.zoneId = samples[0]->zoneId;
    g_recommendation = best;
    if (out) {
        *out = g_recommendation;
    }
    setError("");
    return true;
}

const Recommendation& recommendation() {
    return g_recommendation;
}

bool saveCandidate() {
    if (!g_recommendation.valid || !Irrigation::validZoneId(g_recommendation.zoneId)) {
        setError("no_recommendation");
        return false;
    }
    if (!ZoneConfigStore::saveCandidate(g_recommendation.zoneId, g_recommendation.flow)) {
        setError("save_failed");
        return false;
    }
    setError("");
    return true;
}

}
