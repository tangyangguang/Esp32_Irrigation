#include "IrrigationMqttAdapter.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Esp32Base.h>
#include <esp_crt_bundle.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "FlowMonitor.h"
#include "IrrigationApp.h"
#include "IrrigationConfig.h"

#if __has_include("../../local_private/IrrigationMqttPrivate.h")
#include "../../local_private/IrrigationMqttPrivate.h"
#else
#define IRRIGATION_MQTT_ENABLED 0
#define IRRIGATION_MQTT_BROKER_URI ""
#define IRRIGATION_MQTT_USERNAME ""
#define IRRIGATION_MQTT_PASSWORD ""
#define IRRIGATION_MQTT_DEFINITION_SHA256 ""
#endif

namespace {

constexpr uint32_t kReconnectDelayMs = 5000U;
constexpr uint32_t kActiveRuntimeIntervalMs = 1000U;
constexpr uint32_t kIdleRuntimeIntervalMs = 5000U;
constexpr uint32_t kProgressIntervalMs = 5000U;
constexpr uint32_t kFullStateIntervalMs =
    IrrigationPlatformProtocol::kStateFreshnessSeconds * 800U;

const char* wateringFailure(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::UserStopped: return "stopped";
        case WateringStopReason::FlowStartTimeout: return "flow_start_timeout";
        case WateringStopReason::NoFlowTimeout: return "no_flow";
        case WateringStopReason::LowFlow: return "low_flow";
        case WateringStopReason::HighFlow: return "high_flow";
        case WateringStopReason::LearningTimeout: return "learning_timeout";
        case WateringStopReason::HardwareFailure: return "hardware_failure";
        case WateringStopReason::MaintenanceInterrupted: return "maintenance_interrupted";
        case WateringStopReason::TargetVolumeTimeout: return "target_volume_timeout";
        default: return "internal_state";
    }
}

const char* automaticModeName(AutomaticWateringMode mode) {
    switch (mode) {
        case AutomaticWateringMode::Enabled: return "enabled";
        case AutomaticWateringMode::PausedIndefinitely: return "paused-indefinitely";
        case AutomaticWateringMode::PausedUntil: return "paused-until";
    }
    return "enabled";
}

const char* flowActionName(FlowAlertAction action) {
    return action == FlowAlertAction::StopWatering ? "stop" : "alert";
}

const char* wateringSourceName(WateringSource source) {
    switch (source) {
        case WateringSource::ManualZones: return "manual";
        case WateringSource::SingleOutput: return "single-output";
        case WateringSource::AutomaticPlan: return "automatic";
    }
    return "manual";
}

const char* wateringResultName(WateringResult result) {
    switch (result) {
        case WateringResult::Completed: return "completed";
        case WateringResult::Stopped: return "stopped";
        case WateringResult::Failed: return "failed";
        default: return "failed";
    }
}

const char* zoneResultName(ZoneWateringResult result) {
    switch (result) {
        case ZoneWateringResult::Completed: return "completed";
        case ZoneWateringResult::Stopped: return "stopped";
        case ZoneWateringResult::Failed: return "failed";
        default: return "not-started";
    }
}

uint32_t fingerprint(const WateringStatus& status,
                     const AutomaticWateringState& automatic,
                     bool ready) {
    uint32_t value = ready ? 2166136261U : 16777619U;
    const uint32_t items[] = {
        status.active, static_cast<uint32_t>(status.state),
        static_cast<uint32_t>(status.source), status.planId, status.activeZoneId,
        status.currentStepIndex, status.elapsedSec, status.currentZoneElapsedSec,
        status.currentZoneRemainingSec, status.pulseCount,
        status.currentFlowMlPerMinute, static_cast<uint32_t>(status.lastResult),
        static_cast<uint32_t>(status.lastStopReason),
        static_cast<uint32_t>(automatic.mode), automatic.resumeAtEpoch,
    };
    for (uint32_t item : items) {
        value ^= item;
        value *= 16777619U;
    }
    return value;
}

}  // namespace

bool IrrigationMqttAdapter::begin(IrrigationApp& app) {
    app_ = &app;
    if (IRRIGATION_MQTT_ENABLED != 1) {
        ESP32BASE_LOG_I("irrigation_mqtt", "platform_mqtt_disabled_no_private_config");
        return true;
    }
    if (std::strncmp(IRRIGATION_MQTT_BROKER_URI, "mqtts://", 8) != 0 ||
        IRRIGATION_MQTT_USERNAME[0] == '\0' || IRRIGATION_MQTT_PASSWORD[0] == '\0' ||
        std::strcmp(IRRIGATION_MQTT_DEFINITION_SHA256,
                    IrrigationPlatformProtocol::kDefinitionSha256) != 0) {
        ESP32BASE_LOG_E("irrigation_mqtt", "private_config_invalid_or_definition_mismatch");
        return false;
    }
    if (!preferences_.begin("irr_mqtt", false) || !loadOrCreateDeviceId() ||
        !loadHistory()) {
        ESP32BASE_LOG_E("irrigation_mqtt", "identity_or_command_history_unavailable");
        return false;
    }
    enabled_ = true;
    reconnectAtMs_ = millis();
    return true;
}

void IrrigationMqttAdapter::handle() {
    if (!enabled_ || !app_) return;
    const uint32_t now = millis();
    refreshTrustedTime(now);
    if (restartPending_ && static_cast<int32_t>(now - reconnectAtMs_) >= 0) {
        stopClient();
        restartPending_ = false;
    }
    if (!client_ && !restartPending_ &&
        static_cast<int32_t>(now - reconnectAtMs_) >= 0 &&
        trustedTime_.available()) {
        if (!startClient()) reconnectAtMs_ = now + kReconnectDelayMs;
    }

    QueuedPacket packet;
    if (popIncoming(packet)) processPacket(packet);
    updateRemoteOperation();
    if (!connected_) return;

    if (replayProgressPending_) {
        for (const HistoryEntry& entry : history_.entries()) {
            if (entry.used && IrrigationMqttCore::shouldReplayProgress(entry.progress))
                publishProgress(entry);
        }
        replayProgressPending_ = false;
    }

    if ((remoteOperation_.hasActive() || remoteOperation_.hasPendingStop()) &&
        static_cast<uint32_t>(now - lastProgressPublishMs_) >= kProgressIntervalMs) {
        if (remoteOperation_.hasActive()) {
            if (HistoryEntry* entry = findHistory(remoteOperation_.activeCommandId()))
                publishProgress(*entry);
        }
        if (remoteOperation_.hasPendingStop()) {
            if (HistoryEntry* entry = findHistory(remoteOperation_.pendingStopCommandId()))
                publishProgress(*entry);
        }
        lastProgressPublishMs_ = now;
    }

    const IrrigationConfig* config = app_->configuration();
    const WateringStatus status = app_->wateringStatus();
    const AutomaticWateringState automatic = app_->automaticWateringState();
    const uint32_t currentFingerprint = fingerprint(status, automatic, app_->businessReady());
    if (publishAllPending_ ||
        static_cast<uint32_t>(now - lastStatePublishMs_) >= kFullStateIntervalMs ||
        (config && config->revision != lastConfigRevision_)) {
        publishAllState();
        publishAllPending_ = false;
        lastStatePublishMs_ = now;
        lastRuntimePublishMs_ = now;
        lastConfigRevision_ = config ? config->revision : 0;
        lastRuntimeFingerprint_ = currentFingerprint;
    } else {
        const uint32_t interval = status.active ? kActiveRuntimeIntervalMs
                                                : kIdleRuntimeIntervalMs;
        if (currentFingerprint != lastRuntimeFingerprint_ ||
            static_cast<uint32_t>(now - lastRuntimePublishMs_) >= interval) {
            publishRuntime();
            lastRuntimePublishMs_ = now;
            lastRuntimeFingerprint_ = currentFingerprint;
        }
    }
    publishNextWateringEvent();
    publishNextAppEvent();
}

bool IrrigationMqttAdapter::enabled() const { return enabled_; }
bool IrrigationMqttAdapter::connected() const { return connected_; }

