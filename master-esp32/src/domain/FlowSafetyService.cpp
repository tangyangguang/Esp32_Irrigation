#include "FlowSafetyService.h"

#include "BoardHardware.h"
#include "ConfigStore.h"
#include "EventService.h"
#include "IrrigationConfig.h"

namespace Irrigation {

namespace {
constexpr uint32_t kFlowSampleIntervalMs = 1000;
constexpr uint32_t kFlowSampleWindowMs = 5000;
constexpr uint8_t kFlowSampleCapacity = 6;

uint8_t g_zoneId = 0;
uint32_t g_stepStartedMs = 0;
uint32_t g_runningEnteredMs = 0;
uint32_t g_lastPulseMs = 0;
uint32_t g_lastPulseCount = 0;
uint32_t g_currentFlowMlPerMin = 0;
uint32_t g_flowSampleMs[kFlowSampleCapacity] = {};
uint32_t g_flowSamplePulses[kFlowSampleCapacity] = {};
uint8_t g_flowSampleCount = 0;
uint8_t g_flowSampleNext = 0;
uint32_t g_lastFlowSampleMs = 0;
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

void resetFlowSamples(uint32_t nowMs, uint32_t pulses) {
    for (uint8_t i = 0; i < kFlowSampleCapacity; ++i) {
        g_flowSampleMs[i] = 0;
        g_flowSamplePulses[i] = 0;
    }
    g_flowSampleMs[0] = nowMs;
    g_flowSamplePulses[0] = pulses;
    g_flowSampleCount = 1;
    g_flowSampleNext = 1;
    g_lastFlowSampleMs = nowMs;
}

void addFlowSample(uint32_t nowMs, uint32_t pulses) {
    g_flowSampleMs[g_flowSampleNext] = nowMs;
    g_flowSamplePulses[g_flowSampleNext] = pulses;
    g_flowSampleNext = static_cast<uint8_t>((g_flowSampleNext + 1U) % kFlowSampleCapacity);
    if (g_flowSampleCount < kFlowSampleCapacity) {
        ++g_flowSampleCount;
    }
    g_lastFlowSampleMs = nowMs;
}

bool newestFlowSample(uint32_t& sampleMs, uint32_t& pulses) {
    if (g_flowSampleCount == 0) {
        return false;
    }
    const uint8_t index = static_cast<uint8_t>((g_flowSampleNext + kFlowSampleCapacity - 1U) % kFlowSampleCapacity);
    sampleMs = g_flowSampleMs[index];
    pulses = g_flowSamplePulses[index];
    return true;
}

bool oldestWindowFlowSample(uint32_t newestMs, uint32_t& sampleMs, uint32_t& pulses) {
    if (g_flowSampleCount < 2) {
        return false;
    }
    bool found = false;
    uint32_t oldestMs = newestMs;
    uint32_t oldestPulses = 0;
    for (uint8_t i = 0; i < g_flowSampleCount; ++i) {
        if (g_flowSampleMs[i] == 0 || g_flowSampleMs[i] > newestMs) {
            continue;
        }
        if (newestMs - g_flowSampleMs[i] <= kFlowSampleWindowMs &&
            (!found || g_flowSampleMs[i] < oldestMs)) {
            found = true;
            oldestMs = g_flowSampleMs[i];
            oldestPulses = g_flowSamplePulses[i];
        }
    }
    if (!found || oldestMs == newestMs) {
        return false;
    }
    sampleMs = oldestMs;
    pulses = oldestPulses;
    return true;
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
    resetFlowSamples(nowMs, 0);
    g_idleWindowStartedMs = 0;
    g_idleWindowStartPulses = 0;
    g_lowDeviationStartedMs = 0;
    g_highDeviationStartedMs = 0;
    g_runningObserved = false;
    g_lowFlowReported = false;
    g_highFlowReported = false;
    g_leakReported = false;
}

bool FlowSafetyService::checkFlowGrace(uint32_t nowMs, RunReason&amp; reason) {
    return false;
}

bool FlowSafetyService::checkRunning(uint32_t nowMs, RunReason&amp; reason) {

    const uint32_t pulses = BoardHardware::flowPulseCount();
    if (!g_runningObserved) {
        g_runningObserved = true;
        g_runningEnteredMs = nowMs;
        g_lastPulseMs = nowMs;
        g_lastPulseCount = pulses;
        resetFlowSamples(nowMs, pulses);
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

void FlowSafetyService::updateFlowEstimate(uint32_t nowMs, uint32_t pulses) {
    if (!g_runningObserved || g_runningEnteredMs == 0 || nowMs <= g_runningEnteredMs) {
        g_currentFlowMlPerMin = 0;
        return;
    }

    if (g_flowSampleCount == 0) {
        resetFlowSamples(nowMs, pulses);
        return;
    }

    if (nowMs - g_lastFlowSampleMs < kFlowSampleIntervalMs) {
        return;
    }

    addFlowSample(nowMs, pulses);

    uint32_t newestMs = 0;
    uint32_t newestPulses = 0;
    uint32_t oldestMs = 0;
    uint32_t oldestPulses = 0;
    if (!newestFlowSample(newestMs, newestPulses) ||
        !oldestWindowFlowSample(newestMs, oldestMs, oldestPulses)) {
        return;
    }

    const uint32_t elapsedMs = newestMs - oldestMs;
    if (elapsedMs == 0 || newestPulses < oldestPulses) {
        g_currentFlowMlPerMin = 0;
        return;
    }

    const uint32_t volumeMl = volumeMlFromPulses(newestPulses - oldestPulses);
    g_currentFlowMlPerMin = static_cast<uint32_t>((static_cast<uint64_t>(volumeMl) * 60000ULL) / elapsedMs);
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
