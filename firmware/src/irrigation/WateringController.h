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
    void handle(uint32_t nowMs);
    WateringStatus status() const;

private:
    static bool isValidRequest(const WateringRequest& request, const IrrigationConfig& config);
    bool beginCurrentZone(uint32_t nowMs);
    bool applyValveHoldIfDue(uint32_t nowMs);
    void finishCurrentZone(uint32_t nowMs);
    void enterStoppingZone(uint32_t nowMs, bool stopSession, WateringStopReason reason);
    void finishSession(WateringStopReason reason);

    WateringHardware& hardware_;
    WateringRequest request_{};
    ValveDriveConfig valveDrive_{};
    PumpConfig pump_{};
    FlowProtectionConfig flowProtection_{};
    FlowMonitor flowMonitor_;
    WateringState state_ = WateringState::Idle;
    WateringResult lastResult_ = WateringResult::None;
    WateringStopReason lastStopReason_ = WateringStopReason::None;
    WateringStopReason pendingStopReason_ = WateringStopReason::None;
    uint32_t stateStartedMs_ = 0;
    uint32_t valveOpenedMs_ = 0;
    uint8_t currentStepIndex_ = 0;
    bool active_ = false;
    bool valveHolding_ = false;
    bool stopSessionAfterValveClose_ = false;
};
