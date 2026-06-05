#include "domain/Zone.h"

#include <Arduino.h>
#include <Esp32Base.h>

#ifdef DISABLED
#undef DISABLED
#endif

#include "domain/BusinessEventLog.h"
#include "domain/ValveController.h"
#include "storage/FlowConfigStore.h"
#include "storage/RecordStore.h"

void Zone::begin(const Irrigation::ZoneConfig& config, const ZoneErrorStore::ZoneError& error, uint32_t nowMs) {
    m_config = config;
    m_error = error;
    m_runner.reset();
    m_leakWindowStartedMs = nowMs;
    m_leakWindowStartPulses = 0;
    refreshStateFromConfigAndError();
}

void Zone::applyConfig(const Irrigation::ZoneConfig& config, uint32_t epoch, uint32_t nowMs) {
    const bool wasEnabled = m_config.enabled;
    m_config = config;
    if (wasEnabled && !m_config.enabled && isBusy()) {
        stop(Irrigation::StopSource::CONFIG_CHANGE,
             Irrigation::StopScope::ZONE,
             Irrigation::TaskResult::USER_STOPPED,
             m_runner.runtime().lastPulseCount,
             epoch,
             nowMs);
    }
    refreshStateFromConfigAndError();
}

void Zone::refreshStateFromConfigAndError() {
    if (!m_config.enabled) {
        m_state = Irrigation::ZoneState::DISABLED;
    } else if (m_error.active) {
        m_state = Irrigation::ZoneState::ERROR;
    } else if (!m_runner.active() && (m_state == Irrigation::ZoneState::DISABLED || m_state == Irrigation::ZoneState::ERROR)) {
        m_state = Irrigation::ZoneState::IDLE;
    } else if (!m_runner.active() && (m_state == Irrigation::ZoneState::STARTING || m_state == Irrigation::ZoneState::RUNNING)) {
        m_state = Irrigation::ZoneState::IDLE;
    }
}

void Zone::tick(uint32_t pulseCount, uint32_t flowMlPerMin, bool flowRateReady, uint32_t epoch, uint32_t nowMs) {
    if (!m_runner.active()) {
        refreshStateFromConfigAndError();
        return;
    }
    if (m_state == Irrigation::ZoneState::STARTING || m_state == Irrigation::ZoneState::RUNNING) {
        m_runner.markPulse(pulseCount, nowMs);
    }

    const Irrigation::ActiveTask& task = m_runner.task();
    const Irrigation::TaskRuntime& runtime = m_runner.runtime();
    const uint32_t elapsedMs = m_runner.elapsedMs(nowMs);
    const uint32_t durationMs = task.targetSec * 1000UL;
    if (m_state == Irrigation::ZoneState::STARTING) {
        if (runtime.firstPulseSeen) {
            m_state = Irrigation::ZoneState::RUNNING;
            m_runner.markRunningStarted(nowMs);
            return;
        }
        const uint32_t startLimitMs = static_cast<uint32_t>(task.configSnapshot.baseline.noPulseTimeoutSec) * 1000UL;
        const uint32_t effectiveStartLimitMs = startLimitMs < durationMs ? startLimitMs : durationMs;
        if (elapsedMs >= effectiveStartLimitMs) {
            finish(Irrigation::TaskResult::FLOW_NO_PULSE_TIMEOUT,
                   Irrigation::StopSource::FLOW_MONITOR,
                   Irrigation::StopScope::ZONE,
                   pulseCount,
                   epoch,
                   nowMs);
        }
        return;
    }
    if (m_state == Irrigation::ZoneState::RUNNING) {
        if (elapsedMs >= durationMs) {
            finish(Irrigation::TaskResult::COMPLETED,
                   Irrigation::StopSource::DURATION_REACHED,
                   Irrigation::StopScope::ZONE,
                   pulseCount,
                   epoch,
                   nowMs);
            return;
        }
        m_runner.updateFlowStats(flowMlPerMin, flowRateReady, nowMs);
        const uint32_t noPulseMs = nowMs - runtime.lastPulseMs;
        if (noPulseMs >= static_cast<uint32_t>(task.configSnapshot.baseline.noPulseTimeoutSec) * 1000UL) {
            finish(Irrigation::TaskResult::FLOW_NO_PULSE_TIMEOUT,
                   Irrigation::StopSource::FLOW_MONITOR,
                   Irrigation::StopScope::ZONE,
                   pulseCount,
                   epoch,
                   nowMs);
            return;
        }
    }
    if (m_state != Irrigation::ZoneState::RUNNING && elapsedMs >= durationMs) {
        finish(Irrigation::TaskResult::COMPLETED,
               Irrigation::StopSource::DURATION_REACHED,
               Irrigation::StopScope::ZONE,
               pulseCount,
               epoch,
               nowMs);
        return;
    }
}

