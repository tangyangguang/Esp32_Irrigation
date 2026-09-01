#include "IrrigationIot.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "FlowMonitor.h"
#include "IrrigationApp.h"
#include "IrrigationIotSecrets.h"
#include "IrrigationRecordSync.h"

namespace {

constexpr uint32_t kRunningPublishIntervalMs = 5000U;
constexpr uint16_t kShutdownNetworkGraceMs = 1000U;
constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void makeUuid(char* output, std::size_t length) {
    uint8_t bytes[16]{};
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    std::snprintf(output,
                  length,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                  "%02x%02x%02x%02x%02x%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                  bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                  bytes[12], bytes[13], bytes[14], bytes[15]);
}

void hashBytes(uint64_t& hash, const void* value, std::size_t length) {
    const auto* bytes = static_cast<const uint8_t*>(value);
    for (std::size_t index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

template <typename T>
void hashValue(uint64_t& hash, const T& value) {
    hashBytes(hash, &value, sizeof(value));
}

const char* activityKindName(const WateringStatus& status) {
    if (!status.active) return "idle";
    if (status.purpose == WateringPurpose::FlowCalibration) return "calibration";
    if (status.purpose == WateringPurpose::ZoneFlowLearning) return "learning";
    if (status.source == WateringSource::AutomaticPlan) return "automatic";
    if (status.source == WateringSource::SingleOutput) return "single-output";
    return "manual";
}

const char* phaseName(WateringState state) {
    switch (state) {
        case WateringState::StartingZone: return "starting";
        case WateringState::WaitingForFlow: return "waiting-flow";
        case WateringState::WateringZone: return "watering";
        case WateringState::StoppingZone: return "stopping";
        case WateringState::SwitchingZone: return "switching";
        case WateringState::Idle: return "idle";
    }
    return "idle";
}

const char* wateringReasonName(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::Completed: return "completed";
        case WateringStopReason::UserStopped: return "user_stopped";
        case WateringStopReason::FlowStartTimeout: return "flow_start_timeout";
        case WateringStopReason::NoFlowTimeout: return "no_flow_timeout";
        case WateringStopReason::LowFlow: return "low_flow";
        case WateringStopReason::HighFlow: return "high_flow";
        case WateringStopReason::HardwareFailure: return "hardware_failure";
        case WateringStopReason::MaintenanceInterrupted:
            return "maintenance_interrupted";
        case WateringStopReason::TargetVolumeTimeout:
            return "target_volume_timeout";
        case WateringStopReason::None: return "none";
    }
    return "internal_state";
}

const char* zoneResultName(ZoneWateringResult result) {
    switch (result) {
        case ZoneWateringResult::Completed: return "completed";
        case ZoneWateringResult::Stopped: return "stopped";
        case ZoneWateringResult::Failed: return "failed";
        case ZoneWateringResult::NotStarted: return "not-started";
    }
    return "not-started";
}

const char* flowActionName(FlowAlertAction action) {
    return action == FlowAlertAction::StopWatering ? "stop" : "alert";
}

uint16_t lowestStateBit(uint16_t mask) {
    return static_cast<uint16_t>(mask & static_cast<uint16_t>(0U - mask));
}

bool isProcessCommand(IrrigationIotProtocol::CommandKind kind) {
    return kind == IrrigationIotProtocol::CommandKind::StartManual ||
           kind == IrrigationIotProtocol::CommandKind::SingleOutput;
}

const char* automaticAuditReason(uint8_t reason) {
    using Reason = IrrigationEvents::ReasonCode;
    switch (static_cast<Reason>(reason)) {
        case Reason::PlanBusyManualWatering: return "busy_manual_watering";
        case Reason::PlanBusyAutomaticWatering: return "busy_automatic_watering";
        case Reason::PlanBusyFlowCalibration: return "busy_flow_calibration";
        case Reason::PlanBusyZoneFlowLearning: return "busy_zone_flow_learning";
        case Reason::PlanPreviousResultPending: return "previous_result_pending";
        case Reason::PlanControllerNotReady: return "controller_not_ready";
        case Reason::PlanInvalidRequest: return "invalid_request";
        case Reason::PlanHardwareFailure: return "hardware_failure";
        case Reason::PlanBusy: return "busy";
        default: return "start_rejected";
    }
}

}  // namespace

IrrigationIot& IrrigationIot::instance() {
    static IrrigationIot adapter;
    return adapter;
}

bool IrrigationIot::configure() {
    if (configured_) return true;

    const uint64_t chipId = ESP.getEfuseMac() & 0x0000FFFFFFFFFFFFULL;
    std::snprintf(deviceId_, sizeof(deviceId_), "esp32-irrigation-%012llx", chipId);
    std::snprintf(topicPrefix_,
                  sizeof(topicPrefix_),
                  "iot/%s/v1/%s",
                  IrrigationIotProtocol::kTypeKey,
                  deviceId_);
    std::snprintf(availabilityTopic_, sizeof(availabilityTopic_), "%s/availability",
                  topicPrefix_);
    std::snprintf(stateTopic_, sizeof(stateTopic_), "%s/state", topicPrefix_);
    std::snprintf(eventTopic_, sizeof(eventTopic_), "%s/event", topicPrefix_);
    std::snprintf(commandTopic_, sizeof(commandTopic_), "%s/command", topicPrefix_);
    std::snprintf(recordAckTopic_, sizeof(recordAckTopic_), "%s/record-ack",
                  topicPrefix_);
    std::snprintf(receiptTopic_, sizeof(receiptTopic_), "%s/receipt", topicPrefix_);
    std::snprintf(progressTopic_, sizeof(progressTopic_), "%s/progress", topicPrefix_);

    if (IRRIGATION_IOT_MQTT_HOST[0] == '\0') {
        ESP32BASE_LOG_W("irrigation_iot", "mqtt_not_configured private_settings_missing=true");
        return true;
    }
    if (IRRIGATION_IOT_MQTT_CA_PEM[0] == '\0') {
        ESP32BASE_LOG_E("irrigation_iot", "mqtt_configuration_missing_ca");
        return false;
    }

    prepareConnectionCycle();
    lastWill_.topic = availabilityTopic_;
    lastWill_.payload = reinterpret_cast<const uint8_t*>(lwtPayload_);
    lastWill_.payloadLength = std::strlen(lwtPayload_);
    lastWill_.qos = Esp32BaseMqtt::QOS_1;
    lastWill_.retain = true;

    Esp32BaseMqtt::ConnectionConfig config;
    config.host = IRRIGATION_IOT_MQTT_HOST;
    config.port = IRRIGATION_IOT_MQTT_PORT;
    config.clientId = deviceId_;
    config.keepAliveSeconds = 60;
    config.security = Esp32BaseMqtt::TLS;
    config.username = IRRIGATION_IOT_MQTT_USERNAME[0] != '\0'
                          ? IRRIGATION_IOT_MQTT_USERNAME
                          : nullptr;
    config.password = IRRIGATION_IOT_MQTT_PASSWORD[0] != '\0'
                          ? IRRIGATION_IOT_MQTT_PASSWORD
                          : nullptr;
    config.tls.caCertificatePem = IRRIGATION_IOT_MQTT_CA_PEM;
    config.tls.caCertificateLength =
        std::strlen(IRRIGATION_IOT_MQTT_CA_PEM) + 1U;
    config.lastWill = &lastWill_;
    if (!Esp32BaseMqtt::configure(config)) {
        return false;
    }

    Esp32BaseMqtt::Subscription commandSubscription;
    commandSubscription.topicFilter = commandTopic_;
    commandSubscription.qos = Esp32BaseMqtt::QOS_1;
    Esp32BaseMqtt::Subscription ackSubscription;
    ackSubscription.topicFilter = recordAckTopic_;
    ackSubscription.qos = Esp32BaseMqtt::QOS_1;
    if (!Esp32BaseMqtt::addSubscription(commandSubscription) ||
        !Esp32BaseMqtt::addSubscription(ackSubscription) ||
        !Esp32BaseMqtt::setBeforeConnectCallback(beforeConnect, this)) {
        return false;
    }
    Esp32BaseMqtt::setMessageCallback(mqttMessage, this);
    Esp32BaseMqtt::setEventCallback(mqttEvent, this);
    Esp32Base::setBeforeNetworkStopCallback(beforeNetworkStop, this);
    configured_ = true;
    ESP32BASE_LOG_I("irrigation_iot",
                    "mqtt_configured device_id=%s model=%s definition=%s",
                    deviceId_,
                    IrrigationIotProtocol::kModelKey,
                    IrrigationIotProtocol::kDefinitionChecksum);
    return true;
}

bool IrrigationIot::begin() {
    if (begun_) return configured_ ? journalReady_ : true;
    begun_ = true;
    if (!IrrigationRecordSync::instance().ready()) {
        ESP32BASE_LOG_E("irrigation_iot", "record_streams_unavailable");
    }
    if (!configured_) return true;
    journalReady_ = journal_.begin();
    if (!journalReady_) {
        ESP32BASE_LOG_E("irrigation_iot", "command_journal_unavailable");
    }
    return true;
}

void IrrigationIot::handle(IrrigationApp& app) {
    app_ = &app;
    if (!configured_ || !begun_ || lifecycleStopping_) return;
    const uint32_t nowMs = millis();
    IrrigationRecordSync::instance().handle(nowMs);
    detectActivity(app, nowMs);
    detectStateChanges(app, nowMs);
    if (activityTracked_ && activityCommandId_[0] != '\0' && connected_ &&
        subscriptionsReady_ &&
        static_cast<uint32_t>(nowMs - lastRunningEvidenceMs_) >=
            kRunningPublishIntervalMs) {
        Evidence running;
        std::strcpy(running.commandId, activityCommandId_);
        running.kind = activityCommandKind_;
        running.type = EvidenceType::ProgressRunning;
        running.observedAtMs = currentEpochMs();
        queueEvidence(running);
        lastRunningEvidenceMs_ = nowMs;
        scheduleRuntimeState();
    }
    pump(app);
}

bool IrrigationIot::configured() const { return configured_; }
const char* IrrigationIot::deviceId() const { return deviceId_; }
const char* IrrigationIot::activeCommandId() const {
    return activityTracked_ && activityCommandId_[0] != '\0'
               ? activityCommandId_
               : nullptr;
}

void IrrigationIot::beforeConnect(void* context) {
    if (context) static_cast<IrrigationIot*>(context)->prepareConnectionCycle();
}

uint16_t IrrigationIot::beforeNetworkStop(void* context) {
    return context && static_cast<IrrigationIot*>(context)->publishShutdown()
               ? kShutdownNetworkGraceMs
               : 0U;
}

void IrrigationIot::mqttMessage(const Esp32BaseMqtt::MessageView& message,
                                void* context) {
    if (context) static_cast<IrrigationIot*>(context)->onMessage(message);
}

void IrrigationIot::mqttEvent(const Esp32BaseMqtt::Event& event, void* context) {
    if (context) static_cast<IrrigationIot*>(context)->onEvent(event);
}

void IrrigationIot::prepareConnectionCycle() {
    connected_ = false;
    subscriptionsReady_ = false;
    lifecycleStopping_ = false;
    subscriptionAckMask_ = 0;
    makeUuid(connectionId_, sizeof(connectionId_));
    stateSeq_ = 0;
    const int written = std::snprintf(
        lwtPayload_,
        sizeof(lwtPayload_),
        "{\"protocol\":\"%s\",\"modelKey\":\"%s\",\"online\":false,"
        "\"connectionId\":\"%s\",\"reason\":\"lwt\"}",
        IrrigationIotProtocol::kProtocol,
        IrrigationIotProtocol::kModelKey,
        connectionId_);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(lwtPayload_)) {
        lwtPayload_[0] = '\0';
    }
    lastWill_.payloadLength = std::strlen(lwtPayload_);
    resetConnectionDelivery();
}

bool IrrigationIot::publishShutdown() {
    if (lifecycleStopping_) return false;
    lifecycleStopping_ = true;
    if (!configured_ || !connected_ || connectionId_[0] == '\0' ||
        Esp32BaseMqtt::state() != Esp32BaseMqtt::CONNECTED) {
        ESP32BASE_LOG_W("irrigation_iot",
                        "shutdown_publish_skipped mqtt_connected=false");
        return false;
    }

    char observedAt[25]{};
    char payload[320]{};
    if (!currentObservedAt(observedAt, sizeof(observedAt))) {
        ESP32BASE_LOG_W("irrigation_iot",
                        "shutdown_publish_skipped trusted_time=false");
        return false;
    }
    const int length = std::snprintf(
        payload,
        sizeof(payload),
        "{\"protocol\":\"%s\",\"modelKey\":\"%s\",\"online\":false,"
        "\"connectionId\":\"%s\",\"reason\":\"shutdown\","
        "\"observedAt\":\"%s\"}",
        IrrigationIotProtocol::kProtocol,
        IrrigationIotProtocol::kModelKey,
        connectionId_,
        observedAt);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(payload)) {
        ESP32BASE_LOG_E("irrigation_iot", "shutdown_serialize_failed");
        return false;
    }

