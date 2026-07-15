#include "IrrigationApp.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>

#include <climits>

#include "BoardHardware.h"
#include "BoardPins.h"
#include "IrrigationWeb.h"

namespace {

constexpr const char* kFirmwareName = "esp32-irrigation";
constexpr const char* kFirmwareVersion = "0.5.0";
constexpr const char* kDefaultWebUser = "admin";
constexpr const char* kDefaultWebPassword = "admin";

struct LatestTrustedEpoch {
    uint32_t value = 0;
};

void collectWateringEpoch(const StoredWateringRecord& record, void* user) {
    auto* latest = static_cast<LatestTrustedEpoch*>(user);
    uint32_t epoch = 0;
    if (latest && Esp32BaseRecordStore::resolveCompletedEpoch(record.timing, epoch) &&
        epoch > latest->value) {
        latest->value = epoch;
    }
}

void collectEventEpoch(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    auto* latest = static_cast<LatestTrustedEpoch*>(user);
    uint32_t epoch = 0;
    if (latest && Esp32BaseRecordStore::resolveCompletedEpoch(event.timing, epoch) &&
        epoch > latest->value) {
        latest->value = epoch;
    }
}

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
    Esp32BaseWeb::setDefaultAuth(kDefaultWebUser, kDefaultWebPassword);
    Esp32BaseWeb::setAfterFormatFsCallback(afterFormatFs, this);
    if (!IrrigationParameterConfig::registerFields(parameterConfigSaved, this) ||
        !IrrigationWeb::registerRoutes(*this)) {
        hardware.safeShutdown();
        return false;
    }

    baseReady_ = Esp32Base::begin();
    if (!baseReady_) {
        hardware.safeShutdown();
        return false;
    }
    if (!configStore_.begin()) {
        hardware.safeShutdown();
        return false;
    }

