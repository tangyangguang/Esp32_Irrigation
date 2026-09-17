#include "IrrigationApp.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>

#include <climits>
#include <cstdio>
#include <cstring>

#include "BoardHardware.h"
#include "BoardPins.h"
#include "IrrigationRecords.h"
#include "IrrigationPlatform.h"
#include "IrrigationWeb.h"

namespace {

constexpr const char* kFirmwareName = "esp32-irrigation";
constexpr const char* kFirmwareVersion = "0.9.0";
constexpr const char* kDefaultWebUser = "admin";
constexpr const char* kDefaultWebPassword = "admin";

struct LatestTrustedEpoch {
    uint32_t value = 0;
};

void collectWateringEpoch(const StoredWateringRecord& record, void* user) {
    auto* latest = static_cast<LatestTrustedEpoch*>(user);
    if (latest && record.payload.startedEpoch > latest->value) latest->value = record.payload.startedEpoch;
    uint32_t epoch = 0;
    if (latest && Esp32BaseRecordStore::resolveCompletedEpoch(record.timing, epoch) &&
        epoch > latest->value) {
        latest->value = epoch;
    }
}

void collectEventEpoch(const IrrigationEvents::EventRecord& event, void* user) {
    auto* latest = static_cast<LatestTrustedEpoch*>(user);
    uint32_t epoch = 0;
    if (latest && Esp32BaseRecordStore::resolveCompletedEpoch(event.timing, epoch) &&
        epoch > latest->value) {
        latest->value = epoch;
    }
}

bool failStartup(BoardHardware& hardware, StatusIndicator& indicator) {
    hardware.safeShutdown();
    const uint32_t nowMs = millis();
    indicator.setMode(StatusIndicator::Mode::Critical, nowMs);
    indicator.handle(nowMs);
    return false;
}

}  // namespace

IrrigationApp::IrrigationApp()
    : statusIndicator_(StatusIndicator::instance()) {}

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
    statusIndicator_.begin(millis());

    Serial.begin(115200);
    if (!hardwareReady) {
        return failStartup(hardware, statusIndicator_);
    }

    Wire.begin(BoardPins::kI2cSdaPin, BoardPins::kI2cSclPin);
    IrrigationRecords::instance().bind(wateringRecordStore_, events_.auditStore());
    Esp32Base::setFirmwareInfo(kFirmwareName, kFirmwareVersion);
    Esp32BaseRtc::configure(Wire);
    Esp32BaseOta::setUploadGuard(allowOta, this);
    Esp32BaseStorage::setFormatGuard(allowMaintenance, this);
    Esp32Base::setBeforeLifecycleStopCallback(beforeLifecycleStop, this);
    Esp32BaseWeb::setDefaultAuth(kDefaultWebUser, kDefaultWebPassword);
    Esp32BaseWeb::setAfterFormatFsCallback(afterFormatFs, this);
    if (!IrrigationParameterConfig::registerFields(parameterConfigSaved,
                                                   validateParameterConfig,
                                                   this) ||
        !IrrigationWeb::registerRoutes(*this)) {
        return failStartup(hardware, statusIndicator_);
    }
    Esp32BaseAppConfig::setApplyStatusCallback(parameterApplyStatus);
    // Peripheral MQTT adapter claims Base MQTT before begin; absent
    // provisioning safely leaves the device local-only.
    IrrigationPlatform::configure();
    baseReady_ = Esp32Base::begin();
    if (!baseReady_) {
        return failStartup(hardware, statusIndicator_);
    }
    Esp32BaseWiFi::setPowerSave(true);
    if (!configStore_.begin()) {
        return failStartup(hardware, statusIndicator_);
    }

    if (!applyStoredParameterConfig()) {
        return failStartup(hardware, statusIndicator_);
    }

    const IrrigationConfig* config = configStore_.current();
    if (!config) {
        return failStartup(hardware, statusIndicator_);
    }

    if (!wateringController_.begin(config->valveDrive.pwmFrequencyHz)) {
        return failStartup(hardware, statusIndicator_);
    }

    const bool wateringStoreReady = wateringRecordStore_.begin();
    const bool auditStoreReady = events_.begin();
    const bool recordsRegistered = IrrigationRecords::instance().begin(
        wateringRecordStore_, events_.auditStore());
    const bool recordsReady = wateringStoreReady && auditStoreReady && recordsRegistered;
    // Availability is queried per store; this flag only tracks failed results.
    recordStorageFault_ = false;
    if (!recordsReady) {
        ESP32BASE_LOG_E("irrigation",
                        "business_record_stores_begin_failed watering=%s audit=%s",
                        wateringStoreReady ? "ready" : "failed",
                        auditStoreReady ? "ready" : "failed");
    } else {
        Esp32BaseRecordStore::StoreStatus wateringStatus;
        Esp32BaseRecordStore::StoreStatus auditStatus;
        wateringRecordStore_.readStatus(wateringStatus);
        events_.auditStore().readStatus(auditStatus);
        ESP32BASE_LOG_I("irrigation",
                        "business_record_stores_ready watering_capacity=%lu watering_slot=%lu audit_capacity=%lu audit_slot=%lu",
                        static_cast<unsigned long>(wateringStatus.capacity),
                        static_cast<unsigned long>(wateringStatus.slotSizeBytes),
                        static_cast<unsigned long>(auditStatus.capacity),
                        static_cast<unsigned long>(auditStatus.slotSizeBytes));
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
    events_.readLatest(0, 1, collectEventEpoch, &latestTrusted);
    wateringScheduler_.setTrustedEpochBaseline(latestTrusted.value);

    businessReady_ = true;
    const uint32_t readyMs = millis();
    resetUnexpectedFlowMonitor(readyMs);
    updateStatusIndicator(readyMs);
    ESP32BASE_LOG_I("irrigation", "business_ready records_fault=%s events_fault=%s scheduler_fault=%s",
                    recordStorageFault_ ? "yes" : "no",
                    events_.storageFault() ? "yes" : "no",
                    schedulerStorageFault_ ? "yes" : "no");
    IrrigationPlatform::bindStores(wateringRecordStore_, events_.auditStore());
    IrrigationPlatform::begin();
    return true;
}

