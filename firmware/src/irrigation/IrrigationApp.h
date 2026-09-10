#pragma once

#include "IrrigationConfigStore.h"
#include "IrrigationEvents.h"
#include "IrrigationParameterConfig.h"
#include "StatusIndicator.h"
#include "DeviceAliveCheckpoint.h"
#include "WateringRecordStore.h"
#include "WateringScheduler.h"
#include "WateringSchedulerStore.h"
#include "WateringController.h"
#include "UnexpectedFlowMonitor.h"

class IrrigationApp {
public:
    static IrrigationApp& instance();

    bool begin();
    void handle();

    bool baseReady() const;
    bool businessReady() const;
    WateringStartResult startWatering(const WateringRequest& request);
    WateringStartResult startManualWatering(
        const std::array<uint16_t, BoardPins::kZoneCount>& zoneDurationMinutes);
    WateringStartResult startSingleOutput(uint8_t zoneId,
                                          uint32_t targetDurationSec,
                                          uint32_t targetWaterMl);
    bool stopWatering();
    WateringStatus wateringStatus() const;
    bool wateringActive() const { return wateringController_.active(); }
    FlowHistorySnapshot wateringFlowHistory() const;
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
    bool unexpectedFlowObservationReady() const;
    uint16_t unexpectedFlowDelayRemainingSec() const;
    uint16_t unexpectedFlowWindowRemainingSec() const;
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
    bool saveConfiguration(const IrrigationConfig& proposed,
                           uint32_t expectedRevision,
                           IrrigationEvents::ConfigurationChange change,
                           uint8_t objectId = 0);
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
    void reportSkippedPlan(uint8_t planId, WateringStartResult result) __attribute__((noinline));
    void resetUnexpectedFlowMonitor(uint32_t nowMs);
    void observeEventConditions(uint32_t nowMs, const Esp32BaseTime::Snapshot& now);
    void refreshRtcCondition(uint32_t nowMs, bool force);
    void applyPendingHardwareConfiguration();
    void updateStatusIndicator(uint32_t nowMs);
    void handleParameterConfigSaved();
    bool applyStoredParameterConfig();
    static void parameterConfigSaved(void* user);
    static bool validateParameterConfig(const IrrigationConfig& proposed,
                                        char* error,
                                        size_t errorLength,
                                        void* user);
    static bool allowMaintenance(void* user);
    static bool allowOta(void* user);
    static void beforeLifecycleStop(void* user);
    static void afterFormatFs(const Esp32BaseWeb::FormatFsResult& result, void* user);
    void handleAfterFormatFs(const Esp32BaseWeb::FormatFsResult& result);
    bool saveZoneBaselinePulseRate(uint8_t zoneId,
                                   uint32_t pulseRateX10000,
                                   uint32_t expectedConfigRevision);
    uint32_t trustedEpoch() const;

    bool started_ = false;
    bool baseReady_ = false;
    bool businessReady_ = false;
    bool wateringStartTimeValid_ = false;
    bool finishedWateringStored_ = false;
    bool recordStorageFault_ = false;
    bool wateringRecordStoreRegistered_ = false;
    bool schedulerStorageFault_ = false;
    bool pendingPwmReconfigure_ = false;
    bool rtcObservationInitialized_ = false;
    bool eventConditionsInitialized_ = false;
    uint8_t pendingLearnedZoneId_ = 0;
    uint32_t pendingLearnedBaselinePulseRateX10000_ = 0;
    IrrigationConfig parameterConfigScratch_{};
    IrrigationConfigStore configStore_;
    DeviceAliveCheckpoint aliveCheckpoint_;
    IrrigationEvents events_;
    WateringController wateringController_;
    WateringRecordStore wateringRecordStore_;
    WateringSchedulerStore wateringSchedulerStore_;
    WateringScheduler wateringScheduler_;
    UnexpectedFlowMonitor unexpectedFlowMonitor_;
    StatusIndicator& statusIndicator_;
    Esp32BaseRecordStore::RecordStartTime wateringStartTime_{};
    uint32_t lastRtcRefreshMs_ = 0;
};