esp_err_t IrrigationMqttAdapter::mqttEvent(esp_mqtt_event_handle_t event) {
    if (event && event->user_context)
        static_cast<IrrigationMqttAdapter*>(event->user_context)->onMqttEvent(event);
    return ESP_OK;
}

void IrrigationMqttAdapter::onMqttEvent(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED: {
            connected_ = true;
            char topic[192]{};
            makeTopic(IrrigationMqttCore::Channel::Command, topic, sizeof(topic));
            esp_mqtt_client_subscribe(client_, topic, 1);
            const auto decision = IrrigationMqttCore::connectedDecision();
            if (decision.publishOnlineAvailability) publishAvailability(true);
            publishAllPending_ = decision.publishFullState;
            lastProgressPublishMs_ = millis();
            wateringEventCursor_.disconnected();
            prepareWateringEventCursor();
            appEventCursor_.disconnected();
            prepareAppEventCursor();
            replayProgressPending_ = decision.replayKnownProgress;
            ESP32BASE_LOG_I("irrigation_mqtt", "connected");
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            connected_ = false;
            wateringEventCursor_.disconnected();
            appEventCursor_.disconnected();
            restartPending_ = true;
            reconnectAtMs_ = millis() + kReconnectDelayMs;
            ESP32BASE_LOG_W("irrigation_mqtt", "disconnected_reconnect_scheduled");
            break;
        case MQTT_EVENT_DATA:
            queueIncoming(*event);
            break;
        case MQTT_EVENT_PUBLISHED:
            if (const uint32_t recordId =
                    wateringEventCursor_.matchingAckRecord(event->msg_id)) {
                const bool saved = preferences_.putUInt("watering_cursor", recordId) ==
                                   sizeof(recordId);
                wateringEventCursor_.commitAck(event->msg_id, saved);
            }
            if (const uint32_t recordId =
                    appEventCursor_.matchingAckRecord(event->msg_id)) {
                const bool saved = preferences_.putUInt("app_evt_cursor", recordId) ==
                                   sizeof(recordId);
                appEventCursor_.commitAck(event->msg_id, saved);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP32BASE_LOG_W("irrigation_mqtt", "transport_error");
            break;
        default:
            break;
    }
}

bool IrrigationMqttAdapter::startClient() {
    makeConnectionId();
    makeTopic(IrrigationMqttCore::Channel::Availability,
              availabilityTopic_.data(), availabilityTopic_.size());
    if (!IrrigationPlatformProtocol::formatAvailability(
            false, connectionId_.data(), "lwt", nullptr, lwtPayload_.data(),
            lwtPayload_.size())) return false;
    esp_mqtt_client_config_t config{};
    config.event_handle = mqttEvent;
    config.uri = IRRIGATION_MQTT_BROKER_URI;
    config.client_id = deviceId_.data();
    config.username = IRRIGATION_MQTT_USERNAME;
    config.password = IRRIGATION_MQTT_PASSWORD;
    config.lwt_topic = availabilityTopic_.data();
    config.lwt_msg = lwtPayload_.data();
    config.lwt_msg_len = std::strlen(lwtPayload_.data());
    const auto lwtPolicy =
        IrrigationMqttCore::publicationPolicy(IrrigationMqttCore::Channel::Availability);
    config.lwt_qos = lwtPolicy.qos;
    config.lwt_retain = lwtPolicy.retain;
    config.disable_clean_session = 0;
    config.disable_auto_reconnect = true;
    config.keepalive = 60;
    config.buffer_size = 4096;
    config.out_buffer_size = 4096;
    config.network_timeout_ms = 10000;
    config.reconnect_timeout_ms = 5000;
    config.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
    config.skip_cert_common_name_check = false;
    config.user_context = this;
    client_ = esp_mqtt_client_init(&config);
    if (!client_ || esp_mqtt_client_start(client_) != ESP_OK) {
        stopClient();
        return false;
    }
    return true;
}

void IrrigationMqttAdapter::stopClient() {
    connected_ = false;
    if (!client_) return;
    esp_mqtt_client_stop(client_);
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    assemblyLength_ = 0;
    assemblyExpected_ = 0;
}

void IrrigationMqttAdapter::queueIncoming(const esp_mqtt_event_t& event) {
    if (event.current_data_offset == 0) {
        if (!event.topic || event.topic_len < 0 ||
            !IrrigationMqttCore::matchesCommandTopic(
                event.topic, static_cast<std::size_t>(event.topic_len),
                deviceId_.data()) ||
            event.total_data_len <= 0 ||
            event.total_data_len > static_cast<int>(IrrigationPlatformProtocol::kMaximumCommandBytes)) {
            assemblyExpected_ = 0;
            return;
        }
        assemblyExpected_ = static_cast<uint16_t>(event.total_data_len);
        assemblyLength_ = 0;
        assemblyQos_ = static_cast<uint8_t>(event.qos);
        assemblyRetain_ = event.retain;
    }
    if (assemblyExpected_ == 0 || event.current_data_offset != assemblyLength_ ||
        event.data_len < 0 || assemblyLength_ + event.data_len > assemblyExpected_) {
        assemblyExpected_ = 0;
        assemblyLength_ = 0;
        return;
    }
    std::memcpy(assembly_.data() + assemblyLength_, event.data,
                static_cast<std::size_t>(event.data_len));
    assemblyLength_ += static_cast<uint16_t>(event.data_len);
    if (assemblyLength_ != assemblyExpected_) return;
    assembly_[assemblyLength_] = '\0';
    const bool stop = IrrigationMqttCore::isStopCommandEnvelope(
        assembly_.data(), assemblyLength_);
    portENTER_CRITICAL(&queueMux_);
    const auto queued = commandQueue_.pushClassified(
        assembly_.data(), assemblyLength_, assemblyQos_, assemblyRetain_, stop);
    portEXIT_CRITICAL(&queueMux_);
    if (queued == IrrigationMqttCore::QueuePushResult::NormalSlotFull)
        ESP32BASE_LOG_W("irrigation_mqtt", "normal_command_queue_full");
    else if (queued == IrrigationMqttCore::QueuePushResult::StopSlotFull)
        ESP32BASE_LOG_W("irrigation_mqtt", "stop_already_queued");
    else if (queued == IrrigationMqttCore::QueuePushResult::InvalidTransport)
        ESP32BASE_LOG_W("irrigation_mqtt", "invalid_command_qos_or_retain");
    assemblyExpected_ = 0;
    assemblyLength_ = 0;
}

bool IrrigationMqttAdapter::popIncoming(QueuedPacket& packet) {
    portENTER_CRITICAL(&queueMux_);
    const bool found = commandQueue_.pop(packet);
    portEXIT_CRITICAL(&queueMux_);
    return found;
}

void IrrigationMqttAdapter::processPacket(const QueuedPacket& packet) {
    if (!IrrigationMqttCore::acceptsCommandTransport(packet.qos, packet.retain)) {
        ESP32BASE_LOG_W("irrigation_mqtt", "invalid_command_qos_or_retain");
        return;
    }
    IrrigationPlatformProtocol::Command command;
    if (IrrigationPlatformProtocol::parseCommand(packet.payload.data(), packet.length,
                                                  command) !=
        IrrigationPlatformProtocol::ParseResult::Ok) {
        ESP32BASE_LOG_W("irrigation_mqtt", "invalid_command_payload");
        return;
    }
    std::array<uint8_t, 32> signature{};
    if (!commandSignature(command, signature)) return;
    const uint64_t currentTimeMs = nowMs();
    const auto admission = history_.admit(
        command.commandId.data(), signature, command.expiresAtMs,
        command.capability, currentTimeMs);
    if (admission.result == IrrigationMqttCore::Admission::Replay) {
        replay(*admission.entry);
        return;
    }
    if (admission.result == IrrigationMqttCore::Admission::Conflict) return;
    if (admission.result == IrrigationMqttCore::Admission::Full) return;
    execute(command, *admission.entry, currentTimeMs);
}

void IrrigationMqttAdapter::execute(
    const IrrigationPlatformProtocol::Command& command,
    HistoryEntry& history,
    uint64_t currentTimeMs) {
    const char* rejection = evaluate(command, currentTimeMs);
    if (rejection) {
        if (history_.commitReceipt(history, EvidenceStatus::Rejected, rejection,
                                   persistHistory, this) !=
            IrrigationMqttCore::EvidenceCommitResult::Persisted) return;
        publishReceipt(history);
        return;
    }
    if (history_.commitReceipt(history, EvidenceStatus::Accepted, nullptr,
                               persistHistory, this) !=
        IrrigationMqttCore::EvidenceCommitResult::Persisted) return;
    publishReceipt(history);
    // An accepted receipt only proves admission. Do not change parameters or
    // energize outputs until the recoverable running evidence is durable.
    if (!setProgress(history, EvidenceStatus::Running)) return;

    if (command.capability == IrrigationPlatformProtocol::Capability::Plans) {
        if (applyPlans(command)) {
            setProgress(history, EvidenceStatus::Succeeded);
            publishPlans();
            publishRuntime();
        } else {
            setProgress(history, EvidenceStatus::Failed, "persistence_error");
        }
        return;
    }
    if (command.capability ==
        IrrigationPlatformProtocol::Capability::AutomaticWatering) {
        bool saved = false;
        if (command.automaticMode == AutomaticWateringMode::Enabled)
            saved = app_->resumeAutomaticWatering();
        else if (command.automaticMode == AutomaticWateringMode::PausedIndefinitely)
            saved = app_->pauseAutomaticWateringIndefinitely();
        else
            saved = app_->pauseAutomaticWateringUntil(command.resumeAtEpoch);
        if (saved) {
            setProgress(history, EvidenceStatus::Succeeded);
            publishAutomatic();
            publishRuntime();
        } else {
            setProgress(history, EvidenceStatus::Failed, "persistence_error");
        }
        return;
    }
    if (command.capability == IrrigationPlatformProtocol::Capability::Stop) {
        const WateringStatus status = app_->wateringStatus();
        if (!status.active) {
            remoteOperation_.requestStop(command.commandId.data());
            const auto completion = remoteOperation_.completeIdleStop();
            for (uint8_t index = 0; index < completion.count; ++index) {
                const auto& update = completion.updates[index];
                if (HistoryEntry* entry = findHistory(update.commandId.data()))
                    setProgress(*entry, update.status, update.reason);
            }
            return;
        }
        if (!app_->stopWatering()) {
            setProgress(history, EvidenceStatus::Failed, "hardware_failure");
            return;
        }
        remoteOperation_.requestStop(command.commandId.data());
        return;
    }

    WateringStartResult result = WateringStartResult::InvalidRequest;
    if (command.capability == IrrigationPlatformProtocol::Capability::StartManual) {
        std::array<uint16_t, BoardPins::kZoneCount> durations{};
        for (uint8_t index = 0; index < command.zoneCount; ++index)
            durations[BoardPins::zoneIndex(command.zones[index].zoneId)] =
                command.zones[index].durationMinutes;
        result = app_->startManualWatering(durations);
    } else {
        const IrrigationConfig* config = app_->configuration();
        const uint32_t duration = command.targetWaterMl != 0 && config
                                      ? static_cast<uint32_t>(
                                            config->runLimits.maximumZoneDurationMinutes) * 60U
                                      : command.durationSeconds;
        result = app_->startSingleOutput(command.zoneId, duration,
                                         command.targetWaterMl);
    }
    if (result != WateringStartResult::Started) {
        setProgress(history, EvidenceStatus::Failed, rejectionForStart(result));
        return;
    }
    remoteOperation_.start(command.commandId.data());
    publishRuntime();
}

const char* IrrigationMqttAdapter::evaluate(
    const IrrigationPlatformProtocol::Command& command,
    uint64_t currentTimeMs) const {
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    const IrrigationConfig* config = app_->configuration();
    const WateringStatus status = app_->wateringStatus();
    IrrigationMqttCore::CommandGuardContext context{};
    context.capability = command.capability;
    context.automaticMode = command.automaticMode;
    context.expiresAtMs = command.expiresAtMs;
    context.resumeAtEpoch = command.resumeAtEpoch;
    context.currentTimeMs = currentTimeMs;
    context.businessReady = app_->businessReady();
    context.configurationReady = config != nullptr;
    context.wateringActive = status.active;
    context.normalWatering = status.purpose == WateringPurpose::Normal;
    context.calendarTimeTrusted =
        time.synced &&
        app_->schedulerTimeState() == WateringScheduler::TimeState::Ready;
    const auto guard = IrrigationMqttCore::evaluateCommandGuard(context);
    if (guard != IrrigationMqttCore::CommandGuardRejection::None)
        return IrrigationMqttCore::commandGuardReason(guard);
    if (command.capability == IrrigationPlatformProtocol::Capability::Stop)
        return nullptr;
    if (command.capability == IrrigationPlatformProtocol::Capability::Plans) {
        IrrigationConfig next{};
        return buildPlans(command, next);
    }
    if (command.capability ==
        IrrigationPlatformProtocol::Capability::AutomaticWatering) {
        return nullptr;
    }
    if (status.active || remoteOperation_.hasActive()) return "busy";
    if (command.capability == IrrigationPlatformProtocol::Capability::StartManual) {
        for (uint8_t index = 0; index < command.zoneCount; ++index) {
            const auto& zone = command.zones[index];
            if (!config->zones[BoardPins::zoneIndex(zone.zoneId)].enabled)
                return "zone_unavailable";
            if (zone.durationMinutes > config->runLimits.maximumZoneDurationMinutes)
                return "duration_limit";
        }
        return nullptr;
    }
    if (!config->zones[BoardPins::zoneIndex(command.zoneId)].enabled)
        return "zone_unavailable";
    if (command.targetWaterMl == 0 && command.durationSeconds >
        static_cast<uint32_t>(config->runLimits.maximumZoneDurationMinutes) * 60U)
        return "duration_limit";
    if (command.targetWaterMl >
        static_cast<uint32_t>(config->runLimits.maximumSingleOutputLiters) * 1000U)
        return "volume_limit";
    return nullptr;
}

bool IrrigationMqttAdapter::applyPlans(
    const IrrigationPlatformProtocol::Command& command) {
    IrrigationConfig next{};
    if (buildPlans(command, next)) return false;
    return app_->saveConfiguration(next, command.revision,
                                   IrrigationEvents::ConfigurationChange::PlanUpdated);
}

const char* IrrigationMqttAdapter::buildPlans(
    const IrrigationPlatformProtocol::Command& command,
    IrrigationConfig& next) const {
    const IrrigationConfig* current = app_->configuration();
    if (!current) return "not_ready";
    if (command.revision != current->revision) return "revision_conflict";
    next = *current;
    for (WateringPlan& plan : next.plans) {
        const IrrigationPlatformProtocol::PlanValue* requested = nullptr;
        for (uint8_t index = 0; index < command.planCount; ++index) {
            if (command.plans[index].id == plan.id) {
                requested = &command.plans[index];
                break;
            }
        }
        if (!requested) {
            plan = {};
            plan.id = static_cast<uint8_t>(&plan - next.plans.data() + 1U);
            plan.startMinutes.fill(kUnusedStartMinute);
            continue;
        }
        plan.configured = true;
        plan.scheduleEnabled = requested->automaticEnabled;
        std::snprintf(plan.name.data(), plan.name.size(), "%s", requested->name.data());
        plan.startMinutes.fill(kUnusedStartMinute);
        for (uint8_t index = 0; index < requested->startCount; ++index)
            plan.startMinutes[index] = requested->startMinutes[index];
        for (uint8_t zoneIndex = 0; zoneIndex < next.zones.size(); ++zoneIndex) {
            if (next.zones[zoneIndex].enabled) plan.zoneDurationMinutes[zoneIndex] = 0;
        }
        for (uint8_t index = 0; index < requested->zoneCount; ++index) {
            const auto& zone = requested->zones[index];
            if (!next.zones[BoardPins::zoneIndex(zone.zoneId)].enabled)
                return "zone_unavailable";
            if (zone.durationMinutes > next.runLimits.maximumZoneDurationMinutes)
                return "duration_limit";
            plan.zoneDurationMinutes[BoardPins::zoneIndex(zone.zoneId)] =
                zone.durationMinutes;
        }
    }
    return IrrigationConfigRules::validateRuntimeConstraints(next)
               ? nullptr
               : "plan_conflict";
}

void IrrigationMqttAdapter::updateRemoteOperation() {
    const WateringStatus status = app_->wateringStatus();
    if (!status.active && remoteOperation_.hasPendingStop()) {
        const auto completion = remoteOperation_.completeStoppedOperation();
        for (uint8_t index = 0; index < completion.count; ++index) {
            const auto& update = completion.updates[index];
            if (HistoryEntry* entry = findHistory(update.commandId.data()))
                setProgress(*entry, update.status, update.reason);
        }
        publishRuntime();
        return;
    }
    if (!remoteOperation_.hasActive()) return;
    HistoryEntry* entry = findHistory(remoteOperation_.activeCommandId());
    if (!status.active) {
        const bool succeeded = status.lastResult == WateringResult::Completed;
        const auto update = remoteOperation_.completeOperation(
            succeeded ? EvidenceStatus::Succeeded : EvidenceStatus::Failed,
            succeeded ? nullptr : wateringFailure(status.lastStopReason));
        if (entry) setProgress(*entry, update.status, update.reason);
        publishRuntime();
    }
}

IrrigationMqttAdapter::HistoryEntry* IrrigationMqttAdapter::findHistory(
    const char* commandId) {
    return history_.find(commandId);
}

bool IrrigationMqttAdapter::loadOrCreateDeviceId() {
    constexpr const char* key = "device_id";
    if (preferences_.isKey(key)) {
        const std::size_t length =
            preferences_.getString(key, deviceId_.data(), deviceId_.size());
        return length == deviceId_.size() &&
               IrrigationPlatformProtocol::isCanonicalDeviceId(deviceId_.data());
    }
    uint8_t bytes[16]{};
    esp_fill_random(bytes, sizeof(bytes));
    if (!IrrigationPlatformProtocol::formatUuidV4(
            bytes, deviceId_.data(), deviceId_.size())) return false;
    return preferences_.putString(key, deviceId_.data()) == 36;
}

bool IrrigationMqttAdapter::loadHistory() {
    const std::size_t stored = preferences_.getBytesLength("history");
    if (stored == 0) {
        history_.entries() = {};
        return true;
    }
    auto& entries = history_.entries();
    if (stored != sizeof(entries) ||
        preferences_.getBytes("history", entries.data(), sizeof(entries)) !=
            sizeof(entries)) {
        return false;
    }
    if (!history_.validate()) return false;
    const bool reconciled = history_.reconcileAfterRestart();
    return !reconciled || saveHistory();
}

bool IrrigationMqttAdapter::saveHistory() {
    auto& entries = history_.entries();
    return preferences_.putBytes("history", entries.data(), sizeof(entries)) ==
           sizeof(entries);
}

bool IrrigationMqttAdapter::persistHistory(void* context) {
    return context &&
           static_cast<IrrigationMqttAdapter*>(context)->saveHistory();
}

bool IrrigationMqttAdapter::commandSignature(
    const IrrigationPlatformProtocol::Command& command,
    std::array<uint8_t, 32>& signature) const {
    char canonical[3072]{};
    return IrrigationPlatformProtocol::canonicalizeCommand(
               command, canonical, sizeof(canonical)) &&
           mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(canonical),
                              std::strlen(canonical), signature.data(), 0) == 0;
}