void IrrigationApp::handle() {
    const uint32_t nowMs = millis();
    if (!started_ || !baseReady_) {
        wateringController_.safeShutdown();
        statusIndicator_.setMode(StatusIndicator::Mode::Critical, nowMs);
        statusIndicator_.handle(nowMs);
        return;
    }

    if (Esp32BaseOta::isUploading() || Esp32BaseOta::status() == Esp32BaseOta::SUCCESS) {
        wateringController_.safeShutdown();
        Esp32Base::handle();
        return;
    }
    IrrigationRecords::instance().handle(millis());
    advanceBusiness();
    updateStatusIndicator(nowMs);
    Esp32Base::handle();
    IrrigationPlatform::poll();
}

bool IrrigationApp::baseReady() const {
    return baseReady_;
}

bool IrrigationApp::businessReady() const {
    return businessReady_;
}

WateringStartResult IrrigationApp::startWatering(const WateringRequest& request) {
    if (Esp32BaseOta::isUploading() || Esp32BaseOta::status() == Esp32BaseOta::SUCCESS) {
        return WateringStartResult::Busy;
    }
    if (!businessReady_) {
        return WateringStartResult::NotReady;
    }
    if (wateringController_.active()) {
        return WateringStartResult::Busy;
    }
    if (wateringController_.finishedSession()) {
        return WateringStartResult::PreviousResultPending;
    }
    const IrrigationConfig* config = configStore_.current();
    if (!config) {
        return WateringStartResult::NotReady;
    }

    if (request.purpose == WateringPurpose::Normal) {
        const auto& records = IrrigationRecords::instance();
        if (!records.writable(IrrigationRecords::StoreKind::Watering))
            return WateringStartResult::NotReady;
    }
    if (!WateringController::isValidRequest(request, *config)) return WateringStartResult::InvalidRequest;
    const bool normal = request.purpose == WateringPurpose::Normal;
    Esp32BaseRecordStore::RecordStartTime startTime{};
    const bool captured = wateringRecordStore_.captureStartTime(startTime);
    if (normal && (!captured || !wateringRecordStore_.prepareTask(request)))
        return WateringStartResult::NotReady;
    const WateringStartResult result = wateringController_.start(request, *config, millis());
    if (result == WateringStartResult::Started || wateringController_.finishedSession()) {
        wateringStartTime_ = startTime;
        wateringStartTimeValid_ = captured;
    }
    if (wateringController_.finishedSession()) return WateringStartResult::Started;
    if (normal && result != WateringStartResult::Started) wateringRecordStore_.cancelPreparedTask();
    return result;
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

bool IrrigationApp::readEventStatus(IrrigationEvents::EventStatus& status) const {
    return events_.readStatus(status);
}

IrrigationEvents::ConditionDisplayState IrrigationApp::eventConditionState(
    uint8_t conditionId) const {
    return events_.conditionState(conditionId);
}

bool IrrigationApp::recordStorageFault() const {
    return recordStorageFault_ ||
           !IrrigationRecords::instance().writable(IrrigationRecords::StoreKind::Watering);
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
    return businessReady_ && IrrigationRecords::instance().writable(IrrigationRecords::StoreKind::Audit) &&
           wateringScheduler_.resumeManually();
}

WateringStartResult IrrigationApp::startZoneFlowLearning(uint8_t zoneId) {
    if (pendingLearnedZoneId_ != 0 || !BoardPins::isValidZoneId(zoneId)) {
        return WateringStartResult::InvalidRequest;
    }
    WateringRequest request{};
    request.source = WateringSource::LocalWeb;
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
    const uint8_t zoneId = pendingLearnedZoneId_;
    const uint32_t rate = pendingLearnedBaselinePulseRateX10000_;
    if (setZoneBaseline(zoneId, rate, expectedConfigRevision) !=
        ConfigSaveError::Ok) {
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
    return setZoneBaseline(zoneId, pulseRateX10000, expectedConfigRevision) ==
           ConfigSaveError::Ok;
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
    // Clearing is a no-op request while a pending learning result waits for
    // the local page, or when the zone has no baseline to remove.
    if (!current || pendingLearnedZoneId_ != 0 ||
        !BoardPins::isValidZoneId(zoneId) ||
        current->zones[BoardPins::zoneIndex(zoneId)].baselinePulseRateX10000 == 0) {
        return false;
    }
    return setZoneBaseline(zoneId, 0, expectedConfigRevision) ==
           ConfigSaveError::Ok;
}

void IrrigationApp::discardLearnedZoneFlow() {
    if (!wateringController_.active()) {
        pendingLearnedZoneId_ = 0;
        pendingLearnedBaselinePulseRateX10000_ = 0;
    }
}

namespace {

// Disable-cascade shared by every zone-edit entry point: a disabled zone
// keeps no hidden plan durations, re-enabling starts from a clean plan model.
void clearDisabledZoneDurations(IrrigationConfig& next) {
    for (std::size_t zoneIndex = 0; zoneIndex < next.zones.size(); ++zoneIndex) {
        if (next.zones[zoneIndex].enabled) continue;
        for (WateringPlan& plan : next.plans) {
            plan.zoneDurationMinutes[zoneIndex] = 0;
        }
    }
}

IrrigationApp::ConfigSaveError mapConfigStoreError(const char* error) {
    if (!error) return IrrigationApp::ConfigSaveError::Persistence;
    if (std::strcmp(error, "config_revision_mismatch") == 0)
        return IrrigationApp::ConfigSaveError::RevisionMismatch;
    if (std::strcmp(error, "config_validation_failed") == 0)
        return IrrigationApp::ConfigSaveError::InvalidValue;
    return IrrigationApp::ConfigSaveError::Persistence;
}

}  // namespace

IrrigationApp::ConfigSaveError IrrigationApp::savePlanSlot(
    const WateringPlan& plan,
    bool deleteSlot,
    uint32_t expectedRevision) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || !current) return ConfigSaveError::NotReady;
    if (!IrrigationRecords::instance().writable(IrrigationRecords::StoreKind::Audit))
        return ConfigSaveError::AuditUnavailable;

    IrrigationConfig next = *current;
    if (plan.id < 1 || plan.id > next.plans.size()) return ConfigSaveError::InvalidValue;
    WateringPlan& target = next.plans[plan.id - 1U];

    IrrigationEvents::ConfigurationChange change;
    if (deleteSlot) {
        const bool existed = target.configured;
        target = {};
        target.id = plan.id;
        target.startMinutes.fill(kUnusedStartMinute);
        // Deleting an absent slot is an idempotent success with no change.
        if (!existed) return ConfigSaveError::Ok;
        change = IrrigationEvents::ConfigurationChange::PlanDeleted;
    } else {
        if (!IrrigationConfigRules::validateName(plan.name.data(), plan.name.size()))
            return ConfigSaveError::InvalidValue;
        const bool creating = !target.configured;
        target = plan;
        target.id = plan.id;
        clearDisabledZoneDurations(next);
        change = creating ? IrrigationEvents::ConfigurationChange::PlanCreated
                          : IrrigationEvents::ConfigurationChange::PlanUpdated;
    }

    if (!configStore_.save(next, expectedRevision))
        return mapConfigStoreError(configStore_.lastError());
    wateringScheduler_.rebaseTimeCheck();
    events_.recordConfigurationChanged(change, target.id, configStore_.current());
    return ConfigSaveError::Ok;
}