    if (!applyStoredParameterConfig()) {
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

    const bool storeReady = wateringRecordStore_.begin();
    const bool storeRegistered =
        Esp32BaseWeb::registerBusinessRecordStore(wateringRecordStore_.baseStore());
    wateringRecordStoreRegistered_ = storeRegistered;
    recordStorageFault_ = !storeReady || !storeRegistered ||
                          wateringRecordStore_.state() !=
                              Esp32BaseRecordStore::StoreState::Ready;
    if (!storeReady) {
        ESP32BASE_LOG_E("irrigation",
                        "watering_record_store_begin_failed state=%s error=%s",
                        Esp32BaseRecordStore::storeStateName(wateringRecordStore_.state()),
                        wateringRecordStore_.lastErrorReason());
    }
    if (!storeRegistered) {
        ESP32BASE_LOG_E("irrigation", "watering_record_store_registration_failed");
    }
    if (storeReady) {
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

    schedulerStorageFault_ = !wateringScheduler_.begin(wateringSchedulerStore_);
    wateringScheduler_.setCallbacks(startScheduledWatering, handleSchedulerEvent, this);
    if (schedulerStorageFault_) {
        ESP32BASE_LOG_E("irrigation", "watering_scheduler_store_unavailable");
    }
    aliveCheckpoint_.begin();
    LatestTrustedEpoch latestTrusted;
    latestTrusted.value = aliveCheckpoint_.lastKnownAliveEpoch();
    wateringRecordStore_.readLatest(0, 1, collectWateringEpoch, &latestTrusted);
    Esp32BaseAppEvents::readLatest(0, 1, collectEventEpoch, &latestTrusted);
    wateringScheduler_.setTrustedEpochBaseline(latestTrusted.value);

    businessReady_ = true;
    resetUnexpectedFlowMonitor(millis());
    ESP32BASE_LOG_I("irrigation", "business_ready records_fault=%s events_fault=%s scheduler_fault=%s",
                    recordStorageFault_ ? "yes" : "no",
                    Esp32BaseAppEvents::isEventStoreReady() ? "no" : "yes",
                    schedulerStorageFault_ ? "yes" : "no");
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

WateringStartResult IrrigationApp::startManualWatering(
    const std::array<uint16_t, BoardPins::kZoneCount>& zoneDurationMinutes) {
    const IrrigationConfig* config = configStore_.current();
    if (!config) {
        return WateringStartResult::NotReady;
    }
    WateringRequest request{};
    request.source = WateringSource::ManualZones;
    request.purpose = WateringPurpose::Normal;
    for (uint8_t index = 0; index < config->zones.size(); ++index) {
        if (zoneDurationMinutes[index] == 0) {
            continue;
        }
        if (!config->zones[index].enabled || zoneDurationMinutes[index] > 120) {
            return WateringStartResult::InvalidRequest;
        }
        request.steps[request.stepCount++] = {
            config->zones[index].id,
            static_cast<uint32_t>(zoneDurationMinutes[index]) * 60U,
        };
    }
    return startWatering(request);
}

bool IrrigationApp::stopWatering() {
    return businessReady_ && wateringController_.stop(millis());
}

WateringStatus IrrigationApp::wateringStatus() const {
    return wateringController_.status();
}

FlowHistorySnapshot IrrigationApp::wateringFlowHistory() const {
    return wateringController_.flowHistory();
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

bool IrrigationApp::readWateringRecordStoreStatus(
    Esp32BaseRecordStore::StoreStatus& status) const {
    return wateringRecordStore_.readStatus(status);
}

bool IrrigationApp::readLatestEvents(uint32_t offset,
                                     uint32_t limit,
                                     IrrigationEvents::ReadCallback callback,
                                     void* user) const {
    return events_.readLatest(offset, limit, callback, user);
}

bool IrrigationApp::readEventStatus(Esp32BaseAppEvents::AppEventsStatus& status) const {
    return events_.readStatus(status);
}

bool IrrigationApp::recordStorageFault() const {
    return recordStorageFault_;
}

bool IrrigationApp::eventStorageFault() const {
    return events_.storageFault();
}

bool IrrigationApp::schedulerStorageFault() const {
    return schedulerStorageFault_;
}

bool IrrigationApp::checkpointStorageFault() const {
    return aliveCheckpoint_.storageFault();
}

uint32_t IrrigationApp::lastKnownAliveEpoch() const {
    return aliveCheckpoint_.lastKnownAliveEpoch();
}

bool IrrigationApp::unexpectedFlowAlarm() const {
    return unexpectedFlowMonitor_.alarmActive();
}

AutomaticWateringState IrrigationApp::automaticWateringState() const {
    return wateringScheduler_.automaticState();
}

NextAutomaticWatering IrrigationApp::nextAutomaticWatering() const {
    const IrrigationConfig* config = configStore_.current();
    if (!config) {
        return {NextAutomaticWateringStatus::TimeUnavailable, 0, 0};
    }
    return wateringScheduler_.nextAutomaticWatering(
        *config, Esp32BaseTime::snapshot().epochSec);
}

WateringScheduler::TimeState IrrigationApp::schedulerTimeState() const {
    return wateringScheduler_.timeState();
}

bool IrrigationApp::pauseAutomaticWateringIndefinitely() {
    return businessReady_ && wateringScheduler_.pauseIndefinitely();
}

bool IrrigationApp::pauseAutomaticWateringUntil(uint32_t resumeAtEpoch) {
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    return businessReady_ && wateringScheduler_.pauseUntil(resumeAtEpoch,
                                                           now.synced &&
                                                               wateringScheduler_.timeState() ==
                                                                   WateringScheduler::TimeState::Ready,
                                                           now.epochSec);
}

bool IrrigationApp::resumeAutomaticWatering() {
    return businessReady_ && wateringScheduler_.resumeManually();
}

WateringStartResult IrrigationApp::startFlowCalibration(
    uint8_t zoneId,
    uint16_t maximumDurationMinutes) {
    if (flowCalibrationService_.hasPendingMeasurement() ||
        flowCalibrationService_.sampleCount() >= FlowCalibrationService::kMaximumSamples ||
        !BoardPins::isValidZoneId(zoneId) || maximumDurationMinutes == 0 ||
        maximumDurationMinutes > 10) {
        return WateringStartResult::InvalidRequest;
    }
    WateringRequest request{};
    request.source = WateringSource::ManualZones;
    request.purpose = WateringPurpose::FlowCalibration;
    request.stepCount = 1;
    request.steps[0] = {zoneId, static_cast<uint32_t>(maximumDurationMinutes) * 60U};
    return startWatering(request);
}

bool IrrigationApp::submitFlowCalibrationMeasurement(uint32_t measuredWaterMl) {
    return businessReady_ && !wateringController_.status().active &&
           flowCalibrationService_.addPendingMeasurement(measuredWaterMl, trustedEpoch());
}

bool IrrigationApp::markFlowCalibrationSampleInvalid() {
    return businessReady_ && !wateringController_.status().active &&
           flowCalibrationService_.markPendingInvalid();
}

bool IrrigationApp::updateFlowCalibrationMeasurement(uint8_t index,
                                                     uint32_t measuredWaterMl) {
    return businessReady_ && !wateringController_.status().active &&
           flowCalibrationService_.updateMeasurement(index, measuredWaterMl, trustedEpoch());
}

bool IrrigationApp::deleteFlowCalibrationSample(uint8_t index) {
    return businessReady_ && !wateringController_.status().active &&
           flowCalibrationService_.deleteSample(index, trustedEpoch());
}

bool IrrigationApp::applyFlowCalibrationResult() {
    const uint32_t coefficient = flowCalibrationService_.combinedPulsesPerLiterX100();
    if (!businessReady_ || wateringController_.status().active ||
        flowCalibrationService_.hasPendingMeasurement() ||
        !flowCalibrationService_.resultReady() || coefficient == 0) {
        return false;
    }
    FlowMeterConfig parameters{};
    parameters.pulsesPerLiterX100 = coefficient;
    parameters.calibrationStartupPulseCount =
        flowCalibrationService_.combinedStartupPulseCount();
    parameters.calibrationStartupWaterMl =
        flowCalibrationService_.combinedStartupWaterMl();
    parameters.calibrationSteadyFlowMlPerMinute =
        flowCalibrationService_.combinedSteadyFlowMlPerMinute();
    if (!saveFlowCalibrationParameters(parameters)) {
        return false;
    }
    flowCalibrationService_.markResultApplied(trustedEpoch(), coefficient);
    return true;
}

bool IrrigationApp::saveFlowCalibrationParameters(
    const FlowMeterConfig& parameters) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || wateringController_.status().active || !current ||
        flowCalibrationService_.hasPendingMeasurement()) {
        return false;
    }
    if (current->flowMeter.pulsesPerLiterX100 == parameters.pulsesPerLiterX100 &&
        current->flowMeter.calibrationStartupPulseCount ==
            parameters.calibrationStartupPulseCount &&
        current->flowMeter.calibrationStartupWaterMl ==
            parameters.calibrationStartupWaterMl &&
        current->flowMeter.calibrationSteadyFlowMlPerMinute ==
            parameters.calibrationSteadyFlowMlPerMinute) {
        return true;
    }
    const bool parametersSaved =
        IrrigationParameterConfig::saveFlowCalibrationParameters(parameters);
    if (!parametersSaved || !applyStoredParameterConfig()) {
        return false;
    }
    wateringScheduler_.rebaseTimeCheck();
    resetUnexpectedFlowMonitor(millis());
    events_.recordFlowCalibrationSaved(parameters.pulsesPerLiterX100);
    return true;
}

void IrrigationApp::resetFlowCalibration() {
    if (!wateringController_.status().active) {
        flowCalibrationService_.clear();
    }
}

const FlowCalibrationService& IrrigationApp::flowCalibration() const {
    return flowCalibrationService_;
}

WateringStartResult IrrigationApp::startZoneFlowLearning(uint8_t zoneId) {
    if (pendingLearnedZoneId_ != 0 || !BoardPins::isValidZoneId(zoneId)) {
        return WateringStartResult::InvalidRequest;
    }
    WateringRequest request{};
    request.source = WateringSource::ManualZones;
    request.purpose = WateringPurpose::ZoneFlowLearning;
    request.stepCount = 1;
    request.steps[0] = {zoneId, 10U * 60U};
    return startWatering(request);
}

bool IrrigationApp::saveLearnedZoneFlow(uint32_t expectedConfigRevision) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || wateringController_.status().active || !current ||
        !BoardPins::isValidZoneId(pendingLearnedZoneId_) ||
        pendingLearnedFlowMlPerMinute_ == 0) {
        return false;
    }
    IrrigationConfig next = *current;
    next.zones[BoardPins::zoneIndex(pendingLearnedZoneId_)].learnedFlowMlPerMinute =
        pendingLearnedFlowMlPerMinute_;
    if (!configStore_.save(next, expectedConfigRevision)) {
        return false;
    }
    wateringScheduler_.rebaseTimeCheck();
    events_.recordZoneFlowSaved(pendingLearnedZoneId_, pendingLearnedFlowMlPerMinute_);
    discardLearnedZoneFlow();
    return true;
}