    Esp32BaseMqtt::PublishRequest request;
    request.topic = availabilityTopic_;
    request.payload = reinterpret_cast<const uint8_t*>(payload);
    request.payloadLength = static_cast<std::size_t>(length);
    request.qos = Esp32BaseMqtt::QOS_1;
    request.retain = true;
    const Esp32BaseMqtt::PublishResult result = Esp32BaseMqtt::publish(request);
    if (!result.accepted()) {
        ESP32BASE_LOG_W("irrigation_iot",
                        "shutdown_publish_rejected code=%u",
                        static_cast<unsigned>(result.code));
        return false;
    }

    connected_ = false;
    subscriptionsReady_ = false;
    availabilityPending_ = false;
    ESP32BASE_LOG_I("irrigation_iot",
                    "shutdown_publish_accepted packet_id=%u connection_id=%s",
                    static_cast<unsigned>(result.packetId),
                    connectionId_);
    return true;
}

void IrrigationIot::onMessage(const Esp32BaseMqtt::MessageView& message) {
    if (!app_ || !subscriptionsReady_) return;
    IrrigationIotProtocol::CommandPacket packet;
    packet.topic = message.topic;
    packet.payload = message.payload;
    packet.payloadLength = message.payloadLength;
    packet.qos = static_cast<uint8_t>(message.qos);
    packet.retain = message.retain;
    if (std::strcmp(message.topic, commandTopic_) == 0) {
        IrrigationIotProtocol::Command command;
        const IrrigationIotProtocol::ParseError error =
            IrrigationIotProtocol::parseCommand(packet, commandTopic_, command);
        if (error != IrrigationIotProtocol::ParseError::None) {
            ESP32BASE_LOG_W("irrigation_iot",
                            "command_rejected_before_receipt error=%s",
                            IrrigationIotProtocol::parseErrorName(error));
            return;
        }
        const uint64_t nowMs = currentEpochMs();
        if (nowMs == 0U) {
            ESP32BASE_LOG_W("irrigation_iot",
                            "command_rejected_before_receipt reason=time_untrusted");
            return;
        }
        handleCommand(command, *app_, nowMs);
        return;
    }
    if (std::strcmp(message.topic, recordAckTopic_) == 0) {
        // Reliable record acknowledgement is activated with the unified record
        // store; malformed or premature ACKs never affect command/state flow.
        IrrigationIotProtocol::RecordAck ack;
        const IrrigationIotProtocol::ParseError error =
            IrrigationIotProtocol::parseRecordAck(packet, recordAckTopic_, ack);
        if (error != IrrigationIotProtocol::ParseError::None) {
            ESP32BASE_LOG_W("irrigation_iot", "record_ack_rejected error=%s",
                            IrrigationIotProtocol::parseErrorName(error));
        } else {
            IrrigationRecordSync::StreamKind acknowledgedStream;
            if (IrrigationRecordSync::instance().acknowledge(
                    ack, millis(), &acknowledgedStream)) {
                if (acknowledgedStream ==
                    IrrigationRecordSync::StreamKind::Watering)
                    lastWateringRecordPublishMs_ = 0;
                else
                    lastAuditRecordPublishMs_ = 0;
            } else {
                ESP32BASE_LOG_W("irrigation_iot", "record_ack_rejected error=watermark");
            }
        }
    }
}

void IrrigationIot::onEvent(const Esp32BaseMqtt::Event& event) {
    switch (event.type) {
        case Esp32BaseMqtt::EVENT_CONNECTED:
            connected_ = true;
            subscriptionsReady_ = false;
            subscriptionAckMask_ = 0;
            resetConnectionDelivery();
            break;
        case Esp32BaseMqtt::EVENT_DISCONNECTED:
        case Esp32BaseMqtt::EVENT_CONNECTION_REJECTED:
            connected_ = false;
            lastWateringRecordPublishMs_ = 0;
            lastAuditRecordPublishMs_ = 0;
            subscriptionsReady_ = false;
            subscriptionAckMask_ = 0;
            inFlightKind_ = InFlightKind::None;
            inFlightPacketId_ = 0;
            break;
        case Esp32BaseMqtt::EVENT_SUBSCRIPTION_ACKNOWLEDGED:
            if (event.subscriptionIndex < 2U) {
                subscriptionAckMask_ |= static_cast<uint8_t>(1U << event.subscriptionIndex);
            }
            if (subscriptionAckMask_ == 0x03U) {
                subscriptionsReady_ = true;
                availabilityPending_ = true;
                scheduleAllState();
                if (activityTracked_ && activityCommandId_[0] != '\0') {
                    Evidence running;
                    std::strcpy(running.commandId, activityCommandId_);
                    running.kind = activityCommandKind_;
                    running.type = EvidenceType::ProgressRunning;
                    running.observedAtMs = currentEpochMs();
                    queueEvidence(running);
                }
            }
            break;
        case Esp32BaseMqtt::EVENT_SUBSCRIPTION_REJECTED:
            subscriptionsReady_ = false;
            break;
        case Esp32BaseMqtt::EVENT_PUBLISH_ACKNOWLEDGED:
            markPublishAcknowledged(event.packetId);
            break;
        case Esp32BaseMqtt::EVENT_PUBLISH_DELIVERY_UNCERTAIN:
            if (inFlightKind_ != InFlightKind::None &&
                event.packetId == inFlightPacketId_) {
                inFlightKind_ = InFlightKind::None;
                inFlightPacketId_ = 0;
            }
            break;
        default:
            break;
    }
}

