#include "IrrigationMqtt.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "IrrigationApp.h"
#include "IrrigationPrivateConfig.generated.h"

namespace {

constexpr uint32_t kActiveStateIntervalMs = 5000;
constexpr uint32_t kMinimumPublishSpacingMs = 50;
constexpr uint8_t kMaximumBackgroundInflight = 3;
constexpr uint8_t kAllPlansDirty =
    static_cast<uint8_t>((1U << kWateringPlanCount) - 1U);
constexpr uint8_t kOnlinePayload[] = "online";
constexpr uint8_t kOfflinePayload[] = "offline";

JsonDocument g_mqttJson;
std::array<char, IrrigationMqtt::kPayloadCapacity> g_mqttPayload{};

bool formatTopic(char* output,
                 std::size_t outputLength,
                 const char* format,
                 const char* root,
                 unsigned id = 0) {
    const int written = id == 0
                            ? std::snprintf(output, outputLength, format, root)
                            : std::snprintf(output, outputLength, format, root, id);
    return written > 0 && static_cast<std::size_t>(written) < outputLength;
}

const char* wateringStateName(WateringState state) {
    switch (state) {
        case WateringState::Idle: return "idle";
        case WateringState::StartingZone: return "starting_zone";
        case WateringState::WaitingForFlow: return "waiting_for_flow";
        case WateringState::WateringZone: return "watering_zone";
        case WateringState::StoppingZone: return "stopping_zone";
        case WateringState::SwitchingZone: return "switching_zone";
    }
    return "unknown";
}

const char* wateringSourceName(WateringSource source) {
    switch (source) {
        case WateringSource::ManualZones: return "manual";
        case WateringSource::SingleOutput: return "single_output";
        case WateringSource::AutomaticPlan: return "automatic_plan";
    }
    return "unknown";
}

const char* wateringResultName(WateringResult result) {
    switch (result) {
        case WateringResult::None: return "none";
        case WateringResult::Completed: return "completed";
        case WateringResult::Stopped: return "stopped";
        case WateringResult::Failed: return "failed";
    }
    return "unknown";
}

const char* wateringStopReasonName(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::None: return "none";
        case WateringStopReason::Completed: return "completed";
        case WateringStopReason::UserStopped: return "user_stopped";
        case WateringStopReason::FlowStartTimeout: return "flow_start_timeout";
        case WateringStopReason::NoFlowTimeout: return "no_flow_timeout";
        case WateringStopReason::LowFlow: return "low_flow";
        case WateringStopReason::HighFlow: return "high_flow";
        case WateringStopReason::LearningTimeout: return "learning_timeout";
        case WateringStopReason::HardwareFailure: return "hardware_failure";
        case WateringStopReason::MaintenanceInterrupted:
            return "maintenance_interrupted";
        case WateringStopReason::TargetVolumeTimeout:
            return "target_volume_timeout";
    }
    return "unknown";
}

const char* automaticModeName(AutomaticWateringMode mode) {
    switch (mode) {
        case AutomaticWateringMode::Enabled: return "enabled";
        case AutomaticWateringMode::PausedIndefinitely:
            return "paused_indefinitely";
        case AutomaticWateringMode::PausedUntil: return "paused_until";
    }
    return "unknown";
}

const char* nextWateringStatusName(NextAutomaticWateringStatus status) {
    switch (status) {
        case NextAutomaticWateringStatus::Available: return "available";
        case NextAutomaticWateringStatus::NoEnabledPlans:
            return "no_enabled_plans";
        case NextAutomaticWateringStatus::TimeUnavailable:
            return "time_unavailable";
        case NextAutomaticWateringStatus::RtcRollback: return "rtc_rollback";
        case NextAutomaticWateringStatus::PausedIndefinitely:
            return "paused_indefinitely";
    }
    return "unknown";
}