uint8_t IrrigationApp::pendingLearnedZoneId() const {
    return pendingLearnedZoneId_;
}

uint32_t IrrigationApp::pendingLearnedFlowMlPerMinute() const {
    return pendingLearnedFlowMlPerMinute_;
}

void IrrigationApp::discardLearnedZoneFlow() {
    if (!wateringController_.status().active) {
        pendingLearnedZoneId_ = 0;
        pendingLearnedFlowMlPerMinute_ = 0;
    }
}

const IrrigationConfig* IrrigationApp::configuration() const {
    return configStore_.current();
}

bool IrrigationApp::saveConfiguration(const IrrigationConfig& proposed,
                                      uint32_t expectedRevision,
                                      IrrigationEvents::ConfigurationChange change,
                                      uint8_t objectId) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || !current) {
        return false;
    }
    BoardHardware& hardware = BoardHardware::instance();
    const bool active = wateringController_.status().active;
    const bool frequencyChanged = proposed.valveDrive.pwmFrequencyHz !=
                                  current->valveDrive.pwmFrequencyHz;
    bool hardwareChanged = false;
    if (frequencyChanged && !active) {
        if (!hardware.configureValvePwmFrequency(proposed.valveDrive.pwmFrequencyHz)) {
            return false;
        }
        hardwareChanged = true;
    }
    const uint32_t previousFrequency = current->valveDrive.pwmFrequencyHz;
    if (!configStore_.save(proposed, expectedRevision)) {
        if (hardwareChanged && !hardware.configureValvePwmFrequency(previousFrequency)) {
            businessReady_ = false;
            hardware.safeShutdown();
        }
        return false;
    }
    pendingPwmReconfigure_ = frequencyChanged && active;
    wateringScheduler_.rebaseTimeCheck();
    if (!active) {
        resetUnexpectedFlowMonitor(millis());
    }
    events_.recordConfigurationChanged(change, objectId);
    return true;
}

