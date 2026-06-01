#pragma once

#include <stdint.h>

namespace FlowMeter {

struct CaptureResult {
    uint32_t startedMs;
    uint32_t endedMs;
    uint16_t capturedPulses;
    const char* endedReason;
};

void begin();
void handle();
uint32_t pulseCount(uint8_t road);
uint32_t pulseRatePerMinuteX1000(uint8_t road);
bool beginCapture(uint8_t road, uint32_t detailCaptureMs, uint16_t detailPulseLimit);
bool endCapture(uint16_t* deltas, uint16_t capacity, CaptureResult* result);

}
