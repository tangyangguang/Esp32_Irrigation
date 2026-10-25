#pragma once

#include <cstdint>
#include <memory>

#include "FlowMonitor.h"
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
    bool active() const { return active_; }
    // Real-time curve: fetch small metadata, then up to maxCount samples in
    // logical (oldest-first) order starting at logical offset 0..sampleCount.
    // Each call is a short critical section; the ring never crosses a stack.
    FlowHistoryInfo flowHistoryInfo() const;
    uint16_t readFlowHistory(uint16_t offset, uint32_t* out, uint16_t maxCount) const;
    // Whole-task 5 s overview (persisted after a Normal watering).
    FlowOverviewInfo flowOverviewInfo() const;
    uint16_t readFlowOverview(uint16_t offset, uint32_t* out, uint16_t maxCount) const;
    // Load a persisted overview into RAM (boot) or drop it (format/loss).
    void loadFlowOverview(uint32_t recordId,
                          uint32_t startedEpoch,
                          const uint16_t* zonePointCount,
                          const uint32_t* zoneBaselineFlow,
                          uint8_t zoneCount,
                          const uint32_t* points,
                          uint16_t pointCount);
    void setFlowOverviewRecordId(uint32_t recordId);
    void clearFlowOverview();
    const WateringSessionSummary* finishedSession() const;
    void clearFinishedSession();

    // Returns Started when valid; otherwise InvalidRequest or
    // ZoneUnavailable (a referenced zone exists but is disabled).
    static WateringStartResult validateRequest(const WateringRequest& request,
                                              const IrrigationConfig& config);
private:
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
    void appendOverviewPoint(uint32_t flowMlPerMinute);

    WateringHardware& hardware_;
    WateringRequest request_{};
    ValveDriveConfig valveDrive_{};
    PumpConfig pump_{};
    FlowMeterConfig flowMeter_{};
    FlowProtectionConfig flowProtection_{};
    std::array<uint32_t, BoardPins::kZoneCount> baselinePulseRateX10000_{};
    std::array<uint32_t, kLearningHistoryWindowCount> learningPulseRatesX100_{};
    std::array<uint32_t, kLearningHistoryWindowCount> learningPulseCounts_{};
    std::array<uint32_t, kLearningHistoryWindowCount> learningWindowDurationsMs_{};
    std::array<uint32_t, kFlowHistorySampleCount> flowHistorySamples_{};
    // Six-hour 5 s buffer is large (~17 KB) and would not fit the static DRAM
    // segment, so it is heap-allocated once and held for the controller's life.
    std::unique_ptr<uint32_t[]> flowOverviewPoints_;
    std::array<uint16_t, BoardPins::kZoneCount> flowOverviewZoneCounts_{};
    std::array<uint32_t, BoardPins::kZoneCount> flowOverviewBaseline_{};
    uint16_t flowOverviewCount_ = 0;
    uint8_t flowOverviewZoneCount_ = 0;
    uint32_t flowOverviewStartedEpoch_ = 0;
    uint32_t flowOverviewRecordId_ = 0;
    bool overviewFull_ = false;
    std::array<uint32_t, kLearningDecisionWindowCount> terminalPulseRatesX100_{};
    std::array<uint32_t, kLearningDecisionWindowCount> terminalPulseCounts_{};
    std::array<uint32_t, kLearningDecisionWindowCount> terminalWindowDurationsMs_{};
    FlowMonitor flowMonitor_;
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
    bool overviewActive_ = false;  // accumulate only for Normal waterings
};
