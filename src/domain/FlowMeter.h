#pragma once

#include <stdint.h>

namespace FlowMeter {

static constexpr uint16_t MaxFlowHistoryPoints = 360;

struct CaptureResult {
    uint32_t startedMs;
    uint32_t endedMs;
    uint16_t capturedPulses;
    const char* endedReason;
};

void begin();
void handle();
void configureFlowRate(uint16_t windowSec, uint16_t chartIntervalSec, uint16_t chartHistoryMin);
void configureCalibration(uint8_t flowId, int32_t kUlPerMinPerHz, int32_t offsetMilliHz);
uint32_t pulseCount(uint8_t flowId);
uint32_t pulseRatePerMinuteX1000(uint8_t flowId);
uint32_t flowMillilitersPerMinute(uint8_t flowId);
bool flowRateReady(uint8_t flowId);
uint16_t sampleWindowSec();
uint16_t historyIntervalSec();
uint16_t historyDepthMin();
uint16_t readFlowHistory(uint8_t flowId, uint16_t* out, uint16_t capacity);
bool beginCapture(uint8_t flowId, uint32_t detailCaptureMs, uint16_t detailPulseLimit);
bool endCapture(uint16_t* deltas, uint16_t capacity, CaptureResult* result);

}
