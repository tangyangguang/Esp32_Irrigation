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
constexpr const char* kFirmwareVersion = "0.6.0";
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
        pendingLowFlowEvents_ = {};
        pendingHighFlowEvents_ = {};
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

IrrigationEvents::ConditionDisplayState IrrigationApp::eventConditionState(
    uint8_t conditionId) const {
    return events_.conditionState(conditionId);
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

bool IrrigationApp::unexpectedFlowObservationReady() const {
    return unexpectedFlowMonitor_.observationReady(millis());
}

uint16_t IrrigationApp::unexpectedFlowDelayRemainingSec() const {
    return unexpectedFlowMonitor_.delayRemainingSec(millis());
}

uint16_t IrrigationApp::unexpectedFlowWindowRemainingSec() const {
    return unexpectedFlowMonitor_.windowRemainingSec(millis());
}

uint16_t IrrigationApp::unexpectedFlowObservedWindowSec() const {
    const uint32_t durationMs =
        unexpectedFlowMonitor_.observedDurationMs(millis());
    return durationMs == 0
               ? 0
               : static_cast<uint16_t>((durationMs + 999U) / 1000U);
}

uint32_t IrrigationApp::unexpectedFlowObservedPulseCount() const {
    return unexpectedFlowMonitor_.observedPulseCount();
}

uint32_t IrrigationApp::unexpectedFlowEstimatedMlPerMinute() const {
    const IrrigationConfig* config = configStore_.current();
    const uint32_t durationMs =
        unexpectedFlowMonitor_.observedDurationMs(millis());
    const uint32_t pulseCount = unexpectedFlowMonitor_.observedPulseCount();
    if (!config || durationMs == 0 || pulseCount == 0) return 0;
    const uint64_t rate =
        (static_cast<uint64_t>(pulseCount) * 100000ULL +
         durationMs / 2U) /
        durationMs;
    uint32_t flowMlPerMinute = 0;
    FlowMonitor::pulseRateToFlowMlPerMinute(
        rate > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(rate),
        config->flowMeter.pulsesPerLiterX100,
        flowMlPerMinute);
    return flowMlPerMinute;
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
            parameters.calibrationStartupWaterMl) {
        return true;
    }
    const uint32_t previousCoefficientX100 =
        current->flowMeter.pulsesPerLiterX100;
    const bool parametersSaved =
        IrrigationParameterConfig::saveFlowCalibrationParameters(parameters);
    if (!parametersSaved || !applyStoredParameterConfig()) {
        return false;
    }
    wateringScheduler_.rebaseTimeCheck();
    resetUnexpectedFlowMonitor(millis());
    events_.recordFlowCalibrationSaved(
        previousCoefficientX100,
        parameters.pulsesPerLiterX100);
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
    if (!BoardPins::isValidZoneId(pendingLearnedZoneId_) ||
        pendingLearnedBaselinePulseRateX10000_ == 0) {
        return false;
    }
    if (!saveZoneBaselinePulseRate(pendingLearnedZoneId_,
                                   pendingLearnedBaselinePulseRateX10000_,
                                   expectedConfigRevision)) {
        return false;
    }
    discardLearnedZoneFlow();
    return true;
}

bool IrrigationApp::saveManualZoneBaselineFlow(
    uint8_t zoneId,
    uint32_t flowMlPerMinute,
    uint32_t expectedConfigRevision) {
    const IrrigationConfig* current = configStore_.current();
    if (!current || pendingLearnedZoneId_ != 0 ||
        flowMlPerMinute == 0 || flowMlPerMinute > 100000U) {
        return false;
    }
    uint32_t pulseRateX10000 = 0;
    if (!FlowMonitor::flowMlPerMinuteToPulseRateX10000(
            flowMlPerMinute,
            current->flowMeter.pulsesPerLiterX100,
            pulseRateX10000)) {
        return false;
    }
    uint32_t verifiedFlowMlPerMinute = 0;
    if (!FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
            pulseRateX10000,
            current->flowMeter.pulsesPerLiterX100,
            verifiedFlowMlPerMinute) ||
        verifiedFlowMlPerMinute != flowMlPerMinute) {
        return false;
    }
    return saveZoneBaselinePulseRate(
        zoneId, pulseRateX10000, expectedConfigRevision);
}

