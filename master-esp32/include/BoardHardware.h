#pragma once

#include <stdint.h>

#include "IrrigationTypes.h"

namespace Irrigation {

class BoardHardware {
public:
    struct FlowSnapshot {
        uint32_t pulseCount;
        uint32_t deltaPulses;
        uint32_t nowMs;
    };

    static void begin(const ValveConfig& valveConfig, const SupplyConfig& supplyConfig);
    static void configure(const ValveConfig& valveConfig, const SupplyConfig& supplyConfig);
    static void handle(uint32_t nowMs);
    static void safeOff();

    static bool openValve(uint8_t zoneId, uint32_t nowMs);
    static void closeValve(uint8_t zoneId);
    static void closeAllValves();
    static bool isValveOpen(uint8_t zoneId);
    static uint8_t activeValveCount();

    static void setPumpSignal(bool enabled);
    static bool pumpSignalActive();

    static bool lowLevelRawActive();
    static bool lowLevelActive(uint32_t nowMs);

    static FlowSnapshot flowSnapshot(uint32_t nowMs);
    static uint32_t flowPulseCount();
    static void resetFlowCounter();

private:
    static void onFlowPulse();
};

} // namespace Irrigation