void IrrigationMqttAdapter::replay(const HistoryEntry& entry) {
    publishReceipt(entry);
    if (entry.progress != EvidenceStatus::None) publishProgress(entry);
}

void IrrigationMqttAdapter::publishReceipt(const HistoryEntry& entry) {
    if (!connected_) return;
    char observed[25]{};
    if (!observedAt(observed, sizeof(observed))) return;
    char payload[512]{};
    if (entry.receipt == EvidenceStatus::Accepted) {
        std::snprintf(payload, sizeof(payload),
                      "{\"protocol\":\"%s\",\"connectionId\":\"%s\",\"commandId\":\"%s\",\"capabilityKey\":\"%s\",\"status\":\"accepted\",\"observedAt\":\"%s\"}",
                      IrrigationPlatformProtocol::kProtocol, connectionId_.data(),
                      entry.commandId.data(),
                      IrrigationPlatformProtocol::capabilityKey(entry.capability), observed);
    } else {
        std::snprintf(payload, sizeof(payload),
                      "{\"protocol\":\"%s\",\"connectionId\":\"%s\",\"commandId\":\"%s\",\"capabilityKey\":\"%s\",\"status\":\"rejected\",\"reason\":\"%s\",\"observedAt\":\"%s\"}",
                      IrrigationPlatformProtocol::kProtocol, connectionId_.data(),
                      entry.commandId.data(),
                      IrrigationPlatformProtocol::capabilityKey(entry.capability),
                      entry.reason.data(), observed);
    }
    enqueue(IrrigationMqttCore::Channel::Receipt, payload);
}

