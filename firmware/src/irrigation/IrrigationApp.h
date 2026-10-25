#pragma once

#include "IrrigationConfigStore.h"
#include "IrrigationEvents.h"
#include "IrrigationParameterConfig.h"
#include "StatusIndicator.h"
#include "DeviceAliveCheckpoint.h"
#include "WateringRecordStore.h"
#include "WateringFlowOverviewStore.h"
#include "WateringHistory.h"
#include "WateringScheduler.h"
#include "WateringSchedulerStore.h"
#include "WateringExecutor.h"
#include "UnexpectedFlowMonitor.h"

class IrrigationApp {
public:
    static IrrigationApp& instance();

    bool begin();
    void handle();

    bool businessReady() const;
    WateringStartResult startWatering(const WateringRequest& request);
    bool stopWatering();
    WateringStatus wateringStatus() const;
    bool wateringActive() const { return wateringController_.active(); }
    FlowHistoryInfo wateringFlowHistoryInfo() const;
    uint16_t readWateringFlowHistory(uint16_t offset, uint32_t* out, uint16_t maxCount) const;
    FlowOverviewInfo wateringFlowOverviewInfo() const;
    uint16_t readWateringFlowOverview(uint16_t offset, uint32_t* out, uint16_t maxCount) const;
    void loadWateringFlowOverview(uint32_t startedEpoch, const uint16_t* zonePointCount,
                                  uint8_t zoneCount, const uint32_t* points, uint16_t pointCount);
    void clearWateringFlowOverview();
    // Matched RAM overview for a record-detail dialog, or null when the stored
    // overview belongs to a different/older record.
    const WateringFlowOverviewStore::Snapshot* matchedFlowOverview(uint32_t recordId) const;
    // Record id of the persisted last-task flow curve, or 0 when none exists.
    uint32_t lastFlowOverviewRecordId() const;
    // activeStatus is the caller's existing status snapshot, passed by const
    // reference so the large WateringStatus (about 1.1 KB) is never copied a
    // second time onto the 8/12 KB loopTask stack while rendering a page.
    WateringDaySummary wateringDay(uint32_t day, const WateringStatus& activeStatus);
    bool readLatestWateringRecords(uint32_t offset,
                                   uint32_t limit,
                                   WateringRecordStore::ReadCallback callback,
                                   void* user = nullptr);
    Esp32BaseRecordStore::RecordReadResult readWateringRecordById(
        uint32_t recordId,
        StoredWateringRecord& record);
    bool readWateringRecordStoreStatus(Esp32BaseRecordStore::StoreStatus& status) const;
    bool readLatestEvents(uint32_t offset,
                          uint32_t limit,
                          IrrigationEvents::ReadCallback callback,
                          void* user = nullptr) const;
    bool readEventStatus(IrrigationEvents::EventStatus& status) const;
    IrrigationEvents::ConditionDisplayState eventConditionState(
        uint8_t conditionId) const;
    bool recordStorageFault() const;
    bool eventStorageFault() const;
    bool schedulerStorageFault() const;
    bool checkpointStorageFault() const;
    uint32_t lastKnownAliveEpoch() const;
    bool unexpectedFlowAlarm() const;
    uint16_t unexpectedFlowObservedWindowSec() const;
    uint32_t unexpectedFlowObservedPulseCount() const;
    uint32_t unexpectedFlowEstimatedMlPerMinute() const;
    AutomaticWateringState automaticWateringState() const;
    NextAutomaticWatering nextAutomaticWatering() const;
    WateringScheduler::TimeState schedulerTimeState() const;
    bool pauseAutomaticWateringIndefinitely();
    bool pauseAutomaticWateringUntil(uint32_t resumeAtEpoch);
    bool resumeAutomaticWatering();
    WateringStartResult startZoneFlowLearning(uint8_t zoneId);
    bool saveLearnedZoneFlow(uint32_t expectedConfigRevision);
    bool saveManualZoneBaselineFlow(uint8_t zoneId,
                                    uint32_t flowMlPerMinute,
                                    uint32_t expectedConfigRevision);
    uint8_t pendingLearnedZoneId() const;
    uint32_t pendingLearnedBaselinePulseRateX10000() const;
    uint32_t pendingLearnedFlowMlPerMinute() const;
    bool clearLearnedZoneFlow(uint8_t zoneId, uint32_t expectedConfigRevision);
    void discardLearnedZoneFlow();
    const IrrigationConfig* configuration() const;
    IrrigationConfigStore::LoadResult configurationLoadResult() const;
    enum class ConfigSaveError : uint8_t {
        Ok,
        NotReady,
        Busy,
        RevisionMismatch,
        InvalidValue,
        ZoneUnavailable,
        Persistence,
        AuditUnavailable,
    };
    // Save a single plan slot: creating/updating, deleting or toggling
    // automatic execution. Both local web and platform commands go here.
    ConfigSaveError savePlanSlot(const WateringPlan& plan,
                                 bool deleteSlot,
                                 uint32_t expectedRevision);
    // Save zone identity/enabled state. Disabling a zone clears its duration
    // in every plan as part of the same atomic config save.
    ConfigSaveError saveZoneInfo(uint8_t zoneId,
                                 const char* name,
                                 bool enabled,
                                 uint32_t expectedRevision);
    // Set (non-zero) or clear (zero) a zone baseline pulse rate.
    ConfigSaveError setZoneBaseline(uint8_t zoneId,
                                    uint32_t pulseRateX10000,
                                    uint32_t expectedRevision);
    // Apply one irrigation system parameter field. The single physical store
    // stays Esp32Base NVS; this is another UI surface over the same fields.
    bool applyRemoteSystemField(const char* field,
                                bool valueIsInteger,
                                int32_t integerValue,
                                bool valueIsBoolean,
                                bool booleanValue,
                                const char* textValue);
    const char* configurationError() const;

private:
    IrrigationApp();
    void advanceBusiness();
    void consumeFinishedWatering(uint32_t nowMs);
    static WateringStartResult startScheduledWatering(const WateringRequest& request,
                                                      void* user);
    static void handleSchedulerEvent(WateringScheduler::Event event,
                                     uint8_t planId,
                                     int32_t value,
                                     void* user);
    void reportSchedulerEvent(WateringScheduler::Event event,
                              uint8_t planId,
                              int32_t value);
    void recordStartRejected(uint8_t planId, WateringStartResult result)
        __attribute__((noinline));
    void resetUnexpectedFlowMonitor(uint32_t nowMs);
    void persistFlowOverview(uint32_t startedEpoch);
    void observeEventConditions(uint32_t nowMs, const Esp32BaseTime::Snapshot& now);
    void refreshRtcCondition(uint32_t nowMs, bool force);
    void updateStatusIndicator(uint32_t nowMs);
    void handleParameterConfigSaved();
    bool applyStoredParameterConfig();
    static void parameterConfigSaved(void* user);
    static Esp32BaseAppConfig::ApplyStatus parameterApplyStatus();
    static bool validateParameterConfig(const IrrigationParameters& proposed,
                                        char* error,
                                        size_t errorLength,
                                        void* user);
    static bool allowMaintenance(void* user);
    static bool allowOta(void* user);
    static void beforeLifecycleStop(void* user);
    static void afterFormatFs(const Esp32BaseWeb::FormatFsResult& result, void* user);
    void handleAfterFormatFs(const Esp32BaseWeb::FormatFsResult& result);
    uint32_t trustedEpoch() const;

    bool started_ = false;
    bool baseReady_ = false;
    bool businessReady_ = false;
    bool wateringStartTimeValid_ = false;
    bool finishedWateringStored_ = false;
    bool recordStorageFault_ = false;
    bool schedulerStorageFault_ = false;
    bool rtcObservationInitialized_ = false;
    bool eventConditionsInitialized_ = false;
    uint8_t pendingLearnedZoneId_ = 0;
    uint32_t pendingLearnedBaselinePulseRateX10000_ = 0;
    IrrigationParameters parameterConfigScratch_{};
    IrrigationConfigStore configStore_;
    DeviceAliveCheckpoint aliveCheckpoint_;
    IrrigationEvents events_;
    WateringExecutor wateringController_;
    WateringRecordStore wateringRecordStore_;
    WateringFlowOverviewStore flowOverviewStore_;
    WateringSchedulerStore wateringSchedulerStore_;
    WateringScheduler wateringScheduler_;
    UnexpectedFlowMonitor unexpectedFlowMonitor_;
    StatusIndicator& statusIndicator_;
    Esp32BaseRecordStore::RecordStartTime wateringStartTime_{};
    uint32_t lastRtcRefreshMs_ = 0;
};