void IrrigationIot::handleCommand(
    const IrrigationIotProtocol::Command& command,
    IrrigationApp& app,
    uint64_t nowMs) {
    std::size_t journalIndex = IrrigationCommandJournal::kCapacity;
    const IrrigationCommandJournal::LookupResult lookup =
        journal_.lookup(command, nowMs, journalIndex);
    if (lookup == IrrigationCommandJournal::LookupResult::ConflictingCommand) {
        return;
    }
    if (lookup == IrrigationCommandJournal::LookupResult::SameCommand) {
        const IrrigationCommandJournal::Entry* stored = journal_.entry(journalIndex);
        if (!stored) return;
        queueJournalReceipt(*stored);
        if (stored->progress != IrrigationCommandJournal::ProgressStatus::None) {
            queueJournalProgress(*stored);
        } else if (stored->processOpen && activityTracked_ &&
                   std::strcmp(stored->commandId, activityCommandId_) == 0) {
            Evidence running;
            std::strcpy(running.commandId, stored->commandId);
            running.kind = stored->kind;
            running.type = EvidenceType::ProgressRunning;
            running.observedAtMs = nowMs;
            queueEvidence(running);
        }
        return;
    }

    IrrigationIotProtocol::BusinessContext context =
        businessContext(app, nowMs + 999U);
    const IrrigationIotProtocol::Rejection rejection =
        IrrigationIotProtocol::evaluateCommand(command, context);
    const bool accepted = rejection == IrrigationIotProtocol::Rejection::None;
    const IrrigationCommandJournal::Reason reason =
        IrrigationCommandJournal::fromRejection(rejection);
    const bool processOpen =
        accepted &&
        (isProcessCommand(command.kind) ||
         (command.kind == IrrigationIotProtocol::CommandKind::Stop &&
          context.activeKind != IrrigationIotProtocol::ActiveKind::Idle));
    if (!journalReady_ ||
        !journal_.storeReceipt(command,
                               accepted
                                   ? IrrigationCommandJournal::ReceiptStatus::Accepted
                                   : IrrigationCommandJournal::ReceiptStatus::Rejected,
                               reason,
                               nowMs,
                               processOpen,
                               nowMs,
                               journalIndex)) {
        Evidence rejected;
        std::strcpy(rejected.commandId, command.commandId);
        rejected.kind = command.kind;
        rejected.type = EvidenceType::ReceiptRejected;
        rejected.reason = IrrigationCommandJournal::Reason::PersistenceError;
        rejected.observedAtMs = nowMs;
        queueEvidence(rejected);
        return;
    }
    journalReceiptDelivered_[journalIndex] = false;
    journalProgressDelivered_[journalIndex] = false;
    const IrrigationCommandJournal::Entry* stored = journal_.entry(journalIndex);
    if (stored) queueJournalReceipt(*stored);
    if (!accepted) return;
    executeAcceptedCommand(command, app, journalIndex, nowMs);
}

void IrrigationIot::executeAcceptedCommand(
    const IrrigationIotProtocol::Command& command,
    IrrigationApp& app,
    std::size_t journalIndex,
    uint64_t nowMs) {
    bool succeeded = false;
    IrrigationCommandJournal::Reason failureReason =
        IrrigationCommandJournal::Reason::PersistenceError;
    switch (command.kind) {
        case IrrigationIotProtocol::CommandKind::Plans:
            succeeded = applyPlans(command, app);
            break;
        case IrrigationIotProtocol::CommandKind::AutomaticWatering:
            succeeded = applyAutomatic(command, app);
            break;
        case IrrigationIotProtocol::CommandKind::StartManual:
            succeeded = startManual(command, app);
            failureReason = IrrigationCommandJournal::Reason::HardwareFailure;
            break;
        case IrrigationIotProtocol::CommandKind::SingleOutput:
            succeeded = startSingleOutput(command, app);
            failureReason = IrrigationCommandJournal::Reason::HardwareFailure;
            break;
        case IrrigationIotProtocol::CommandKind::Stop: {
            const WateringStatus status = app.wateringStatus();
            if (!status.active) {
                succeeded = true;
            } else if (app.stopWatering() &&
                       addPendingStopCommand(command.commandId, journalIndex)) {
                scheduleRuntimeState();
                return;
            } else {
                failureReason = IrrigationCommandJournal::Reason::HardwareFailure;
            }
            break;
        }
    }

    if (isProcessCommand(command.kind) && succeeded) {
        beginActivity(app.wateringStatus(), &command, journalIndex, millis());
        Evidence running;
        std::strcpy(running.commandId, command.commandId);
        running.kind = command.kind;
        running.type = EvidenceType::ProgressRunning;
        running.observedAtMs = nowMs;
        queueEvidence(running);
        scheduleAllState();
        return;
    }

    const IrrigationCommandJournal::ProgressStatus finalStatus =
        succeeded ? IrrigationCommandJournal::ProgressStatus::Succeeded
                  : IrrigationCommandJournal::ProgressStatus::Failed;
    if (journal_.storeFinal(journalIndex, finalStatus,
                            succeeded ? IrrigationCommandJournal::Reason::None
                                      : failureReason,
                            nowMs)) {
        journalProgressDelivered_[journalIndex] = false;
        const IrrigationCommandJournal::Entry* stored = journal_.entry(journalIndex);
        if (stored) queueJournalProgress(*stored);
    } else {
        Evidence failed;
        std::strcpy(failed.commandId, command.commandId);
        failed.kind = command.kind;
        failed.type = EvidenceType::ProgressFailed;
        failed.reason = IrrigationCommandJournal::Reason::PersistenceError;
        failed.observedAtMs = nowMs;
        queueEvidence(failed);
    }
    scheduleAllState();
}

bool IrrigationIot::applyPlans(const IrrigationIotProtocol::Command& command,
                               IrrigationApp& app) {
    const IrrigationConfig* current = app.configuration();
    if (!current) return false;
    IrrigationConfig next;
    if (!IrrigationIotProtocol::buildPlanReplacement(command, *current, next)) {
        return false;
    }
    return app.saveConfiguration(next,
                                 command.expectedRevision,
                                 IrrigationEvents::ConfigurationChange::PlanUpdated);
}

bool IrrigationIot::applyAutomatic(
    const IrrigationIotProtocol::Command& command,
    IrrigationApp& app) {
    switch (command.automaticMode) {
        case IrrigationIotProtocol::AutomaticMode::Enabled:
            return app.resumeAutomaticWatering();
        case IrrigationIotProtocol::AutomaticMode::PausedIndefinitely:
            return app.pauseAutomaticWateringIndefinitely();
        case IrrigationIotProtocol::AutomaticMode::PausedUntil:
            return app.pauseAutomaticWateringUntil(command.resumeAtEpoch);
    }
    return false;
}

bool IrrigationIot::startManual(const IrrigationIotProtocol::Command& command,
                                IrrigationApp& app) {
    std::array<uint16_t, BoardPins::kZoneCount> durations{};
    for (uint8_t index = 0; index < command.zoneCount; ++index) {
        durations[command.zones[index].zoneId - 1U] =
            command.zones[index].durationMinutes;
    }
    return app.startManualWatering(durations) == WateringStartResult::Started;
}

bool IrrigationIot::startSingleOutput(
    const IrrigationIotProtocol::Command& command,
    IrrigationApp& app) {
    const IrrigationConfig* config = app.configuration();
    if (!config) return false;
    const uint32_t duration =
        command.singleOutputMode == IrrigationIotProtocol::SingleOutputMode::Duration
            ? command.durationSeconds
            : static_cast<uint32_t>(config->runLimits.maximumZoneDurationMinutes) *
                  60U;
    const uint32_t water =
        command.singleOutputMode == IrrigationIotProtocol::SingleOutputMode::Volume
            ? command.targetWaterMl
            : 0U;
    return app.startSingleOutput(command.zoneId, duration, water) ==
           WateringStartResult::Started;
}

IrrigationIotProtocol::BusinessContext IrrigationIot::businessContext(
    const IrrigationApp& app,
    uint64_t nowMs) const {
    IrrigationIotProtocol::BusinessContext context;
    const IrrigationConfig* config = app.configuration();
    const WateringStatus status = app.wateringStatus();
    context.nowMs = nowMs;
    context.ready = app.businessReady() && config && journalReady_;
    context.recordWritable = IrrigationRecordSync::instance().writable();
    context.activeKind = activeKind(status);
    if (config) {
        context.plansRevision = config->revision;
        context.maximumZoneDurationMinutes =
            config->runLimits.maximumZoneDurationMinutes;
        context.maximumSingleOutputLiters =
            config->runLimits.maximumSingleOutputLiters;
        for (uint8_t index = 0; index < config->zones.size(); ++index) {
            context.enabledZones[index] = config->zones[index].enabled;
        }
    }
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    context.timeTrusted = now.synced;
    return context;
}