void IrrigationMqttAdapter::publishProgress(const HistoryEntry& entry) {
    if (!connected_ || !IrrigationMqttCore::shouldReplayProgress(entry.progress)) return;
    char observed[25]{};
    if (!observedAt(observed, sizeof(observed))) return;
    const char* status = entry.progress == EvidenceStatus::Running
                             ? "running"
                             : entry.progress == EvidenceStatus::Succeeded ? "succeeded"
                                                                           : "failed";
    char payload[512]{};
    if (entry.progress == EvidenceStatus::Failed) {
        std::snprintf(payload, sizeof(payload),
                      "{\"protocol\":\"%s\",\"connectionId\":\"%s\",\"commandId\":\"%s\",\"capabilityKey\":\"%s\",\"status\":\"%s\",\"reason\":\"%s\",\"observedAt\":\"%s\"}",
                      IrrigationPlatformProtocol::kProtocol, connectionId_.data(),
                      entry.commandId.data(),
                      IrrigationPlatformProtocol::capabilityKey(entry.capability), status,
                      entry.reason.data(), observed);
    } else {
        std::snprintf(payload, sizeof(payload),
                      "{\"protocol\":\"%s\",\"connectionId\":\"%s\",\"commandId\":\"%s\",\"capabilityKey\":\"%s\",\"status\":\"%s\",\"observedAt\":\"%s\"}",
                      IrrigationPlatformProtocol::kProtocol, connectionId_.data(),
                      entry.commandId.data(),
                      IrrigationPlatformProtocol::capabilityKey(entry.capability), status,
                      observed);
    }
    enqueue(IrrigationMqttCore::Channel::Progress, payload);
}

bool IrrigationMqttAdapter::setProgress(HistoryEntry& entry,
                                        EvidenceStatus status,
                                        const char* reason) {
    const auto committed = history_.commitProgress(
        entry, status, reason, persistHistory, this);
    if (committed == IrrigationMqttCore::EvidenceCommitResult::InvalidTransition)
        return false;
    if (committed == IrrigationMqttCore::EvidenceCommitResult::PersistenceFailed) {
        ESP32BASE_LOG_E("irrigation_mqtt", "command_evidence_save_failed");
        return false;
    }
    publishProgress(entry);
    return true;
}

void IrrigationMqttAdapter::publishAllState() {
    publishRuntime();
    publishPlans();
    publishAutomatic();
    publishZones();
    publishZoneMaintenance();
    publishCalibration();
    publishSystemParameters();
}

