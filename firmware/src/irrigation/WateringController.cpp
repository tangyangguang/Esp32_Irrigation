#include "WateringController.h"

namespace {

bool elapsed(uint32_t nowMs, uint32_t startedMs, uint32_t durationMs) {
    return static_cast<uint32_t>(nowMs - startedMs) >= durationMs;
}

}  // namespace

WateringController::WateringController(WateringHardware& hardware) : hardware_(hardware) {}

WateringStartResult WateringController::start(const WateringRequest& request,
                                              const IrrigationConfig& config,
                                              uint32_t nowMs) {
    if (active_) {
        return WateringStartResult::Busy;
    }
    if (finishedSessionReady_) {
        return WateringStartResult::PreviousResultPending;
    }
    if (!isValidRequest(request, config)) {
        return WateringStartResult::InvalidRequest;
    }

    request_ = request;
    valveDrive_ = config.valveDrive;
    pump_ = config.pump;
    flowMeter_ = config.flowMeter;
    flowProtection_ = config.flowProtection;
    currentStepIndex_ = 0;
    lastResult_ = WateringResult::None;
    lastStopReason_ = WateringStopReason::None;
    pendingStopReason_ = WateringStopReason::None;
    pendingZoneResult_ = ZoneWateringResult::NotStarted;
    stopSessionAfterValveClose_ = false;
    sessionSummary_ = {};
    sessionSummary_.source = request.source;
    sessionSummary_.purpose = request.purpose;
    sessionSummary_.planId = request.planId;
    sessionSummary_.zoneCount = request.stepCount;
    for (uint8_t index = 0; index < request.stepCount; ++index) {
        sessionSummary_.zones[index].zoneId = request.steps[index].zoneId;
        sessionSummary_.zones[index].result = ZoneWateringResult::NotStarted;
        sessionSummary_.zones[index].plannedDurationSec = request.steps[index].targetDurationSec;
    }
    sessionStartedMs_ = nowMs;
    active_ = true;
    if (!beginCurrentZone(nowMs)) {
        finishSession(WateringStopReason::HardwareFailure, nowMs);
        return WateringStartResult::HardwareFailure;
    }
    return WateringStartResult::Started;
}

bool WateringController::stop(uint32_t nowMs) {
    if (!active_) {
        return false;
    }
    if (state_ == WateringState::StoppingZone) {
        stopSessionAfterValveClose_ = true;
        pendingStopReason_ = WateringStopReason::UserStopped;
        return true;
    }
    if (flowMonitor_.flowEstablished()) {
        wateringEndedMs_ = nowMs;
        wateringEndCaptured_ = true;
    }
    pendingZoneResult_ = ZoneWateringResult::Stopped;
    if (pump_.enabled) {
        hardware_.setPumpSignal(false);
        enterStoppingZone(nowMs, true, WateringStopReason::UserStopped);
    } else {
        finishSession(WateringStopReason::UserStopped, nowMs);
    }
    return true;
}

