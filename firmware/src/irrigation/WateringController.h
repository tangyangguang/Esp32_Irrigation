#pragma once

#include <cstdint>

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
    const WateringSessionSummary* finishedSession() const;
    void clearFinishedSession();

private:
    static bool isValidRequest(const WateringRequest& request, const IrrigationConfig& config);
    bool beginCurrentZone(uint32_t nowMs);
    bool applyValveHoldIfDue(uint32_t nowMs);
    void finishCurrentZone(uint32_t nowMs);
    void enterStoppingZone(uint32_t nowMs, bool stopSession, WateringStopReason reason);
    void finalizeCurrentZone(ZoneWateringResult result, uint32_t nowMs);
    void finishSession(WateringStopReason reason, uint32_t nowMs);

    WateringHardware& hardware_;
    WateringRequest request_{};
    ValveDriveConfig valveDrive_{};
    PumpConfig pump_{};
    FlowMeterConfig flowMeter_{};
    FlowProtectionConfig flowProtection_{};
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
    uint32_t wateringEndedMs_ = 0;
    uint8_t currentStepIndex_ = 0;
    bool active_ = false;
    bool valveHolding_ = false;
    bool currentZoneStarted_ = false;
    bool currentZoneFinalized_ = false;
    bool wateringEndCaptured_ = false;
    bool stopSessionAfterValveClose_ = false;
    bool finishedSessionReady_ = false;
};