bool Zone::start(Irrigation::TaskType type,
                 Irrigation::StartSource source,
                 uint32_t planId,
                 uint8_t planSlot,
                 const char* planName,
                 uint32_t targetSec,
                 uint32_t maxWateringDurationSec,
                 uint16_t flowSampleWindowSec,
                 uint32_t pulseCount,
                 uint32_t epoch,
                 uint32_t nowMs) {
    refreshStateFromConfigAndError();
    if (m_state != Irrigation::ZoneState::IDLE || !m_config.enabled || targetSec < 1 || targetSec > maxWateringDurationSec) {
        return false;
    }
    const Irrigation::FlowMeterConfig& flowConfig = FlowConfigStore::get(m_config.flowId);
    if (!flowConfig.enabled) {
        return false;
    }
    if (!m_runner.start(type, source, planId, planSlot, planName, targetSec, pulseCount, epoch, nowMs, flowSampleWindowSec, m_config, flowConfig)) {
        return false;
    }
    if (!ValveController::setRoad(m_config.zoneId, true, "zone start")) {
        m_runner.reset();
        return false;
    }
    m_state = Irrigation::ZoneState::STARTING;
    return true;
}

bool Zone::stop(Irrigation::StopSource source, Irrigation::StopScope scope, Irrigation::TaskResult result, uint32_t pulseCount, uint32_t epoch, uint32_t nowMs) {
    if (!m_runner.active()) {
        (void)ValveController::off(m_config.zoneId, "zone stop idle");
        refreshStateFromConfigAndError();
        return false;
    }
    finish(result, source, scope, pulseCount, epoch, nowMs);
    return true;
}

bool Zone::markLeak(uint32_t pulseCount, uint32_t epoch, uint32_t nowMs) {
    if (m_runner.active()) {
        finish(Irrigation::TaskResult::IDLE_FLOW_PROTECTED,
               Irrigation::StopSource::LEAK_MONITOR,
               Irrigation::StopScope::ALL,
               pulseCount,
               epoch,
               nowMs);
    }
    persistError(Irrigation::ZoneErrorCode::IDLE_FLOW_DETECTED,
                 Irrigation::StopSource::LEAK_MONITOR,
                 Irrigation::TaskResult::IDLE_FLOW_PROTECTED);
    m_error = ZoneErrorStore::get(m_config.zoneId);
    refreshStateFromConfigAndError();
    return true;
}

void Zone::finish(Irrigation::TaskResult result, Irrigation::StopSource source, Irrigation::StopScope scope, uint32_t pulseCount, uint32_t epoch, uint32_t nowMs) {
    const bool wasActive = m_runner.active();
    m_runner.finish(result, source, scope, pulseCount, epoch, nowMs);
    (void)ValveController::off(m_config.zoneId, "zone finish");
    if (!wasActive) {
        refreshStateFromConfigAndError();
        return;
    }
    const Irrigation::ActiveTask& task = m_runner.task();
    const Irrigation::FinishedTask& finished = m_runner.finished();
    const uint32_t pulses = finished.endedPulseCount >= task.startedPulseCount ? finished.endedPulseCount - task.startedPulseCount : 0;
    RecordStore::WateringRecord record = {};
    record.zoneId = m_config.zoneId;
    record.taskType = static_cast<uint8_t>(task.type);
    record.startSource = static_cast<uint8_t>(task.startSource);
    record.stopSource = static_cast<uint8_t>(source);
    record.stopScope = static_cast<uint8_t>(scope);
    record.result = static_cast<uint8_t>(result);
    record.planId = task.planId;
    strlcpy(record.planNameSnapshot, task.planNameSnapshot, sizeof(record.planNameSnapshot));
    record.targetSec = task.targetSec;
    record.startedEpoch = task.startedEpoch;
    record.endedEpoch = epoch;
    record.startedUptimeMs = task.startedUptimeMs;
    record.endedUptimeMs = nowMs;
    record.startedPulseCount = task.startedPulseCount;
    record.endedPulseCount = finished.endedPulseCount;
    const uint32_t durationMs = finished.endedUptimeMs >= task.startedUptimeMs ? finished.endedUptimeMs - task.startedUptimeMs : 0;
    record.estimatedMilliliters = FlowConfigStore::estimateMilliliters(task.configSnapshot.calibration, pulses, durationMs);
    record.flowSampleWindowSec = task.flowSampleWindowSec;
    record.flowStatsValid = m_runner.runtime().flowStatsValid;
    record.maxFlowMlPerMin = m_runner.runtime().maxFlowMlPerMin;
    record.maxFlowFirstAtSec = m_runner.runtime().maxFlowFirstAtSec;
    record.minFlowMlPerMin = m_runner.runtime().minFlowMlPerMin;
    record.minFlowFirstAtSec = m_runner.runtime().minFlowFirstAtSec;
    record.configSnapshot = task.configSnapshot;
    if (!RecordStore::append(record)) {
        BusinessEventLog::appendRecordAppendFailed(m_config.zoneId, task.planId, result);
    }

    const bool error = result == Irrigation::TaskResult::FLOW_LOW_STOPPED ||
                       result == Irrigation::TaskResult::FLOW_NO_PULSE_TIMEOUT ||
                       result == Irrigation::TaskResult::FLOW_HIGH_STOPPED;
    if (error) {
        BusinessEventLog::appendFlowFault(m_config.zoneId, result, task.targetSec, pulses, true);
    } else if (result == Irrigation::TaskResult::IDLE_FLOW_PROTECTED || result == Irrigation::TaskResult::FACTORY_RESET_PROTECTED) {
        BusinessEventLog::appendSafetyStop(m_config.zoneId, result, BusinessEventLog::sourceFromStop(source));
    }
    if (result == Irrigation::TaskResult::FLOW_NO_PULSE_TIMEOUT) {
        persistError(Irrigation::ZoneErrorCode::FLOW_NO_PULSE_TIMEOUT, source, result);
        m_error = ZoneErrorStore::get(m_config.zoneId);
    } else if (result == Irrigation::TaskResult::FLOW_LOW_STOPPED) {
        persistError(Irrigation::ZoneErrorCode::FLOW_LOW, source, result);
        m_error = ZoneErrorStore::get(m_config.zoneId);
    } else if (result == Irrigation::TaskResult::FLOW_HIGH_STOPPED) {
        persistError(Irrigation::ZoneErrorCode::FLOW_HIGH, source, result);
        m_error = ZoneErrorStore::get(m_config.zoneId);
    }
    refreshStateFromConfigAndError();
}