const char* IrrigationApp::configurationError() const {
    return configStore_.lastError();
}

void IrrigationApp::advanceBusiness() {
    if (!businessReady_) {
        BoardHardware::instance().safeShutdown();
        return;
    }
    const uint32_t nowMs = millis();
    if (!eventConditionsInitialized_) {
        events_.syncStorageStatus();
        refreshRtcCondition(nowMs, true);
        eventConditionsInitialized_ = true;
    }
    refreshRtcCondition(nowMs, false);
    wateringController_.handle(nowMs);
    consumeFinishedWatering();
    const IrrigationConfig* config = configStore_.current();
    if (config && !wateringController_.status().active) {
        unexpectedFlowMonitor_.observe(nowMs, BoardHardware::instance().flowPulseCount());
    }
    if (config) {
        const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
        if (wateringScheduler_.storageReady()) {
            wateringScheduler_.handle(*config,
                                      now.synced,
                                      now.source == Esp32BaseTime::SOURCE_NTP,
                                      now.epochSec);
        }
        observeEventConditions(nowMs, now);
        Esp32BaseRecordStore::StoreStatus recordStatus{};
        Esp32BaseAppEvents::AppEventsStatus eventStatus{};
        if (wateringRecordStore_.readStatus(recordStatus) &&
            Esp32BaseAppEvents::readStatus(eventStatus)) {
            const uint64_t activitySequence =
                (static_cast<uint64_t>(recordStatus.nextRecordId) << 32U) |
                eventStatus.eventStore.nextRecordId;
            aliveCheckpoint_.handle(now,
                                    config->timeSafety.aliveCheckpointHours,
                                    wateringController_.status().active,
                                    activitySequence);
        }
    }
}

