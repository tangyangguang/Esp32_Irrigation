#pragma once

#include <stdint.h>

namespace irrigation {

struct FlowMeterSnapshot {
    uint32_t pulseCount;
    uint32_t lastPulseMs;
    int8_t inputPin;
    bool hasPulse;
};

class FlowMeterService {
public:
    // Owns pulse counting only. It does not decide whether a Zone may start or
    // stop; IrrigationRuntime owns those business decisions.
    bool begin(int8_t inputPin);
    void handle();
    void reset();

    FlowMeterSnapshot snapshot() const;

private:
    static void onPulse(void* arg);
    void recordPulse();

    FlowMeterSnapshot _snapshot = {};
    volatile uint32_t _pulseCount = 0;
    uint32_t _lastPulseMs = 0;
    uint32_t _lastObservedPulseCount = 0;
    int8_t _inputPin = -1;
    bool _ready = false;
};

}  // namespace irrigation