const char* startResultCode(WateringStartResult result) {
    switch (result) {
        case WateringStartResult::Started: return "started";
        case WateringStartResult::NotReady: return "not_ready";
        case WateringStartResult::Busy: return "busy";
        case WateringStartResult::PreviousResultPending:
            return "previous_result_pending";
        case WateringStartResult::InvalidRequest: return "invalid_request";
        case WateringStartResult::HardwareFailure: return "hardware_failure";
    }
    return "rejected";
}

void hashValue(uint32_t& hash, uint32_t value) {
    for (uint8_t index = 0; index < 4; ++index) {
        hash ^= static_cast<uint8_t>(value >> (index * 8U));
        hash *= 16777619U;
    }
}

bool serializeCurrentJson(std::size_t& length) {
    const std::size_t required = measureJson(g_mqttJson);
    if (required == 0 || required >= g_mqttPayload.size()) {
        return false;
    }
    length = serializeJson(g_mqttJson,
                           g_mqttPayload.data(),
                           g_mqttPayload.size());
    return length == required;
}

}  // namespace

IrrigationMqtt& IrrigationMqtt::instance() {
    static IrrigationMqtt mqtt;
    return mqtt;
}

bool IrrigationMqtt::configure(IrrigationApp& app) {
    app_ = &app;
    if (!IrrigationPrivateConfig::MQTT_CONFIGURED) {
        ESP32BASE_LOG_W("irrigation_mqtt",
                        "disabled reason=private_config_missing");
        return false;
    }

    const uint64_t mac = ESP.getEfuseMac();
    std::snprintf(deviceId_.data(),
                  deviceId_.size(),
                  "esp32-%04x%08lx",
                  static_cast<unsigned>((mac >> 32U) & 0xFFFFU),
                  static_cast<unsigned long>(mac & 0xFFFFFFFFULL));
    std::snprintf(clientId_.data(),
                  clientId_.size(),
                  "irrigation-%s",
                  deviceId_.data());
    const int rootWritten =
        std::snprintf(rootTopic_.data(),
                      rootTopic_.size(),
                      "%s/%s",
                      IrrigationPrivateConfig::MQTT_TOPIC_PREFIX,
                      deviceId_.data());
    if (rootWritten <= 0 ||
        static_cast<std::size_t>(rootWritten) >= rootTopic_.size() ||
        !formatTopic(availabilityTopic_.data(),
                     availabilityTopic_.size(),
                     "%s/availability",
                     rootTopic_.data()) ||
        !formatTopic(metaTopic_.data(),
                     metaTopic_.size(),
                     "%s/meta",
                     rootTopic_.data()) ||
        !formatTopic(stateTopic_.data(),
                     stateTopic_.size(),
                     "%s/state",
                     rootTopic_.data()) ||
        !formatTopic(commandTopic_.data(),
                     commandTopic_.size(),
                     "%s/command",
                     rootTopic_.data()) ||
        !formatTopic(resultTopic_.data(),
                     resultTopic_.size(),
                     "%s/result",
                     rootTopic_.data())) {
        ESP32BASE_LOG_E("irrigation_mqtt", "configuration_rejected reason=topic_length");
        return false;
    }

    lastWill_.topic = availabilityTopic_.data();
    lastWill_.payload = kOfflinePayload;
    lastWill_.payloadLength = sizeof(kOfflinePayload) - 1U;
    lastWill_.qos = Esp32BaseMqtt::QOS_1;
    lastWill_.retain = true;

    Esp32BaseMqtt::ConnectionConfig connection;
    connection.host = IrrigationPrivateConfig::MQTT_HOST;
    connection.port =
        static_cast<uint16_t>(IrrigationPrivateConfig::MQTT_PORT);
    connection.clientId = clientId_.data();
    connection.keepAliveSeconds = 60;
    connection.security = Esp32BaseMqtt::TLS;
    connection.username = IrrigationPrivateConfig::MQTT_USERNAME;
    connection.password = IrrigationPrivateConfig::MQTT_PASSWORD;
    connection.tls.caCertificatePem =
        IrrigationPrivateConfig::MQTT_CA_PEM;
    connection.tls.caCertificateLength =
        sizeof(IrrigationPrivateConfig::MQTT_CA_PEM);
    connection.lastWill = &lastWill_;
    if (!Esp32BaseMqtt::configure(connection)) {
        ESP32BASE_LOG_E("irrigation_mqtt",
                        "configuration_rejected reason=%s",
                        Esp32BaseMqtt::errorName(
                            Esp32BaseMqtt::status().lastError));
        return false;
    }

    Esp32BaseMqtt::Subscription subscription;
    subscription.topicFilter = commandTopic_.data();
    subscription.qos = Esp32BaseMqtt::QOS_1;
    if (!Esp32BaseMqtt::addSubscription(subscription)) {
        ESP32BASE_LOG_E("irrigation_mqtt",
                        "configuration_rejected reason=subscription");
        return false;
    }
    Esp32BaseMqtt::setMessageCallback(messageCallback, this);
    Esp32BaseMqtt::setEventCallback(eventCallback, this);
    enabled_ = true;
    ESP32BASE_LOG_I("irrigation_mqtt",
                    "configured device_id=%s topic_root=%s",
                    deviceId_.data(),
                    rootTopic_.data());
    return true;
}