void IrrigationApp::consumeFinishedWatering() {
    const WateringSessionSummary* summary = wateringController_.finishedSession();
    if (!summary) {
        return;
    }

    if (summary->purpose == WateringPurpose::FlowCalibration) {
        flowCalibrationService_.captureFinishedSession(*summary, trustedEpoch());
    } else if (summary->purpose == WateringPurpose::ZoneFlowLearning &&
               summary->zoneCount == 1 &&
               summary->zones[0].suggestedFlowMlPerMinute != 0) {
        pendingLearnedZoneId_ = summary->zones[0].zoneId;
        pendingLearnedFlowMlPerMinute_ = summary->zones[0].suggestedFlowMlPerMinute;
    }

    if (summary->purpose == WateringPurpose::Normal && summary->anyFlowEstablished) {
        if (!wateringStartTimeValid_ ||
            !wateringRecordStore_.appendCompleted(wateringStartTime_, *summary)) {
            recordStorageFault_ = true;
            events_.recordWateringRecordSaveFailed(
                wateringStartTimeValid_ ? IrrigationEvents::ReasonCode::RecordAppendFailed
                                        : IrrigationEvents::ReasonCode::RecordStartTimeUnavailable,
                wateringRecordStore_.state(),
                wateringRecordStore_.lastError());
        } else {
            recordStorageFault_ = wateringRecordStore_.state() !=
                                  Esp32BaseRecordStore::StoreState::Ready;
        }
    }

    events_.recordAbnormalWateringStop(*summary);
    events_.recordFlowDeviationEvents(*summary);
    resetUnexpectedFlowMonitor(millis());
    applyPendingHardwareConfiguration();
    wateringController_.clearFinishedSession();
    wateringStartTime_ = {};
    wateringStartTimeValid_ = false;
}

uint32_t IrrigationApp::trustedEpoch() const {
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    return now.synced ? now.epochSec : 0;
}

void IrrigationApp::applyPendingHardwareConfiguration() {
    if (!pendingPwmReconfigure_ || wateringController_.status().active) {
        return;
    }
    const IrrigationConfig* config = configStore_.current();
    if (!config || !BoardHardware::instance().configureValvePwmFrequency(
                       config->valveDrive.pwmFrequencyHz)) {
        businessReady_ = false;
        BoardHardware::instance().safeShutdown();
        return;
    }
    pendingPwmReconfigure_ = false;
}

void IrrigationApp::parameterConfigSaved(void* user) {
    if (user) static_cast<IrrigationApp*>(user)->handleParameterConfigSaved();
}

bool IrrigationApp::applyStoredParameterConfig() {
    const IrrigationConfig* current = configStore_.current();
    if (!current) return false;
    parameterConfigScratch_ = *current;
    return IrrigationParameterConfig::applyStored(parameterConfigScratch_) &&
           configStore_.applyRuntimeParameters(parameterConfigScratch_);
}

