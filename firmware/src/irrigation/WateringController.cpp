#include "WateringController.h"

namespace {

bool elapsed(uint32_t nowMs, uint32_t startedMs, uint32_t durationMs) {
    return static_cast<uint32_t>(nowMs - startedMs) >= durationMs;
}

uint32_t learningAllowedPulseRateSpreadX100(uint32_t averageRateX100) {
    const uint32_t percentTolerance = static_cast<uint32_t>(
        (static_cast<uint64_t>(averageRateX100) * 10U + 50U) / 100U);
    constexpr uint32_t kOnePulseWindowToleranceX100 = 20U;
    return percentTolerance > kOnePulseWindowToleranceX100
               ? percentTolerance
               : kOnePulseWindowToleranceX100;
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
    calibrationStability_ = config.calibrationStability;
    flowProtection_ = config.flowProtection;
    baselinePulseRateX10000_.fill(0);
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
        sessionSummary_.zones[index].targetWaterMl = request.steps[index].targetWaterMl;
        baselinePulseRateX10000_[index] =
            config.zones[BoardPins::zoneIndex(request.steps[index].zoneId)]
                .baselinePulseRateX10000;
        sessionSummary_.zones[index].flowBaselineAvailable =
            baselinePulseRateX10000_[index] != 0;
        FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
            baselinePulseRateX10000_[index],
            flowMeter_.pulsesPerLiterX100,
            sessionSummary_.zones[index].baselineFlowMlPerMinute);
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
    captureCalibrationStop(nowMs);
    if (flowMonitor_.flowEstablished()) {
        wateringEndedMs_ = nowMs;
        wateringEndedPulseCount_ = hardware_.flowPulseCount();
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

    if (request_.purpose == WateringPurpose::FlowCalibration &&
        state_ != WateringState::StoppingZone &&
        currentStepIndex_ < request_.stepCount &&
        elapsed(nowMs,
                sessionStartedMs_,
                request_.steps[currentStepIndex_].targetDurationSec * 1000U)) {
        finishCurrentZone(nowMs);
        return;
    }
    if (request_.purpose == WateringPurpose::ZoneFlowLearning &&
        state_ != WateringState::StoppingZone &&
        currentStepIndex_ < request_.stepCount &&
        elapsed(nowMs,
                sessionStartedMs_,
                request_.steps[currentStepIndex_].targetDurationSec * 1000U)) {
        finishSession(WateringStopReason::LearningTimeout, nowMs);
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
                wateringStartedPulseCount_ = hardware_.flowPulseCount();
                flowMonitor_.beginRateWindow(nowMs, hardware_.flowPulseCount());
                if (request_.purpose == WateringPurpose::FlowCalibration) {
                    calibrationFlowEstablishedMs_ = nowMs;
                    calibrationDetector_.begin(nowMs,
                                               hardware_.flowPulseCount(),
                                               calibrationStability_);
                }
            } else if (flowMonitor_.flowStartTimedOut(nowMs, flowProtection_.flowStartTimeoutSec)) {
                finishSession(WateringStopReason::FlowStartTimeout, nowMs);
            }
            break;

        case WateringState::WateringZone: {
            flowMonitor_.observe(nowMs, hardware_.flowPulseCount());
            if (request_.purpose == WateringPurpose::FlowCalibration) {
                calibrationDetector_.observe(nowMs, hardware_.flowPulseCount());
            }
            if (flowMonitor_.noFlowTimedOut(nowMs, flowProtection_.noFlowTimeoutSec)) {
                finishSession(WateringStopReason::NoFlowTimeout, nowMs);
                break;
            }
            const WateringStep& step = request_.steps[currentStepIndex_];
            if (step.targetWaterMl != 0) {
                const uint32_t pulseCount = static_cast<uint32_t>(
                    hardware_.flowPulseCount() - zoneStartedPulseCount_);
                uint32_t estimatedWaterMl = 0;
                if (FlowMonitor::estimateWaterMilliliters(
                        pulseCount,
                        flowMeter_.pulsesPerLiterX100,
                        estimatedWaterMl) &&
                    estimatedWaterMl >= step.targetWaterMl) {
                    finishCurrentZone(nowMs);
                    break;
                }
            }
            if (!checkFlowRate(nowMs)) {
                break;
            }
            if (elapsed(nowMs, stateStartedMs_, step.targetDurationSec * 1000U)) {
                if (request_.source == WateringSource::SingleOutput &&
                    step.targetWaterMl != 0) {
                    finishSession(WateringStopReason::TargetVolumeTimeout, nowMs);
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
                    enterSwitchingZone(nowMs);
                }
            }
            break;

        case WateringState::SwitchingZone:
            if (elapsed(nowMs, stateStartedMs_, valveDrive_.switchDelayMs)) {
                if (!beginCurrentZone(nowMs)) {
                    finishSession(WateringStopReason::HardwareFailure, nowMs);
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
    result.flowEstablished =
        active_ && currentZoneStarted_ && flowMonitor_.flowEstablished();
    result.lastResult = lastResult_;
    result.lastStopReason = lastStopReason_;
    result.purpose = request_.purpose;
    result.elapsedSec = active_ ? static_cast<uint32_t>(lastHandledMs_ - sessionStartedMs_) / 1000U
                                : sessionSummary_.elapsedSec;
    result.currentFlowMlPerMinute = currentFlowMlPerMinute_;
    result.learningAverageMlPerMinute = learningAverageMlPerMinute_;
    result.learningMinimumMlPerMinute = learningMinimumMlPerMinute_;
    result.learningMaximumMlPerMinute = learningMaximumMlPerMinute_;
    result.learningWindowCount = learningWindowCount_;
    result.learningTotalWindowCount = learningTotalWindowCount_;
    if (learningWindowCount_ != 0) {
        const uint8_t decisionCount = learningWindowCount_ < kLearningDecisionWindowCount
                                          ? learningWindowCount_
                                          : kLearningDecisionWindowCount;
        const uint8_t decisionStart = learningWindowCount_ - decisionCount;
        uint32_t minimumRate = UINT32_MAX;
        uint32_t maximumRate = 0;
        uint64_t totalRate = 0;
        const uint32_t firstSequence =
            learningTotalWindowCount_ - learningWindowCount_ + 1U;
        for (uint8_t index = 0; index < learningWindowCount_; ++index) {
            const uint32_t rate = learningPulseRatesX100_[index];
            if (index >= decisionStart) {
                minimumRate = rate < minimumRate ? rate : minimumRate;
                maximumRate = rate > maximumRate ? rate : maximumRate;
                totalRate += rate;
            }
            WateringStatus::LearningWindowSample& window =
                result.learningWindows[index];
            window.sequence = firstSequence + index;
            window.pulseCount = learningPulseCounts_[index];
            window.windowMs = learningWindowDurationsMs_[index];
            window.pulseRateX100 = rate;
            FlowMonitor::pulseRateToFlowMlPerMinute(
                rate, flowMeter_.pulsesPerLiterX100, window.flowMlPerMinute);
        }
        result.learningAveragePulseRateX100 = static_cast<uint32_t>(
            (totalRate + decisionCount / 2U) / decisionCount);
        result.learningMinimumPulseRateX100 = minimumRate;
        result.learningMaximumPulseRateX100 = maximumRate;
        result.learningAllowedPulseRateSpreadX100 =
            learningAllowedPulseRateSpreadX100(
                result.learningAveragePulseRateX100);
    }
    result.flowHistoryGeneration = flowHistoryGeneration_;
    result.flowSampleSerial = flowSampleSerial_;
    result.zones = sessionSummary_.zones;

    if (active_ && currentStepIndex_ < request_.stepCount) {
        FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
            baselinePulseRateX10000_[currentStepIndex_],
            flowMeter_.pulsesPerLiterX100,
            result.expectedFlowMlPerMinute);
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
        result.currentZoneTargetWaterMl =
            request_.steps[currentStepIndex_].targetWaterMl;
        const uint32_t limitElapsedSec = request_.purpose == WateringPurpose::FlowCalibration
                                             ? result.elapsedSec
                                             : result.currentZoneElapsedSec;
        result.currentZoneRemainingSec = limitElapsedSec < targetSec
                                             ? targetSec - limitElapsedSec
                                             : 0U;
        if (currentZoneStarted_) {
            ZoneWateringSummary& current = result.zones[currentStepIndex_];
            current.actualWateringSec = result.currentZoneElapsedSec;
            current.pulseCount = result.pulseCount;
            current.waterEstimateCapped = !FlowMonitor::estimateWaterMilliliters(
                current.pulseCount, flowMeter_.pulsesPerLiterX100,
                current.estimatedWaterMl);
            fillCalibrationMetrics(current,
                                   lastHandledMs_,
                                   hardware_.flowPulseCount());
        }
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
         request.source != WateringSource::SingleOutput &&
         request.source != WateringSource::AutomaticPlan) ||
        (request.purpose != WateringPurpose::Normal &&
         request.purpose != WateringPurpose::FlowCalibration &&
         request.purpose != WateringPurpose::ZoneFlowLearning)) {
        return false;
    }
    const bool manualSource = request.source == WateringSource::ManualZones ||
                              request.source == WateringSource::SingleOutput;
    if ((manualSource && request.planId != 0) ||
        (!manualSource &&
         (request.planId == 0 || request.planId > kWateringPlanCount))) {
        return false;
    }
    if (request.source == WateringSource::SingleOutput &&
        (request.purpose != WateringPurpose::Normal || request.stepCount != 1)) {
        return false;
    }

    uint8_t previousZoneId = 0;
    for (uint8_t index = 0; index < request.stepCount; ++index) {
        const WateringStep& step = request.steps[index];
        if (!BoardPins::isValidZoneId(step.zoneId) ||
            step.zoneId <= previousZoneId ||
            !config.zones[BoardPins::zoneIndex(step.zoneId)].enabled ||
            step.targetDurationSec == 0 ||
            (request.purpose == WateringPurpose::Normal &&
             step.targetDurationSec >
                 static_cast<uint32_t>(config.runLimits.maximumZoneDurationMinutes) * 60U) ||
            (request.purpose == WateringPurpose::FlowCalibration &&
             step.targetDurationSec > 10U * 60U) ||
            (request.purpose != WateringPurpose::FlowCalibration &&
             request.source != WateringSource::SingleOutput &&
             step.targetWaterMl != 0) ||
            (request.source == WateringSource::SingleOutput &&
             step.targetWaterMl != 0 &&
             (step.targetWaterMl < 100U ||
              step.targetWaterMl >
                  static_cast<uint32_t>(config.runLimits.maximumSingleOutputLiters) * 1000U)) ||
            (request.purpose == WateringPurpose::FlowCalibration &&
             step.targetWaterMl != 0 &&
             (step.targetWaterMl < 1000U || step.targetWaterMl > 1000000U))) {
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
    wateringStartedPulseCount_ = 0;
    wateringEndedPulseCount_ = 0;
    lowFlowDurationMs_ = 0;
    highFlowDurationMs_ = 0;
    lowFlowRecoveryDurationMs_ = 0;
    highFlowRecoveryDurationMs_ = 0;
    calibrationFlowEstablishedMs_ = 0;
    calibrationStopMs_ = 0;
    calibrationStopPulseCount_ = 0;
    calibrationStopCaptured_ = false;
    if (request_.purpose == WateringPurpose::FlowCalibration) {
        calibrationDetector_.begin(nowMs,
                                   zoneStartedPulseCount_,
                                   calibrationStability_);
    }
    learningWindowCount_ = 0;
    learningTotalWindowCount_ = 0;
    learningPulseRatesX100_.fill(0);
    learningPulseCounts_.fill(0);
    learningWindowDurationsMs_.fill(0);
    terminalWindowCount_ = 0;
    terminalPulseRatesX100_.fill(0);
    terminalPulseCounts_.fill(0);
    terminalWindowDurationsMs_.fill(0);
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
    if (valveHolding_ || state_ == WateringState::Idle ||
        state_ == WateringState::SwitchingZone) {
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
    captureCalibrationStop(nowMs);
    const bool lastStep = currentStepIndex_ + 1U >= request_.stepCount;
    wateringEndedMs_ = nowMs;
    wateringEndedPulseCount_ = hardware_.flowPulseCount();
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
    enterSwitchingZone(nowMs);
}

void WateringController::enterStoppingZone(uint32_t nowMs,
                                           bool stopSession,
                                           WateringStopReason reason) {
    state_ = WateringState::StoppingZone;
    stateStartedMs_ = nowMs;
    stopSessionAfterValveClose_ = stopSession;
    pendingStopReason_ = reason;
}

void WateringController::enterSwitchingZone(uint32_t nowMs) {
    ++currentStepIndex_;
    state_ = WateringState::SwitchingZone;
    stateStartedMs_ = nowMs;
    stopSessionAfterValveClose_ = false;
    currentFlowMlPerMinute_ = 0;
}

void WateringController::finalizeCurrentZone(ZoneWateringResult result, uint32_t nowMs) {
    if (!currentZoneStarted_ || currentZoneFinalized_) {
        return;
    }
    ZoneWateringSummary& zone = sessionSummary_.zones[currentStepIndex_];
    zone.result = result;
    zone.pulseCount = static_cast<uint32_t>(hardware_.flowPulseCount() - zoneStartedPulseCount_);
    fillCalibrationMetrics(zone, nowMs, hardware_.flowPulseCount());
    if (flowMonitor_.flowEstablished()) {
        if (!wateringEndCaptured_) {
            wateringEndedMs_ = nowMs;
            wateringEndedPulseCount_ = hardware_.flowPulseCount();
            wateringEndCaptured_ = true;
        }
        zone.actualWateringSec = static_cast<uint32_t>(wateringEndedMs_ - wateringStartedMs_) / 1000U;
        sessionSummary_.anyFlowEstablished = true;
        const uint32_t wateringDurationMs = wateringEndedMs_ - wateringStartedMs_;
        if (wateringDurationMs != 0) {
            const uint32_t wateringPulses =
                wateringEndedPulseCount_ - wateringStartedPulseCount_;
            const uint64_t rate =
                (static_cast<uint64_t>(wateringPulses) * 100000ULL +
                 wateringDurationMs / 2U) /
                wateringDurationMs;
            const uint32_t pulseRateX100 =
                rate > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(rate);
            FlowMonitor::pulseRateToFlowMlPerMinute(
                pulseRateX100,
                flowMeter_.pulsesPerLiterX100,
                zone.averageFlowMlPerMinute);
        }
    }
    zone.waterEstimateCapped = !FlowMonitor::estimateWaterMilliliters(
        zone.pulseCount, flowMeter_.pulsesPerLiterX100, zone.estimatedWaterMl);
    finalizeTerminalFlow(zone);
    currentZoneFinalized_ = true;
    currentZoneStarted_ = false;
}

void WateringController::finalizeTerminalFlow(ZoneWateringSummary& zone) {
    if (request_.purpose != WateringPurpose::Normal || terminalWindowCount_ == 0) {
        return;
    }
    uint32_t minimumRate = UINT32_MAX;
    uint32_t maximumRate = 0;
    uint64_t totalRate = 0;
    uint64_t totalPulses = 0;
    uint64_t totalWindowMs = 0;
    bool allWindowsHavePulses = true;
    for (uint8_t index = 0; index < terminalWindowCount_; ++index) {
        const uint32_t rate = terminalPulseRatesX100_[index];
        minimumRate = rate < minimumRate ? rate : minimumRate;
        maximumRate = rate > maximumRate ? rate : maximumRate;
        totalRate += rate;
        totalPulses += terminalPulseCounts_[index];
        totalWindowMs += terminalWindowDurationsMs_[index];
        allWindowsHavePulses =
            allWindowsHavePulses && terminalPulseCounts_[index] != 0;
    }
    if (totalWindowMs == 0) {
        return;
    }
    const uint64_t weightedRate =
        (totalPulses * 100000ULL + totalWindowMs / 2U) / totalWindowMs;
    const uint32_t terminalRateX100 =
        weightedRate > UINT32_MAX ? UINT32_MAX
                                  : static_cast<uint32_t>(weightedRate);
    const uint32_t averageRateX100 = static_cast<uint32_t>(
        (totalRate + terminalWindowCount_ / 2U) / terminalWindowCount_);
    zone.terminalFlowAvailable = true;
    zone.terminalFlowStable =
        terminalWindowCount_ == kLearningDecisionWindowCount &&
        allWindowsHavePulses &&
        maximumRate - minimumRate <=
            learningAllowedPulseRateSpreadX100(averageRateX100);
    FlowMonitor::pulseRateToFlowMlPerMinute(
        terminalRateX100,
        flowMeter_.pulsesPerLiterX100,
        zone.terminalFlowMlPerMinute);
    FlowMonitor::pulseRateToFlowMlPerMinute(
        minimumRate,
        flowMeter_.pulsesPerLiterX100,
        zone.terminalMinimumFlowMlPerMinute);
    FlowMonitor::pulseRateToFlowMlPerMinute(
        maximumRate,
        flowMeter_.pulsesPerLiterX100,
        zone.terminalMaximumFlowMlPerMinute);
}

void WateringController::finishSession(WateringStopReason reason, uint32_t nowMs) {
    captureCalibrationStop(nowMs);
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

void WateringController::captureCalibrationStop(uint32_t nowMs) {
    if (request_.purpose != WateringPurpose::FlowCalibration ||
        calibrationStopCaptured_) {
        return;
    }
    calibrationStopMs_ = nowMs;
    calibrationStopPulseCount_ = hardware_.flowPulseCount();
    calibrationStopCaptured_ = true;
}

void WateringController::fillCalibrationMetrics(ZoneWateringSummary& zone,
                                                uint32_t nowMs,
                                                uint32_t pulseCount) const {
    if (request_.purpose != WateringPurpose::FlowCalibration) return;
    const CalibrationStabilityConfig& detectorConfig = calibrationDetector_.config();
    zone.calibrationWindowSec = detectorConfig.windowSec != 0
                                    ? detectorConfig.windowSec
                                    : calibrationStability_.windowSec;
    zone.calibrationRequiredWindows = detectorConfig.requiredWindows != 0
                                          ? detectorConfig.requiredWindows
                                          : calibrationStability_.requiredWindows;
    zone.calibrationAllowedVariationPercent =
        detectorConfig.allowedVariationPercent != 0
            ? detectorConfig.allowedVariationPercent
            : calibrationStability_.allowedVariationPercent;
    zone.calibrationCollectedWindows = calibrationDetector_.collectedWindowCount();
    zone.calibrationLatestPulseRateX100 = calibrationDetector_.latestRateX100();
    if (flowMonitor_.flowEstablished()) {
        zone.calibrationFlowEstablishedMs = calibrationFlowEstablishedMs_ - valveOpenedMs_;
    }
    zone.calibrationSteadyDetected = calibrationDetector_.steadyDetected();
    zone.calibrationSteadyLaterUnstable =
        calibrationDetector_.steadyLaterUnstable();
    if (!zone.calibrationSteadyDetected) return;

    const uint32_t steadyStartedMs = calibrationDetector_.steadyStartedMs();
    const uint32_t steadyStartedPulseCount =
        calibrationDetector_.steadyStartedPulseCount();
    const uint32_t steadyEndedMs = calibrationStopCaptured_ ? calibrationStopMs_ : nowMs;
    const uint32_t steadyEndedPulseCount = calibrationStopCaptured_
                                               ? calibrationStopPulseCount_
                                               : pulseCount;
    zone.calibrationSteadyStartedMs = steadyStartedMs - valveOpenedMs_;
    zone.calibrationStartupPulses = steadyStartedPulseCount - zoneStartedPulseCount_;
    zone.calibrationSteadyDurationMs = steadyEndedMs - steadyStartedMs;
    zone.calibrationSteadyPulses = steadyEndedPulseCount - steadyStartedPulseCount;
    zone.calibrationPulseRateX100 = calibrationDetector_.stableAverageRateX100();
    if (calibrationStopCaptured_) {
        zone.calibrationStopDurationMs = nowMs - calibrationStopMs_;
        zone.calibrationStopPulses = pulseCount - calibrationStopPulseCount_;
    }
}

bool WateringController::checkFlowRate(uint32_t nowMs) {
    const bool learning = request_.purpose == WateringPurpose::ZoneFlowLearning;
    const bool normalWatering = request_.purpose == WateringPurpose::Normal;
    const bool deviationCheck = normalWatering &&
                                baselinePulseRateX10000_[currentStepIndex_] != 0;
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
    if (normalWatering) {
        if (terminalWindowCount_ < terminalPulseRatesX100_.size()) {
            const uint8_t index = terminalWindowCount_++;
            terminalPulseRatesX100_[index] = sample.pulseRateX100;
            terminalPulseCounts_[index] = sample.pulseCount;
            terminalWindowDurationsMs_[index] = sample.windowMs;
        } else {
            for (std::size_t index = 1; index < terminalPulseRatesX100_.size(); ++index) {
                terminalPulseRatesX100_[index - 1] = terminalPulseRatesX100_[index];
                terminalPulseCounts_[index - 1] = terminalPulseCounts_[index];
                terminalWindowDurationsMs_[index - 1] =
                    terminalWindowDurationsMs_[index];
            }
            terminalPulseRatesX100_.back() = sample.pulseRateX100;
            terminalPulseCounts_.back() = sample.pulseCount;
            terminalWindowDurationsMs_.back() = sample.windowMs;
        }
    }
    if (learning) {
        ++learningTotalWindowCount_;
        if (learningWindowCount_ < learningPulseRatesX100_.size()) {
            const uint8_t index = learningWindowCount_++;
            learningPulseRatesX100_[index] = sample.pulseRateX100;
            learningPulseCounts_[index] = sample.pulseCount;
            learningWindowDurationsMs_[index] = sample.windowMs;
        } else {
            for (std::size_t index = 1; index < learningPulseRatesX100_.size(); ++index) {
                learningPulseRatesX100_[index - 1] = learningPulseRatesX100_[index];
                learningPulseCounts_[index - 1] = learningPulseCounts_[index];
                learningWindowDurationsMs_[index - 1] =
                    learningWindowDurationsMs_[index];
            }
            learningPulseRatesX100_.back() = sample.pulseRateX100;
            learningPulseCounts_.back() = sample.pulseCount;
            learningWindowDurationsMs_.back() = sample.windowMs;
        }
        const uint8_t decisionCount = learningWindowCount_ < kLearningDecisionWindowCount
                                          ? learningWindowCount_
                                          : kLearningDecisionWindowCount;
        const uint8_t decisionStart = learningWindowCount_ - decisionCount;
        uint32_t minimum = UINT32_MAX;
        uint32_t maximum = 0;
        uint64_t totalRate = 0;
        uint64_t totalPulses = 0;
        uint64_t totalWindowMs = 0;
        bool allWindowsHavePulses = true;
        for (uint8_t index = decisionStart; index < learningWindowCount_; ++index) {
            const uint32_t rate = learningPulseRatesX100_[index];
            minimum = rate < minimum ? rate : minimum;
            maximum = rate > maximum ? rate : maximum;
            totalRate += rate;
            totalPulses += learningPulseCounts_[index];
            totalWindowMs += learningWindowDurationsMs_[index];
            allWindowsHavePulses =
                allWindowsHavePulses && learningPulseCounts_[index] != 0;
        }
        const uint32_t averageRateX100 = static_cast<uint32_t>(
            (totalRate + decisionCount / 2U) / decisionCount);
        FlowMonitor::pulseRateToFlowMlPerMinute(
            averageRateX100, flowMeter_.pulsesPerLiterX100,
            learningAverageMlPerMinute_);
        FlowMonitor::pulseRateToFlowMlPerMinute(
            minimum, flowMeter_.pulsesPerLiterX100,
            learningMinimumMlPerMinute_);
        FlowMonitor::pulseRateToFlowMlPerMinute(
            maximum, flowMeter_.pulsesPerLiterX100,
            learningMaximumMlPerMinute_);
        const uint32_t tolerance =
            learningAllowedPulseRateSpreadX100(averageRateX100);
        if (decisionCount == kLearningDecisionWindowCount &&
            allWindowsHavePulses && totalWindowMs != 0 &&
            maximum - minimum <= tolerance) {
            const uint64_t weightedRate =
                (totalPulses * 10000000ULL + totalWindowMs / 2U) /
                totalWindowMs;
            zone.suggestedBaselinePulseRateX10000 =
                weightedRate > UINT32_MAX ? UINT32_MAX
                                          : static_cast<uint32_t>(weightedRate);
            finishSession(WateringStopReason::Completed, nowMs);
            return false;
        }
        return true;
    }

    if (!deviationCheck) {
        return true;
    }

    const uint64_t baseline = baselinePulseRateX10000_[currentStepIndex_];
    const uint64_t sampleRateX10000 =
        static_cast<uint64_t>(sample.pulseRateX100) * 100U;
    const bool low = sampleRateX10000 * 100U <
                     baseline * flowProtection_.lowFlowPercent;
    const bool high = sampleRateX10000 * 100U >
                      baseline * flowProtection_.highFlowPercent;
    const uint32_t confirmMs = static_cast<uint32_t>(
        flowProtection_.flowDeviationConfirmSec) * 1000U;

    if (zone.lowFlowActive) {
        if (low) {
            lowFlowRecoveryDurationMs_ = 0;
        } else {
            lowFlowRecoveryDurationMs_ =
                UINT32_MAX - lowFlowRecoveryDurationMs_ < sample.windowMs
                    ? UINT32_MAX
                    : lowFlowRecoveryDurationMs_ + sample.windowMs;
            if (lowFlowRecoveryDurationMs_ >= confirmMs) {
                zone.lowFlowActive = false;
                lowFlowRecoveryDurationMs_ = 0;
            }
        }
    } else if (low) {
        lowFlowDurationMs_ =
            UINT32_MAX - lowFlowDurationMs_ < sample.windowMs
                ? UINT32_MAX
                : lowFlowDurationMs_ + sample.windowMs;
        if (lowFlowDurationMs_ >= confirmMs) {
            zone.lowFlowActive = true;
            lowFlowDurationMs_ = 0;
            zone.lowFlowDetected = true;
            zone.lowFlowDetectedMlPerMinute = sample.flowMlPerMinute;
            if (flowProtection_.lowFlowAction == FlowAlertAction::StopWatering) {
                finishSession(WateringStopReason::LowFlow, nowMs);
                return false;
            }
        }
    } else {
        lowFlowDurationMs_ = 0;
    }

    if (zone.highFlowActive) {
        if (high) {
            highFlowRecoveryDurationMs_ = 0;
        } else {
            highFlowRecoveryDurationMs_ =
                UINT32_MAX - highFlowRecoveryDurationMs_ < sample.windowMs
                    ? UINT32_MAX
                    : highFlowRecoveryDurationMs_ + sample.windowMs;
            if (highFlowRecoveryDurationMs_ >= confirmMs) {
                zone.highFlowActive = false;
                highFlowRecoveryDurationMs_ = 0;
            }
        }
    } else if (high) {
        highFlowDurationMs_ =
            UINT32_MAX - highFlowDurationMs_ < sample.windowMs
                ? UINT32_MAX
                : highFlowDurationMs_ + sample.windowMs;
        if (highFlowDurationMs_ >= confirmMs) {
            zone.highFlowActive = true;
            highFlowDurationMs_ = 0;
            zone.highFlowDetected = true;
            zone.highFlowDetectedMlPerMinute = sample.flowMlPerMinute;
            if (flowProtection_.highFlowAction == FlowAlertAction::StopWatering) {
                finishSession(WateringStopReason::HighFlow, nowMs);
                return false;
            }
        }
    } else {
        highFlowDurationMs_ = 0;
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