void IrrigationMqtt::handle() {
    if (!enabled_ || !connected_ || !app_) {
        return;
    }
    observeChanges();
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - lastPublishAttemptMs_) <
        kMinimumPublishSpacingMs) {
        return;
    }
    if (Esp32BaseMqtt::diagnostics().inflightQos1 >=
        kMaximumBackgroundInflight) {
        return;
    }
    lastPublishAttemptMs_ = now;

    if (pendingResultLength_ != 0) {
        if (publishPayload(resultTopic_.data(),
                           pendingResult_.data(),
                           pendingResultLength_,
                           false)) {
            pendingResultLength_ = 0;
        }
        return;
    }
    if (stateDirty_) {
        if (publishState()) {
            stateDirty_ = false;
            lastStatePublishMs_ = now;
        }
        return;
    }
    if (metaDirty_) {
        if (publishMeta()) {
            metaDirty_ = false;
        }
        return;
    }
    for (uint8_t index = 0; index < kWateringPlanCount; ++index) {
        const uint8_t bit = static_cast<uint8_t>(1U << index);
        if ((plansDirty_ & bit) == 0) {
            continue;
        }
        if (publishPlan(static_cast<uint8_t>(index + 1U))) {
            plansDirty_ = static_cast<uint8_t>(plansDirty_ & ~bit);
        }
        return;
    }
}

bool IrrigationMqtt::enabled() const {
    return enabled_;
}

void IrrigationMqtt::messageCallback(
    const Esp32BaseMqtt::MessageView& message,
    void* context) {
    if (context) {
        static_cast<IrrigationMqtt*>(context)->onMessage(message);
    }
}

void IrrigationMqtt::eventCallback(const Esp32BaseMqtt::Event& event,
                                   void* context) {
    if (context) {
        static_cast<IrrigationMqtt*>(context)->onEvent(event);
    }
}

void IrrigationMqtt::onMessage(
    const Esp32BaseMqtt::MessageView& message) {
    if (message.topicLength != std::strlen(commandTopic_.data()) ||
        std::memcmp(message.topic,
                    commandTopic_.data(),
                    message.topicLength) != 0) {
        return;
    }
    if (message.retain) {
        publishResult("", false, "retained_command_rejected");
        return;
    }
    if (message.qos != Esp32BaseMqtt::QOS_1) {
        publishResult("", false, "command_qos1_required");
        return;
    }

    IrrigationMqttProtocol::Command command;
    IrrigationMqttProtocol::ParseError error;
    if (!IrrigationMqttProtocol::parse(message.payload,
                                       message.payloadLength,
                                       command,
                                       error)) {
        publishResult("",
                      false,
                      IrrigationMqttProtocol::parseErrorName(error));
        return;
    }
    if (pendingResultLength_ != 0) {
        ESP32BASE_LOG_W("irrigation_mqtt",
                        "command_skipped reason=result_pending");
        return;
    }
    if (isDuplicateCommand(command.id.data())) {
        publishResult(command.id.data(), false, "duplicate_command");
        return;
    }
    rememberCommand(command.id.data());
    execute(command);
}

