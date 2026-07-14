#include "IrrigationApp.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>

#include "BoardHardware.h"
#include "BoardPins.h"

namespace {

constexpr const char* kFirmwareName = "esp32-irrigation";
constexpr const char* kFirmwareVersion = "0.2.0";

}  // namespace

IrrigationApp::IrrigationApp() : wateringController_(BoardHardware::instance()) {}

IrrigationApp& IrrigationApp::instance() {
    static IrrigationApp app;
    return app;
}

bool IrrigationApp::begin() {
    if (started_) {
        return baseReady_;
    }
    started_ = true;

    // This must remain the first hardware operation of application startup.
    BoardHardware& hardware = BoardHardware::instance();
    const bool hardwareReady = hardware.begin(20000);

    Serial.begin(115200);
    if (!hardwareReady) {
        hardware.safeShutdown();
        return false;
    }

    Wire.begin(BoardPins::kI2cSdaPin, BoardPins::kI2cSclPin);
    Esp32Base::setFirmwareInfo(kFirmwareName, kFirmwareVersion);
    Esp32BaseRtc::configure(Wire);
    Esp32BaseWeb::setAfterFormatFsCallback(afterFormatFs, this);

    baseReady_ = Esp32Base::begin();
    if (!baseReady_) {
        hardware.safeShutdown();
        return false;
    }

    if (!configStore_.begin()) {
        hardware.safeShutdown();
        return false;
    }

    const IrrigationConfig* config = configStore_.current();
    if (!config ||
        (config->valveDrive.pwmFrequencyHz != 20000U &&
         !hardware.configureValvePwmFrequency(config->valveDrive.pwmFrequencyHz))) {
        hardware.safeShutdown();
        return false;
    }

    if (!wateringRecordStore_.begin()) {
        recordStorageFault_ = true;
        reportRecordStorageFault(IrrigationEvents::ReasonCode::RecordStoreBegin);
        ESP32BASE_LOG_E("irrigation",
                        "watering_record_store_begin_failed state=%s error=%s",
                        Esp32BaseRecordStore::storeStateName(wateringRecordStore_.state()),
                        wateringRecordStore_.lastErrorReason());
    } else {
        recordStorageFault_ = wateringRecordStore_.state() !=
                              Esp32BaseRecordStore::StoreState::Ready;
        Esp32BaseRecordStore::StoreStatus status;
        wateringRecordStore_.readStatus(status);
        ESP32BASE_LOG_I("irrigation",
                        "watering_record_store_ready state=%s records=%lu capacity=%lu slot_bytes=%lu max_bytes=%lu",
                        Esp32BaseRecordStore::storeStateName(status.state),
                        static_cast<unsigned long>(status.recordCount),
                        static_cast<unsigned long>(status.capacity),
                        static_cast<unsigned long>(status.slotSizeBytes),
                        static_cast<unsigned long>(status.maximumStoreBytes));
    }

    businessReady_ = true;
    ESP32BASE_LOG_I("irrigation", "business_ready records_fault=%s events_fault=%s",
                    recordStorageFault_ ? "yes" : "no",
                    eventStorageFault_ ? "yes" : "no");
    return true;
}

void IrrigationApp::handle() {
    if (!started_ || !baseReady_) {
        BoardHardware::instance().safeShutdown();
        return;
    }

    advanceBusiness();
    Esp32Base::handle();
}

bool IrrigationApp::baseReady() const {
    return baseReady_;
}

bool IrrigationApp::businessReady() const {
    return businessReady_;
}

WateringStartResult IrrigationApp::startWatering(const WateringRequest& request) {
    if (!businessReady_) {
        return WateringStartResult::NotReady;
    }
    if (wateringController_.status().active) {
        return WateringStartResult::Busy;
    }
    if (wateringController_.finishedSession()) {
        return WateringStartResult::PreviousResultPending;
    }
    const IrrigationConfig* config = configStore_.current();
    if (!config) {
        return WateringStartResult::NotReady;
    }

    Esp32BaseRecordStore::RecordStartTime startTime;
    const bool captured = wateringRecordStore_.captureStartTime(startTime);
    const WateringStartResult result = wateringController_.start(request, *config, millis());
    if (result == WateringStartResult::Started) {
        wateringStartTime_ = startTime;
        wateringStartTimeValid_ = captured;
    }
    return result;
}

bool IrrigationApp::stopWatering() {
    return businessReady_ && wateringController_.stop(millis());
}

WateringStatus IrrigationApp::wateringStatus() const {
    return wateringController_.status();
}