void WateringController::handle(uint32_t nowMs) {
    if (!active_) {
        return;
    }
    if (!applyValveHoldIfDue(nowMs)) {
        finishSession(WateringStopReason::HardwareFailure, nowMs);
        return;
    }

    switch (state_) {
        case WateringState::StartingZone:
            if (elapsed(nowMs, stateStartedMs_, pump_.startDelayMs)) {
                flowMonitor_.begin(nowMs, hardware_.flowPulseCount());
                state_ = WateringState::WaitingForFlow;
                stateStartedMs_ = nowMs;
            }
            break;

        case WateringState::WaitingForFlow:
            flowMonitor_.observe(nowMs, hardware_.flowPulseCount());
            if (flowMonitor_.flowEstablished()) {
                state_ = WateringState::WateringZone;
                stateStartedMs_ = nowMs;
                wateringStartedMs_ = nowMs;
            } else if (flowMonitor_.flowStartTimedOut(nowMs, flowProtection_.flowStartTimeoutSec)) {
                finishSession(WateringStopReason::FlowStartTimeout, nowMs);
            }
            break;

        case WateringState::WateringZone: {
            flowMonitor_.observe(nowMs, hardware_.flowPulseCount());
            if (flowMonitor_.noFlowTimedOut(nowMs, flowProtection_.noFlowTimeoutSec)) {
                finishSession(WateringStopReason::NoFlowTimeout, nowMs);
                break;
            }
            const WateringStep& step = request_.steps[currentStepIndex_];
            if (elapsed(nowMs, stateStartedMs_, step.targetDurationSec * 1000U)) {
                finishCurrentZone(nowMs);
            }
            break;
        }

        case WateringState::StoppingZone:
            if (elapsed(nowMs, stateStartedMs_, pump_.stopToValveCloseDelayMs)) {
                hardware_.closeValves();
                finalizeCurrentZone(pendingZoneResult_, nowMs);
                if (stopSessionAfterValveClose_) {
                    finishSession(pendingStopReason_, nowMs);
                } else {
                    ++currentStepIndex_;
                    if (!beginCurrentZone(nowMs)) {
                        finishSession(WateringStopReason::HardwareFailure, nowMs);
                    }
                }
            }
            break;

        case WateringState::Idle:
            break;
    }
}

const WateringSessionSummary* WateringController::finishedSession() const {
    return finishedSessionReady_ ? &sessionSummary_ : nullptr;
}

void WateringController::clearFinishedSession() {
    finishedSessionReady_ = false;
}

WateringStatus WateringController::status() const {
    return {
        active_,
        state_,
        static_cast<uint8_t>(active_ ? request_.steps[currentStepIndex_].zoneId : 0U),
        currentStepIndex_,
        active_ && flowMonitor_.flowEstablished(),
        lastResult_,
        lastStopReason_,
    };
}

bool WateringController::isValidRequest(const WateringRequest& request, const IrrigationConfig& config) {
    if (request.stepCount == 0 || request.stepCount > request.steps.size()) {
        return false;
    }
    if ((request.source != WateringSource::ManualZones &&
         request.source != WateringSource::ManualPlan &&
         request.source != WateringSource::AutomaticPlan) ||
        (request.purpose != WateringPurpose::Normal &&
         request.purpose != WateringPurpose::FlowCalibration &&
         request.purpose != WateringPurpose::ZoneFlowLearning)) {
        return false;
    }
    if ((request.source == WateringSource::ManualZones && request.planId != 0) ||
        (request.source != WateringSource::ManualZones &&
         (request.planId == 0 || request.planId > kWateringPlanCount))) {
        return false;
    }

    uint8_t previousZoneId = 0;
    for (uint8_t index = 0; index < request.stepCount; ++index) {
        const WateringStep& step = request.steps[index];
        if (!BoardPins::isValidZoneId(step.zoneId) ||
            step.zoneId <= previousZoneId ||
            !config.zones[BoardPins::zoneIndex(step.zoneId)].enabled ||
            step.targetDurationSec == 0 ||
            step.targetDurationSec > 120U * 60U) {
            return false;
        }
        previousZoneId = step.zoneId;
    }
    return true;
}

bool WateringController::beginCurrentZone(uint32_t nowMs) {
    const WateringStep& step = request_.steps[currentStepIndex_];
    zoneStartedPulseCount_ = hardware_.flowPulseCount();
    currentZoneStarted_ = false;
    currentZoneFinalized_ = false;
    wateringEndCaptured_ = false;
    if (!hardware_.openValve(step.zoneId, 100)) {
        return false;
    }
    currentZoneStarted_ = true;
    valveOpenedMs_ = nowMs;
    valveHolding_ = false;
    flowMonitor_.begin(nowMs, hardware_.flowPulseCount());
    if (pump_.enabled && !hardware_.setPumpSignal(true)) {
        return false;
    }
    state_ = pump_.startDelayMs == 0 ? WateringState::WaitingForFlow : WateringState::StartingZone;
    stateStartedMs_ = nowMs;
    return true;
}