void IrrigationMqtt::onEvent(const Esp32BaseMqtt::Event& event) {
    if (event.type == Esp32BaseMqtt::EVENT_CONNECTED) {
        connected_ = true;
        markAllDirty();
        publishAvailability();
    } else if (event.type == Esp32BaseMqtt::EVENT_DISCONNECTED ||
               event.type == Esp32BaseMqtt::EVENT_CONNECTION_REJECTED) {
        connected_ = false;
    }
}

void IrrigationMqtt::execute(
    const IrrigationMqttProtocol::Command& command) {
    if (!app_ || !app_->businessReady()) {
        publishResult(command.id.data(), false, "not_ready");
        return;
    }
    const IrrigationConfig* current = app_->configuration();
    if (!current) {
        publishResult(command.id.data(), false, "configuration_unavailable");
        return;
    }

    switch (command.action) {
        case IrrigationMqttProtocol::Action::SetPlan: {
            if (command.revision != current->revision) {
                publishResult(command.id.data(), false, "revision_conflict");
                return;
            }
            configScratch_ = *current;
            WateringPlan& plan = configScratch_.plans[command.planId - 1U];
            const bool creating = !plan.configured;
            plan.configured = true;
            plan.scheduleEnabled = command.planEnabled;
            plan.name = command.planName;
            plan.startMinutes = command.startMinutes;
            plan.zoneDurationMinutes = command.zoneDurationMinutes;
            for (std::size_t index = 0; index < plan.zoneDurationMinutes.size();
                 ++index) {
                if (!current->zones[index].enabled) {
                    plan.zoneDurationMinutes[index] = 0;
                }
            }
            const bool saved = app_->saveConfiguration(
                configScratch_,
                command.revision,
                creating
                    ? IrrigationEvents::ConfigurationChange::PlanCreated
                    : IrrigationEvents::ConfigurationChange::PlanUpdated,
                command.planId);
            publishResult(command.id.data(),
                          saved,
                          saved ? "saved" : app_->configurationError());
            return;
        }
        case IrrigationMqttProtocol::Action::DeletePlan: {
            if (command.revision != current->revision) {
                publishResult(command.id.data(), false, "revision_conflict");
                return;
            }
            if (!current->plans[command.planId - 1U].configured) {
                publishResult(command.id.data(), false, "plan_not_found");
                return;
            }
            configScratch_ = *current;
            WateringPlan& plan = configScratch_.plans[command.planId - 1U];
            plan = {};
            plan.id = command.planId;
            plan.startMinutes.fill(kUnusedStartMinute);
            const bool saved = app_->saveConfiguration(
                configScratch_,
                command.revision,
                IrrigationEvents::ConfigurationChange::PlanDeleted,
                command.planId);
            publishResult(command.id.data(),
                          saved,
                          saved ? "deleted" : app_->configurationError());
            return;
        }
        case IrrigationMqttProtocol::Action::PauseAutomatic: {
            const AutomaticWateringState state =
                app_->automaticWateringState();
            bool success = false;
            if (command.resumeAtEpoch == 0) {
                success =
                    state.mode ==
                        AutomaticWateringMode::PausedIndefinitely ||
                    app_->pauseAutomaticWateringIndefinitely();
            } else {
                success =
                    (state.mode == AutomaticWateringMode::PausedUntil &&
                     state.resumeAtEpoch == command.resumeAtEpoch) ||
                    app_->pauseAutomaticWateringUntil(
                        command.resumeAtEpoch);
            }
            publishResult(command.id.data(),
                          success,
                          success ? "paused" : "pause_rejected");
            return;
        }
        case IrrigationMqttProtocol::Action::ResumeAutomatic: {
            const bool success =
                app_->automaticWateringState().mode ==
                    AutomaticWateringMode::Enabled ||
                app_->resumeAutomaticWatering();
            publishResult(command.id.data(),
                          success,
                          success ? "resumed" : "resume_rejected");
            return;
        }
        case IrrigationMqttProtocol::Action::StartPlan: {
            const WateringPlan& plan =
                current->plans[command.planId - 1U];
            if (!plan.configured) {
                publishResult(command.id.data(), false, "plan_not_found");
                return;
            }
            std::array<uint16_t, BoardPins::kZoneCount> durations =
                plan.zoneDurationMinutes;
            for (std::size_t index = 0; index < durations.size(); ++index) {
                if (!current->zones[index].enabled) {
                    durations[index] = 0;
                }
            }
            const WateringStartResult result =
                app_->startManualWatering(durations);
            publishResult(command.id.data(),
                          result == WateringStartResult::Started,
                          startResultCode(result));
            return;
        }
        case IrrigationMqttProtocol::Action::StartManual: {
            const WateringStartResult result =
                app_->startManualWatering(
                    command.zoneDurationMinutes);
            publishResult(command.id.data(),
                          result == WateringStartResult::Started,
                          startResultCode(result));
            return;
        }
        case IrrigationMqttProtocol::Action::Stop: {
            const WateringStatus status = app_->wateringStatus();
            if (!status.active) {
                publishResult(command.id.data(), true, "already_idle");
            } else if (status.purpose != WateringPurpose::Normal) {
                publishResult(command.id.data(),
                              false,
                              "maintenance_active");
            } else {
                const bool success = app_->stopWatering();
                publishResult(command.id.data(),
                              success,
                              success ? "stopping" : "stop_rejected");
            }
            return;
        }
    }
}

