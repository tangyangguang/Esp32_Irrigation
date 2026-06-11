#pragma once

#include "IrrigationPinMap.h"

namespace irrigation {

class IrrigationConfig;

class IrrigationHardware {
public:
    // Thin hardware boundary for GPIO, PWM, flow input, low-level input, valves
    // and pump start output. It must not own runtime state transitions.
    bool begin(const IrrigationConfig& config);
    void handle();
    void closeAllOutputs();

    bool ready() const;
    const IrrigationPinMap& pinMap() const;

private:
    IrrigationPinMap _pinMap = kRecommendedPinMap;
    bool _ready = false;
};

}  // namespace irrigation