IrrigationApp::ConfigSaveError IrrigationApp::saveZoneInfo(
    uint8_t zoneId,
    const char* name,
    bool enabled,
    uint32_t expectedRevision) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || !current) return ConfigSaveError::NotReady;
    if (!IrrigationRecords::instance().writable(IrrigationRecords::StoreKind::Audit))
        return ConfigSaveError::AuditUnavailable;
    if (!BoardPins::isValidZoneId(zoneId) ||
        !IrrigationConfigRules::validateName(name, kObjectNameCapacity))
        return ConfigSaveError::InvalidValue;

    IrrigationConfig next = *current;
    ZoneConfig& zone = next.zones[BoardPins::zoneIndex(zoneId)];
    zone.enabled = enabled;
    std::snprintf(zone.name.data(), zone.name.size(), "%s", name);
    if (!enabled) clearDisabledZoneDurations(next);

    if (!configStore_.save(next, expectedRevision))
        return mapConfigStoreError(configStore_.lastError());
    wateringScheduler_.rebaseTimeCheck();
    events_.recordZoneChanged(zoneId,
                              enabled,
                              configStore_.current()->revision);
    return ConfigSaveError::Ok;
}

IrrigationApp::ConfigSaveError IrrigationApp::setZoneBaseline(
    uint8_t zoneId,
    uint32_t pulseRateX10000,
    uint32_t expectedRevision) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || !current) return ConfigSaveError::NotReady;
    if (wateringController_.active()) return ConfigSaveError::Busy;
    if (!IrrigationRecords::instance().writable(IrrigationRecords::StoreKind::Audit))
        return ConfigSaveError::AuditUnavailable;
    if (!BoardPins::isValidZoneId(zoneId)) return ConfigSaveError::InvalidValue;

    IrrigationConfig next = *current;
    ZoneConfig& zone = next.zones[BoardPins::zoneIndex(zoneId)];
    const uint32_t previousPulseRateX10000 = zone.baselinePulseRateX10000;
    zone.baselinePulseRateX10000 = pulseRateX10000;
    if (!IrrigationConfigRules::validate(next))
        return ConfigSaveError::InvalidValue;
    if (!configStore_.save(next, expectedRevision))
        return mapConfigStoreError(configStore_.lastError());

    wateringScheduler_.rebaseTimeCheck();
    uint32_t previousFlowMlPerMinute = 0;
    uint32_t savedFlowMlPerMinute = 0;
    FlowMonitor::pulseRateX10000ToFlowMlPerMinute(previousPulseRateX10000,
                                                 next.flowMeter.pulsesPerLiterX100,
                                                 previousFlowMlPerMinute);
    FlowMonitor::pulseRateX10000ToFlowMlPerMinute(pulseRateX10000,
                                                 next.flowMeter.pulsesPerLiterX100,
                                                 savedFlowMlPerMinute);
    events_.recordZoneFlowSaved(zoneId,
                                previousFlowMlPerMinute,
                                pulseRateX10000,
                                savedFlowMlPerMinute);
    return ConfigSaveError::Ok;
}