bool IrrigationApp::saveZoneBaselinePulseRate(
    uint8_t zoneId,
    uint32_t pulseRateX10000,
    uint32_t expectedConfigRevision) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || wateringController_.status().active || !current ||
        !BoardPins::isValidZoneId(zoneId) || pulseRateX10000 == 0) {
        return false;
    }
    IrrigationConfig next = *current;
    const uint32_t previousPulseRateX10000 =
        current->zones[BoardPins::zoneIndex(zoneId)].baselinePulseRateX10000;
    next.zones[BoardPins::zoneIndex(zoneId)].baselinePulseRateX10000 =
        pulseRateX10000;
    if (!configStore_.save(next, expectedConfigRevision)) {
        return false;
    }
    wateringScheduler_.rebaseTimeCheck();
    uint32_t savedFlowMlPerMinute = 0;
    uint32_t previousFlowMlPerMinute = 0;
    FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
        previousPulseRateX10000,
        next.flowMeter.pulsesPerLiterX100,
        previousFlowMlPerMinute);
    FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
        pulseRateX10000,
        next.flowMeter.pulsesPerLiterX100,
        savedFlowMlPerMinute);
    events_.recordZoneFlowSaved(
        zoneId, previousFlowMlPerMinute, savedFlowMlPerMinute);
    return true;
}

uint8_t IrrigationApp::pendingLearnedZoneId() const {
    return pendingLearnedZoneId_;
}

uint32_t IrrigationApp::pendingLearnedBaselinePulseRateX10000() const {
    return pendingLearnedBaselinePulseRateX10000_;
}

uint32_t IrrigationApp::pendingLearnedFlowMlPerMinute() const {
    const IrrigationConfig* current = configStore_.current();
    uint32_t flowMlPerMinute = 0;
    if (current) {
        FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
            pendingLearnedBaselinePulseRateX10000_,
            current->flowMeter.pulsesPerLiterX100,
            flowMlPerMinute);
    }
    return flowMlPerMinute;
}

bool IrrigationApp::clearLearnedZoneFlow(uint8_t zoneId,
                                         uint32_t expectedConfigRevision) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || wateringController_.status().active || !current ||
        pendingLearnedZoneId_ != 0 ||
        !BoardPins::isValidZoneId(zoneId) ||
        current->zones[BoardPins::zoneIndex(zoneId)].baselinePulseRateX10000 == 0) {
        return false;
    }
    IrrigationConfig next = *current;
    uint32_t previousFlowMlPerMinute = 0;
    FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
        current->zones[BoardPins::zoneIndex(zoneId)].baselinePulseRateX10000,
        current->flowMeter.pulsesPerLiterX100,
        previousFlowMlPerMinute);
    next.zones[BoardPins::zoneIndex(zoneId)].baselinePulseRateX10000 = 0;
    if (!configStore_.save(next, expectedConfigRevision)) {
        return false;
    }
    wateringScheduler_.rebaseTimeCheck();
    events_.recordZoneFlowSaved(zoneId, previousFlowMlPerMinute, 0);
    return true;
}

