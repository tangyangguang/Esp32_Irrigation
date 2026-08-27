#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Preferences.h>
#include <mqtt_client.h>

#include "IrrigationMqttCore.h"
#include "IrrigationPlatformProtocol.h"
#include "WateringRecordStore.h"

class IrrigationApp;
struct IrrigationConfig;

class IrrigationMqttAdapter {
public:
    bool begin(IrrigationApp& app);
    void handle();
    bool enabled() const;
    bool connected() const;

private:
    using EvidenceStatus = IrrigationMqttCore::EvidenceStatus;
    using HistoryEntry = IrrigationMqttCore::EvidenceEntry;
    using CommandQueue = IrrigationMqttCore::CommandQueue<
        IrrigationPlatformProtocol::kMaximumCommandBytes>;
    using QueuedPacket = CommandQueue::Packet;

    static esp_err_t mqttEvent(esp_mqtt_event_handle_t event);
    void onMqttEvent(esp_mqtt_event_handle_t event);
    bool startClient();
    void stopClient();
    void queueIncoming(const esp_mqtt_event_t& event);
    bool popIncoming(QueuedPacket& packet);
    void processPacket(const QueuedPacket& packet);
    void execute(const IrrigationPlatformProtocol::Command& command,
                 HistoryEntry& history,
                 uint64_t currentTimeMs);
    const char* evaluate(const IrrigationPlatformProtocol::Command& command,
                         uint64_t currentTimeMs) const;
    const char* buildPlans(const IrrigationPlatformProtocol::Command& command,
                           IrrigationConfig& next) const;
    bool applyPlans(const IrrigationPlatformProtocol::Command& command);
    void updateRemoteOperation();

    HistoryEntry* findHistory(const char* commandId);
    bool loadOrCreateDeviceId();
    bool loadHistory();
    bool saveHistory();
    static bool persistHistory(void* context);
    bool commandSignature(const IrrigationPlatformProtocol::Command& command,
                          std::array<uint8_t, 32>& signature) const;
    void replay(const HistoryEntry& entry);
    void publishReceipt(const HistoryEntry& entry);
    void publishProgress(const HistoryEntry& entry);
    bool setProgress(HistoryEntry& entry, EvidenceStatus status,
                     const char* reason = nullptr);

    void publishAllState();
    void publishRuntime();
    void publishPlans();
    void publishAutomatic();
    void publishZones();
    void publishZoneMaintenance();
    void publishCalibration();
    void publishSystemParameters();
    void prepareWateringEventCursor();
    void publishNextWateringEvent();
    bool publishWateringEvent(uint32_t recordId);
    void prepareAppEventCursor();
    void publishNextAppEvent();
    bool publishAppEvent(uint32_t recordId);
    const char* appEventKey(const Esp32BaseAppEvents::EventRecord& event) const;
    void makeWateringEventId(uint32_t recordId,
                             const Esp32BaseRecordStore::RecordTiming& timing,
                             char* output,
                             std::size_t size) const;
    void makeAppEventId(uint32_t recordId,
                        const Esp32BaseRecordStore::RecordTiming& timing,
                        char* output,
                        std::size_t size) const;
    void publishState(const char* capabilityKey, const char* valueJson);
    void publishAvailability(bool online, const char* reason = nullptr);
    bool enqueue(IrrigationMqttCore::Channel channel, const char* payload);
    bool observedAt(char* output, std::size_t size);
    void refreshTrustedTime(uint32_t currentMillis);
    uint64_t nowMs();
    void makeConnectionId();
    void makeTopic(IrrigationMqttCore::Channel channel,
                   char* output,
                   std::size_t size) const;
    const char* phaseName(WateringState state) const;
    const char* activityKind(const WateringStatus& status) const;
    const char* rejectionForStart(WateringStartResult result) const;

    IrrigationApp* app_ = nullptr;
    esp_mqtt_client_handle_t client_ = nullptr;
    Preferences preferences_;
    CommandQueue commandQueue_{};
    IrrigationMqttCore::EvidenceStore history_{};
    IrrigationMqttCore::RemoteOperationTracker remoteOperation_{};
    portMUX_TYPE queueMux_ = portMUX_INITIALIZER_UNLOCKED;
    std::array<char, IrrigationPlatformProtocol::kMaximumCommandBytes + 1> assembly_{};
    uint16_t assemblyLength_ = 0;
    uint16_t assemblyExpected_ = 0;
    uint8_t assemblyQos_ = 0;
    bool assemblyRetain_ = false;
    bool enabled_ = false;
    bool connected_ = false;
    bool restartPending_ = false;
    bool publishAllPending_ = false;
    bool replayProgressPending_ = false;
    uint32_t reconnectAtMs_ = 0;
    uint32_t lastStatePublishMs_ = 0;
    uint32_t lastRuntimePublishMs_ = 0;
    uint32_t lastProgressPublishMs_ = 0;
    uint32_t lastConfigRevision_ = 0;
    uint32_t lastRuntimeFingerprint_ = 0;
    IrrigationMqttCore::TrustedTimeAnchor trustedTime_{};
    uint32_t lastWateringEventPollMs_ = 0;
    IrrigationMqttCore::EventCursor wateringEventCursor_{};
    uint32_t lastAppEventPollMs_ = 0;
    IrrigationMqttCore::EventCursor appEventCursor_{};
    uint32_t seq_ = 0;
    std::array<char, 37> deviceId_{};
    std::array<char, 37> connectionId_{};
    std::array<char, 192> availabilityTopic_{};
    std::array<char, 256> lwtPayload_{};
};
