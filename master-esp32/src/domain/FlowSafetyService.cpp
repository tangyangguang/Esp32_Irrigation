#include "FlowSafetyService.h"

#include "BoardHardware.h"
#include "ConfigStore.h"
#include "EventService.h"
#include "IrrigationConfig.h"

namespace Irrigation {

namespace {
constexpr uint32_t kFlowSampleWindowMs = 5000;

uint8_t g_zoneId = 0;
uint32_t g_stepStartedMs = 0;
uint32_t g_runningEnteredMs = 0;
uint32_t g_lastPulseMs = 0;
uint32_t g_lastPulseCount = 0;
uint32_t g_currentFlowMlPerMin = 0;
uint32_t g_flowSampleStartedMs = 0;
uint32_t g_flowSampleStartPulses = 0;
uint32_t g_idleWindowStartedMs = 0;
uint32_t g_idleWindowStartPulses = 0;
uint32_t g_lowDeviationStartedMs = 0;
uint32_t g_highDeviationStartedMs = 0;
bool g_runningObserved = false;
bool g_lowFlowReported = false;
bool g_highFlowReported = false;
bool g_leakReported = false;

uint32_t volumeMlFromPulses(uint32_t pulses) {
    const uint32_t pulsesPerLiter = ConfigStore::config().flow.pulsesPerLiter;
    if (pulses == 0 || pulsesPerLiter == 0) {
        return 0;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(pulses) * 1000ULL) / pulsesPerLiter);
}

void appendFlowNotice(const char* type, uint32_t currentFlow, uint32_t standardFlow) {
    EventService::append(Esp32BaseAppEventLog::LEVEL_WARN,
                         "flow",
                         type,
                         "zone_standard_flow",
                         "zone",
                         0,
                         g_zoneId,
                         currentFlow,
                         standardFlow,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
}

void appendLeakNotice(uint32_t deltaPulses, uint32_t windowSec, uint32_t threshold) {
    EventService::append(Esp32BaseAppEventLog::LEVEL_WARN,
                         "flow",
                         "standby_leak",
                         "idle_pulse_window",
                         "flow",
                         0,
                         deltaPulses,
                         windowSec,
                         threshold,
                         Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2 | Esp32BaseAppEventLog::VALUE3);
}
} // namespace

void FlowSafetyService::handleIdle(uint32_t nowMs) {
    const FlowConfig& flow = ConfigStore::config().flow;
    if (flow.leakWindowSec == 0 || flow.leakPulseThreshold == 0) {
        g_idleWindowStartedMs = 0;
        g_leakReported = false;
        return;
    }

    const uint32_t pulses = BoardHardware::flowPulseCount();
    if (g_idleWindowStartedMs == 0) {
        g_idleWindowStartedMs = nowMs;
        g_idleWindowStartPulses = pulses;
        g_leakReported = false;
        return;
    }

    const uint32_t windowMs = flow.leakWindowSec * 1000UL;
    if (nowMs - g_idleWindowStartedMs >= windowMs) {
        g_idleWindowStartedMs = nowMs;
        g_idleWindowStartPulses = pulses;
        g_leakReported = false;
        return;
    }

    const uint32_t deltaPulses = pulses - g_idleWindowStartPulses;
    if (!g_leakReported && deltaPulses >= flow.leakPulseThreshold) {
        appendLeakNotice(deltaPulses, flow.leakWindowSec, flow.leakPulseThreshold);
        g_leakReported = true;
    }
}

void FlowSafetyService::beginStep(uint32_t nowMs, uint8_t zoneId) {
    BoardHardware::resetFlowCounter();
    g_zoneId = zoneId;
    g_stepStartedMs = nowMs;
    g_runningEnteredMs = 0;
    g_lastPulseMs = nowMs;
    g_lastPulseCount = 0;
    g_currentFlowMlPerMin = 0;
    g_flowSampleStartedMs = nowMs;
    g_flowSampleStartPulses = 0;
    g_idleWindowStartedMs = 0;
    g_idleWindowStartPulses = 0;
    g_lowDeviationStartedMs = 0;
    g_highDeviationStartedMs = 0;
    g_runningObserved = false;
    g_lowFlowReported = false;
    g_highFlowReported = false;
    g_leakReported = false;
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
        g_flowSampleStartedMs = nowMs;
        g_flowSampleStartPulses = pulses;
    }

