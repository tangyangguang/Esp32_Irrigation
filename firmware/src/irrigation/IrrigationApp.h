#pragma once

#include "IrrigationConfigStore.h"
#include "IrrigationEvents.h"
#include "IrrigationParameterConfig.h"
#include "FlowCalibrationService.h"
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
    bool stopWatering();
    WateringStatus wateringStatus() const;
    FlowHistorySnapshot wateringFlowHistory() const;
    bool readLatestWateringRecords(uint32_t offset,
                                   uint32_t limit,
                                   WateringRecordStore::ReadCallback callback,
                                   void* user = nullptr);
    Esp32BaseRecordStore::RecordReadResult readWateringRecordById(
        uint32_t recordId,
        StoredWateringRecord& record);
    bool readWateringRecordStoreStatus(Esp32BaseRecordStore::StoreStatus& status) const;
    bool recordStorageFault() const;
    bool eventStorageFault() const;
    bool schedulerStorageFault() const;
    bool checkpointStorageFault() const;
    uint32_t lastKnownAliveEpoch() const;
    bool unexpectedFlowAlarm() const;
    AutomaticWateringState automaticWateringState() const;
    NextAutomaticWatering nextAutomaticWatering() const;
    WateringScheduler::TimeState schedulerTimeState() const;
    bool pauseAutomaticWateringIndefinitely();
    bool pauseAutomaticWateringUntil(uint32_t resumeAtEpoch);
    bool resumeAutomaticWatering();
    WateringStartResult startFlowCalibration(uint8_t zoneId,
                                             uint16_t maximumDurationMinutes);
    bool submitFlowCalibrationMeasurement(uint32_t measuredWaterMl);
    bool markFlowCalibrationSampleInvalid();
    bool updateFlowCalibrationMeasurement(uint8_t index, uint32_t measuredWaterMl);
    bool deleteFlowCalibrationSample(uint8_t index);
    bool applyFlowCalibrationResult();
    void resetFlowCalibration();
    const FlowCalibrationService& flowCalibration() const;
    WateringStartResult startZoneFlowLearning(uint8_t zoneId);
    bool saveLearnedZoneFlow(uint32_t expectedConfigRevision);
    uint8_t pendingLearnedZoneId() const;
    uint32_t pendingLearnedFlowMlPerMinute() const;
    void discardLearnedZoneFlow();
    const IrrigationConfig* configuration() const;
    bool saveConfiguration(const IrrigationConfig& proposed,
                           uint32_t expectedRevision);
    const char* configurationError() const;

private:
    IrrigationApp();
    void advanceBusiness();
    void consumeFinishedWatering();
    void reportRecordStorageFault(IrrigationEvents::ReasonCode operation);
    static WateringStartResult startScheduledWatering(const WateringRequest& request,
                                                      void* user);
    static void handleSchedulerEvent(WateringScheduler::Event event,
                                     uint8_t planId,
                                     int32_t value,
                                     void* user);
    void reportSchedulerEvent(WateringScheduler::Event event,
                              uint8_t planId,
                              int32_t value);
    void resetUnexpectedFlowMonitor(uint32_t nowMs);
    void applyPendingHardwareConfiguration();
    void handleParameterConfigSaved();
    bool applyStoredParameterConfig();
    static void parameterConfigSaved(void* user);
    static void afterFormatFs(const Esp32BaseWeb::FormatFsResult& result, void* user);
    void handleAfterFormatFs(const Esp32BaseWeb::FormatFsResult& result);
    uint32_t trustedEpoch() const;

    bool started_ = false;
    bool baseReady_ = false;
    bool businessReady_ = false;
    bool wateringStartTimeValid_ = false;
    bool recordStorageFault_ = false;
    bool wateringRecordStoreRegistered_ = false;
    bool eventStorageFault_ = false;
    bool schedulerStorageFault_ = false;
    bool pendingPwmReconfigure_ = false;
    uint8_t pendingLearnedZoneId_ = 0;
    uint32_t pendingLearnedFlowMlPerMinute_ = 0;
    IrrigationConfig parameterConfigScratch_{};
    IrrigationConfigStore configStore_;
    FlowCalibrationService flowCalibrationService_;
    DeviceAliveCheckpoint aliveCheckpoint_;
    WateringController wateringController_;
    WateringRecordStore wateringRecordStore_;
    WateringSchedulerStore wateringSchedulerStore_;
    WateringScheduler wateringScheduler_;
    UnexpectedFlowMonitor unexpectedFlowMonitor_;
    Esp32BaseRecordStore::RecordStartTime wateringStartTime_{};
};