void Zone::persistError(Irrigation::ZoneErrorCode code, Irrigation::StopSource source, Irrigation::TaskResult result) {
    (void)ZoneErrorStore::setError(m_config.zoneId, code, source, result);
    BusinessEventLog::appendZoneLocked(m_config.zoneId, code, result);
}

bool Zone::clearError(uint32_t nowMs) {
    if (!ZoneErrorStore::clearError(m_config.zoneId)) {
        return false;
    }
    m_error = ZoneErrorStore::get(m_config.zoneId);
    m_leakWindowStartedMs = nowMs;
    refreshStateFromConfigAndError();
    return true;
}

bool Zone::checkIdleLeak(uint32_t pulseCount, uint32_t nowMs, uint16_t windowSec, uint16_t threshold, uint32_t* observedPulses) {
    if (observedPulses) {
        *observedPulses = 0;
    }
    if (m_state != Irrigation::ZoneState::IDLE) {
        resetLeakWindow(pulseCount, nowMs);
        return false;
    }
    const uint32_t windowMs = static_cast<uint32_t>(windowSec) * 1000UL;
    if (nowMs - m_leakWindowStartedMs < windowMs) {
        return false;
    }
    const uint32_t delta = pulseCount >= m_leakWindowStartPulses ? pulseCount - m_leakWindowStartPulses : 0;
    resetLeakWindow(pulseCount, nowMs);
    if (observedPulses) {
        *observedPulses = delta;
    }
    return delta >= threshold;
}

void Zone::resetLeakWindow(uint32_t pulseCount, uint32_t nowMs) {
    m_leakWindowStartPulses = pulseCount;
    m_leakWindowStartedMs = nowMs;
}

Irrigation::ZoneStatus Zone::status(uint32_t pulseCount, uint32_t flowRatePerMinuteX1000, uint32_t flowMlPerMin, bool flowRateReady, uint32_t nowMs) const {
    Irrigation::ZoneStatus status = {};
    status.zoneId = m_config.zoneId;
    status.state = m_state;
    status.enabled = m_config.enabled;
    status.busy = isBusy();
    status.errorActive = m_error.active;
    status.errorCode = m_error.errorCode;
    status.leakAlert = m_error.active && m_error.errorCode == Irrigation::ZoneErrorCode::IDLE_FLOW_DETECTED;
    status.flowRatePerMinuteX1000 = flowRatePerMinuteX1000;
    status.flowMlPerMin = flowMlPerMin;
    status.flowRateReady = flowRateReady;
    status.taskType = m_runner.task().type;
    status.planId = m_runner.task().planId;
    if (m_runner.active()) {
        const Irrigation::ActiveTask& task = m_runner.task();
        status.targetSec = task.targetSec;
        status.elapsedSec = m_runner.elapsedMs(nowMs) / 1000UL;
        status.remainingSec = status.elapsedSec < task.targetSec ? task.targetSec - status.elapsedSec : 0;
        status.pulses = pulseCount >= task.startedPulseCount ? pulseCount - task.startedPulseCount : 0;
        const uint32_t durationMs = m_runner.elapsedMs(nowMs);
        status.estimatedMilliliters = FlowConfigStore::estimateMilliliters(task.configSnapshot.calibration, status.pulses, durationMs);
    }
    return status;
}

const Irrigation::ZoneConfig& Zone::config() const {
    return m_config;
}

Irrigation::ZoneState Zone::state() const {
    return m_state;
}

bool Zone::isBusy() const {
    return m_state == Irrigation::ZoneState::STARTING || m_state == Irrigation::ZoneState::RUNNING;
}

bool Zone::isError() const {
    return m_state == Irrigation::ZoneState::ERROR;
}

bool Zone::isIdle() const {
    return m_state == Irrigation::ZoneState::IDLE;
}