IrrigationIotProtocol::ActiveKind IrrigationIot::activeKind(
    const WateringStatus& status) const {
    if (!status.active) return IrrigationIotProtocol::ActiveKind::Idle;
    if (status.purpose == WateringPurpose::FlowCalibration)
        return IrrigationIotProtocol::ActiveKind::Calibration;
    if (status.purpose == WateringPurpose::ZoneFlowLearning)
        return IrrigationIotProtocol::ActiveKind::Learning;
    if (status.source == WateringSource::AutomaticPlan)
        return IrrigationIotProtocol::ActiveKind::Automatic;
    if (status.source == WateringSource::SingleOutput)
        return IrrigationIotProtocol::ActiveKind::SingleOutput;
    return IrrigationIotProtocol::ActiveKind::Manual;
}

void IrrigationIot::detectActivity(IrrigationApp& app, uint32_t nowMs) {
    const WateringStatus status = app.wateringStatus();
    if (status.active && !activityTracked_) {
        beginActivity(status, nullptr, IrrigationCommandJournal::kCapacity, nowMs);
        scheduleAllState();
    } else if (!status.active && activityTracked_) {
        const uint64_t observedAtMs = currentEpochMs();
        finishActivity(status, observedAtMs);
        finishStopCommands(observedAtMs, true);
        scheduleAllState();
    } else if (!status.active && pendingStopCount_ != 0U) {
        finishStopCommands(currentEpochMs(), true);
        scheduleAllState();
    }
}

void IrrigationIot::beginActivity(
    const WateringStatus& status,
    const IrrigationIotProtocol::Command* command,
    std::size_t journalIndex,
    uint32_t nowMs) {
    activityTracked_ = true;
    makeUuid(activityId_, sizeof(activityId_));
    activityStartedMs_ = nowMs;
    activityCommandId_[0] = '\0';
    activityJournalIndex_ = journalIndex;
    activityHasRequestedDuration_ = true;
    uint64_t requestedMs = 0;
    for (uint8_t index = 0; index < status.stepCount; ++index) {
        requestedMs += static_cast<uint64_t>(status.zones[index].plannedDurationSec) *
                       1000ULL;
    }
    if (command && command->kind == IrrigationIotProtocol::CommandKind::SingleOutput &&
        command->singleOutputMode == IrrigationIotProtocol::SingleOutputMode::Volume) {
        activityHasRequestedDuration_ = false;
    }
    if (requestedMs == 0U || requestedMs > UINT32_MAX) {
        activityHasRequestedDuration_ = false;
    }
    activityRequestedDurationMs_ = activityHasRequestedDuration_
                                       ? static_cast<uint32_t>(requestedMs)
                                       : 0U;
    if (command) {
        std::strcpy(activityCommandId_, command->commandId);
        activityCommandKind_ = command->kind;
    }
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    activityHasDeadline_ = activityHasRequestedDuration_ && time.synced;
    activityDeadlineEpoch_ = activityHasDeadline_
                                 ? time.epochSec +
                                       (activityRequestedDurationMs_ + 999U) / 1000U
                                 : 0U;
    lastRunningEvidenceMs_ = nowMs;
    lastActivityStateMs_ = nowMs;
}

void IrrigationIot::finishActivity(const WateringStatus& status,
                                   uint64_t observedAtMs) {
    if (activityCommandId_[0] != '\0' &&
        activityJournalIndex_ < IrrigationCommandJournal::kCapacity) {
        if (!connected_ || !subscriptionsReady_ || observedAtMs == 0U) {
            journal_.closeWithoutFinal(activityJournalIndex_);
        } else {
            IrrigationCommandJournal::ProgressStatus finalStatus =
                IrrigationCommandJournal::ProgressStatus::Failed;
            IrrigationCommandJournal::Reason reason =
                wateringFailureReason(status.lastStopReason);
            if (status.lastResult == WateringResult::Completed) {
                finalStatus = IrrigationCommandJournal::ProgressStatus::Succeeded;
                reason = IrrigationCommandJournal::Reason::None;
            } else if (status.lastResult == WateringResult::Stopped &&
                       status.lastStopReason == WateringStopReason::UserStopped) {
                finalStatus = IrrigationCommandJournal::ProgressStatus::Canceled;
                reason = IrrigationCommandJournal::Reason::None;
            }
            if (journal_.storeFinal(activityJournalIndex_, finalStatus, reason,
                                    observedAtMs)) {
                journalProgressDelivered_[activityJournalIndex_] = false;
                const IrrigationCommandJournal::Entry* stored =
                    journal_.entry(activityJournalIndex_);
                if (stored) queueJournalProgress(*stored);
            }
        }
    }
    activityTracked_ = false;
    activityId_[0] = '\0';
    activityCommandId_[0] = '\0';
    activityJournalIndex_ = IrrigationCommandJournal::kCapacity;
    activityHasRequestedDuration_ = false;
    activityHasDeadline_ = false;
}

void IrrigationIot::finishStopCommands(uint64_t observedAtMs, bool succeeded) {
    for (uint8_t index = 0; index < pendingStopCount_; ++index) {
        PendingStopCommand& pending = pendingStops_[index];
        if (!connected_ || !subscriptionsReady_ || observedAtMs == 0U) {
            journal_.closeWithoutFinal(pending.journalIndex);
        } else if (journal_.storeFinal(
                       pending.journalIndex,
                       succeeded
                           ? IrrigationCommandJournal::ProgressStatus::Succeeded
                           : IrrigationCommandJournal::ProgressStatus::Failed,
                       succeeded ? IrrigationCommandJournal::Reason::None
                                 : IrrigationCommandJournal::Reason::HardwareFailure,
                       observedAtMs)) {
            journalProgressDelivered_[pending.journalIndex] = false;
            const IrrigationCommandJournal::Entry* stored =
                journal_.entry(pending.journalIndex);
            if (stored) queueJournalProgress(*stored);
        }
        pending = {};
    }
    pendingStopCount_ = 0;
}

bool IrrigationIot::addPendingStopCommand(const char* commandId,
                                          std::size_t journalIndex) {
    if (!commandId || pendingStopCount_ >= IrrigationCommandJournal::kCapacity) {
        return false;
    }
    PendingStopCommand& pending = pendingStops_[pendingStopCount_++];
    std::strcpy(pending.commandId, commandId);
    pending.journalIndex = journalIndex;
    return true;
}

IrrigationCommandJournal::Reason IrrigationIot::wateringFailureReason(
    WateringStopReason reason) const {
    switch (reason) {
        case WateringStopReason::FlowStartTimeout:
            return IrrigationCommandJournal::Reason::FlowStartTimeout;
        case WateringStopReason::NoFlowTimeout:
            return IrrigationCommandJournal::Reason::NoFlowTimeout;
        case WateringStopReason::LowFlow:
            return IrrigationCommandJournal::Reason::LowFlow;
        case WateringStopReason::HighFlow:
            return IrrigationCommandJournal::Reason::HighFlow;
        case WateringStopReason::TargetVolumeTimeout:
            return IrrigationCommandJournal::Reason::TargetVolumeTimeout;
        case WateringStopReason::HardwareFailure:
            return IrrigationCommandJournal::Reason::HardwareFailure;
        default:
            return IrrigationCommandJournal::Reason::InternalState;
    }
}

void IrrigationIot::scheduleAllState() { pendingStateMask_ |= StateAll; }
void IrrigationIot::scheduleRuntimeState() {
    pendingStateMask_ |= StateRuntime | StateOverview;
}

void IrrigationIot::detectStateChanges(const IrrigationApp& app, uint32_t nowMs) {
    const uint64_t fingerprint = stateFingerprint(app);
    if (!stateFingerprintSet_ || fingerprint != lastStateFingerprint_) {
        lastStateFingerprint_ = fingerprint;
        stateFingerprintSet_ = true;
        scheduleAllState();
    }
    if (static_cast<uint32_t>(nowMs - lastStateScheduleMs_) >=
        IrrigationIotProtocol::kStateRepublishMs) {
        lastStateScheduleMs_ = nowMs;
        scheduleAllState();
    }
    if (activityTracked_ &&
        static_cast<uint32_t>(nowMs - lastActivityStateMs_) >=
            kRunningPublishIntervalMs) {
        lastActivityStateMs_ = nowMs;
        scheduleRuntimeState();
    }
}

uint64_t IrrigationIot::stateFingerprint(const IrrigationApp& app) const {
    uint64_t hash = kFnvOffset;
    const IrrigationConfig* config = app.configuration();
    const WateringStatus status = app.wateringStatus();
    hashValue(hash, app.businessReady());
    hashValue(hash, journalReady_);
    hashValue(hash, status.active);
    hashValue(hash, status.state);
    hashValue(hash, status.source);
    hashValue(hash, status.purpose);
    hashValue(hash, status.activeZoneId);
    hashValue(hash, status.lastResult);
    hashValue(hash, status.lastStopReason);
    hashValue(hash, app.unexpectedFlowAlarm());
    hashValue(hash, app.recordStorageFault());
    hashValue(hash, app.eventStorageFault());
    hashValue(hash, app.schedulerStorageFault());
    hashValue(hash, app.schedulerTimeState());
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    hashValue(hash, time.synced);
    hashValue(hash, time.source);
    const bool rtcAvailable = Esp32BaseRtc::isAvailable();
    hashValue(hash, rtcAvailable);
    if (config) hashValue(hash, config->revision);
    const AutomaticWateringState automatic = app.automaticWateringState();
    hashValue(hash, automatic.mode);
    hashValue(hash, automatic.resumeAtEpoch);
    return hash;
}