void IrrigationApp::discardLearnedZoneFlow() {
    if (!wateringController_.status().active) {
        pendingLearnedZoneId_ = 0;
        pendingLearnedBaselinePulseRateX10000_ = 0;
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
    reportNewFlowDeviationEvents();
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

void IrrigationApp::reportNewFlowDeviationEvents() {
    const WateringStatus status = wateringController_.status();
    if (status.purpose != WateringPurpose::Normal) return;
    for (uint8_t index = 0; index < status.stepCount &&
                            index < status.zones.size(); ++index) {
        const ZoneWateringSummary& zone = status.zones[index];
        if (!BoardPins::isValidZoneId(zone.zoneId)) continue;
        const uint8_t zoneIndex = BoardPins::zoneIndex(zone.zoneId);
        PendingFlowDeviationEvent& low = pendingLowFlowEvents_[zoneIndex];
        if (zone.lowFlowActive && !low.pending) {
            low.detectedFlowMlPerMinute = zone.lowFlowDetectedMlPerMinute;
            low.baselineFlowMlPerMinute = zone.baselineFlowMlPerMinute;
            low.pending = true;
        }
        PendingFlowDeviationEvent& high = pendingHighFlowEvents_[zoneIndex];
        if (zone.highFlowActive && !high.pending) {
            high.detectedFlowMlPerMinute = zone.highFlowDetectedMlPerMinute;
            high.baselineFlowMlPerMinute = zone.baselineFlowMlPerMinute;
            high.pending = true;
        }
    }
    if (status.active) {
        return;
    }
    for (uint8_t zoneIndex = 0; zoneIndex < BoardPins::kZoneCount; ++zoneIndex) {
        const uint8_t zoneId = static_cast<uint8_t>(zoneIndex + 1U);
        PendingFlowDeviationEvent& low = pendingLowFlowEvents_[zoneIndex];
        if (low.pending) {
            ZoneWateringSummary zone{};
            zone.zoneId = zoneId;
            zone.lowFlowDetectedMlPerMinute = low.detectedFlowMlPerMinute;
            zone.baselineFlowMlPerMinute = low.baselineFlowMlPerMinute;
            events_.recordFlowDeviationEvent(
                zone,
                IrrigationEvents::ReasonCode::LowFlow,
                status.lastStopReason == WateringStopReason::LowFlow,
                status.source,
                status.planId);
            low = {};
        }
        PendingFlowDeviationEvent& high = pendingHighFlowEvents_[zoneIndex];
        if (high.pending) {
            ZoneWateringSummary zone{};
            zone.zoneId = zoneId;
            zone.highFlowDetectedMlPerMinute = high.detectedFlowMlPerMinute;
            zone.baselineFlowMlPerMinute = high.baselineFlowMlPerMinute;
            events_.recordFlowDeviationEvent(
                zone,
                IrrigationEvents::ReasonCode::HighFlow,
                status.lastStopReason == WateringStopReason::HighFlow,
                status.source,
                status.planId);
            high = {};
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
               summary->zones[0].suggestedBaselinePulseRateX10000 != 0) {
        pendingLearnedZoneId_ = summary->zones[0].zoneId;
        pendingLearnedBaselinePulseRateX10000_ =
            summary->zones[0].suggestedBaselinePulseRateX10000;
    }

    if (summary->purpose == WateringPurpose::Normal) {
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
                                   config->flowProtection.unexpectedFlowWindowSec,
                                   config->flowProtection.unexpectedFlowPulseCount);
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
        config ? config->flowProtection.unexpectedFlowWindowSec : 0,
        config ? config->flowProtection.unexpectedFlowPulseCount : 0);
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

    const bool conditionHistoryReset = events_.resetConditionHistory();
    rtcObservationInitialized_ = false;
    eventConditionsInitialized_ = false;

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
    bool recordStatusReady = wateringRecordStore_.readStatus(recordStatus);
    bool recordStoreReady = recordStatusReady && recordStatus.ready &&
                            recordStatus.writable;
    if (!recordStoreReady) {
        ESP32BASE_LOG_W(
            "irrigation",
            "watering_record_store_recover_after_format base_reload=%s state=%s error=%s",
            result.businessRecordStoresReloadSuccess ? "success" : "failed",
            recordStatusReady
                ? Esp32BaseRecordStore::storeStateName(recordStatus.state)
                : "uninitialized",
            recordStatusReady && recordStatus.errorReason
                ? recordStatus.errorReason
                : "status_unavailable");
        recordStoreReady = wateringRecordStore_.begin();
        recordStatusReady = wateringRecordStore_.readStatus(recordStatus);
        recordStoreReady = recordStoreReady && recordStatusReady &&
                           recordStatus.ready && recordStatus.writable;
    }
    if (recordStoreReady && !wateringRecordStoreRegistered_) {
        wateringRecordStoreRegistered_ =
            Esp32BaseWeb::registerBusinessRecordStore(
                wateringRecordStore_.baseStore());
    }
    const bool recordsReady =
        wateringRecordStoreRegistered_ && recordStoreReady;
    if (!recordsReady) {
        ESP32BASE_LOG_E(
            "irrigation",
            "watering_record_store_recovery_failed registered=%s state=%s error=%s",
            wateringRecordStoreRegistered_ ? "yes" : "no",
            recordStatusReady
                ? Esp32BaseRecordStore::storeStateName(recordStatus.state)
                : "uninitialized",
            recordStatusReady && recordStatus.errorReason
                ? recordStatus.errorReason
                : "status_unavailable");
    }
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
                    "after_format_reinitialized business_ready=%s records_ready=%s scheduler_ready=%s checkpoint_ready=%s condition_history_reset=%s",
                    businessReady_ ? "yes" : "no",
                    recordsReady ? "yes" : "no",
                    schedulerReady ? "yes" : "no",
                    checkpointReady ? "yes" : "no",
                    conditionHistoryReset ? "yes" : "no");
}
