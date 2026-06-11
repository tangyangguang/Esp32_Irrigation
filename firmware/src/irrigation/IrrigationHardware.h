#pragma once

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

private:
    bool _ready = false;
};

}  // namespace irrigation