bool IrrigationApp::applyRemoteSystemField(const char* field,
                                           bool valueIsInteger,
                                           int32_t integerValue,
                                           bool valueIsBoolean,
                                           bool booleanValue,
                                           const char* textValue) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || !current || !Esp32BaseConfig::isReady() || !field)
        return false;
    if (!IrrigationRecords::instance().writable(IrrigationRecords::StoreKind::Audit))
        return false;

    // Build a range-checked candidate (no NVS write) and add the same plan
    // cross-check the local parameter page runs before persisting.
    IrrigationParameters candidate{};
    char error[128]{};
    if (!IrrigationParameterConfig::buildRemoteFieldCandidate(field,
                                                              valueIsInteger,
                                                              integerValue,
                                                              valueIsBoolean,
                                                              booleanValue,
                                                              textValue,
                                                              candidate) ||
        !validateParameterConfig(candidate, error, sizeof(error), this)) {
        return false;
    }

    // Candidate is valid: persist the single field, read everything back and
    // apply it to the runtime. The field name is a firmware-side whitelist.
    if (!IrrigationParameterConfig::applyRemoteField(field,
                                                     valueIsInteger,
                                                     integerValue,
                                                     valueIsBoolean,
                                                     booleanValue,
                                                     textValue)) {
        return false;
    }
    parameterConfigScratch_ = *current;
    if (IrrigationParameterConfig::applyStored(parameterConfigScratch_) &&
        validateParameterConfig(parameterConfigScratch_, error, sizeof(error), this)) {
        if (!configStore_.applyRuntimeParameters(parameterConfigScratch_) ||
            !wateringController_.configureValvePwmFrequency(
                parameterConfigScratch_.valveDrive.pwmFrequencyHz)) {
            businessReady_ = false;
            wateringController_.safeShutdown();
            return false;
        }
        wateringScheduler_.rebaseTimeCheck();
        if (!wateringController_.active()) resetUnexpectedFlowMonitor(millis());
        const uint8_t fieldIndex =
            IrrigationParameterConfig::fieldIndex(field);
        if (fieldIndex)
            events_.recordSystemFieldChanged(fieldIndex);
        return true;
    }

    // Rejected by combined validation: restore the previous single field and
    // reload the last-known-good runtime parameters.
    IrrigationParameterConfig::writeStoredField(field, *current);
    parameterConfigScratch_ = *current;
    IrrigationParameterConfig::applyStored(parameterConfigScratch_);
    configStore_.applyRuntimeParameters(parameterConfigScratch_);
    return false;
}