void IrrigationIot::queueEvidence(const Evidence& evidence) {
    if (evidence.observedAtMs == 0U || evidenceQueued(evidence)) return;
    if (evidenceCount_ >= kEvidenceCapacity) {
        ESP32BASE_LOG_E("irrigation_iot",
                        "evidence_queue_full command_id=%s",
                        evidence.commandId);
        return;
    }
    evidence_[evidenceWrite_] = evidence;
    evidenceWrite_ = (evidenceWrite_ + 1U) % kEvidenceCapacity;
    ++evidenceCount_;
}

void IrrigationIot::queueJournalReceipt(
    const IrrigationCommandJournal::Entry& entry) {
    Evidence evidence;
    std::strcpy(evidence.commandId, entry.commandId);
    evidence.kind = entry.kind;
    evidence.type = entry.receipt == IrrigationCommandJournal::ReceiptStatus::Accepted
                        ? EvidenceType::ReceiptAccepted
                        : EvidenceType::ReceiptRejected;
    evidence.reason = entry.receiptReason;
    evidence.observedAtMs = entry.receiptObservedAtMs;
    queueEvidence(evidence);
}

void IrrigationIot::queueJournalProgress(
    const IrrigationCommandJournal::Entry& entry) {
    Evidence evidence;
    std::strcpy(evidence.commandId, entry.commandId);
    evidence.kind = entry.kind;
    evidence.reason = entry.progressReason;
    evidence.observedAtMs = entry.progressObservedAtMs;
    switch (entry.progress) {
        case IrrigationCommandJournal::ProgressStatus::Succeeded:
            evidence.type = EvidenceType::ProgressSucceeded;
            break;
        case IrrigationCommandJournal::ProgressStatus::Canceled:
            evidence.type = EvidenceType::ProgressCanceled;
            break;
        case IrrigationCommandJournal::ProgressStatus::Failed:
            evidence.type = EvidenceType::ProgressFailed;
            break;
        case IrrigationCommandJournal::ProgressStatus::None:
            return;
    }
    queueEvidence(evidence);
}

void IrrigationIot::queuePendingJournalEvidence() {
    if (!journalReady_) return;
    for (std::size_t index = 0; index < IrrigationCommandJournal::kCapacity;
         ++index) {
        const IrrigationCommandJournal::Entry* entry = journal_.entry(index);
        if (!entry || entry->receipt ==
                          IrrigationCommandJournal::ReceiptStatus::None) {
            continue;
        }
        if (!journalReceiptDelivered_[index]) {
            queueJournalReceipt(*entry);
            if (evidenceCount_ >= kEvidenceCapacity) return;
        }
        if (entry->progress != IrrigationCommandJournal::ProgressStatus::None &&
            !journalProgressDelivered_[index]) {
            queueJournalProgress(*entry);
            if (evidenceCount_ >= kEvidenceCapacity) return;
        }
    }
}

void IrrigationIot::markJournalEvidenceDelivered(const Evidence& evidence) {
    const bool receipt = evidence.type == EvidenceType::ReceiptAccepted ||
                         evidence.type == EvidenceType::ReceiptRejected;
    const bool terminal = evidence.type == EvidenceType::ProgressSucceeded ||
                          evidence.type == EvidenceType::ProgressCanceled ||
                          evidence.type == EvidenceType::ProgressFailed;
    if (!receipt && !terminal) return;
    for (std::size_t index = 0; index < IrrigationCommandJournal::kCapacity;
         ++index) {
        const IrrigationCommandJournal::Entry* entry = journal_.entry(index);
        if (!entry || entry->kind != evidence.kind ||
            std::strcmp(entry->commandId, evidence.commandId) != 0) {
            continue;
        }
        if (receipt) journalReceiptDelivered_[index] = true;
        if (terminal) journalProgressDelivered_[index] = true;
        return;
    }
}

bool IrrigationIot::evidenceQueued(const Evidence& evidence) const {
    for (std::size_t offset = 0; offset < evidenceCount_; ++offset) {
        const Evidence& queued =
            evidence_[(evidenceRead_ + offset) % kEvidenceCapacity];
        if (queued.type == evidence.type && queued.kind == evidence.kind &&
            std::strcmp(queued.commandId, evidence.commandId) == 0) {
            return true;
        }
    }
    return false;
}

void IrrigationIot::removeEvidenceHead() {
    if (evidenceCount_ == 0U) return;
    evidence_[evidenceRead_] = {};
    evidenceRead_ = (evidenceRead_ + 1U) % kEvidenceCapacity;
    --evidenceCount_;
}

void IrrigationIot::pump(IrrigationApp& app) {
    if (!connected_ || !subscriptionsReady_ ||
        inFlightKind_ != InFlightKind::None) {
        return;
    }
    if (availabilityPending_) {
        publishAvailability();
        return;
    }
    queuePendingJournalEvidence();
    if (evidenceCount_ != 0U) {
        publishEvidence();
        return;
    }
    if (pendingStateMask_ != 0U) {
        publishState(app, static_cast<StateBit>(lowestStateBit(pendingStateMask_)));
        return;
    }
    const uint32_t nowMs = millis();
    for (uint8_t attempt = 0; attempt < 2U; ++attempt) {
        const IrrigationRecordSync::StreamKind stream =
            attempt == 0U
                ? nextRecordStream_
                : nextRecordStream_ == IrrigationRecordSync::StreamKind::Watering
                      ? IrrigationRecordSync::StreamKind::Audit
                      : IrrigationRecordSync::StreamKind::Watering;
        uint32_t& lastPublish =
            stream == IrrigationRecordSync::StreamKind::Watering
                ? lastWateringRecordPublishMs_
                : lastAuditRecordPublishMs_;
        if (IrrigationRecordSync::instance().pendingCount(stream) == 0U ||
            (lastPublish != 0U &&
             static_cast<uint32_t>(nowMs - lastPublish) <
                 IrrigationIotProtocol::kRecordAckRetryMs))
            continue;
        if (publishRecord(stream)) {
            nextRecordStream_ =
                stream == IrrigationRecordSync::StreamKind::Watering
                    ? IrrigationRecordSync::StreamKind::Audit
                    : IrrigationRecordSync::StreamKind::Watering;
        }
        return;
    }
}

bool IrrigationIot::publishAvailability() {
    char observedAt[25]{};
    if (!currentObservedAt(observedAt, sizeof(observedAt))) return false;
    char payload[320]{};
    const int length = std::snprintf(
        payload,
        sizeof(payload),
        "{\"protocol\":\"%s\",\"modelKey\":\"%s\",\"online\":true,"
        "\"connectionId\":\"%s\",\"observedAt\":\"%s\"}",
        IrrigationIotProtocol::kProtocol,
        IrrigationIotProtocol::kModelKey,
        connectionId_,
        observedAt);
    return length > 0 && static_cast<std::size_t>(length) < sizeof(payload) &&
           publishBuffer(availabilityTopic_, payload,
                         static_cast<std::size_t>(length), true,
                         InFlightKind::Availability);
}

bool IrrigationIot::publishEvidence() {
    if (evidenceCount_ == 0U) return false;
    const Evidence& evidence = evidence_[evidenceRead_];
    char observedAt[25]{};
    if (!IrrigationIotProtocol::formatTimestamp(
            static_cast<uint32_t>(evidence.observedAtMs / 1000ULL),
            static_cast<uint16_t>(evidence.observedAtMs % 1000ULL),
            observedAt, sizeof(observedAt))) {
        return false;
    }
    const bool receipt = evidence.type == EvidenceType::ReceiptAccepted ||
                         evidence.type == EvidenceType::ReceiptRejected;
    const bool failed = evidence.type == EvidenceType::ProgressFailed;
    const bool rejected = evidence.type == EvidenceType::ReceiptRejected;
    const char* status = nullptr;
    switch (evidence.type) {
        case EvidenceType::ReceiptAccepted: status = "accepted"; break;
        case EvidenceType::ReceiptRejected: status = "rejected"; break;
        case EvidenceType::ProgressRunning: status = "running"; break;
        case EvidenceType::ProgressSucceeded: status = "succeeded"; break;
        case EvidenceType::ProgressCanceled: status = "canceled"; break;
        case EvidenceType::ProgressFailed: status = "failed"; break;
    }
    char payload[512]{};
    const int length = (rejected || failed)
                           ? std::snprintf(
                                 payload, sizeof(payload),
                                 "{\"protocol\":\"%s\",\"connectionId\":\"%s\","
                                 "\"commandId\":\"%s\",\"capabilityKey\":\"%s\","
                                 "\"status\":\"%s\",\"reason\":\"%s\","
                                 "\"observedAt\":\"%s\"}",
                                 IrrigationIotProtocol::kProtocol, connectionId_,
                                 evidence.commandId,
                                 IrrigationIotProtocol::capabilityKey(evidence.kind),
                                 status,
                                 IrrigationCommandJournal::reasonName(evidence.reason),
                                 observedAt)
                           : std::snprintf(
                                 payload, sizeof(payload),
                                 "{\"protocol\":\"%s\",\"connectionId\":\"%s\","
                                 "\"commandId\":\"%s\",\"capabilityKey\":\"%s\","
                                 "\"status\":\"%s\",\"observedAt\":\"%s\"}",
                                 IrrigationIotProtocol::kProtocol, connectionId_,
                                 evidence.commandId,
                                 IrrigationIotProtocol::capabilityKey(evidence.kind),
                                 status, observedAt);
    return length > 0 && static_cast<std::size_t>(length) < sizeof(payload) &&
           publishBuffer(receipt ? receiptTopic_ : progressTopic_, payload,
                         static_cast<std::size_t>(length), false,
                         InFlightKind::Evidence);
}