bool WateringController::applyValveHoldIfDue(uint32_t nowMs) {
    if (valveHolding_ || state_ == WateringState::Idle) {
        return true;
    }
    if (!elapsed(nowMs, valveOpenedMs_, valveDrive_.pullInTimeMs)) {
        return true;
    }
    if (!hardware_.setActiveValveDuty(valveDrive_.holdDutyPercent)) {
        return false;
    }
    valveHolding_ = true;
    return true;
}

void WateringController::finishCurrentZone(uint32_t nowMs) {
    const bool lastStep = currentStepIndex_ + 1U >= request_.stepCount;
    wateringEndedMs_ = nowMs;
    wateringEndCaptured_ = flowMonitor_.flowEstablished();
    pendingZoneResult_ = ZoneWateringResult::Completed;
    if (pump_.enabled) {
        hardware_.setPumpSignal(false);
        enterStoppingZone(nowMs, lastStep, WateringStopReason::Completed);
        return;
    }

    hardware_.closeValves();
    finalizeCurrentZone(ZoneWateringResult::Completed, nowMs);
    if (lastStep) {
        finishSession(WateringStopReason::Completed, nowMs);
        return;
    }
    ++currentStepIndex_;
    if (!beginCurrentZone(nowMs)) {
        finishSession(WateringStopReason::HardwareFailure, nowMs);
    }
}

void WateringController::enterStoppingZone(uint32_t nowMs,
                                           bool stopSession,
                                           WateringStopReason reason) {
    state_ = WateringState::StoppingZone;
    stateStartedMs_ = nowMs;
    stopSessionAfterValveClose_ = stopSession;
    pendingStopReason_ = reason;
}

void WateringController::finalizeCurrentZone(ZoneWateringResult result, uint32_t nowMs) {
    if (!currentZoneStarted_ || currentZoneFinalized_) {
        return;
    }
    ZoneWateringSummary& zone = sessionSummary_.zones[currentStepIndex_];
    zone.result = result;
    zone.pulseCount = static_cast<uint32_t>(hardware_.flowPulseCount() - zoneStartedPulseCount_);
    if (flowMonitor_.flowEstablished()) {
        if (!wateringEndCaptured_) {
            wateringEndedMs_ = nowMs;
            wateringEndCaptured_ = true;
        }
        zone.actualWateringSec = static_cast<uint32_t>(wateringEndedMs_ - wateringStartedMs_) / 1000U;
        sessionSummary_.anyFlowEstablished = true;
    }
    zone.waterEstimateCapped = !FlowMonitor::estimateWaterMilliliters(
        zone.pulseCount, flowMeter_.pulsesPerLiterX100, zone.estimatedWaterMl);
    currentZoneFinalized_ = true;
    currentZoneStarted_ = false;
}

void WateringController::finishSession(WateringStopReason reason, uint32_t nowMs) {
    hardware_.safeShutdown();
    ZoneWateringResult zoneResult = ZoneWateringResult::Failed;
    if (reason == WateringStopReason::Completed) {
        zoneResult = ZoneWateringResult::Completed;
    } else if (reason == WateringStopReason::UserStopped) {
        zoneResult = ZoneWateringResult::Stopped;
    }
    finalizeCurrentZone(zoneResult, nowMs);
    active_ = false;
    state_ = WateringState::Idle;
    lastStopReason_ = reason;
    if (reason == WateringStopReason::Completed) {
        lastResult_ = WateringResult::Completed;
    } else if (reason == WateringStopReason::UserStopped) {
        lastResult_ = WateringResult::Stopped;
    } else {
        lastResult_ = WateringResult::Failed;
    }
    sessionSummary_.result = lastResult_;
    sessionSummary_.stopReason = reason;
    sessionSummary_.elapsedSec = static_cast<uint32_t>(nowMs - sessionStartedMs_) / 1000U;
    finishedSessionReady_ = true;
}
