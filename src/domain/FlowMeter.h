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
void setStablePulsePerLiter(uint8_t road, uint16_t stablePulsePerLiter);
uint32_t pulseCount(uint8_t road);
uint32_t pulseRatePerMinuteX1000(uint8_t road);
uint32_t flowMillilitersPerMinute(uint8_t road);
bool flowRateReady(uint8_t road);
uint16_t flowRateWindowSec();
uint16_t flowChartIntervalSec();
uint16_t flowChartHistoryMin();
uint16_t readFlowHistory(uint8_t road, uint16_t* out, uint16_t capacity);
bool beginCapture(uint8_t road, uint32_t detailCaptureMs, uint16_t detailPulseLimit);
bool endCapture(uint16_t* deltas, uint16_t capacity, CaptureResult* result);

}
