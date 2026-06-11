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

    int8_t flowInputPin() const;
    int8_t lowLevelInputPin() const;
    int8_t valveOutputPin(uint8_t zoneId) const;
    int8_t pumpStartOutputPin() const;

    bool setValveOutput(uint8_t zoneId, bool enabled);
    void setPumpStartOutput(bool enabled);
    bool lowLevelActive() const;

private:
    IrrigationPinMap _pinMap = kRecommendedPinMap;
    bool _ready = false;
};

}  // namespace irrigation