bool IrrigationIot::publishRecord(IrrigationRecordSync::StreamKind stream) {
    std::size_t payloadLength = 0;
    if (!serializeRecord(stream, publishPayload_, sizeof(publishPayload_),
                         payloadLength)) return false;
    const bool accepted = publishBuffer(eventTopic_, publishPayload_,
                                        payloadLength, false,
                                        InFlightKind::Record);
    if (accepted) {
        uint32_t& lastPublish =
            stream == IrrigationRecordSync::StreamKind::Watering
                ? lastWateringRecordPublishMs_
                : lastAuditRecordPublishMs_;
        lastPublish = millis();
        inFlightRecordStream_ = stream;
    }
    return accepted;
}

bool IrrigationIot::serializeRecord(IrrigationRecordSync::StreamKind stream,
                                    char* output,
                                    std::size_t outputLength,
                                    std::size_t& payloadLength) {
    payloadLength = 0;
    IrrigationRecordSync::PendingRecord record;
    if (!IrrigationRecordSync::instance().readOldestPending(stream, record))
        return false;
    uint32_t completedEpoch = 0;
    const bool timed = Esp32BaseRecordStore::resolveCompletedEpoch(
        record.timing, completedEpoch);
    char observedAt[25]{};
    if (timed && !IrrigationIotProtocol::formatTimestamp(
                     completedEpoch, 0, observedAt, sizeof(observedAt)))
        return false;

    JsonDocument document;
    document["protocol"] = IrrigationIotProtocol::kProtocol;
    document["connectionId"] = connectionId_;
    document["recordStreamId"] = IrrigationRecordSync::instance().streamId(stream);
    document["recordSequence"] = record.sequence;
    if (timed) document["observedAt"] = observedAt;
    else document["observedAt"] = nullptr;
    JsonObject data = document["data"].to<JsonObject>();

    if (stream == IrrigationRecordSync::StreamKind::Watering) {
        const WateringRecordPayload& watering = record.watering;
        document["eventKey"] = watering.result == WateringResult::Completed
                                   ? "watering.completed"
                                   : watering.result == WateringResult::Stopped
                                         ? "watering.stopped"
                                         : "watering.failed";
        data["sourceKey"] = watering.source == WateringSource::AutomaticPlan
                                ? "device_schedule"
                                : "wechat_miniprogram";
        char commandId[IrrigationIotProtocol::kUuidBufferSize]{};
        if (!WateringRecordCodec::formatRelatedCommandId(
                watering, commandId, sizeof(commandId))) return false;
        if (commandId[0] != '\0') data["relatedCommandId"] = commandId;
        else data["relatedCommandId"] = nullptr;
        if (watering.source == WateringSource::AutomaticPlan)
            data["planId"] = watering.planId;
        else
            data["planId"] = nullptr;
        if (!timed || completedEpoch < record.timing.durationSec) return false;
        const uint32_t startedEpoch = completedEpoch - record.timing.durationSec;
        char startedAt[25]{};
        if (!IrrigationIotProtocol::formatTimestamp(startedEpoch, 0, startedAt,
                                                    sizeof(startedAt)))
            return false;
        data["startedAt"] = startedAt;
        data["completedAt"] = observedAt;
        data["durationSeconds"] = record.timing.durationSec;
        data["timeQuality"] = "trusted";
        data["result"] = watering.result == WateringResult::Completed
                             ? "completed"
                             : watering.result == WateringResult::Stopped
                                   ? "stopped"
                                   : "failed";
        data["reason"] = wateringReasonName(watering.stopReason);
        JsonArray zones = data["zones"].to<JsonArray>();
        for (uint8_t index = 0; index < watering.zones.size(); ++index) {
            const ZoneWateringRecord& source = watering.zones[index];
            if (source.plannedDurationSec == 0U) continue;
            JsonObject zone = zones.add<JsonObject>();
            zone["zoneId"] = index + 1U;
            zone["targetSeconds"] = source.plannedDurationSec;
            if (source.targetWaterMl != 0U)
                zone["targetWaterMl"] = source.targetWaterMl;
            else
                zone["targetWaterMl"] = nullptr;
            zone["actualSeconds"] = source.actualWateringSec;
            zone["zoneResult"] = zoneResultName(source.result);
            zone["pulseCount"] = source.pulseCount;
            zone["estimatedWaterMl"] = source.estimatedWaterMl;
            if ((source.flags & WateringRecordCodec::kZoneFlagFlowBaselineAvailable) != 0U) {
                zone["baselinePulseRateX10000"] = source.baselinePulseRateX10000;
                zone["baselineFlowMlPerMinute"] = source.baselineFlowMlPerMinute;
            } else {
                zone["baselinePulseRateX10000"] = nullptr;
                zone["baselineFlowMlPerMinute"] = nullptr;
            }
            if (source.result != ZoneWateringResult::NotStarted)
                zone["averageFlowMlPerMinute"] = source.averageFlowMlPerMinute;
            else
                zone["averageFlowMlPerMinute"] = nullptr;
            zone["lowFlowDetected"] =
                (source.flags & WateringRecordCodec::kZoneFlagLowFlow) != 0U;
            zone["highFlowDetected"] =
                (source.flags & WateringRecordCodec::kZoneFlagHighFlow) != 0U;
        }
    } else {
        const IrrigationAuditPayload& audit = record.audit;
        using AuditKind = IrrigationAuditPayload::Kind;
        if (audit.kind == AuditKind::AutomaticRun) {
            document["eventKey"] = "operation.automatic-run.completed";
            data["actionKey"] = "automatic.plan-run";
            data["sourceKey"] = "device_schedule";
            data["status"] = audit.flags == 0U ? "succeeded"
                               : audit.flags == 1U ? "canceled"
                               : audit.flags == 2U ? "failed" : "skipped";
            data["reason"] = audit.flags == 3U
                                 ? automaticAuditReason(audit.reason)
                                 : wateringReasonName(
                                       static_cast<WateringStopReason>(audit.reason));
            if (audit.flags == 3U || !timed) {
                data["startedAt"] = nullptr;
                data["durationSeconds"] = nullptr;
            } else {
                if (completedEpoch < record.timing.durationSec) return false;
                char startedAt[25]{};
                if (!IrrigationIotProtocol::formatTimestamp(
                        completedEpoch - record.timing.durationSec, 0,
                        startedAt, sizeof(startedAt))) return false;
                data["startedAt"] = startedAt;
                data["durationSeconds"] = record.timing.durationSec;
            }
            if (timed) data["endedAt"] = observedAt;
            else return false;
            JsonObject parameters = data["parameters"].to<JsonObject>();
            parameters["planId"] = audit.objectId;
        } else if (audit.kind == AuditKind::AutomaticStateChanged) {
            using Reason = IrrigationEvents::ReasonCode;
            const Reason reason = static_cast<Reason>(audit.reason);
            const bool resumed = reason == Reason::ResumedManually ||
                                 reason == Reason::ResumedAutomatically;
            document["eventKey"] = resumed ? "automatic.resumed" : "automatic.paused";
            if (reason == Reason::PausedIndefinitely) {
                data["mode"] = "paused-indefinitely";
                data["resumeAtEpoch"] = nullptr;
            } else if (reason == Reason::PausedUntil) {
                data["mode"] = "paused-until";
                data["resumeAtEpoch"] = audit.value1;
            } else if (reason == Reason::ResumedAutomatically) {
                data["mode"] = "expired";
            } else if (reason == Reason::ResumedManually) {
                data["mode"] = "enabled";
                data["resumeAtEpoch"] = nullptr;
            } else {
                return false;
            }
        } else if (audit.kind == AuditKind::PlansChanged) {
            document["eventKey"] = "configuration.plans-changed";
            data["revision"] = audit.value1;
            JsonArray planIds = data["planIds"].to<JsonArray>();
            for (uint8_t planId = 1U; planId <= 8U; ++planId)
                if ((audit.value2 & (1UL << (planId - 1U))) != 0U)
                    planIds.add(planId);
        } else if (audit.kind == AuditKind::CalibrationSaved) {
            document["eventKey"] = "calibration.result-saved";
            data["coefficientPulsesPerLiterX100"] = audit.value1;
            data["pulseCount"] = audit.value2;
            data["waterMl"] = audit.value3;
        } else if (audit.kind == AuditKind::ZoneBaselineSaved) {
            document["eventKey"] = "zone.baseline-saved";
            data["zoneId"] = audit.objectId;
            data["baselinePulseRateX10000"] = audit.value1;
            data["baselineFlowMlPerMinute"] = audit.value2;
        } else {
            return false;
        }
    }
    if (document.overflowed()) return false;
    payloadLength = serializeJson(document, output, outputLength);
    if (payloadLength == 0U || payloadLength >= outputLength ||
        payloadLength > ESP32BASE_MQTT_MAX_PAYLOAD_BYTES) {
        payloadLength = 0U;
        return false;
    }
    inFlightRecordSequence_ = record.sequence;
    return true;
}