bool IrrigationMqtt::publishAvailability() {
    return publishPayload(
        availabilityTopic_.data(),
        reinterpret_cast<const char*>(kOnlinePayload),
        sizeof(kOnlinePayload) - 1U,
        true);
}

bool IrrigationMqtt::publishMeta() {
    const IrrigationConfig* config = app_->configuration();
    g_mqttJson.clear();
    g_mqttJson["v"] = IrrigationMqttProtocol::kVersion;
    g_mqttJson["available"] = config != nullptr;
    g_mqttJson["device_id"] = deviceId_.data();
    JsonArray zones = g_mqttJson["zones"].to<JsonArray>();
    if (config) {
        for (const ZoneConfig& zone : config->zones) {
            JsonObject object = zones.add<JsonObject>();
            object["id"] = zone.id;
            object["enabled"] = zone.enabled;
            object["name"] = zone.name.data();
        }
    }
    std::size_t length = 0;
    return serializeCurrentJson(length) &&
           publishPayload(metaTopic_.data(),
                          g_mqttPayload.data(),
                          length,
                          true);
}

bool IrrigationMqtt::publishState() {
    const IrrigationConfig* config = app_->configuration();
    const WateringStatus watering = app_->wateringStatus();
    const AutomaticWateringState automatic =
        app_->automaticWateringState();
    const NextAutomaticWatering next = app_->nextAutomaticWatering();

    g_mqttJson.clear();
    g_mqttJson["v"] = IrrigationMqttProtocol::kVersion;
    g_mqttJson["ready"] = app_->businessReady();
    g_mqttJson["revision"] = config ? config->revision : 0;
    JsonObject wateringJson = g_mqttJson["watering"].to<JsonObject>();
    wateringJson["active"] = watering.active;
    wateringJson["state"] = wateringStateName(watering.state);
    wateringJson["source"] = wateringSourceName(watering.source);
    wateringJson["plan_id"] = watering.planId;
    wateringJson["zone_id"] = watering.activeZoneId;
    wateringJson["remaining_s"] = watering.currentZoneRemainingSec;
    wateringJson["flow_ml_min"] = watering.currentFlowMlPerMinute;
    wateringJson["water_ml"] = watering.totalEstimatedWaterMl;
    wateringJson["last_result"] =
        wateringResultName(watering.lastResult);
    wateringJson["stop_reason"] =
        wateringStopReasonName(watering.lastStopReason);

    JsonObject automaticJson =
        g_mqttJson["automatic"].to<JsonObject>();
    automaticJson["mode"] = automaticModeName(automatic.mode);
    automaticJson["resume_at"] = automatic.resumeAtEpoch;
    automaticJson["next_status"] =
        nextWateringStatusName(next.status);
    automaticJson["next_plan_id"] = next.planId;
    automaticJson["next_at"] = next.scheduledEpoch;

    JsonObject faults = g_mqttJson["faults"].to<JsonObject>();
    faults["unexpected_flow"] = app_->unexpectedFlowAlarm();
    faults["watering_records"] = app_->recordStorageFault();
    faults["events"] = app_->eventStorageFault();
    faults["scheduler"] = app_->schedulerStorageFault();
    faults["checkpoint"] = app_->checkpointStorageFault();

    std::size_t length = 0;
    return serializeCurrentJson(length) &&
           publishPayload(stateTopic_.data(),
                          g_mqttPayload.data(),
                          length,
                          true);
}