void IrrigationApp::handleParameterConfigSaved() {
    const IrrigationConfig* current = configStore_.current();
    if (!current) return;
    parameterConfigScratch_ = *current;
    if (!IrrigationParameterConfig::applyStored(parameterConfigScratch_)) {
        businessReady_ = false;
        BoardHardware::instance().safeShutdown();
        return;
    }
    const bool frequencyChanged = parameterConfigScratch_.valveDrive.pwmFrequencyHz !=
                                  current->valveDrive.pwmFrequencyHz;
    const bool active = wateringController_.status().active;
    if (frequencyChanged && !active &&
        !BoardHardware::instance().configureValvePwmFrequency(
            parameterConfigScratch_.valveDrive.pwmFrequencyHz)) {
        businessReady_ = false;
        BoardHardware::instance().safeShutdown();
        return;
    }
    if (!configStore_.applyRuntimeParameters(parameterConfigScratch_)) {
        businessReady_ = false;
        BoardHardware::instance().safeShutdown();
        return;
    }
    pendingPwmReconfigure_ = frequencyChanged && active;
    wateringScheduler_.rebaseTimeCheck();
    if (!active) resetUnexpectedFlowMonitor(millis());
    events_.recordConfigurationChanged(
        IrrigationEvents::ConfigurationChange::SystemParametersUpdated);
}

void IrrigationApp::resetUnexpectedFlowMonitor(uint32_t nowMs) {
    const IrrigationConfig* config = configStore_.current();
    if (!config) {
        return;
    }
    unexpectedFlowMonitor_.begin(
        nowMs,
        BoardHardware::instance().flowPulseCount(),
        config->flowProtection.unexpectedFlowDelaySec,
        config->flowProtection.unexpectedFlowWindowSec,
        config->flowProtection.unexpectedFlowPulseCount);
    events_.observeClosedValveFlow(Esp32BaseAppEvents::ObservedConditionState::Unknown,
                                   0,
                                   config->flowProtection.unexpectedFlowWindowSec);
}

void IrrigationApp::refreshRtcCondition(uint32_t nowMs, bool force) {
    constexpr uint32_t kRtcRefreshIntervalMs = 60000U;
    if (!force && rtcObservationInitialized_ &&
        static_cast<uint32_t>(nowMs - lastRtcRefreshMs_) < kRtcRefreshIntervalMs) {
        return;
    }
    if (rtcObservationInitialized_) Esp32BaseRtc::refresh();
    rtcObservationInitialized_ = true;
    lastRtcRefreshMs_ = nowMs;
    events_.observeRtcAvailability(Esp32BaseRtc::isAvailable(),
                                   static_cast<uint8_t>(Esp32BaseRtc::status()));
}

void IrrigationApp::observeEventConditions(uint32_t nowMs,
                                           const Esp32BaseTime::Snapshot& now) {
    events_.observeTrustedTime(now.synced);
    Esp32BaseAppEvents::ObservedConditionState rollbackState =
        Esp32BaseAppEvents::ObservedConditionState::Unknown;
    if (wateringScheduler_.timeState() == WateringScheduler::TimeState::Ready) {
        rollbackState = Esp32BaseAppEvents::ObservedConditionState::Inactive;
    } else if (wateringScheduler_.timeState() == WateringScheduler::TimeState::RtcRollback) {
        rollbackState = Esp32BaseAppEvents::ObservedConditionState::Active;
    }
    events_.observeRtcRollback(rollbackState);

    const IrrigationConfig* config = configStore_.current();
    Esp32BaseAppEvents::ObservedConditionState flowState =
        Esp32BaseAppEvents::ObservedConditionState::Unknown;
    if (config && !wateringController_.status().active &&
        unexpectedFlowMonitor_.observationReady(nowMs)) {
        flowState = unexpectedFlowMonitor_.alarmActive()
                        ? Esp32BaseAppEvents::ObservedConditionState::Active
                        : Esp32BaseAppEvents::ObservedConditionState::Inactive;
    }
    events_.observeClosedValveFlow(
        flowState,
        unexpectedFlowMonitor_.observedPulseCount(),
        config ? config->flowProtection.unexpectedFlowWindowSec : 0);
}