const IrrigationConfig* IrrigationApp::configuration() const {
    return configStore_.current();
}

IrrigationConfigStore::LoadResult IrrigationApp::configurationLoadResult() const {
    return configStore_.loadResult();
}

bool IrrigationApp::saveConfiguration(const IrrigationConfig& proposed,
                                      uint32_t expectedRevision,
                                      IrrigationEvents::ConfigurationChange change,
                                      uint8_t objectId) {
    const IrrigationConfig* current = configStore_.current();
    if (!businessReady_ || !current) {
        return false;
    }
    const bool audited = change == IrrigationEvents::ConfigurationChange::PlanCreated ||
                         change == IrrigationEvents::ConfigurationChange::PlanUpdated ||
                         change == IrrigationEvents::ConfigurationChange::PlanDeleted;
    if (audited && !IrrigationRecords::instance().writable(IrrigationRecords::StoreKind::Audit)) return false;
    const bool active = wateringController_.active();
    if (!configStore_.save(proposed, expectedRevision)) return false;
    wateringScheduler_.rebaseTimeCheck();
    if (!active) {
        resetUnexpectedFlowMonitor(millis());
    }
    events_.recordConfigurationChanged(change, objectId, configStore_.current());
    return true;
}

const char* IrrigationApp::configurationError() const {
    return configStore_.lastError();
}

void IrrigationApp::advanceBusiness() {
    if (!businessReady_) {
        wateringController_.safeShutdown();
        consumeFinishedWatering(millis());
        return;
    }
    const uint32_t nowMs = millis();
    if (!eventConditionsInitialized_) {
        refreshRtcCondition(nowMs, true);
        eventConditionsInitialized_ = true;
    }
    refreshRtcCondition(nowMs, false);
    // A deferred hardware change may fail immediately after execution ends.
    // Its completed fact must still reach storage while new starts are blocked.
    consumeFinishedWatering(nowMs);
    if (!wateringController_.hardwareReady()) {
        businessReady_ = false;
        wateringController_.safeShutdown();
        return;
    }
    const IrrigationConfig* config = configStore_.current();
    if (config && !wateringController_.active()) {
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
        IrrigationEvents::EventStatus eventStatus{};
        if (wateringRecordStore_.readStatus(recordStatus) &&
            events_.readStatus(eventStatus)) {
            const uint64_t activitySequence =
                (static_cast<uint64_t>(recordStatus.nextRecordId) << 32U) |
                eventStatus.eventStore.nextRecordId;
            aliveCheckpoint_.handle(now,
                                    config->timeSafety.aliveCheckpointHours,
                                    wateringController_.active(),
                                    activitySequence);
        }
    }
}

