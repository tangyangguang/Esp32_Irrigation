#pragma once

#include <stdint.h>

#include "IrrigationConstants.h"

namespace irrigation {

// Recommended ESP32 Dev Module pin map for bring-up and early PCB routing.
// Keep final board-specific changes centralized here.
struct IrrigationPinMap {
    int8_t flowInput;
    int8_t lowLevelInput;
    int8_t valveOutputs[kMaxZones];
    int8_t pumpStartOutput;
};

constexpr IrrigationPinMap kRecommendedPinMap = {
    34,                         // FLOW_IN, input-only, external pull-up required.
    35,                         // LOW_LEVEL_IN, input-only, external pull-up required.
    {25, 26, 27, 14, 13, 23},   // VALVE_OUT_1..6, LEDC-capable output GPIOs.
    32                          // PUMP_START_OUT, optional relay/module control.
};

}  // namespace irrigation
