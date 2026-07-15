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
    learnedFlowMlPerMinute_.fill(0);
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
        learnedFlowMlPerMinute_[index] =
            config.zones[BoardPins::zoneIndex(request.steps[index].zoneId)]
                .learnedFlowMlPerMinute;
    }
    sessionStartedMs_ = nowMs;
    lastHandledMs_ = nowMs;
    currentFlowMlPerMinute_ = 0;
    learningAverageMlPerMinute_ = 0;
    learningMinimumMlPerMinute_ = 0;
    learningMaximumMlPerMinute_ = 0;
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

bool WateringController::abortForMaintenance(uint32_t nowMs) {
    if (!active_) {
        return false;
    }
    finishSession(WateringStopReason::MaintenanceInterrupted, nowMs);
    return true;
}

void WateringController::handle(uint32_t nowMs) {
    lastHandledMs_ = nowMs;
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
                flowMonitor_.beginRateWindow(nowMs, hardware_.flowPulseCount());
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
            if (!checkFlowRate(nowMs)) {
                break;
            }
            const WateringStep& step = request_.steps[currentStepIndex_];
            if (elapsed(nowMs, stateStartedMs_, step.targetDurationSec * 1000U)) {
                if (request_.purpose == WateringPurpose::ZoneFlowLearning) {
                    finishSession(WateringStopReason::LearningTimeout, nowMs);
                } else {
                    finishCurrentZone(nowMs);
                }
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
    WateringStatus result{};
    result.active = active_;
    result.state = state_;
    result.source = request_.source;
    result.planId = request_.planId;
    result.stepCount = request_.stepCount;
    result.activeZoneId = static_cast<uint8_t>(active_ && currentStepIndex_ < request_.stepCount
                                                   ? request_.steps[currentStepIndex_].zoneId
                                                   : 0U);
    result.lastZoneId = static_cast<uint8_t>(request_.stepCount == 0
                                                ? 0U
                                                : request_.steps[currentStepIndex_].zoneId);
    result.currentStepIndex = currentStepIndex_;
    result.flowEstablished = active_ && flowMonitor_.flowEstablished();
    result.lastResult = lastResult_;
    result.lastStopReason = lastStopReason_;
    result.purpose = request_.purpose;
    result.elapsedSec = active_ ? static_cast<uint32_t>(lastHandledMs_ - sessionStartedMs_) / 1000U
                                : sessionSummary_.elapsedSec;
    result.currentFlowMlPerMinute = currentFlowMlPerMinute_;
    result.learningAverageMlPerMinute = learningAverageMlPerMinute_;
    result.learningMinimumMlPerMinute = learningMinimumMlPerMinute_;
    result.learningMaximumMlPerMinute = learningMaximumMlPerMinute_;
    result.learningSampleCount = learningRateSampleCount_;
    result.flowHistoryGeneration = flowHistoryGeneration_;
    result.flowSampleSerial = flowSampleSerial_;
    result.zones = sessionSummary_.zones;

    if (active_ && currentStepIndex_ < request_.stepCount) {
        result.expectedFlowMlPerMinute = learnedFlowMlPerMinute_[currentStepIndex_];
        result.pulseCount = currentZoneStarted_
                                ? static_cast<uint32_t>(hardware_.flowPulseCount() -
                                                        zoneStartedPulseCount_)
                                : 0U;
        if (currentZoneStarted_ && flowMonitor_.flowEstablished()) {
            const uint32_t endedMs = wateringEndCaptured_ ? wateringEndedMs_ : lastHandledMs_;
            result.currentZoneElapsedSec =
                static_cast<uint32_t>(endedMs - wateringStartedMs_) / 1000U;
        }
        const uint32_t targetSec = request_.steps[currentStepIndex_].targetDurationSec;
        result.currentZoneRemainingSec = result.currentZoneElapsedSec < targetSec
                                             ? targetSec - result.currentZoneElapsedSec
                                             : 0U;
        ZoneWateringSummary& current = result.zones[currentStepIndex_];
        current.actualWateringSec = result.currentZoneElapsedSec;
        current.pulseCount = result.pulseCount;
        current.waterEstimateCapped = !FlowMonitor::estimateWaterMilliliters(
            current.pulseCount, flowMeter_.pulsesPerLiterX100, current.estimatedWaterMl);
    }

    if (!(active_ && state_ == WateringState::StoppingZone && stopSessionAfterValveClose_ &&
          pendingStopReason_ != WateringStopReason::Completed)) {
        for (uint8_t index = currentStepIndex_; active_ && index < request_.stepCount; ++index) {
            result.plannedRemainingSec += index == currentStepIndex_
                                              ? result.currentZoneRemainingSec
                                              : request_.steps[index].targetDurationSec;
        }
    }
    for (uint8_t index = 0; index < result.stepCount; ++index) {
        result.totalEstimatedWaterMl += result.zones[index].estimatedWaterMl;
    }
    return result;
}

FlowHistorySnapshot WateringController::flowHistory() const {
    FlowHistorySnapshot snapshot{};
    snapshot.zoneId = flowHistoryZoneId_;
    snapshot.sampleCount = flowHistoryCount_;
    snapshot.generation = flowHistoryGeneration_;
    snapshot.latestSerial = flowSampleSerial_;
    for (uint16_t index = 0; index < flowHistoryCount_; ++index) {
        snapshot.samples[index] = flowHistorySamples_[
            (flowHistoryStart_ + index) % flowHistorySamples_.size()];
    }
    return snapshot;
}

bool WateringController::isValidRequest(const WateringRequest& request, const IrrigationConfig& config) {
    if (request.stepCount == 0 || request.stepCount > request.steps.size()) {
        return false;
    }
    if ((request.source != WateringSource::ManualZones &&
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
    lowFlowTiming_ = false;
    highFlowTiming_ = false;
    learningRateSampleCount_ = 0;
    learningRateSamples_.fill(0);
    flowHistoryStart_ = 0;
    flowHistoryCount_ = 0;
    flowHistoryZoneId_ = step.zoneId;
    ++flowHistoryGeneration_;
    currentFlowMlPerMinute_ = 0;
    learningAverageMlPerMinute_ = 0;
    learningMinimumMlPerMinute_ = 0;
    learningMaximumMlPerMinute_ = 0;
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

bool WateringController::checkFlowRate(uint32_t nowMs) {
    const bool learning = request_.purpose == WateringPurpose::ZoneFlowLearning;
    const bool deviationCheck = request_.purpose == WateringPurpose::Normal &&
                                learnedFlowMlPerMinute_[currentStepIndex_] != 0;
    FlowMonitor::RateSample sample{};
    if (!flowMonitor_.takeRateSample(nowMs,
                                     hardware_.flowPulseCount(),
                                     flowMeter_.pulsesPerLiterX100,
                                     sample)) {
        return true;
    }

    ZoneWateringSummary& zone = sessionSummary_.zones[currentStepIndex_];
    currentFlowMlPerMinute_ = sample.flowMlPerMinute;
    appendFlowSample(sample.flowMlPerMinute);
    if (learning) {
        if (learningRateSampleCount_ < learningRateSamples_.size()) {
            learningRateSamples_[learningRateSampleCount_++] = sample.flowMlPerMinute;
        } else {
            for (std::size_t index = 1; index < learningRateSamples_.size(); ++index) {
                learningRateSamples_[index - 1] = learningRateSamples_[index];
            }
            learningRateSamples_.back() = sample.flowMlPerMinute;
        }
        if (learningRateSampleCount_ == learningRateSamples_.size()) {
            uint32_t minimum = UINT32_MAX;
            uint32_t maximum = 0;
            uint64_t total = 0;
            for (const uint32_t rate : learningRateSamples_) {
                minimum = rate < minimum ? rate : minimum;
                maximum = rate > maximum ? rate : maximum;
                total += rate;
            }
            const uint32_t average = static_cast<uint32_t>(
                (total + learningRateSamples_.size() / 2U) / learningRateSamples_.size());
            learningAverageMlPerMinute_ = average;
            learningMinimumMlPerMinute_ = minimum;
            learningMaximumMlPerMinute_ = maximum;
            if (average != 0 &&
                static_cast<uint64_t>(maximum - minimum) * 100U <=
                    static_cast<uint64_t>(average) * 10U) {
                zone.suggestedFlowMlPerMinute = average;
                finishSession(WateringStopReason::Completed, nowMs);
                return false;
            }
        }
        if (learningRateSampleCount_ < learningRateSamples_.size()) {
            uint32_t minimum = UINT32_MAX;
            uint32_t maximum = 0;
            uint64_t total = 0;
            for (uint8_t index = 0; index < learningRateSampleCount_; ++index) {
                const uint32_t rate = learningRateSamples_[index];
                minimum = rate < minimum ? rate : minimum;
                maximum = rate > maximum ? rate : maximum;
                total += rate;
            }
            learningAverageMlPerMinute_ = static_cast<uint32_t>(
                (total + learningRateSampleCount_ / 2U) / learningRateSampleCount_);
            learningMinimumMlPerMinute_ = minimum;
            learningMaximumMlPerMinute_ = maximum;
        }
        return true;
    }

    if (!deviationCheck) {
        return true;
    }

    const uint64_t learned = learnedFlowMlPerMinute_[currentStepIndex_];
    const bool low = static_cast<uint64_t>(sample.flowMlPerMinute) * 100U <
                     learned * flowProtection_.lowFlowPercent;
    const bool high = static_cast<uint64_t>(sample.flowMlPerMinute) * 100U >
                      learned * flowProtection_.highFlowPercent;
    const uint32_t confirmMs = static_cast<uint32_t>(
        flowProtection_.flowDeviationConfirmSec) * 1000U;

    if (low && !zone.lowFlowDetected) {
        if (!lowFlowTiming_) {
            lowFlowTiming_ = true;
            lowFlowStartedMs_ = nowMs;
        } else if (elapsed(nowMs, lowFlowStartedMs_, confirmMs)) {
            zone.lowFlowDetected = true;
            if (flowProtection_.lowFlowAction == FlowAlertAction::StopWatering) {
                finishSession(WateringStopReason::LowFlow, nowMs);
                return false;
            }
        }
    } else if (!low) {
        lowFlowTiming_ = false;
    }

    if (high && !zone.highFlowDetected) {
        if (!highFlowTiming_) {
            highFlowTiming_ = true;
            highFlowStartedMs_ = nowMs;
        } else if (elapsed(nowMs, highFlowStartedMs_, confirmMs)) {
            zone.highFlowDetected = true;
            if (flowProtection_.highFlowAction == FlowAlertAction::StopWatering) {
                finishSession(WateringStopReason::HighFlow, nowMs);
                return false;
            }
        }
    } else if (!high) {
        highFlowTiming_ = false;
    }
    return true;
}

void WateringController::appendFlowSample(uint32_t flowMlPerMinute) {
    uint16_t index = 0;
    if (flowHistoryCount_ < flowHistorySamples_.size()) {
        index = static_cast<uint16_t>((flowHistoryStart_ + flowHistoryCount_) %
                                      flowHistorySamples_.size());
        ++flowHistoryCount_;
    } else {
        index = flowHistoryStart_;
        flowHistoryStart_ = static_cast<uint16_t>((flowHistoryStart_ + 1U) %
                                                  flowHistorySamples_.size());
    }
    flowHistorySamples_[index] = flowMlPerMinute;
    ++flowSampleSerial_;
}