void IrrigationMqttAdapter::publishRuntime() {
    if (!connected_ || !app_) return;
    const IrrigationConfig* config = app_->configuration();
    const WateringStatus status = app_->wateringStatus();
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    const AutomaticWateringState automatic = app_->automaticWateringState();
    const NextAutomaticWatering next = app_->nextAutomaticWatering();
    JsonDocument document;
    document["ready"] = app_->businessReady();
    const char* readyReason = "none";
    if (!app_->businessReady()) {
        readyReason = app_->configurationLoadResult() ==
                              IrrigationConfigStore::LoadResult::InvalidConfig
                          ? "configuration_unavailable"
                          : "startup_check_failed";
    }
    document["readyReason"] = readyReason;
    JsonObject timeValue = document["time"].to<JsonObject>();
    const bool trusted = time.synced &&
                         app_->schedulerTimeState() == WateringScheduler::TimeState::Ready;
    timeValue["trusted"] = trusted;
    timeValue["source"] = time.synced
                              ? (time.source == Esp32BaseTime::SOURCE_NTP ? "ntp" : "rtc")
                              : "none";
    if (time.synced) timeValue["epoch"] = time.epochSec;
    else timeValue["epoch"] = nullptr;
    timeValue["rtcAvailable"] = Esp32BaseRtc::isAvailable();
    timeValue["rtcRollback"] =
        app_->schedulerTimeState() == WateringScheduler::TimeState::RtcRollback;
    JsonObject nextValue = document["nextAutomatic"].to<JsonObject>();
    if (automatic.mode == AutomaticWateringMode::PausedUntil) {
        nextValue["status"] = "paused-until";
        nextValue["planId"] = nullptr;
        nextValue["scheduledAtEpoch"] = automatic.resumeAtEpoch;
    } else {
        const char* nextStatus = "time-unavailable";
        switch (next.status) {
            case NextAutomaticWateringStatus::Available: nextStatus = "available"; break;
            case NextAutomaticWateringStatus::NoEnabledPlans: nextStatus = "no-enabled-plans"; break;
            case NextAutomaticWateringStatus::TimeUnavailable: nextStatus = "time-unavailable"; break;
            case NextAutomaticWateringStatus::RtcRollback: nextStatus = "rtc-rollback"; break;
            case NextAutomaticWateringStatus::PausedIndefinitely: nextStatus = "paused-indefinitely"; break;
        }
        nextValue["status"] = nextStatus;
        if (next.status == NextAutomaticWateringStatus::Available) {
            nextValue["planId"] = next.planId;
            nextValue["scheduledAtEpoch"] = next.scheduledEpoch;
        } else {
            nextValue["planId"] = nullptr;
            nextValue["scheduledAtEpoch"] = nullptr;
        }
    }
    JsonObject activity = document["activity"].to<JsonObject>();
    activity["kind"] = activityKind(status);
    if (remoteOperation_.hasActive() && status.active)
        activity["commandId"] = remoteOperation_.activeCommandId();
    else activity["commandId"] = nullptr;
    if (status.active && BoardPins::isValidZoneId(status.activeZoneId))
        activity["zoneId"] = status.activeZoneId;
    else activity["zoneId"] = nullptr;
    activity["phase"] = status.active ? phaseName(status.state) : "idle";
    activity["elapsedSeconds"] = status.active ? status.elapsedSec : 0;
    activity["remainingSeconds"] = status.active ? status.plannedRemainingSec : 0;
    if (status.active && status.currentZoneTargetWaterMl == 0)
        activity["targetDurationSeconds"] = status.currentZoneElapsedSec +
                                                status.currentZoneRemainingSec;
    else activity["targetDurationSeconds"] = nullptr;
    if (status.active && status.currentZoneTargetWaterMl != 0)
        activity["targetWaterMl"] = status.currentZoneTargetWaterMl;
    else activity["targetWaterMl"] = nullptr;
    uint64_t pulses = 0;
    uint64_t water = 0;
    for (uint8_t index = 0; status.active && index < status.stepCount; ++index) {
        pulses += status.zones[index].pulseCount;
        water += status.zones[index].estimatedWaterMl;
    }
    activity["pulseCount"] = pulses > UINT32_MAX ? UINT32_MAX : pulses;
    activity["estimatedWaterMl"] = water > UINT32_MAX ? UINT32_MAX : water;
    if (status.active && (status.state == WateringState::WaitingForFlow ||
                          status.state == WateringState::WateringZone))
        activity["flowMlPerMinute"] = status.currentFlowMlPerMinute;
    else activity["flowMlPerMinute"] = nullptr;
    bool lowActive = false;
    bool highActive = false;
    if (status.active && status.currentStepIndex < status.stepCount) {
        lowActive = status.zones[status.currentStepIndex].lowFlowActive;
        highActive = status.zones[status.currentStepIndex].highFlowActive;
    }
    activity["lowFlowActive"] = lowActive;
    activity["highFlowActive"] = highActive;
    JsonArray steps = activity["steps"].to<JsonArray>();
    for (uint8_t index = 0; status.active && index < status.stepCount; ++index) {
        const ZoneWateringSummary& zone = status.zones[index];
        JsonObject item = steps.add<JsonObject>();
        item["zoneId"] = zone.zoneId;
        item["targetSeconds"] = zone.plannedDurationSec;
        if (zone.targetWaterMl) item["targetWaterMl"] = zone.targetWaterMl;
        else item["targetWaterMl"] = nullptr;
        item["status"] = !status.active || index < status.currentStepIndex
                             ? "completed"
                             : index == status.currentStepIndex ? "current" : "pending";
    }
    JsonArray faults = document["faults"].to<JsonArray>();
    if (!Esp32BaseRtc::isAvailable()) faults.add("rtc_unavailable");
    if (!time.synced) faults.add("time_unavailable");
    if (app_->schedulerTimeState() == WateringScheduler::TimeState::RtcRollback)
        faults.add("rtc_rollback");
    if (app_->unexpectedFlowAlarm()) faults.add("unexpected_flow");
    if (!config) faults.add("configuration_storage");
    if (app_->schedulerStorageFault()) faults.add("scheduler_storage");
    if (app_->recordStorageFault()) faults.add("record_storage");
    if (app_->eventStorageFault()) faults.add("event_storage");
    char value[4096]{};
    serializeJson(document, value, sizeof(value));
    publishState("state.runtime", value);
}

void IrrigationMqttAdapter::publishPlans() {
    const IrrigationConfig* config = app_->configuration();
    if (!connected_ || !config) return;
    JsonDocument document;
    document["revision"] = config->revision;
    JsonArray plans = document["plans"].to<JsonArray>();
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
            if (!config->zones[index].enabled || plan.zoneDurationMinutes[index] == 0)
                continue;
            JsonObject zone = zones.add<JsonObject>();
            zone["zoneId"] = config->zones[index].id;
            zone["durationMinutes"] = plan.zoneDurationMinutes[index];
        }
    }
    char value[3072]{};
    serializeJson(document, value, sizeof(value));
    publishState("parameter.plans", value);
}

void IrrigationMqttAdapter::publishAutomatic() {
    if (!connected_) return;
    const AutomaticWateringState automatic = app_->automaticWateringState();
    JsonDocument document;
    document["mode"] = automaticModeName(automatic.mode);
    if (automatic.mode == AutomaticWateringMode::PausedUntil)
        document["resumeAtEpoch"] = automatic.resumeAtEpoch;
    else document["resumeAtEpoch"] = nullptr;
    char value[128]{};
    serializeJson(document, value, sizeof(value));
    publishState("parameter.automatic-watering", value);
}

void IrrigationMqttAdapter::publishZones() {
    const IrrigationConfig* config = app_->configuration();
    if (!connected_ || !config) return;
    JsonDocument document;
    JsonArray zones = document["zones"].to<JsonArray>();
    for (const ZoneConfig& zone : config->zones) {
        if (!zone.enabled) continue;
        JsonObject item = zones.add<JsonObject>();
        item["zoneId"] = zone.id;
        item["name"] = zone.name.data();
    }
    char value[1024]{};
    serializeJson(document, value, sizeof(value));
    publishState("state.zones", value);
}