    if (pulses != g_lastPulseCount) {
        g_lastPulseCount = pulses;
        g_lastPulseMs = nowMs;
    }

    const uint32_t confirmMs = ConfigStore::config().flow.noFlowConfirmSec * 1000UL;
    if (confirmMs > 0 && nowMs - g_lastPulseMs >= confirmMs) {
        reason = RunReason::NoFlow;
        return true;
    }

    updateFlowEstimate(nowMs, pulses);
    checkFlowDeviation(nowMs);
    return false;
}

uint32_t FlowSafetyService::currentStepPulses() {
    return BoardHardware::flowPulseCount();
}

uint32_t FlowSafetyService::currentFlowMlPerMin() {
    return g_currentFlowMlPerMin;
}

uint32_t FlowSafetyService::currentStepVolumeMl() {
    return volumeMlFromPulses(BoardHardware::flowPulseCount());
}

bool FlowSafetyService::checkLowLevel(uint32_t nowMs, RunReason& reason) {
    if (BoardHardware::lowLevelActive(nowMs)) {
        reason = RunReason::LowLevel;
        return true;
    }
    return false;
}

void FlowSafetyService::updateFlowEstimate(uint32_t nowMs, uint32_t pulses) {
    if (!g_runningObserved || g_runningEnteredMs == 0 || nowMs <= g_runningEnteredMs) {
        g_currentFlowMlPerMin = 0;
        return;
    }

    if (g_flowSampleStartedMs == 0) {
        g_flowSampleStartedMs = nowMs;
        g_flowSampleStartPulses = pulses;
        return;
    }

    const uint32_t elapsedMs = nowMs - g_flowSampleStartedMs;
    if (elapsedMs < kFlowSampleWindowMs) {
        return;
    }

    const uint32_t volumeMl = volumeMlFromPulses(pulses - g_flowSampleStartPulses);
    g_currentFlowMlPerMin = static_cast<uint32_t>((static_cast<uint64_t>(volumeMl) * 60000ULL) / elapsedMs);
    g_flowSampleStartedMs = nowMs;
    g_flowSampleStartPulses = pulses;
}

void FlowSafetyService::checkFlowDeviation(uint32_t nowMs) {
    const IrrigationConfig& config = ConfigStore::config();
    if (config.flow.pulsesPerLiter == 0 ||
        config.flow.lowHighFlowConfirmSec == 0 ||
        g_zoneId == 0) {
        return;
    }

    const uint8_t index = zoneIndexFromId(g_zoneId);
    if (index >= kMaxZones) {
        return;
    }

    const uint32_t standardFlow = config.zones[index].standardFlowMlPerMin;
    if (standardFlow == 0 || g_currentFlowMlPerMin == 0) {
        return;
    }

    const uint32_t confirmMs = config.flow.lowHighFlowConfirmSec * 1000UL;
    const uint32_t lowThreshold = (static_cast<uint64_t>(standardFlow) * config.flow.lowFlowPercent) / 100ULL;
    const uint32_t highThreshold = (static_cast<uint64_t>(standardFlow) * config.flow.highFlowPercent) / 100ULL;

    if (lowThreshold > 0 && g_currentFlowMlPerMin < lowThreshold) {
        if (g_lowDeviationStartedMs == 0) {
            g_lowDeviationStartedMs = nowMs;
        } else if (!g_lowFlowReported && nowMs - g_lowDeviationStartedMs >= confirmMs) {
            appendFlowNotice("low_flow", g_currentFlowMlPerMin, standardFlow);
            g_lowFlowReported = true;
        }
    } else {
        g_lowDeviationStartedMs = 0;
    }

    if (highThreshold > 0 && g_currentFlowMlPerMin > highThreshold) {
        if (g_highDeviationStartedMs == 0) {
            g_highDeviationStartedMs = nowMs;
        } else if (!g_highFlowReported && nowMs - g_highDeviationStartedMs >= confirmMs) {
            appendFlowNotice("high_flow", g_currentFlowMlPerMin, standardFlow);
            g_highFlowReported = true;
        }
    } else {
        g_highDeviationStartedMs = 0;
    }
}

} // namespace Irrigation
