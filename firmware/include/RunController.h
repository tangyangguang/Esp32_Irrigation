#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

class RunController {
public:
    static void begin();
    static void handle(uint32_t nowMs);

    static bool startManual(const uint32_t zoneDurationSec[kMaxZones], RunReason& reason);
    static bool startPlan(uint8_t planId, RunReason& reason);
    static bool startPlanNow(uint8_t planId, RunReason& reason);
    static bool startCalibration(uint8_t zoneId, uint32_t durationSec, RunReason& reason);
    static bool stop(RunReason reason = RunReason::UserStop);

    static bool busy();
    static StatusSnapshot statusSnapshot();
    static const WateringRun& currentRun();
    static const char* lastError();

private:
    static void enterState(RunState state, uint32_t nowMs);
    static void finish(RunResult result, RunReason reason, uint32_t nowMs);
};

} // namespace Irrigation