void IrrigationMqttAdapter::publishZoneMaintenance() {
    const IrrigationConfig* config = app_->configuration();
    if (!connected_ || !config) return;
    JsonDocument document;
    JsonArray zones = document["zones"].to<JsonArray>();
    for (const ZoneConfig& zone : config->zones) {
        if (!zone.enabled) continue;
        JsonObject item = zones.add<JsonObject>();
        item["zoneId"] = zone.id;
        if (zone.baselinePulseRateX10000 == 0) {
            item["baselinePulseRateX10000"] = nullptr;
            item["baselineFlowMlPerMinute"] = nullptr;
        } else {
            uint32_t flow = 0;
            FlowMonitor::pulseRateX10000ToFlowMlPerMinute(
                zone.baselinePulseRateX10000, config->flowMeter.pulsesPerLiterX100,
                flow);
            item["baselinePulseRateX10000"] = zone.baselinePulseRateX10000;
            item["baselineFlowMlPerMinute"] = flow;
        }
    }
    char value[1536]{};
    serializeJson(document, value, sizeof(value));
    publishState("state.zone-maintenance", value);
}

void IrrigationMqttAdapter::publishCalibration() {
    const IrrigationConfig* config = app_->configuration();
    if (!connected_ || !config) return;
    JsonDocument document;
    document["coefficientPulsesPerLiterX100"] = config->flowMeter.pulsesPerLiterX100;
    document["startupPulseCount"] = config->flowMeter.calibrationStartupPulseCount;
    document["startupWaterMl"] = config->flowMeter.calibrationStartupWaterMl;
    char value[256]{};
    serializeJson(document, value, sizeof(value));
    publishState("state.calibration", value);
}

void IrrigationMqttAdapter::publishSystemParameters() {
    const IrrigationConfig* config = app_->configuration();
    if (!connected_ || !config) return;
    JsonDocument document;
    JsonObject valve = document["valve"].to<JsonObject>();
    valve["pullInTimeMs"] = config->valveDrive.pullInTimeMs;
    valve["switchDelayMs"] = config->valveDrive.switchDelayMs;
    valve["pwmFrequencyHz"] = config->valveDrive.pwmFrequencyHz;
    valve["holdDutyPercent"] = config->valveDrive.holdDutyPercent;
    JsonObject pump = document["pump"].to<JsonObject>();
    pump["enabled"] = config->pump.enabled;
    pump["startDelayMs"] = config->pump.startDelayMs;
    pump["stopToValveCloseDelayMs"] = config->pump.stopToValveCloseDelayMs;
    JsonObject meter = document["meter"].to<JsonObject>();
    meter["pulsesPerLiterX100"] = config->flowMeter.pulsesPerLiterX100;
    meter["calibrationWindowSeconds"] = config->calibrationStability.windowSec;
    meter["calibrationRequiredWindows"] = config->calibrationStability.requiredWindows;
    meter["calibrationAllowedVariationPercent"] =
        config->calibrationStability.allowedVariationPercent;
    meter["flowStartTimeoutSeconds"] = config->flowProtection.flowStartTimeoutSec;
    meter["noFlowTimeoutSeconds"] = config->flowProtection.noFlowTimeoutSec;
    JsonObject flow = document["flow"].to<JsonObject>();
    flow["unexpectedFlowDelaySeconds"] = config->flowProtection.unexpectedFlowDelaySec;
    flow["unexpectedFlowWindowSeconds"] = config->flowProtection.unexpectedFlowWindowSec;
    flow["unexpectedFlowPulseCount"] = config->flowProtection.unexpectedFlowPulseCount;
    flow["deviationConfirmSeconds"] = config->flowProtection.flowDeviationConfirmSec;
    flow["lowFlowPercent"] = config->flowProtection.lowFlowPercent;
    flow["highFlowPercent"] = config->flowProtection.highFlowPercent;
    flow["lowFlowAction"] = flowActionName(config->flowProtection.lowFlowAction);
    flow["highFlowAction"] = flowActionName(config->flowProtection.highFlowAction);
    JsonObject limits = document["limits"].to<JsonObject>();
    limits["maximumZoneDurationMinutes"] = config->runLimits.maximumZoneDurationMinutes;
    limits["maximumSingleOutputLiters"] = config->runLimits.maximumSingleOutputLiters;
    JsonObject system = document["system"].to<JsonObject>();
    system["rtcRollbackThresholdMinutes"] = config->timeSafety.rtcRollbackThresholdMinutes;
    system["aliveCheckpointHours"] = config->timeSafety.aliveCheckpointHours;
    char value[2048]{};
    serializeJson(document, value, sizeof(value));
    publishState("state.system-parameters", value);
}

void IrrigationMqttAdapter::prepareWateringEventCursor() {
    Esp32BaseRecordStore::StoreStatus status{};
    if (!app_->readWateringRecordStoreStatus(status) || status.recordCount == 0) {
        wateringEventCursor_.prepare(0, 0, 0, 0);
        return;
    }
    const uint32_t cursor = preferences_.getUInt("watering_cursor", 0);
    wateringEventCursor_.prepare(cursor, status.oldestRecordId,
                                 status.newestRecordId, status.recordCount);
}

void IrrigationMqttAdapter::publishNextWateringEvent() {
    if (!connected_ || wateringEventCursor_.awaitingAck()) return;
    if (wateringEventCursor_.nextRecordId() == 0) {
        const uint32_t now = millis();
        if (static_cast<uint32_t>(now - lastWateringEventPollMs_) < 1000U) return;
        lastWateringEventPollMs_ = now;
        prepareWateringEventCursor();
    }
    if (wateringEventCursor_.nextRecordId() != 0)
        publishWateringEvent(wateringEventCursor_.nextRecordId());
}

bool IrrigationMqttAdapter::publishWateringEvent(uint32_t recordId) {
    StoredWateringRecord record{};
    const auto read = app_->readWateringRecordById(recordId, record);
    if (read != Esp32BaseRecordStore::RecordReadResult::Found) {
        if (read == Esp32BaseRecordStore::RecordReadResult::NotFound ||
            read == Esp32BaseRecordStore::RecordReadResult::Corrupt) {
            Esp32BaseRecordStore::StoreStatus status{};
            if (app_->readWateringRecordStoreStatus(status) &&
                recordId <= status.newestRecordId) {
                const bool saved = preferences_.putUInt("watering_cursor", recordId) ==
                                   sizeof(recordId);
                wateringEventCursor_.skipRecord(recordId, status.newestRecordId, saved);
            } else {
                wateringEventCursor_.skipRecord(recordId, 0, false);
            }
        }
        return false;
    }
    uint32_t completedEpoch = 0;
    uint32_t startedEpoch = 0;
    if (!Esp32BaseRecordStore::resolveCompletedEpoch(record.timing, completedEpoch) ||
        !Esp32BaseRecordStore::resolveStartedEpoch(record.timing, startedEpoch))
        return false;

    char completedAt[25]{};
    char startedAt[25]{};
    char eventId[37]{};
    if (!IrrigationPlatformProtocol::formatUtcIso8601(
            completedEpoch, completedAt, sizeof(completedAt)) ||
        !IrrigationPlatformProtocol::formatUtcIso8601(
            startedEpoch, startedAt, sizeof(startedAt)))
        return false;
    makeWateringEventId(recordId, record.timing, eventId, sizeof(eventId));

    JsonDocument document;
    document["protocol"] = IrrigationPlatformProtocol::kProtocol;
    document["connectionId"] = connectionId_.data();
    document["eventId"] = eventId;
    document["eventKey"] = record.payload.result == WateringResult::Completed
                               ? "watering.completed"
                           : record.payload.result == WateringResult::Stopped
                               ? "watering.stopped"
                               : "watering.failed";
    document["observedAt"] = completedAt;
    JsonObject data = document["data"].to<JsonObject>();
    data["source"] = wateringSourceName(record.payload.source);
    if (record.payload.planId != 0) data["planId"] = record.payload.planId;
    data["startedAt"] = startedAt;
    data["completedAt"] = completedAt;
    data["result"] = wateringResultName(record.payload.result);
    data["reason"] = record.payload.result == WateringResult::Completed
                         ? "completed"
                     : record.payload.result == WateringResult::Stopped
                         ? "stopped"
                         : wateringFailure(record.payload.stopReason);
    JsonArray zones = data["zones"].to<JsonArray>();
    for (uint8_t index = 0; index < record.payload.zones.size(); ++index) {
        const ZoneWateringRecord& zone = record.payload.zones[index];
        if (zone.plannedDurationSec == 0) continue;
        JsonObject item = zones.add<JsonObject>();
        item["zoneId"] = index + 1U;
        item["zoneName"] = zone.name.data();
        item["targetSeconds"] = zone.plannedDurationSec;
        if (zone.targetWaterMl != 0) item["targetWaterMl"] = zone.targetWaterMl;
        else item["targetWaterMl"] = nullptr;
        item["actualSeconds"] = zone.actualWateringSec;
        item["zoneResult"] = zoneResultName(zone.result);
        item["pulseCount"] = zone.pulseCount;
        item["estimatedWaterMl"] = zone.estimatedWaterMl;
        if ((zone.flags & WateringRecordCodec::kZoneFlagFlowBaselineAvailable) != 0)
            item["baselineFlowMlPerMinute"] = zone.baselineFlowMlPerMinute;
        else item["baselineFlowMlPerMinute"] = nullptr;
        item["averageFlowMlPerMinute"] = zone.actualWateringSec != 0
                                             ? zone.averageFlowMlPerMinute
                                             : 0;
        item["lowFlowDetected"] =
            (zone.flags & WateringRecordCodec::kZoneFlagLowFlow) != 0;
        item["highFlowDetected"] =
            (zone.flags & WateringRecordCodec::kZoneFlagHighFlow) != 0;
    }
    char payload[4096]{};
    const std::size_t written = serializeJson(document, payload, sizeof(payload));
    if (written == 0 || written >= sizeof(payload) - 1U) return false;
    char topic[192]{};
    makeTopic(IrrigationMqttCore::Channel::Event, topic, sizeof(topic));
    const auto policy =
        IrrigationMqttCore::publicationPolicy(IrrigationMqttCore::Channel::Event);
    const int messageId = esp_mqtt_client_enqueue(
        client_, topic, payload, static_cast<int>(written), policy.qos,
        policy.retain, true);
    if (messageId < 0) return false;
    wateringEventCursor_.enqueued(messageId, recordId);
    return true;
}

