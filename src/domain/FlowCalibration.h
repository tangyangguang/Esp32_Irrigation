#pragma once

#include <stdint.h>

#include "domain/ZoneTypes.h"

namespace FlowCalibration {

static constexpr uint8_t MaxSamples = 5;
static constexpr uint16_t MaxDetailPulses = 5000;

enum class State : uint8_t {
    IDLE = 0,
    CAPTURING = 1,
    WAITING_ACTUAL = 2,
};

struct Sample {
    bool exists;
    bool valid;
    bool stableDetected;
    uint8_t zoneId;
    uint32_t startedMs;
    uint32_t endedMs;
    uint32_t durationMs;
    uint32_t totalPulses;
    uint32_t actualMl;
    uint16_t detailCapturedPulses;
    uint16_t* detailPulseDeltas;
    char detailCaptureEndedReason[16];
    uint16_t* windowPulses;
    uint16_t windowPulseCount;
    uint32_t stableStartMs;
    uint16_t startupPulseInSample;
    uint32_t stableRatePerMinuteX1000;
    uint16_t rateVariationPermille;
    char invalidReason[24];
};

struct SampleError {
    uint8_t sampleIndex;
    uint32_t actualMl;
    uint32_t estimatedMl;
    int32_t errorMl;
    uint16_t errorPermille;
};

struct Recommendation {
    bool valid;
    uint8_t zoneId;
    uint8_t sampleCount;
    uint8_t stableDetectedCount;
    Irrigation::FlowParameters flow;
    uint16_t volumeSpanPermille;
    uint16_t pulseSpanPermille;
    uint16_t averageErrorPermille;
    uint16_t maxErrorPermille;
    SampleError errors[MaxSamples];
};

void begin();
void handle(const Irrigation::SystemConfig& config);
bool active();
State state();
uint8_t activeZoneId();
const char* lastError();
bool start(uint8_t zoneId, const Irrigation::SystemConfig& config);
bool stop();
bool submitActualMilliliters(uint32_t actualMl);
bool updateActualMilliliters(uint8_t sampleIndex, uint32_t actualMl);
bool clear();
uint8_t sampleCount();
uint8_t validSampleCount();
const Sample& sample(uint8_t index);
uint16_t samplePulseDelta(uint8_t sampleIndex, uint16_t pulseIndex);
uint16_t sampleWindowPulse(uint8_t sampleIndex, uint16_t pointIndex);
bool computeRecommendation(Recommendation* out);
const Recommendation& recommendation();
bool applyRecommendation();

}
