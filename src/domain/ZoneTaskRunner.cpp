#include "domain/ZoneTaskRunner.h"

#include <string.h>

void ZoneTaskRunner::reset() {
    m_task = {};
    m_runtime = {};
    m_finished = {};
}

bool ZoneTaskRunner::start(Irrigation::TaskType type,
                           Irrigation::StartSource source,
                           uint32_t planId,
                           uint8_t planSlot,
                           const char* planName,
                           uint32_t targetSec,
                           uint32_t pulseCount,
                           uint32_t epoch,
                           uint32_t nowMs,
                           uint16_t flowRateWindowSec,
                           const Irrigation::ZoneConfig& config) {
    if (m_task.active || targetSec == 0) {
        return false;
    }
    reset();
    m_task.active = true;
    m_task.type = type;
    m_task.startSource = source;
    m_task.planId = planId;
    m_task.planSlot = planSlot;
    if (planName) {
        strlcpy(m_task.planNameSnapshot, planName, sizeof(m_task.planNameSnapshot));
    }
    m_task.targetSec = targetSec;
    m_task.startedEpoch = epoch;
    m_task.startedUptimeMs = nowMs;
    m_task.startedPulseCount = pulseCount;
    m_task.flowRateWindowSec = flowRateWindowSec;
    m_task.configSnapshot.flow.startupPulseLimit = config.flow.startupPulseLimit;
    m_task.configSnapshot.flow.startupEstimatedMl = config.flow.startupEstimatedMl;
    m_task.configSnapshot.flow.stablePulsePerLiter = config.flow.stablePulsePerLiter;
    m_task.configSnapshot.startTimeoutSec = config.startTimeoutSec;
    m_task.configSnapshot.flowNoPulseTimeoutSec = config.flowNoPulseTimeoutSec;
    m_task.configSnapshot.suppressError = config.suppressError;
    m_runtime.lastPulseCount = pulseCount;
    m_runtime.lastPulseMs = nowMs;
    m_runtime.firstPulseSeen = false;
    return true;
}

bool ZoneTaskRunner::active() const {
    return m_task.active;
}

const Irrigation::ActiveTask& ZoneTaskRunner::task() const {
    return m_task;
}

const Irrigation::TaskRuntime& ZoneTaskRunner::runtime() const {
    return m_runtime;
}

const Irrigation::FinishedTask& ZoneTaskRunner::finished() const {
    return m_finished;
}

void ZoneTaskRunner::markPulse(uint32_t pulseCount, uint32_t nowMs) {
    if (pulseCount != m_runtime.lastPulseCount) {
        m_runtime.lastPulseCount = pulseCount;
        m_runtime.lastPulseMs = nowMs;
        m_runtime.firstPulseSeen = true;
    }
}

void ZoneTaskRunner::markRunningStarted(uint32_t nowMs) {
    if (m_runtime.runningStartedMs == 0) {
        m_runtime.runningStartedMs = nowMs;
    }
}

void ZoneTaskRunner::updateFlowStats(uint32_t flowMlPerMin, bool flowRateReady, uint32_t nowMs) {
    if (!m_task.active || m_runtime.runningStartedMs == 0 || !flowRateReady) {
        return;
    }
    const uint32_t windowMs = static_cast<uint32_t>(m_task.flowRateWindowSec) * 1000UL;
    if (nowMs - m_runtime.runningStartedMs < windowMs) {
        return;
    }
    const uint32_t firstAtSec = nowMs >= m_task.startedUptimeMs ? (nowMs - m_task.startedUptimeMs) / 1000UL : 0;
    if (!m_runtime.flowStatsValid) {
        m_runtime.flowStatsValid = true;
        m_runtime.maxFlowMlPerMin = flowMlPerMin;
        m_runtime.maxFlowFirstAtSec = firstAtSec;
        m_runtime.minFlowMlPerMin = flowMlPerMin;
        m_runtime.minFlowFirstAtSec = firstAtSec;
        return;
    }
    if (flowMlPerMin > m_runtime.maxFlowMlPerMin) {
        m_runtime.maxFlowMlPerMin = flowMlPerMin;
        m_runtime.maxFlowFirstAtSec = firstAtSec;
    }
    if (flowMlPerMin < m_runtime.minFlowMlPerMin) {
        m_runtime.minFlowMlPerMin = flowMlPerMin;
        m_runtime.minFlowFirstAtSec = firstAtSec;
    }
}

void ZoneTaskRunner::finish(Irrigation::TaskResult result,
                            Irrigation::StopSource source,
                            Irrigation::StopScope scope,
                            uint32_t pulseCount,
                            uint32_t epoch,
                            uint32_t nowMs) {
    if (!m_task.active) {
        return;
    }
    m_finished = {};
    m_finished.valid = true;
    m_finished.result = result;
    m_finished.stopSource = source;
    m_finished.stopScope = scope;
    m_finished.endedEpoch = epoch;
    m_finished.endedUptimeMs = nowMs;
    m_finished.endedPulseCount = pulseCount;
    m_runtime.lastPulseCount = pulseCount;
    m_task.active = false;
}

uint32_t ZoneTaskRunner::elapsedMs(uint32_t nowMs) const {
    return m_task.active && nowMs >= m_task.startedUptimeMs ? nowMs - m_task.startedUptimeMs : 0;
}

uint32_t ZoneTaskRunner::pulseDelta(uint32_t pulseCount) const {
    return pulseCount >= m_task.startedPulseCount ? pulseCount - m_task.startedPulseCount : 0;
}
