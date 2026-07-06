#pragma once

#include <stdint.h>

#include "IrrigationTypes.h"

namespace Irrigation {

class FlowSafetyService {
public:
    static void beginStep(uint32_t nowMs);
    static bool checkFlowGrace(uint32_t nowMs, RunReason& reason);
    static bool checkRunning(uint32_t nowMs, RunReason& reason);
    static uint32_t currentStepPulses();

private:
    static bool checkLowLevel(uint32_t nowMs, RunReason& reason);
};

} // namespace Irrigation

