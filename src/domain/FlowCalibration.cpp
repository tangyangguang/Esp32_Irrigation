#include "domain/FlowCalibration.h"

#include <string.h>

#include "storage/FlowConfigStore.h"

namespace {

FlowCalibration::State g_state = FlowCalibration::State::IDLE;
uint8_t g_activeZoneId = 0;
FlowCalibration::Recommendation g_recommendation = {};
FlowCalibration::Sample g_emptySample = {};
char g_lastError[64] = {};

void setError(const char* error) {
    strlcpy(g_lastError, error ? error : "", sizeof(g_lastError));
}

}

namespace FlowCalibration {

void begin() {
    g_state = State::IDLE;
    g_activeZoneId = 0;
    g_recommendation = {};
    setError("");
}

void handle(const Irrigation::SystemConfig& config) {
    (void)config;
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
    out->sampleCapacity = MaxSamples;
}

const char* lastError() {
    return g_lastError;
}

bool start(uint8_t zoneId, const Irrigation::SystemConfig& config) {
    (void)config;
    if (!Irrigation::validZoneId(zoneId) || active()) {
        setError("calibration_unavailable");
        return false;
    }
    g_activeZoneId = zoneId;
    g_state = State::WAITING_ACTUAL;
    g_recommendation = {};
    g_recommendation.valid = true;
    g_recommendation.flowId = 1;
    g_recommendation.sampleCount = 0;
    g_recommendation.calibration = FlowConfigStore::defaultCalibration();
    setError("");
    return true;
}

bool stop() {
    if (!active()) {
        setError("calibration_not_active");
        return false;
    }
    g_state = State::IDLE;
    g_activeZoneId = 0;
    setError("");
    return true;
}

bool abort(const char* reason) {
    (void)reason;
    g_state = State::IDLE;
    g_activeZoneId = 0;
    setError("");
    return true;
}

bool submitActualMilliliters(uint32_t actualMl) {
    (void)actualMl;
    if (!active()) {
        setError("calibration_not_active");
        return false;
    }
    return stop();
}

bool updateActualMilliliters(uint8_t sampleIndex, uint32_t actualMl) {
    (void)sampleIndex;
    (void)actualMl;
    setError("calibration_samples_not_ready");
    return false;
}

bool clear() {
    g_state = State::IDLE;
    g_activeZoneId = 0;
    g_recommendation = {};
    setError("");
    return true;
}

uint8_t sampleCount() {
    return 0;
}

uint8_t validSampleCount() {
    return 0;
}

const Sample& sample(uint8_t index) {
    (void)index;
    return g_emptySample;
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
    if (out) {
        *out = g_recommendation;
    }
    return g_recommendation.valid;
}

const Recommendation& recommendation() {
    return g_recommendation;
}

bool saveCandidate() {
    setError("calibration_save_not_ready");
    return false;
}

}