bool IrrigationIot::publishState(IrrigationApp& app, StateBit state) {
    std::size_t payloadLength = 0;
    if (!serializeState(app, state, publishPayload_, sizeof(publishPayload_),
                        payloadLength)) {
        return false;
    }
    const bool accepted = publishBuffer(stateTopic_, publishPayload_,
                                        payloadLength, false,
                                        InFlightKind::State,
                                        static_cast<uint16_t>(state));
    if (accepted) ++stateSeq_;
    return accepted;
}

bool IrrigationIot::serializeState(IrrigationApp& app,
                                   StateBit state,
                                   char* output,
                                   std::size_t outputLength,
                                   std::size_t& payloadLength) {
    payloadLength = 0;
    char observedAt[25]{};
    if (!currentObservedAt(observedAt, sizeof(observedAt))) return false;
    const IrrigationConfig* config = app.configuration();
    if (!config) return false;
    const WateringStatus status = app.wateringStatus();

    JsonDocument document;
    document["protocol"] = IrrigationIotProtocol::kProtocol;
    document["connectionId"] = connectionId_;
    document["seq"] = stateSeq_;
    document["observedAt"] = observedAt;
    JsonObject value = document["value"].to<JsonObject>();

    if (state == StateRuntime) {
        document["capabilityKey"] = "state.runtime";
        const bool ready = app.businessReady() && !app.schedulerStorageFault() &&
                           journalReady_;
        value["ready"] = ready;
        value["readyReason"] = !app.businessReady()
                                   ? "startup_check_failed"
                                   : !config
                                         ? "configuration_unavailable"
                                         : app.schedulerStorageFault()
                                               ? "scheduler_storage_unavailable"
                                               : !journalReady_
                                                     ? "configuration_unavailable"
                                                     : "none";
        const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
        JsonObject timeJson = value["time"].to<JsonObject>();
        timeJson["trusted"] = time.synced;
        timeJson["source"] = !time.synced
                                  ? "none"
                                  : time.source == Esp32BaseTime::SOURCE_NTP ? "ntp"
                                                                            : "rtc";
        if (time.synced) timeJson["epoch"] = time.epochSec;
        else timeJson["epoch"] = nullptr;
        timeJson["rtcAvailable"] = Esp32BaseRtc::isAvailable();
        timeJson["rtcRollback"] =
            app.schedulerTimeState() == WateringScheduler::TimeState::RtcRollback;

        JsonObject next = value["nextAutomatic"].to<JsonObject>();
        const AutomaticWateringState automatic = app.automaticWateringState();
        if (automatic.mode == AutomaticWateringMode::PausedIndefinitely) {
            next["status"] = "paused-indefinitely";
            next["planId"] = nullptr;
            next["scheduledAtEpoch"] = nullptr;
        } else if (automatic.mode == AutomaticWateringMode::PausedUntil) {
            next["status"] = "paused-until";
            next["planId"] = nullptr;
            next["scheduledAtEpoch"] = automatic.resumeAtEpoch;
        } else {
            const NextAutomaticWatering candidate = app.nextAutomaticWatering();
            switch (candidate.status) {
                case NextAutomaticWateringStatus::Available:
                    next["status"] = "available";
                    next["planId"] = candidate.planId;
                    next["scheduledAtEpoch"] = candidate.scheduledEpoch;
                    break;
                case NextAutomaticWateringStatus::NoEnabledPlans:
                    next["status"] = "no-enabled-plans";
                    next["planId"] = nullptr;
                    next["scheduledAtEpoch"] = nullptr;
                    break;
                case NextAutomaticWateringStatus::TimeUnavailable:
                    next["status"] = "time-unavailable";
                    next["planId"] = nullptr;
                    next["scheduledAtEpoch"] = nullptr;
                    break;
                case NextAutomaticWateringStatus::RtcRollback:
                    next["status"] = "rtc-rollback";
                    next["planId"] = nullptr;
                    next["scheduledAtEpoch"] = nullptr;
                    break;
                case NextAutomaticWateringStatus::PausedIndefinitely:
                    next["status"] = "paused-indefinitely";
                    next["planId"] = nullptr;
                    next["scheduledAtEpoch"] = nullptr;
                    break;
            }
        }

        JsonObject activity = value["activity"].to<JsonObject>();
        activity["kind"] = activityKindName(status);
        if (status.active && activityCommandId_[0] != '\0')
            activity["commandId"] = activityCommandId_;
        else activity["commandId"] = nullptr;
        if (status.active && status.activeZoneId != 0)
            activity["zoneId"] = status.activeZoneId;
        else activity["zoneId"] = nullptr;
        activity["phase"] = status.active ? phaseName(status.state) : "idle";
        if (status.active && activityTracked_) activity["activityId"] = activityId_;
        else activity["activityId"] = nullptr;
        if (status.active && activityHasRequestedDuration_)
            activity["requestedDurationMs"] = activityRequestedDurationMs_;
        else activity["requestedDurationMs"] = nullptr;
        const uint32_t activityElapsedMs =
            status.active && activityHasRequestedDuration_
                ? std::min(static_cast<uint32_t>(millis() - activityStartedMs_),
                           activityRequestedDurationMs_)
                : status.active ? status.elapsedMs : 0U;
        const uint32_t activityRemainingMs =
            status.active && activityHasRequestedDuration_
                ? activityRequestedDurationMs_ - activityElapsedMs
                : 0U;
        activity["elapsedMs"] = activityElapsedMs;
        activity["remainingMs"] = activityRemainingMs;
        if (status.active && activityHasDeadline_) {
            char deadline[25]{};
            IrrigationIotProtocol::formatTimestamp(activityDeadlineEpoch_, 0,
                                                   deadline, sizeof(deadline));
            activity["deadlineAt"] = deadline;
        } else {
            activity["deadlineAt"] = nullptr;
        }
        activity["timeQuality"] =
            status.active && activityHasDeadline_ ? "synchronized" : "uncertain";
        activity["clockUncertaintyMs"] = nullptr;
        if (status.active && status.currentZoneTargetWaterMl != 0)
            activity["targetWaterMl"] = status.currentZoneTargetWaterMl;
        else activity["targetWaterMl"] = nullptr;
        uint64_t pulseCount = 0;
        for (uint8_t index = 0; index < status.stepCount; ++index)
            pulseCount += status.zones[index].pulseCount;
        activity["pulseCount"] = pulseCount;
        activity["estimatedWaterMl"] = status.totalEstimatedWaterMl;
        if (status.active &&
            (status.state == WateringState::WaitingForFlow ||
             status.state == WateringState::WateringZone))
            activity["flowMlPerMinute"] = status.currentFlowMlPerMinute;
        else activity["flowMlPerMinute"] = nullptr;
        const ZoneWateringSummary* currentZone =
            status.active && status.currentStepIndex < status.stepCount
                ? &status.zones[status.currentStepIndex]
                : nullptr;
        activity["lowFlowActive"] = currentZone ? currentZone->lowFlowActive : false;
        activity["highFlowActive"] = currentZone ? currentZone->highFlowActive : false;
        JsonArray steps = activity["steps"].to<JsonArray>();
        for (uint8_t index = 0; status.active && index < status.stepCount; ++index) {
            JsonObject step = steps.add<JsonObject>();
            step["zoneId"] = status.zones[index].zoneId;
            step["targetDurationMs"] =
                status.zones[index].plannedDurationSec * 1000U;
            if (status.zones[index].targetWaterMl != 0)
                step["targetWaterMl"] = status.zones[index].targetWaterMl;
            else step["targetWaterMl"] = nullptr;
            step["status"] = index < status.currentStepIndex
                                   ? "completed"
                                   : index == status.currentStepIndex ? "current"
                                                                      : "pending";
        }
        JsonArray faults = value["faults"].to<JsonArray>();
        if (!Esp32BaseRtc::isAvailable()) faults.add("rtc_unavailable");
        if (!time.synced) faults.add("time_unavailable");
        if (app.schedulerTimeState() == WateringScheduler::TimeState::RtcRollback)
            faults.add("rtc_rollback");
        if (app.unexpectedFlowAlarm()) faults.add("unexpected_flow");
        if (!journalReady_) faults.add("configuration_storage");
        if (app.schedulerStorageFault()) faults.add("scheduler_storage");
        if (app.recordStorageFault()) faults.add("record_storage");
        if (app.eventStorageFault()) faults.add("event_storage");
    } else if (state == StateOverview) {
        document["capabilityKey"] = "state.overview";
        const bool critical = !app.businessReady() || !journalReady_ ||
                              app.unexpectedFlowAlarm() ||
                              app.schedulerStorageFault();
        const bool warning = !Esp32BaseRtc::isAvailable() ||
                             app.recordStorageFault() || app.eventStorageFault() ||
                             app.schedulerTimeState() !=
                                 WateringScheduler::TimeState::Ready;
        value["activity"] = status.active ? "active" : "idle";
        value["health"] = critical ? "critical" : warning ? "warning" : "normal";
    } else if (state == StatePlans) {
        document["capabilityKey"] = "parameter.plans";
        value["revision"] = config->revision;
        JsonArray plans = value["plans"].to<JsonArray>();
        for (const WateringPlan& plan : config->plans) {
            if (!plan.configured) continue;
            JsonObject item = plans.add<JsonObject>();
            item["id"] = plan.id;
            item["name"] = plan.name.data();
            item["automaticEnabled"] = plan.scheduleEnabled;
            JsonArray starts = item["startMinutes"].to<JsonArray>();
            for (uint16_t minute : plan.startMinutes)
                if (minute != kUnusedStartMinute) starts.add(minute);
            JsonArray zones = item["zones"].to<JsonArray>();
            for (uint8_t index = 0; index < config->zones.size(); ++index) {
                if (!config->zones[index].enabled ||
                    plan.zoneDurationMinutes[index] == 0)
                    continue;
                JsonObject zone = zones.add<JsonObject>();
                zone["zoneId"] = config->zones[index].id;
                zone["durationMinutes"] = plan.zoneDurationMinutes[index];
            }
        }
    } else if (state == StateAutomatic) {
        document["capabilityKey"] = "parameter.automatic-watering";
        const AutomaticWateringState automatic = app.automaticWateringState();
        if (automatic.mode == AutomaticWateringMode::Enabled) {
            value["mode"] = "enabled";
            value["resumeAtEpoch"] = nullptr;
        } else if (automatic.mode == AutomaticWateringMode::PausedIndefinitely) {
            value["mode"] = "paused-indefinitely";
            value["resumeAtEpoch"] = nullptr;
        } else {
            value["mode"] = "paused-until";
            value["resumeAtEpoch"] = automatic.resumeAtEpoch;
        }
    } else if (state == StateZones || state == StateZoneMaintenance) {
        document["capabilityKey"] =
            state == StateZones ? "state.zones" : "state.zone-maintenance";
        JsonArray zones = value["zones"].to<JsonArray>();
        for (const ZoneConfig& zoneConfig : config->zones) {
            if (!zoneConfig.enabled) continue;
            JsonObject zone = zones.add<JsonObject>();
            zone["zoneId"] = zoneConfig.id;
            if (state == StateZones) {
                zone["name"] = zoneConfig.name.data();
            } else {
                if (zoneConfig.baselinePulseRateX10000 != 0) {
                    zone["baselinePulseRateX10000"] =
                        zoneConfig.baselinePulseRateX10000;
                    uint32_t flow = 0;
                    FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
                        zoneConfig.baselinePulseRateX10000,
                        config->flowMeter.pulsesPerLiterX100,
                        flow);
                    zone["baselineFlowMlPerMinute"] = flow;
                } else {
                    zone["baselinePulseRateX10000"] = nullptr;
                    zone["baselineFlowMlPerMinute"] = nullptr;
                }
            }
        }
    } else if (state == StateCalibration) {
        document["capabilityKey"] = "state.calibration";
        value["coefficientPulsesPerLiterX100"] =
            config->flowMeter.pulsesPerLiterX100;
        value["startupPulseCount"] =
            config->flowMeter.calibrationStartupPulseCount;
        value["startupWaterMl"] = config->flowMeter.calibrationStartupWaterMl;
    } else if (state == StateSystemParameters) {
        document["capabilityKey"] = "state.system-parameters";
        JsonObject valve = value["valve"].to<JsonObject>();
        valve["pullInTimeMs"] = config->valveDrive.pullInTimeMs;
        valve["switchDelayMs"] = config->valveDrive.switchDelayMs;
        valve["pwmFrequencyHz"] = config->valveDrive.pwmFrequencyHz;
        valve["holdDutyPercent"] = config->valveDrive.holdDutyPercent;
        JsonObject pump = value["pump"].to<JsonObject>();
        pump["enabled"] = config->pump.enabled;
        pump["startDelayMs"] = config->pump.startDelayMs;
        pump["stopToValveCloseDelayMs"] = config->pump.stopToValveCloseDelayMs;
        JsonObject meter = value["meter"].to<JsonObject>();
        meter["pulsesPerLiterX100"] = config->flowMeter.pulsesPerLiterX100;
        meter["calibrationWindowSeconds"] = config->calibrationStability.windowSec;
        meter["calibrationRequiredWindows"] =
            config->calibrationStability.requiredWindows;
        meter["calibrationAllowedVariationPercent"] =
            config->calibrationStability.allowedVariationPercent;
        meter["flowStartTimeoutSeconds"] =
            config->flowProtection.flowStartTimeoutSec;
        meter["noFlowTimeoutSeconds"] = config->flowProtection.noFlowTimeoutSec;
        JsonObject flow = value["flow"].to<JsonObject>();
        flow["unexpectedFlowDelaySeconds"] =
            config->flowProtection.unexpectedFlowDelaySec;
        flow["unexpectedFlowWindowSeconds"] =
            config->flowProtection.unexpectedFlowWindowSec;
        flow["unexpectedFlowPulseCount"] =
            config->flowProtection.unexpectedFlowPulseCount;
        flow["deviationConfirmSeconds"] =
            config->flowProtection.flowDeviationConfirmSec;
        flow["lowFlowPercent"] = config->flowProtection.lowFlowPercent;
        flow["highFlowPercent"] = config->flowProtection.highFlowPercent;
        flow["lowFlowAction"] = flowActionName(config->flowProtection.lowFlowAction);
        flow["highFlowAction"] = flowActionName(config->flowProtection.highFlowAction);
        JsonObject limits = value["limits"].to<JsonObject>();
        limits["maximumZoneDurationMinutes"] =
            config->runLimits.maximumZoneDurationMinutes;
        limits["maximumSingleOutputLiters"] =
            config->runLimits.maximumSingleOutputLiters;
        JsonObject system = value["system"].to<JsonObject>();
        system["rtcRollbackThresholdMinutes"] =
            config->timeSafety.rtcRollbackThresholdMinutes;
        system["aliveCheckpointHours"] = config->timeSafety.aliveCheckpointHours;
    } else {
        return false;
    }

    if (document.overflowed()) return false;
    payloadLength = serializeJson(document, output, outputLength);
    if (payloadLength == 0U || payloadLength >= outputLength ||
        payloadLength > ESP32BASE_MQTT_MAX_PAYLOAD_BYTES) {
        payloadLength = 0;
        return false;
    }
    return true;
}