bool IrrigationMqtt::publishPlan(uint8_t planId) {
    const IrrigationConfig* config = app_->configuration();
    if (planId < 1 || planId > kWateringPlanCount) {
        return false;
    }
    g_mqttJson.clear();
    g_mqttJson["v"] = IrrigationMqttProtocol::kVersion;
    g_mqttJson["available"] = config != nullptr;
    g_mqttJson["revision"] = config ? config->revision : 0;
    g_mqttJson["id"] = planId;
    const WateringPlan* plan =
        config ? &config->plans[planId - 1U] : nullptr;
    g_mqttJson["configured"] = plan && plan->configured;
    g_mqttJson["enabled"] = plan && plan->scheduleEnabled;
    g_mqttJson["name"] = plan ? plan->name.data() : "";
    JsonArray starts = g_mqttJson["starts"].to<JsonArray>();
    if (plan) {
        for (const uint16_t minute : plan->startMinutes) {
            if (minute != kUnusedStartMinute) {
                starts.add(minute);
            }
        }
    }
    JsonArray durations = g_mqttJson["durations"].to<JsonArray>();
    if (plan) {
        for (const uint16_t duration : plan->zoneDurationMinutes) {
            durations.add(duration);
        }
    }
    std::array<char, kTopicCapacity> topic{};
    if (!formatTopic(topic.data(),
                     topic.size(),
                     "%s/plan/%u",
                     rootTopic_.data(),
                     planId)) {
        return false;
    }
    std::size_t length = 0;
    return serializeCurrentJson(length) &&
           publishPayload(topic.data(),
                          g_mqttPayload.data(),
                          length,
                          true);
}

bool IrrigationMqtt::publishResult(const char* commandId,
                                   bool success,
                                   const char* code) {
    g_mqttJson.clear();
    g_mqttJson["v"] = IrrigationMqttProtocol::kVersion;
    g_mqttJson["id"] = commandId ? commandId : "";
    g_mqttJson["ok"] = success;
    g_mqttJson["code"] = code ? code : "unknown";
    const IrrigationConfig* config =
        app_ ? app_->configuration() : nullptr;
    g_mqttJson["revision"] = config ? config->revision : 0;
    std::size_t length = 0;
    if (!serializeCurrentJson(length)) {
        return false;
    }
    if (publishPayload(resultTopic_.data(),
                       g_mqttPayload.data(),
                       length,
                       false)) {
        return true;
    }
    if (length < pendingResult_.size()) {
        std::memcpy(pendingResult_.data(),
                    g_mqttPayload.data(),
                    length);
        pendingResult_[length] = '\0';
        pendingResultLength_ = length;
    }
    return false;
}

