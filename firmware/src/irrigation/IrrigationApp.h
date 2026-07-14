#pragma once

#include "IrrigationConfigStore.h"
#include "IrrigationEvents.h"
#include "WateringRecordStore.h"
#include "WateringController.h"

class IrrigationApp {
public:
    static IrrigationApp& instance();

    bool begin();
    void handle();

    bool baseReady() const;
    bool businessReady() const;
    WateringStartResult startWatering(const WateringRequest& request);
    bool stopWatering();
    WateringStatus wateringStatus() const;
    bool readLatestWateringRecords(uint32_t offset,
                                   uint32_t limit,
                                   WateringRecordStore::ReadCallback callback,
                                   void* user = nullptr);
    Esp32BaseRecordStore::RecordReadResult readWateringRecordById(
        uint32_t recordId,
        StoredWateringRecord& record);
    bool clearWateringRecords(bool userConfirmed);
    bool readWateringRecordStoreStatus(Esp32BaseRecordStore::StoreStatus& status) const;
    bool recordStorageFault() const;
    bool eventStorageFault() const;

private:
    IrrigationApp();
    void advanceBusiness();
    void consumeFinishedWatering();
    void reportRecordStorageFault(IrrigationEvents::ReasonCode operation);
    static void afterFormatFs(const Esp32BaseWeb::FormatFsResult& result, void* user);
    void handleAfterFormatFs(const Esp32BaseWeb::FormatFsResult& result);

    bool started_ = false;
    bool baseReady_ = false;
    bool businessReady_ = false;
    bool wateringStartTimeValid_ = false;
    bool recordStorageFault_ = false;
    bool eventStorageFault_ = false;
    IrrigationConfigStore configStore_;
    WateringController wateringController_;
    WateringRecordStore wateringRecordStore_;
    Esp32BaseRecordStore::RecordStartTime wateringStartTime_{};
};
