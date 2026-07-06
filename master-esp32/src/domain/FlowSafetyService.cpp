#include "FlowSafetyService.h"

#include "BoardHardware.h"
#include "ConfigStore.h"

namespace Irrigation {

namespace {
uint32_t g_stepStartedMs = 0;
uint32_t g_runningEnteredMs = 0;
uint32_t g_lastPulseMs = 0;
uint32_t g_lastPulseCount = 0;
bool g_runningObserved = false;
} // namespace

void FlowSafetyService::beginStep(uint32_t nowMs) {
    BoardHardware::resetFlowCounter();
    g_stepStartedMs = nowMs;
    g_runningEnteredMs = 0;
    g_lastPulseMs = nowMs;
    g_lastPulseCount = 0;
    g_runningObserved = false;
}

bool FlowSafetyService::checkFlowGrace(uint32_t nowMs, RunReason& reason) {
    return checkLowLevel(nowMs, reason);
}

bool FlowSafetyService::checkRunning(uint32_t nowMs, RunReason& reason) {
    if (checkLowLevel(nowMs, reason)) {
        return true;
    }

    const uint32_t pulses = BoardHardware::flowPulseCount();
    if (!g_runningObserved) {
        g_runningObserved = true;
        g_runningEnteredMs = nowMs;
        g_lastPulseMs = nowMs;
        g_lastPulseCount = pulses;
    }

    if (pulses != g_lastPulseCount) {
        g_lastPulseCount = pulses;
        g_lastPulseMs = nowMs;
        return false;
    }

    const uint32_t confirmMs = ConfigStore::config().flow.noFlowConfirmSec * 1000UL;
    if (confirmMs > 0 && nowMs - g_lastPulseMs >= confirmMs) {
        reason = RunReason::NoFlow;
        return true;
    }

    return false;
}

uint32_t FlowSafetyService::currentStepPulses() {
    return BoardHardware::flowPulseCount();
}

bool FlowSafetyService::checkLowLevel(uint32_t nowMs, RunReason& reason) {
    if (BoardHardware::lowLevelActive(nowMs)) {
        reason = RunReason::LowLevel;
        return true;
    }
    return false;
}

} // namespace Irrigation