bool IrrigationApp::readLatestWateringRecords(uint32_t offset,
                                              uint32_t limit,
                                              WateringRecordStore::ReadCallback callback,
                                              void* user) {
    return wateringRecordStore_.readLatest(offset, limit, callback, user);
}

Esp32BaseRecordStore::RecordReadResult IrrigationApp::readWateringRecordById(
    uint32_t recordId,
    StoredWateringRecord& record) {
    return wateringRecordStore_.readById(recordId, record);
}

bool IrrigationApp::clearWateringRecords(bool userConfirmed) {
    if (!businessReady_ || wateringController_.status().active || !userConfirmed) {
        return false;
    }
    const bool cleared = wateringRecordStore_.clear(true);
    recordStorageFault_ = !cleared ||
                          wateringRecordStore_.state() != Esp32BaseRecordStore::StoreState::Ready;
    if (!cleared) {
        reportRecordStorageFault(IrrigationEvents::ReasonCode::RecordClearFailed);
    }
    return cleared;
}

bool IrrigationApp::readWateringRecordStoreStatus(
    Esp32BaseRecordStore::StoreStatus& status) const {
    return wateringRecordStore_.readStatus(status);
}

bool IrrigationApp::recordStorageFault() const {
    return recordStorageFault_;
}

bool IrrigationApp::eventStorageFault() const {
    return eventStorageFault_;
}

void IrrigationApp::advanceBusiness() {
    wateringController_.handle(millis());
    consumeFinishedWatering();
}

void IrrigationApp::consumeFinishedWatering() {
    const WateringSessionSummary* summary = wateringController_.finishedSession();
    if (!summary) {
        return;
    }

    if (summary->purpose == WateringPurpose::Normal && summary->anyFlowEstablished) {
        if (!wateringStartTimeValid_ ||
            !wateringRecordStore_.appendCompleted(wateringStartTime_, *summary)) {
            recordStorageFault_ = true;
            reportRecordStorageFault(wateringStartTimeValid_
                                         ? IrrigationEvents::ReasonCode::RecordAppendFailed
                                         : IrrigationEvents::ReasonCode::RecordStartTimeUnavailable);
        } else {
            recordStorageFault_ = wateringRecordStore_.state() !=
                                  Esp32BaseRecordStore::StoreState::Ready;
        }
    }

    if (!IrrigationEvents::appendAbnormalWateringStop(*summary)) {
        eventStorageFault_ = true;
    }
    wateringController_.clearFinishedSession();
    wateringStartTime_ = {};
    wateringStartTimeValid_ = false;
}

void IrrigationApp::reportRecordStorageFault(IrrigationEvents::ReasonCode operation) {
    if (!IrrigationEvents::appendRecordStorageFault(operation,
                                                    wateringRecordStore_.state(),
                                                    wateringRecordStore_.lastError())) {
        eventStorageFault_ = true;
    }
}

void IrrigationApp::afterFormatFs(const Esp32BaseWeb::FormatFsResult& result, void* user) {
    if (user) {
        static_cast<IrrigationApp*>(user)->handleAfterFormatFs(result);
    }
}

void IrrigationApp::handleAfterFormatFs(const Esp32BaseWeb::FormatFsResult& result) {
    BoardHardware& hardware = BoardHardware::instance();
    wateringController_.abortForMaintenance(millis());
    hardware.safeShutdown();
    businessReady_ = false;

    if (!result.mountSuccess) {
        recordStorageFault_ = true;
        eventStorageFault_ = true;
        return;
    }

    const bool configReady = configStore_.begin();
    const IrrigationConfig* config = configStore_.current();
    const bool pwmReady = configReady && config &&
                          hardware.configureValvePwmFrequency(
                              config->valveDrive.pwmFrequencyHz);
    const bool recordsReady = wateringRecordStore_.reload();
    eventStorageFault_ = !Esp32BaseAppEvents::isReady();
    recordStorageFault_ = !recordsReady ||
                          wateringRecordStore_.state() != Esp32BaseRecordStore::StoreState::Ready;
    if (!recordsReady) {
        reportRecordStorageFault(IrrigationEvents::ReasonCode::RecordStoreReload);
    }
    businessReady_ = configReady && pwmReady;
    if (!businessReady_) {
        hardware.safeShutdown();
    }
    ESP32BASE_LOG_W("irrigation",
                    "after_format_reinitialized business_ready=%s records_ready=%s",
                    businessReady_ ? "yes" : "no",
                    recordsReady ? "yes" : "no");
}
