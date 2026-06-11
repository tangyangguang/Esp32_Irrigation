#pragma once

#include <stdint.h>

namespace irrigation {

struct FlowMeterSnapshot {
    uint32_t pulseCount;
    uint32_t flowMlPerMin;
    bool hasPulse;
};

class FlowMeterService {
public:
    // Owns pulse-derived measurements only. It does not decide whether a Zone
    // may start or stop; IrrigationRuntime owns those business decisions.
    bool begin();
    void handle();

    FlowMeterSnapshot snapshot() const;

private:
    FlowMeterSnapshot _snapshot = {};
    bool _ready = false;
};

}  // namespace irrigation