WateringStartResult IrrigationApp::startScheduledWatering(const WateringRequest& request,
                                                          void* user) {
    return user ? static_cast<IrrigationApp*>(user)->startWatering(request)
                : WateringStartResult::NotReady;
}

void IrrigationApp::handleSchedulerEvent(WateringScheduler::Event event,
                                         uint8_t planId,
                                         int32_t value,
                                         void* user) {
    if (user) {
        static_cast<IrrigationApp*>(user)->reportSchedulerEvent(event, planId, value);
    }
}

void IrrigationApp::reportSchedulerEvent(WateringScheduler::Event event,
                                         uint8_t planId,
                                         int32_t) {
    switch (event) {
        case WateringScheduler::Event::PausedIndefinitely:
            events_.recordAutomaticWateringPaused(true, 0);
            break;
        case WateringScheduler::Event::PausedUntil:
            events_.recordAutomaticWateringPaused(
                false, wateringScheduler_.automaticState().resumeAtEpoch);
            break;
        case WateringScheduler::Event::ResumedManually:
            events_.recordAutomaticWateringResumed(false);
            break;
        case WateringScheduler::Event::ResumedAutomatically:
            events_.recordAutomaticWateringResumed(true);
            break;
        case WateringScheduler::Event::PlanSkippedBusy:
            events_.recordAutomaticPlanSkipped(planId, true);
            break;
        case WateringScheduler::Event::PlanStartRejected:
            events_.recordAutomaticPlanSkipped(planId, false);
            break;
        case WateringScheduler::Event::StorageFault:
            schedulerStorageFault_ = true;
            events_.recordSchedulerStateSaveFailed();
            break;
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
        events_.syncStorageStatus();
        return;
    }

    bool configReady = configStore_.begin();
    if (configReady) configReady = applyStoredParameterConfig();
    const IrrigationConfig* config = configStore_.current();
    const bool pwmReady = configReady && config &&
                          hardware.configureValvePwmFrequency(
                              config->valveDrive.pwmFrequencyHz);
    const bool schedulerCleared = wateringSchedulerStore_.clear();
    wateringScheduler_.setCallbacks(nullptr, nullptr, nullptr);
    const bool schedulerLoaded = wateringScheduler_.begin(wateringSchedulerStore_);
    wateringScheduler_.setCallbacks(startScheduledWatering, handleSchedulerEvent, this);
    const bool schedulerReady = schedulerCleared && schedulerLoaded;
    if (!schedulerReady) {
        wateringScheduler_.disable();
    }
    const bool checkpointReady = aliveCheckpoint_.begin();
    schedulerStorageFault_ = !schedulerReady;
    Esp32BaseRecordStore::StoreStatus recordStatus{};
    const bool recordStatusReady = wateringRecordStore_.readStatus(recordStatus);
    const bool recordsReady = wateringRecordStoreRegistered_ &&
                              result.businessRecordStoresReloadSuccess &&
                              recordStatusReady &&
                              recordStatus.state == Esp32BaseRecordStore::StoreState::Ready;
    events_.syncStorageStatus();
    recordStorageFault_ = !recordsReady;
    businessReady_ = configReady && pwmReady;
    if (businessReady_) {
        resetUnexpectedFlowMonitor(millis());
    }
    if (!businessReady_) {
        hardware.safeShutdown();
    }
    ESP32BASE_LOG_W("irrigation",
                    "after_format_reinitialized business_ready=%s records_ready=%s scheduler_ready=%s checkpoint_ready=%s",
                    businessReady_ ? "yes" : "no",
                    recordsReady ? "yes" : "no",
                    schedulerReady ? "yes" : "no",
                    checkpointReady ? "yes" : "no");
}