bool IrrigationIot::publishBuffer(const char* topic,
                                  const char* payload,
                                  std::size_t payloadLength,
                                  bool retain,
                                  InFlightKind kind,
                                  uint16_t stateBit) {
    Esp32BaseMqtt::PublishRequest request;
    request.topic = topic;
    request.payload = reinterpret_cast<const uint8_t*>(payload);
    request.payloadLength = payloadLength;
    request.qos = Esp32BaseMqtt::QOS_1;
    request.retain = retain;
    const Esp32BaseMqtt::PublishResult result = Esp32BaseMqtt::publish(request);
    if (!result.accepted()) return false;
    inFlightKind_ = kind;
    inFlightPacketId_ = result.packetId;
    inFlightStateBit_ = stateBit;
    return true;
}

bool IrrigationIot::currentObservedAt(char* output,
                                      std::size_t outputLength,
                                      uint64_t* epochMs) const {
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    if (!now.synced || now.epochSec == 0U) return false;
    if (epochMs) *epochMs = static_cast<uint64_t>(now.epochSec) * 1000ULL;
    return IrrigationIotProtocol::formatTimestamp(now.epochSec, 0, output,
                                                   outputLength);
}

uint64_t IrrigationIot::currentEpochMs() const {
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    return now.synced ? static_cast<uint64_t>(now.epochSec) * 1000ULL : 0ULL;
}

void IrrigationIot::markPublishAcknowledged(uint16_t packetId) {
    if (inFlightKind_ == InFlightKind::None || packetId != inFlightPacketId_) {
        return;
    }
    switch (inFlightKind_) {
        case InFlightKind::Availability:
            availabilityPending_ = false;
            break;
        case InFlightKind::Evidence:
            if (evidenceCount_ != 0U) {
                markJournalEvidenceDelivered(evidence_[evidenceRead_]);
            }
            removeEvidenceHead();
            break;
        case InFlightKind::State:
            pendingStateMask_ &= static_cast<uint16_t>(~inFlightStateBit_);
            break;
        case InFlightKind::Record:
            if (inFlightRecordStream_ ==
                IrrigationRecordSync::StreamKind::Watering)
                lastWateringRecordPublishMs_ = millis();
            else
                lastAuditRecordPublishMs_ = millis();
            break;
        case InFlightKind::None:
            break;
    }
    inFlightKind_ = InFlightKind::None;
    inFlightPacketId_ = 0;
    inFlightStateBit_ = 0;
    inFlightRecordSequence_ = 0;
}

void IrrigationIot::resetConnectionDelivery() {
    availabilityPending_ = false;
    inFlightKind_ = InFlightKind::None;
    inFlightPacketId_ = 0;
    inFlightStateBit_ = 0;
    inFlightRecordSequence_ = 0;
    lastWateringRecordPublishMs_ = 0;
    lastAuditRecordPublishMs_ = 0;
    nextRecordStream_ = IrrigationRecordSync::StreamKind::Watering;
    scheduleAllState();
}
