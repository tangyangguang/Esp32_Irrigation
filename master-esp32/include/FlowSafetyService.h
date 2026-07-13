#pragma once

#include <stdint.h>

#include "IrrigationTypes.h"

namespace Irrigation {

class FlowSafetyService {
public:
    static void handleIdle(uint32_t nowMs);
    static void beginStep(uint32_t nowMs, uint8_t zoneId);
    static bool checkFlowGrace(uint32_t nowMs, RunReason& reason);
    static bool checkRunning(uint32_t nowMs, RunReason& reason);
    static uint32_t currentStepPulses();
    static uint32_t currentFlowMlPerMin();
    static uint32_t currentStepVolumeMl();

private:
    static void updateFlowEstimate(uint32_t nowMs, uint32_t pulses);
    static void checkFlowDeviation(uint32_t nowMs);
};

} // namespace Irrigation
