#pragma once

#include <Esp32Base.h>

#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

class IrrigationEvents {
public:
    enum class EventCode : uint32_t {
        WateringStoppedAbnormally = 1001,
        AutomaticWateringStateChanged = 1002,
        AutomaticPlanSkipped = 1003,
        RtcRollback = 1004,
        FlowDeviation = 1005,
        ClosedValveFlow = 1006,
        FlowCalibrationSaved = 1007,
        ZoneFlowSaved = 1008,
        ConfigurationChanged = 1009,
        WateringRecordSaveFailed = 1101,
        SchedulerStateSaveFailed = 1102,
        RtcUnavailable = 1103,
        TrustedTimeUnavailable = 1104,
    };

    enum class ReasonCode : uint32_t {
        FlowStartTimeout = 1,
        NoFlowTimeout = 2,
        HardwareFailure = 3,
        MaintenanceInterrupted = 4,
        LowFlow = 5,
        HighFlow = 6,
        ClosedValveFlow = 7,
        RecordStartTimeUnavailable = 103,
        RecordAppendFailed = 104,
        PausedIndefinitely = 201,
        PausedUntil = 202,
        ResumedManually = 203,
        ResumedAutomatically = 204,
        PlanBusy = 211,
        PlanStartRejected = 212,
        SchedulerStateStorage = 221,
        RtcRollback = 231,
        RtcUnavailable = 232,
        TrustedTimeUnavailable = 233,
        CalibrationCoefficientSaved = 301,
        ZoneFlowSaved = 302,
        PlanCreated = 401,
        PlanUpdated = 402,
        PlanDeleted = 403,
        ZoneUpdated = 404,
        SystemParametersUpdated = 405,
    };

    enum class ConfigurationChange : uint8_t {
        PlanCreated,
        PlanUpdated,
        PlanDeleted,
        ZoneUpdated,
        SystemParametersUpdated,
    };

    enum class Category : uint8_t {
        WateringAndFlow,
        AutomaticWatering,
        SettingsAndCalibration,
        TimeAndStorage,
    };

    using ReadCallback = Esp32BaseAppEvents::ReadCallback;

    IrrigationEvents();

    void syncStorageStatus();
    bool storageFault() const;
    bool readStatus(Esp32BaseAppEvents::AppEventsStatus& status) const;
    bool readLatest(uint32_t offset,
                    uint32_t limit,
                    ReadCallback callback,
                    void* user = nullptr) const;

    void recordAbnormalWateringStop(const WateringSessionSummary& summary);
    void recordFlowDeviationEvents(const WateringSessionSummary& summary);
    void recordAutomaticWateringPaused(bool indefinitely, uint32_t resumeAtEpoch);
    void recordAutomaticWateringResumed(bool automatically);
    void recordAutomaticPlanSkipped(uint8_t planId, bool busy);
    void recordFlowCalibrationSaved(uint32_t coefficientX100);
    void recordZoneFlowSaved(uint8_t zoneId, uint32_t flowMlPerMinute);
    void recordConfigurationChanged(ConfigurationChange change, uint8_t objectId = 0);
    void recordWateringRecordSaveFailed(ReasonCode reason,
                                        Esp32BaseRecordStore::StoreState state,
                                        Esp32BaseRecordStore::StoreError error);
    void recordSchedulerStateSaveFailed();

    void observeRtcAvailability(bool available, uint8_t statusCode);
    void observeTrustedTime(bool trusted);
    void observeRtcRollback(Esp32BaseAppEvents::ObservedConditionState state);
    void observeClosedValveFlow(Esp32BaseAppEvents::ObservedConditionState state,
                                uint32_t pulseCount,
                                uint16_t windowSec);

    static Category category(const Esp32BaseAppEvents::EventRecord& event);
    static const char* categoryName(Category category);
    static const char* levelName(Esp32BaseAppEvents::Level level);
    static void formatTitle(const Esp32BaseAppEvents::EventRecord& event,
                            char* out,
                            std::size_t length);
    static void formatSummary(const Esp32BaseAppEvents::EventRecord& event,
                              char* out,
                              std::size_t length);

private:
    static constexpr uint8_t kRtcUnavailableConditionId = 1;
    static constexpr uint8_t kTrustedTimeUnavailableConditionId = 2;
    static constexpr uint8_t kRtcRollbackConditionId = 3;
    static constexpr uint8_t kClosedValveFlowConditionId = 4;
    static constexpr uint16_t kFlagValue1Capped = 1U << 0U;

    void append(const Esp32BaseAppEvents::EventInput& event);
    void observe(Esp32BaseAppEvents::ConditionStateTracker& tracker,
                 Esp32BaseAppEvents::ObservedConditionState state,
                 const Esp32BaseAppEvents::EventInput& event);
    void handleDiscreteResult(Esp32BaseAppEvents::DiscreteEventAppendResult result);
    void handleConditionResult(Esp32BaseAppEvents::ConditionObservationResult result);
    void updateStorageFault(bool fault, const char* reason);
    static ReasonCode wateringReason(WateringStopReason reason);

    Esp32BaseAppEvents::ConditionStateTracker rtcUnavailableCondition_;
    Esp32BaseAppEvents::ConditionStateTracker trustedTimeUnavailableCondition_;
    Esp32BaseAppEvents::ConditionStateTracker rtcRollbackCondition_;
    Esp32BaseAppEvents::ConditionStateTracker closedValveFlowCondition_;
    bool storageStateKnown_ = false;
    bool storageFault_ = true;
};
