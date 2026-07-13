#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

class PlanService {
public:
    static const WateringPlan* find(uint8_t planId);
    static bool snapshot(uint8_t planId, PlanSnapshot& out);
    static bool createPlan(const char* name, uint8_t& planIdOut);
    static bool savePlan(const WateringPlan& plan);
    static bool deletePlan(uint8_t planId);
    static bool setEnabled(uint8_t planId, bool enabled);
    static bool canRunNow(uint8_t planId, RunReason& reason);
    static bool buildSteps(uint8_t planId, WateringStep* out, uint8_t maxSteps, uint8_t& stepCount, RunReason& reason);
    static const char* lastError();

private:
    static void setLastError(const char* error);
};

} // namespace Irrigation