void IrrigationMqttAdapter::prepareAppEventCursor() {
    Esp32BaseAppEvents::AppEventsStatus status{};
    if (!app_->readEventStatus(status) || status.eventStore.recordCount == 0) {
        appEventCursor_.prepare(0, 0, 0, 0);
        return;
    }
    const uint32_t cursor = preferences_.getUInt("app_evt_cursor", 0);
    appEventCursor_.prepare(cursor, status.eventStore.oldestRecordId,
                            status.eventStore.newestRecordId,
                            status.eventStore.recordCount);
}

void IrrigationMqttAdapter::publishNextAppEvent() {
    if (!connected_ || appEventCursor_.awaitingAck()) return;
    if (appEventCursor_.nextRecordId() == 0) {
        const uint32_t now = millis();
        if (static_cast<uint32_t>(now - lastAppEventPollMs_) < 1000U) return;
        lastAppEventPollMs_ = now;
        prepareAppEventCursor();
    }
    if (appEventCursor_.nextRecordId() != 0)
        publishAppEvent(appEventCursor_.nextRecordId());
}

bool IrrigationMqttAdapter::publishAppEvent(uint32_t recordId) {
    Esp32BaseAppEvents::EventRecord event{};
    const auto read = Esp32BaseAppEvents::readById(recordId, event);
    if (read != Esp32BaseAppEvents::EventReadResult::Found) {
        if (read == Esp32BaseAppEvents::EventReadResult::NotFound ||
            read == Esp32BaseAppEvents::EventReadResult::Corrupt) {
            Esp32BaseAppEvents::AppEventsStatus status{};
            if (app_->readEventStatus(status) &&
                recordId <= status.eventStore.newestRecordId) {
                const bool saved = preferences_.putUInt("app_evt_cursor", recordId) ==
                                   sizeof(recordId);
                appEventCursor_.skipRecord(recordId,
                                           status.eventStore.newestRecordId, saved);
            } else appEventCursor_.skipRecord(recordId, 0, false);
        }
        return false;
    }
    const char* eventKey = appEventKey(event);
    if (!eventKey) {
        Esp32BaseAppEvents::AppEventsStatus status{};
        if (app_->readEventStatus(status)) {
            const bool saved = preferences_.putUInt("app_evt_cursor", recordId) ==
                               sizeof(recordId);
            appEventCursor_.skipRecord(recordId,
                                       status.eventStore.newestRecordId, saved);
        }
        return false;
    }
    uint32_t observedEpoch = 0;
    if (!Esp32BaseRecordStore::resolveCompletedEpoch(event.timing, observedEpoch))
        return false;
    char observed[25]{};
    char eventId[37]{};
    if (!IrrigationPlatformProtocol::formatUtcIso8601(
            observedEpoch, observed, sizeof(observed))) return false;
    makeAppEventId(recordId, event.timing, eventId, sizeof(eventId));

    JsonDocument document;
    document["protocol"] = IrrigationPlatformProtocol::kProtocol;
    document["connectionId"] = connectionId_.data();
    document["eventId"] = eventId;
    document["eventKey"] = eventKey;
    document["observedAt"] = observed;
    JsonObject data = document["data"].to<JsonObject>();
    data["objectId"] = event.objectId;
    data["reasonCode"] = event.reasonCode;
    data["value1"] = event.value1;
    data["value2"] = event.value2;
    data["result"] = event.eventKind == Esp32BaseAppEvents::EventKind::ConditionRecovered
                         ? "recovered"
                         : "recorded";
    if (IrrigationEvents::hasWateringContext(event)) {
        data["source"] = wateringSourceName(IrrigationEvents::wateringSource(event));
        const uint8_t planId = IrrigationEvents::wateringPlanId(event);
        if (planId != 0) data["planId"] = planId;
    }
    char payload[1024]{};
    const std::size_t written = serializeJson(document, payload, sizeof(payload));
    if (written == 0 || written >= sizeof(payload) - 1U) return false;
    char topic[192]{};
    makeTopic(IrrigationMqttCore::Channel::Event, topic, sizeof(topic));
    const auto policy =
        IrrigationMqttCore::publicationPolicy(IrrigationMqttCore::Channel::Event);
    const int messageId = esp_mqtt_client_enqueue(
        client_, topic, payload, static_cast<int>(written), policy.qos,
        policy.retain, true);
    if (messageId < 0) return false;
    appEventCursor_.enqueued(messageId, recordId);
    return true;
}

