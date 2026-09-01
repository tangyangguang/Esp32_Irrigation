#pragma once

#include <Esp32Base.h>

#include <cstddef>
#include <cstdint>

#include "IrrigationAuditStore.h"
#include "IrrigationConfig.h"
#include "IrrigationTypes.h"

class IrrigationEvents {
public:
    enum class EventCode : uint32_t {
        WateringStoppedAbnormally = 1001,
        AutomaticWateringStateChanged = 1002,
        AutomaticPlanSkipped = 1003,
        RtcRollback = 1004,
        FlowDeviation = 1201,
        ClosedValveFlow = 1006,
        FlowCalibrationSaved = 1202,
        ZoneFlowSaved = 1203,
        ConfigurationChanged = 1009,
        WateringRecordSaveFailed = 1101,
        SchedulerStateSaveFailed = 1102,
        RtcUnavailable = 1103,
        TrustedTimeUnavailable = 1104,
    };
    enum class ReasonCode : uint32_t {
        PausedIndefinitely = 1,
        PausedUntil,
        ResumedManually,
        ResumedAutomatically,
        PlanBusy,
        PlanStartRejected,
        PlanBusyManualWatering,
        PlanBusyAutomaticWatering,
        PlanBusyFlowCalibration,
        PlanBusyZoneFlowLearning,
        PlanPreviousResultPending,
        PlanControllerNotReady,
        PlanInvalidRequest,
        PlanHardwareFailure,
        FlowStartTimeout,
        NoFlowTimeout,
        HardwareFailure,
        MaintenanceInterrupted,
        LowFlow,
        HighFlow,
        ClosedValveFlow,
        TargetVolumeTimeout,
        RecordStartTimeUnavailable,
        RecordAppendFailed,
        SchedulerStateStorage,
        RtcRollback,
        RtcUnavailable,
        TrustedTimeUnavailable,
        CalibrationCoefficientSaved,
        ZoneFlowSaved,
        PlanCreated,
        PlanUpdated,
        PlanDeleted,
        ZoneUpdated,
        SystemParametersUpdated,
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
    enum class ConditionDisplayState : uint8_t {
        Unknown,
        Normal,
        Active,
        ConfirmingActivation,
        ConfirmingRecovery,
    };
    enum class Level : uint8_t { Info, Warning, Error };
    struct EventRecord {
        uint32_t recordId = 0;
        Esp32BaseRecordStore::RecordTiming timing{};
        uint32_t eventCode = 0;
        uint32_t reasonCode = 0;
        uint8_t conditionId = 0;
        uint8_t eventKind = 0;
        uint32_t objectId = 0;
        int32_t value1 = 0;
        int32_t value2 = 0;
        uint8_t flags = 0;
        Level level = Level::Info;
    };
    struct EventStatus {
        Esp32BaseRecordStore::StoreStatus eventStore{};
        bool conditionStateLoaded = false;
        bool conditionStateSavePending = false;
    };
    using ReadCallback = void (*)(const EventRecord&, void*);

    IrrigationEvents();
    bool begin();
    IrrigationAuditStore& auditStore();
    void syncStorageStatus();
    bool resetConditionHistory();
    bool storageFault() const;
    bool readStatus(EventStatus& status) const;
    bool readLatest(uint32_t offset, uint32_t limit,
                    ReadCallback callback, void* user = nullptr) const;

    void recordAbnormalWateringStop(const WateringSessionSummary&) {}
    void recordFlowDeviationEvent(const ZoneWateringSummary&, ReasonCode, bool,
                                  WateringSource, uint8_t) {}
    void recordAutomaticWateringPaused(bool indefinitely, uint32_t resumeAtEpoch);
    void recordAutomaticWateringResumed(bool automatically);
    void recordAutomaticPlanSkipped(uint8_t planId, const char* planName,
                                    WateringStartResult result,
                                    const WateringStatus& status);
    void recordAutomaticRun(
        const Esp32BaseRecordStore::RecordStartTime& startTime,
        const WateringSessionSummary& summary);
    void recordFlowCalibrationSaved(uint32_t previousCoefficientX100,
                                    uint32_t coefficientX100,
                                    uint32_t pulseCount,
                                    uint32_t waterMl);
    void recordZoneFlowSaved(uint8_t zoneId,
                             uint32_t previousFlowMlPerMinute,
                             uint32_t pulseRateX10000,
                             uint32_t flowMlPerMinute);
    void recordConfigurationChanged(ConfigurationChange change,
                                    uint8_t objectId = 0,
                                    const IrrigationConfig* config = nullptr);
    void recordWateringRecordSaveFailed(ReasonCode,
                                        Esp32BaseRecordStore::StoreState,
                                        Esp32BaseRecordStore::StoreError) {}
    void recordSchedulerStateSaveFailed() {}
    void recordBusinessStorageFailed(const char*, const char*) {}

    void observeRtcAvailability(bool available, uint8_t statusCode);
    void observeTrustedTime(bool trusted);
    void observeRtcRollback(Esp32BaseConditions::ObservedState state);
    void observeClosedValveFlow(Esp32BaseConditions::ObservedState state,
                                uint32_t pulseCount,
                                uint32_t detectedFlowMlPerMinute,
                                uint16_t windowSec,
                                uint16_t thresholdPulseCount);
    ConditionDisplayState conditionState(uint8_t conditionId) const;

    static Category category(const EventRecord& event);
    static const char* categoryName(Category category);
    static const char* levelName(Level level);
    static bool hasWateringContext(const EventRecord&) { return false; }
    static WateringSource wateringSource(const EventRecord&) {
        return WateringSource::ManualZones;
    }
    static uint8_t wateringPlanId(const EventRecord& event);
    static void formatTitle(const EventRecord& event, char* out,
                            std::size_t length,
                            const char* planName = nullptr,
                            const char* zoneName = nullptr);
    static void formatSummary(const EventRecord& event, char* out,
                              std::size_t length);

private:
    static constexpr uint8_t kRtcUnavailableConditionId = 1;
    static constexpr uint8_t kTrustedTimeUnavailableConditionId = 2;
    static constexpr uint8_t kRtcRollbackConditionId = 3;
    static constexpr uint8_t kClosedValveFlowConditionId = 4;

    bool append(const IrrigationAuditPayload& payload);
    bool append(const Esp32BaseRecordStore::RecordStartTime& startTime,
                const IrrigationAuditPayload& payload);
    void observe(Esp32BaseConditions::ConditionTracker& tracker,
                 Esp32BaseConditions::ObservedState state,
                 ConditionDisplayState& displayState);
    static EventRecord present(const StoredIrrigationAuditRecord& stored);
    static ReasonCode automaticSkipReason(WateringStartResult result,
                                          const WateringStatus& status);

    IrrigationAuditStore auditStore_;
    Esp32BaseConditions::ConditionTracker rtcUnavailableCondition_;
    Esp32BaseConditions::ConditionTracker trustedTimeUnavailableCondition_;
    Esp32BaseConditions::ConditionTracker rtcRollbackCondition_;
    Esp32BaseConditions::ConditionTracker closedValveFlowCondition_;
    ConditionDisplayState rtcUnavailableState_ = ConditionDisplayState::Unknown;
    ConditionDisplayState trustedTimeUnavailableState_ = ConditionDisplayState::Unknown;
    ConditionDisplayState rtcRollbackState_ = ConditionDisplayState::Unknown;
    ConditionDisplayState closedValveFlowState_ = ConditionDisplayState::Unknown;
    bool storageFault_ = true;
};