void IrrigationApp::consumeFinishedWatering(uint32_t nowMs) {
    const WateringSessionSummary* summary = wateringController_.finishedSession();
    if (!summary) {
        return;
    }

    if (summary->purpose == WateringPurpose::ZoneFlowLearning &&
               summary->zoneCount == 1 &&
               summary->zones[0].suggestedBaselinePulseRateX10000 != 0) {
        pendingLearnedZoneId_ = summary->zones[0].zoneId;
        pendingLearnedBaselinePulseRateX10000_ =
            summary->zones[0].suggestedBaselinePulseRateX10000;
    }

    if (summary->purpose == WateringPurpose::Normal) {
        if (!finishedWateringStored_) {
            finishedWateringStored_ = wateringStartTimeValid_ &&
                wateringRecordStore_.appendCompleted(
                    wateringStartTime_, *summary);
        }
        recordStorageFault_ = !finishedWateringStored_ ||
            !IrrigationRecords::instance().writable(
                IrrigationRecords::StoreKind::Watering);
        if (!finishedWateringStored_ || !wateringRecordStore_.cancelPreparedTask()) return;
        recordStorageFault_ = false;
    }

    resetUnexpectedFlowMonitor(nowMs);
    wateringController_.clearFinishedSession();
    finishedWateringStored_ = false;
    wateringStartTime_ = {};
    wateringStartTimeValid_ = false;
}

uint32_t IrrigationApp::trustedEpoch() const {
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    return now.synced ? now.epochSec : 0;
}

void IrrigationApp::updateStatusIndicator(uint32_t nowMs) {
    StatusIndicator::Mode mode = StatusIndicator::Mode::ReadyIdle;
    if (!businessReady_ || recordStorageFault() || eventStorageFault() ||
        schedulerStorageFault_ || checkpointStorageFault() || unexpectedFlowAlarm()) {
        mode = StatusIndicator::Mode::Critical;
    } else if (wateringController_.active()) {
        mode = StatusIndicator::Mode::Active;
    }
    statusIndicator_.setMode(mode, nowMs);
    statusIndicator_.handle(nowMs);
}

