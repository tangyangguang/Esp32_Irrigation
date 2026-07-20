#pragma once

#include <cstdint>

#include "FlowMonitor.h"
#include "CalibrationStabilityDetector.h"
#include "IrrigationTypes.h"
#include "WateringHardware.h"

class WateringController {
public:
    explicit WateringController(WateringHardware& hardware);

    WateringStartResult start(const WateringRequest& request,
                              const IrrigationConfig& config,
                              uint32_t nowMs);
    bool stop(uint32_t nowMs);
    bool abortForMaintenance(uint32_t nowMs);
    void handle(uint32_t nowMs);
    WateringStatus status() const;
    FlowHistorySnapshot flowHistory() const;
    const WateringSessionSummary* finishedSession() const;
    void clearFinishedSession();

private:
    static bool isValidRequest(const WateringRequest& request, const IrrigationConfig& config);
    bool beginCurrentZone(uint32_t nowMs);
    bool applyValveHoldIfDue(uint32_t nowMs);
    void finishCurrentZone(uint32_t nowMs);
    void enterStoppingZone(uint32_t nowMs, bool stopSession, WateringStopReason reason);
    void enterSwitchingZone(uint32_t nowMs);
    void finalizeCurrentZone(ZoneWateringResult result, uint32_t nowMs);
    void finalizeTerminalFlow(ZoneWateringSummary& zone);
    void finishSession(WateringStopReason reason, uint32_t nowMs);
    bool checkFlowRate(uint32_t nowMs);
    void appendFlowSample(uint32_t flowMlPerMinute);
    void captureCalibrationStop(uint32_t nowMs);
    void fillCalibrationMetrics(ZoneWateringSummary& zone,
                                uint32_t nowMs,
                                uint32_t pulseCount) const;

    WateringHardware& hardware_;
    WateringRequest request_{};
    ValveDriveConfig valveDrive_{};
    PumpConfig pump_{};
    FlowMeterConfig flowMeter_{};
    FlowProtectionConfig flowProtection_{};
    CalibrationStabilityConfig calibrationStability_{};
    std::array<uint32_t, BoardPins::kZoneCount> baselinePulseRateX10000_{};
    std::array<uint32_t, kLearningHistoryWindowCount> learningPulseRatesX100_{};
    std::array<uint32_t, kLearningHistoryWindowCount> learningPulseCounts_{};
    std::array<uint32_t, kLearningHistoryWindowCount> learningWindowDurationsMs_{};
    std::array<uint32_t, kFlowHistorySampleCount> flowHistorySamples_{};
    std::array<uint32_t, kLearningDecisionWindowCount> terminalPulseRatesX100_{};
    std::array<uint32_t, kLearningDecisionWindowCount> terminalPulseCounts_{};
    std::array<uint32_t, kLearningDecisionWindowCount> terminalWindowDurationsMs_{};
    FlowMonitor flowMonitor_;
    CalibrationStabilityDetector calibrationDetector_;
    WateringState state_ = WateringState::Idle;
    WateringResult lastResult_ = WateringResult::None;
    WateringStopReason lastStopReason_ = WateringStopReason::None;
    WateringStopReason pendingStopReason_ = WateringStopReason::None;
    ZoneWateringResult pendingZoneResult_ = ZoneWateringResult::NotStarted;
    WateringSessionSummary sessionSummary_{};
    uint32_t stateStartedMs_ = 0;
    uint32_t sessionStartedMs_ = 0;
    uint32_t valveOpenedMs_ = 0;
    uint32_t zoneStartedPulseCount_ = 0;
    uint32_t wateringStartedMs_ = 0;
    uint32_t wateringStartedPulseCount_ = 0;
    uint32_t wateringEndedMs_ = 0;
    uint32_t wateringEndedPulseCount_ = 0;
    uint32_t lastHandledMs_ = 0;
    uint32_t lowFlowDurationMs_ = 0;
    uint32_t highFlowDurationMs_ = 0;
    uint32_t lowFlowRecoveryDurationMs_ = 0;
    uint32_t highFlowRecoveryDurationMs_ = 0;
    uint32_t currentFlowMlPerMinute_ = 0;
    uint32_t calibrationFlowEstablishedMs_ = 0;
    uint32_t calibrationStopMs_ = 0;
    uint32_t calibrationStopPulseCount_ = 0;
    uint32_t flowHistoryGeneration_ = 0;
    uint32_t flowSampleSerial_ = 0;
    uint32_t learningAverageMlPerMinute_ = 0;
    uint32_t learningMinimumMlPerMinute_ = 0;
    uint32_t learningMaximumMlPerMinute_ = 0;
    uint8_t currentStepIndex_ = 0;
    uint8_t learningWindowCount_ = 0;
    uint8_t terminalWindowCount_ = 0;
    uint32_t learningTotalWindowCount_ = 0;
    uint16_t flowHistoryStart_ = 0;
    uint16_t flowHistoryCount_ = 0;
    uint8_t flowHistoryZoneId_ = 0;
    bool active_ = false;
    bool valveHolding_ = false;
    bool currentZoneStarted_ = false;
    bool currentZoneFinalized_ = false;
    bool wateringEndCaptured_ = false;
    bool stopSessionAfterValveClose_ = false;
    bool finishedSessionReady_ = false;
    bool calibrationStopCaptured_ = false;
};