const char* IrrigationMqttAdapter::appEventKey(
    const Esp32BaseAppEvents::EventRecord& event) const {
    using EventCode = IrrigationEvents::EventCode;
    using ReasonCode = IrrigationEvents::ReasonCode;
    switch (static_cast<EventCode>(event.eventCode)) {
        case EventCode::AutomaticWateringStateChanged:
            return event.reasonCode == static_cast<uint32_t>(ReasonCode::PausedIndefinitely) ||
                           event.reasonCode == static_cast<uint32_t>(ReasonCode::PausedUntil)
                       ? "automatic.paused"
                       : "automatic.resumed";
        case EventCode::AutomaticPlanSkipped: return "automatic.plan-skipped";
        case EventCode::FlowDeviation:
            return event.reasonCode == static_cast<uint32_t>(ReasonCode::LowFlow)
                       ? "flow.low"
                       : "flow.high";
        case EventCode::ClosedValveFlow:
            return event.eventKind == Esp32BaseAppEvents::EventKind::ConditionRecovered
                       ? "flow.unexpected-cleared"
                       : "flow.unexpected";
        case EventCode::FlowCalibrationSaved: return "calibration.result-saved";
        case EventCode::ZoneFlowSaved: return "zone.baseline-saved";
        case EventCode::ConfigurationChanged:
            if (event.reasonCode == static_cast<uint32_t>(ReasonCode::PlanCreated) ||
                event.reasonCode == static_cast<uint32_t>(ReasonCode::PlanUpdated) ||
                event.reasonCode == static_cast<uint32_t>(ReasonCode::PlanDeleted))
                return "configuration.plans-changed";
            return event.reasonCode == static_cast<uint32_t>(ReasonCode::ZoneUpdated)
                       ? "configuration.zones-changed"
                       : "configuration.system-parameters-changed";
        case EventCode::WateringRecordSaveFailed:
        case EventCode::SchedulerStateSaveFailed: return "storage.business-failed";
        case EventCode::WateringStoppedAbnormally:
            if (event.reasonCode == static_cast<uint32_t>(ReasonCode::FlowStartTimeout) ||
                event.reasonCode == static_cast<uint32_t>(ReasonCode::NoFlowTimeout))
                return "flow.no-flow";
            if (event.reasonCode == static_cast<uint32_t>(ReasonCode::LowFlow))
                return "flow.low";
            if (event.reasonCode == static_cast<uint32_t>(ReasonCode::HighFlow))
                return "flow.high";
            return nullptr;
        default: return nullptr;
    }
}

void IrrigationMqttAdapter::makeWateringEventId(
    uint32_t recordId,
    const Esp32BaseRecordStore::RecordTiming& timing,
    char* output,
    std::size_t size) const {
    char identity[160]{};
    std::snprintf(identity, sizeof(identity), "%s|watering|%lu|%lu|%lu|%lu",
                  deviceId_.data(), static_cast<unsigned long>(recordId),
                  static_cast<unsigned long>(timing.completedBootId),
                  static_cast<unsigned long>(timing.completedUptimeSec),
                  static_cast<unsigned long>(timing.completedEpochSec));
    uint8_t digest[32]{};
    mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(identity),
                       std::strlen(identity), digest, 0);
    digest[6] = static_cast<uint8_t>((digest[6] & 0x0FU) | 0x50U);
    digest[8] = static_cast<uint8_t>((digest[8] & 0x3FU) | 0x80U);
    std::snprintf(output, size,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  digest[0], digest[1], digest[2], digest[3], digest[4], digest[5],
                  digest[6], digest[7], digest[8], digest[9], digest[10], digest[11],
                  digest[12], digest[13], digest[14], digest[15]);
}

void IrrigationMqttAdapter::makeAppEventId(
    uint32_t recordId,
    const Esp32BaseRecordStore::RecordTiming& timing,
    char* output,
    std::size_t size) const {
    char identity[160]{};
    std::snprintf(identity, sizeof(identity), "%s|app-event|%lu|%lu|%lu|%lu",
                  deviceId_.data(), static_cast<unsigned long>(recordId),
                  static_cast<unsigned long>(timing.completedBootId),
                  static_cast<unsigned long>(timing.completedUptimeSec),
                  static_cast<unsigned long>(timing.completedEpochSec));
    uint8_t digest[32]{};
    mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(identity),
                       std::strlen(identity), digest, 0);
    digest[6] = static_cast<uint8_t>((digest[6] & 0x0FU) | 0x50U);
    digest[8] = static_cast<uint8_t>((digest[8] & 0x3FU) | 0x80U);
    std::snprintf(output, size,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  digest[0], digest[1], digest[2], digest[3], digest[4], digest[5],
                  digest[6], digest[7], digest[8], digest[9], digest[10], digest[11],
                  digest[12], digest[13], digest[14], digest[15]);
}

void IrrigationMqttAdapter::publishState(const char* capabilityKey,
                                         const char* valueJson) {
    char observed[25]{};
    if (!connected_ || !observedAt(observed, sizeof(observed))) return;
    char payload[4608]{};
    const int written = std::snprintf(
        payload, sizeof(payload),
        "{\"protocol\":\"%s\",\"connectionId\":\"%s\",\"capabilityKey\":\"%s\",\"seq\":%lu,\"value\":%s,\"observedAt\":\"%s\"}",
        IrrigationPlatformProtocol::kProtocol, connectionId_.data(), capabilityKey,
        static_cast<unsigned long>(seq_++), valueJson, observed);
    if (written > 0 && static_cast<std::size_t>(written) < sizeof(payload))
        enqueue(IrrigationMqttCore::Channel::State, payload);
}

void IrrigationMqttAdapter::publishAvailability(bool online, const char* reason) {
    char observed[25]{};
    if (!observedAt(observed, sizeof(observed))) return;
    char payload[320]{};
    const char* finalReason = online ? nullptr : (reason ? reason : "shutdown");
    if (IrrigationPlatformProtocol::formatAvailability(
            online, connectionId_.data(), finalReason, observed, payload,
            sizeof(payload)))
        enqueue(IrrigationMqttCore::Channel::Availability, payload);
}

bool IrrigationMqttAdapter::enqueue(IrrigationMqttCore::Channel channel,
                                    const char* payload) {
    if (!connected_ || !client_ || !payload) return false;
    char topic[192]{};
    makeTopic(channel, topic, sizeof(topic));
    const auto policy = IrrigationMqttCore::publicationPolicy(channel);
    return esp_mqtt_client_enqueue(client_, topic, payload, 0, policy.qos,
                                   policy.retain, true) >= 0;
}

bool IrrigationMqttAdapter::observedAt(char* output, std::size_t size) {
    const uint32_t currentMillis = millis();
    refreshTrustedTime(currentMillis);
    const uint32_t epoch = trustedTime_.currentEpochSec(currentMillis);
    return epoch != 0 &&
           IrrigationPlatformProtocol::formatUtcIso8601(epoch, output, size);
}

void IrrigationMqttAdapter::refreshTrustedTime(uint32_t currentMillis) {
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    trustedTime_.observe(now.synced, now.epochSec, currentMillis);
}

uint64_t IrrigationMqttAdapter::nowMs() {
    const uint32_t currentMillis = millis();
    refreshTrustedTime(currentMillis);
    return trustedTime_.currentTimeMs(currentMillis);
}

void IrrigationMqttAdapter::makeConnectionId() {
    uint8_t bytes[16]{};
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    std::snprintf(connectionId_.data(), connectionId_.size(),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                  bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                  bytes[12], bytes[13], bytes[14], bytes[15]);
    seq_ = 0;
}

void IrrigationMqttAdapter::makeTopic(IrrigationMqttCore::Channel channel,
                                      char* output,
                                      std::size_t size) const {
    if (!IrrigationPlatformProtocol::formatTopic(
            deviceId_.data(), IrrigationMqttCore::channelName(channel),
            output, size) && size != 0)
        output[0] = '\0';
}

const char* IrrigationMqttAdapter::phaseName(WateringState state) const {
    switch (state) {
        case WateringState::StartingZone: return "starting";
        case WateringState::WaitingForFlow: return "waiting-flow";
        case WateringState::WateringZone: return "watering";
        case WateringState::SwitchingZone: return "switching";
        case WateringState::StoppingZone: return "stopping";
        default: return "idle";
    }
}

const char* IrrigationMqttAdapter::activityKind(const WateringStatus& status) const {
    if (!status.active) return "idle";
    if (status.purpose == WateringPurpose::FlowCalibration) return "calibration";
    if (status.purpose == WateringPurpose::ZoneFlowLearning) return "learning";
    if (status.source == WateringSource::AutomaticPlan) return "automatic";
    if (status.source == WateringSource::SingleOutput) return "single-output";
    return "manual";
}

const char* IrrigationMqttAdapter::rejectionForStart(
    WateringStartResult result) const {
    switch (result) {
        case WateringStartResult::NotReady: return "not_ready";
        case WateringStartResult::Busy:
        case WateringStartResult::PreviousResultPending: return "busy";
        case WateringStartResult::InvalidRequest: return "invalid_parameters";
        case WateringStartResult::HardwareFailure: return "hardware_failure";
        default: return "internal_state";
    }
}