bool IrrigationMqtt::publishPayload(const char* topic,
                                    const char* payload,
                                    std::size_t length,
                                    bool retain) {
    Esp32BaseMqtt::PublishRequest request;
    request.topic = topic;
    request.payload =
        reinterpret_cast<const uint8_t*>(payload);
    request.payloadLength = length;
    request.qos = Esp32BaseMqtt::QOS_1;
    request.retain = retain;
    const Esp32BaseMqtt::PublishResult result =
        Esp32BaseMqtt::publish(request);
    if (!result.accepted()) {
        ESP32BASE_LOG_W("irrigation_mqtt",
                        "publish_rejected code=%u",
                        static_cast<unsigned>(result.code));
    }
    return result.accepted();
}

void IrrigationMqtt::markAllDirty() {
    metaDirty_ = true;
    stateDirty_ = true;
    plansDirty_ = kAllPlansDirty;
    observedRevision_ = 0;
    observedStateFingerprint_ = 0;
}

void IrrigationMqtt::observeChanges() {
    const IrrigationConfig* config = app_->configuration();
    if (config && config->revision != observedRevision_) {
        observedRevision_ = config->revision;
        metaDirty_ = true;
        stateDirty_ = true;
        plansDirty_ = kAllPlansDirty;
    }
    const uint32_t fingerprint = stateFingerprint();
    if (fingerprint != observedStateFingerprint_) {
        observedStateFingerprint_ = fingerprint;
        stateDirty_ = true;
    }
    const WateringStatus status = app_->wateringStatus();
    if (status.active &&
        static_cast<uint32_t>(millis() - lastStatePublishMs_) >=
            kActiveStateIntervalMs) {
        stateDirty_ = true;
    }
}

bool IrrigationMqtt::isDuplicateCommand(const char* commandId) const {
    if (!commandId || commandId[0] == '\0') {
        return false;
    }
    for (const auto& stored : recentCommands_) {
        if (stored[0] != '\0' &&
            std::strcmp(stored.data(), commandId) == 0) {
            return true;
        }
    }
    return false;
}

void IrrigationMqtt::rememberCommand(const char* commandId) {
    std::snprintf(recentCommands_[nextRecentCommand_].data(),
                  recentCommands_[nextRecentCommand_].size(),
                  "%s",
                  commandId);
    nextRecentCommand_ =
        static_cast<uint8_t>((nextRecentCommand_ + 1U) %
                             recentCommands_.size());
}

uint32_t IrrigationMqtt::stateFingerprint() const {
    if (!app_) {
        return 0;
    }
    uint32_t hash = 2166136261U;
    const WateringStatus watering = app_->wateringStatus();
    hashValue(hash, app_->businessReady());
    hashValue(hash, watering.active);
    hashValue(hash, static_cast<uint32_t>(watering.state));
    hashValue(hash, static_cast<uint32_t>(watering.source));
    hashValue(hash, watering.planId);
    hashValue(hash, watering.activeZoneId);
    hashValue(hash, static_cast<uint32_t>(watering.lastResult));
    hashValue(hash, static_cast<uint32_t>(watering.lastStopReason));
    const AutomaticWateringState automatic =
        app_->automaticWateringState();
    hashValue(hash, static_cast<uint32_t>(automatic.mode));
    hashValue(hash, automatic.resumeAtEpoch);
    const NextAutomaticWatering next = app_->nextAutomaticWatering();
    hashValue(hash, static_cast<uint32_t>(next.status));
    hashValue(hash, next.planId);
    hashValue(hash, next.scheduledEpoch);
    hashValue(hash, app_->unexpectedFlowAlarm());
    hashValue(hash, app_->recordStorageFault());
    hashValue(hash, app_->eventStorageFault());
    hashValue(hash, app_->schedulerStorageFault());
    hashValue(hash, app_->checkpointStorageFault());
    return hash;
}