Esp32BaseAppConfig::ApplyStatus IrrigationApp::parameterApplyStatus() {
    const auto& app = instance();
    if (!app.businessReady_ || !app.wateringController_.hardwareReady())
        return Esp32BaseAppConfig::ApplyStatus::Failed;
    return app.wateringController_.parametersPending()
        ? Esp32BaseAppConfig::ApplyStatus::Pending : Esp32BaseAppConfig::ApplyStatus::Applied;
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

bool IrrigationApp::validateParameterConfig(const IrrigationParameters& proposed,
                                            char* error,
                                            size_t errorLength,
                                            void* user) {
    const auto* app = static_cast<IrrigationApp*>(user);
    const IrrigationConfig* current = app ? app->configStore_.current() : nullptr;
    if (!current) return true;
    for (const WateringPlan& plan : current->plans) {
        if (!plan.configured) continue;
        for (uint8_t zoneIndex = 0; zoneIndex < plan.zoneDurationMinutes.size(); ++zoneIndex) {
            if (plan.zoneDurationMinutes[zoneIndex] >
                proposed.runLimits.maximumZoneDurationMinutes) {
                std::snprintf(error,
                              errorLength,
                              "计划“%s”的水路 %u 为 %u 分钟，请先调整计划。",
                              plan.name.data(),
                              static_cast<unsigned>(zoneIndex + 1U),
                              static_cast<unsigned>(plan.zoneDurationMinutes[zoneIndex]));
                return false;
            }
        }
    }
    return true;
}

void IrrigationApp::handleParameterConfigSaved() {
    const IrrigationConfig* current = configStore_.current();
    if (!current) return;
    parameterConfigScratch_ = *current;
    if (!IrrigationParameterConfig::applyStored(parameterConfigScratch_)) {
        businessReady_ = false;
        wateringController_.safeShutdown();
        return;
    }
    const bool active = wateringController_.active();
    if (!configStore_.applyRuntimeParameters(parameterConfigScratch_) ||
        !wateringController_.configureValvePwmFrequency(parameterConfigScratch_.valveDrive.pwmFrequencyHz)) {
        businessReady_ = false;
        wateringController_.safeShutdown();
        return;
    }
    wateringScheduler_.rebaseTimeCheck();
    if (!active) resetUnexpectedFlowMonitor(millis());
    events_.recordConfigurationChanged(
        IrrigationEvents::ConfigurationChange::SystemParametersUpdated,
        0,
        configStore_.current());
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
    events_.observeClosedValveFlow(Esp32BaseConditions::ObservedState::Unknown,
                                   0,
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
    Esp32BaseConditions::ObservedState rollbackState =
        Esp32BaseConditions::ObservedState::Unknown;
    if (wateringScheduler_.timeState() == WateringScheduler::TimeState::Ready) {
        rollbackState = Esp32BaseConditions::ObservedState::Inactive;
    } else if (wateringScheduler_.timeState() == WateringScheduler::TimeState::RtcRollback) {
        rollbackState = Esp32BaseConditions::ObservedState::Active;
    }
    events_.observeRtcRollback(rollbackState);

    const IrrigationConfig* config = configStore_.current();
    Esp32BaseConditions::ObservedState flowState =
        Esp32BaseConditions::ObservedState::Unknown;
    if (config && !wateringController_.active() &&
        unexpectedFlowMonitor_.observationReady(nowMs)) {
        flowState = unexpectedFlowMonitor_.alarmActive()
                        ? Esp32BaseConditions::ObservedState::Active
                        : Esp32BaseConditions::ObservedState::Inactive;
    }
    uint32_t detectedFlowMlPerMinute = 0;
    if (config && config->flowProtection.unexpectedFlowWindowSec != 0U) {
        const uint64_t pulseRateX10000 =
            static_cast<uint64_t>(unexpectedFlowMonitor_.observedPulseCount()) *
            10000ULL /
            config->flowProtection.unexpectedFlowWindowSec;
        if (pulseRateX10000 <= UINT32_MAX) {
            FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
                static_cast<uint32_t>(pulseRateX10000),
                config->flowMeter.pulsesPerLiterX100,
                detectedFlowMlPerMinute);
        }
    }
    events_.observeClosedValveFlow(
        flowState,
        unexpectedFlowMonitor_.observedPulseCount(),
        detectedFlowMlPerMinute,
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
                                         int32_t value) {
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
        case WateringScheduler::Event::PlanStartRejected: {
            recordStartRejected(planId, static_cast<WateringStartResult>(value));
            break;
        }
        case WateringScheduler::Event::StorageFault:
            schedulerStorageFault_ = true;
            break;
    }
}

// Builds a zero-duration failed watering record for a plan start point the
// device reached but could not accept. Kept out of the scheduler callback
// frame, including LTO, because it writes through the record store.
void __attribute__((noinline)) IrrigationApp::recordStartRejected(
    uint8_t planId, WateringStartResult result) {
    const IrrigationConfig* config = configStore_.current();
    if (!config || planId < 1U || planId > config->plans.size()) return;
    const WateringPlan& plan = config->plans[planId - 1U];
    if (!plan.configured) return;

    WateringStopReason reason = WateringStopReason::ControllerNotReady;
    if (result == WateringStartResult::Busy) {
        const WateringStatus status = wateringController_.status();
        if (status.active && status.purpose == WateringPurpose::ZoneFlowLearning)
            reason = WateringStopReason::BusyZoneFlowLearning;
        else if (status.active && status.source == WateringSource::AutomaticPlan)
            reason = WateringStopReason::BusyAutomaticWatering;
        else if (status.active)
            reason = WateringStopReason::BusyManualWatering;
    } else if (result == WateringStartResult::PreviousResultPending) {
        reason = WateringStopReason::PreviousResultPending;
    } else if (result == WateringStartResult::InvalidRequest) {
        reason = WateringStopReason::InvalidRequest;
    }

    WateringSessionSummary summary{};
    summary.source = WateringSource::AutomaticPlan;
    summary.targetMode = WateringTargetMode::Duration;
    summary.purpose = WateringPurpose::Normal;
    summary.planId = planId;
    summary.planName = plan.name;
    summary.result = WateringResult::StartFailed;
    summary.stopReason = reason;
    for (uint8_t zoneIndex = 0;
         zoneIndex < BoardPins::kZoneCount &&
         summary.zoneCount < summary.zones.size();
         ++zoneIndex) {
        const uint16_t durationMinutes = plan.zoneDurationMinutes[zoneIndex];
        if (!config->zones[zoneIndex].enabled || durationMinutes == 0U) continue;
        auto& zone = summary.zones[summary.zoneCount++];
        zone.zoneId = config->zones[zoneIndex].id;
        zone.result = ZoneWateringResult::NotStarted;
        zone.plannedDurationSec = static_cast<uint32_t>(durationMinutes) * 60U;
    }

    if (!wateringRecordStore_.appendStartRejected(summary, trustedEpoch())) {
        recordStorageFault_ = true;
    }
}

bool IrrigationApp::allowMaintenance(void* user) {
    auto* app = static_cast<IrrigationApp*>(user);
    return app && !app->wateringController_.active() && app->pendingLearnedZoneId_ == 0;
}

bool IrrigationApp::allowOta(void* user) {
    auto* app = static_cast<IrrigationApp*>(user);
    return allowMaintenance(user) && !app->wateringController_.finishedSession() &&
           !app->events_.auditStore().hasPending();
}

void IrrigationApp::beforeLifecycleStop(void* user) {
    auto* app = static_cast<IrrigationApp*>(user);
    // Hardware closure always precedes filesystem work and bounded network waits.
    if (!app) return;
    app->wateringController_.safeShutdown();
    app->consumeFinishedWatering(millis());
    app->businessReady_ = false;
}

void IrrigationApp::afterFormatFs(const Esp32BaseWeb::FormatFsResult& result, void* user) {
    if (user) {
        static_cast<IrrigationApp*>(user)->handleAfterFormatFs(result);
    }
}

void IrrigationApp::handleAfterFormatFs(const Esp32BaseWeb::FormatFsResult& result) {
    wateringController_.safeShutdown();
    businessReady_ = false;

    if (!result.formatSuccess || !result.mountSuccess) {
        recordStorageFault_ = true;
        return;
    }

    // Explicit successful formatting discards the old generation, including
    // any completed RAM facts; they must not reappear in the new empty history.
    wateringController_.clearFinishedSession();
    finishedWateringStored_ = false;
    wateringStartTime_ = {};
    wateringStartTimeValid_ = false;
    const bool conditionHistoryReset = events_.resetConditionHistory();
    const bool localRecordsReady =
        IrrigationRecords::instance().reloadAfterFormat();
    rtcObservationInitialized_ = false;
    eventConditionsInitialized_ = false;

    bool configReady = configStore_.begin();
    if (configReady) configReady = applyStoredParameterConfig();
    const IrrigationConfig* config = configStore_.current();
    const bool pwmReady = configReady && config &&
                          wateringController_.begin(
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
    Esp32BaseRecordStore::StoreStatus wateringStatus{};
    Esp32BaseRecordStore::StoreStatus auditStatus{};
    const bool recordsReady = result.businessRecordStoresReloadSuccess &&
        wateringRecordStore_.readStatus(wateringStatus) &&
        events_.auditStore().readStatus(auditStatus) &&
        wateringStatus.ready && wateringStatus.writable &&
        auditStatus.ready && auditStatus.writable && localRecordsReady;
    if (!recordsReady) {
        ESP32BASE_LOG_E("irrigation",
                        "business_record_stores_recovery_failed base_reload=%s watering=%s audit=%s",
                        result.businessRecordStoresReloadSuccess ? "success" : "failed",
                        Esp32BaseRecordStore::storeStateName(wateringStatus.state),
                        Esp32BaseRecordStore::storeStateName(auditStatus.state));
    }
    recordStorageFault_ = false; // Each local store reports its own readiness.
    businessReady_ = configReady && pwmReady;
    if (businessReady_) {
        resetUnexpectedFlowMonitor(millis());
    }
    if (!businessReady_) {
        wateringController_.safeShutdown();
    }
    ESP32BASE_LOG_W("irrigation",
                    "after_format_reinitialized business_ready=%s records_ready=%s scheduler_ready=%s checkpoint_ready=%s condition_history_reset=%s",
                    businessReady_ ? "yes" : "no",
                    recordsReady ? "yes" : "no",
                    schedulerReady ? "yes" : "no",
                    checkpointReady ? "yes" : "no",
                    conditionHistoryReset ? "yes" : "no");
}

WateringDaySummary IrrigationApp::wateringDay(uint32_t day) {
    return WateringHistory::summarize(wateringRecordStore_, day, wateringStatus(), wateringRecordStore_.taskStartedEpoch());
}
