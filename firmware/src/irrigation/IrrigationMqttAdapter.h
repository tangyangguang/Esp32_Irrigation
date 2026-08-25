#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Preferences.h>
#include <mqtt_client.h>

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
    static constexpr uint8_t kCommandQueueDepth = 2;
    static constexpr uint8_t kHistoryCapacity = 8;

    enum class EvidenceStatus : uint8_t {
        None,
        Accepted,
        Rejected,
        Running,
        Succeeded,
        Failed,
    };

    struct QueuedPacket {
        uint16_t length = 0;
        uint8_t qos = 0;
        bool retain = false;
        bool ready = false;
        std::array<char, IrrigationPlatformProtocol::kMaximumCommandBytes + 1> payload{};
    };

    struct HistoryEntry {
        std::array<char, 37> commandId{};
        std::array<uint8_t, 32> signature{};
        uint64_t expiresAtMs = 0;
        IrrigationPlatformProtocol::Capability capability =
            IrrigationPlatformProtocol::Capability::Stop;
        EvidenceStatus receipt = EvidenceStatus::None;
        EvidenceStatus progress = EvidenceStatus::None;
        std::array<char, 32> reason{};
        bool used = false;
    };

    struct ActiveRemoteCommand {
        std::array<char, 37> commandId{};
        IrrigationPlatformProtocol::Capability capability =
            IrrigationPlatformProtocol::Capability::Stop;
        bool present = false;
    };

    static esp_err_t mqttEvent(esp_mqtt_event_handle_t event);
    void onMqttEvent(esp_mqtt_event_handle_t event);
    bool startClient();
    void stopClient();
    void queueIncoming(const esp_mqtt_event_t& event);
    bool popIncoming(QueuedPacket& packet);
    void processPacket(const QueuedPacket& packet);
    void execute(const IrrigationPlatformProtocol::Command& command,
                 HistoryEntry& history);
    const char* evaluate(const IrrigationPlatformProtocol::Command& command) const;
    const char* buildPlans(const IrrigationPlatformProtocol::Command& command,
                           IrrigationConfig& next) const;
    bool applyPlans(const IrrigationPlatformProtocol::Command& command);
    void updateRemoteOperation();

    HistoryEntry* findHistory(const char* commandId);
    HistoryEntry* allocateHistory(uint64_t nowMs);
    bool loadHistory();
    bool saveHistory();
    bool commandSignature(const IrrigationPlatformProtocol::Command& command,
                          std::array<uint8_t, 32>& signature) const;
    void replay(const HistoryEntry& entry);
    void publishReceipt(const HistoryEntry& entry);
    void publishProgress(const HistoryEntry& entry);
    void setProgress(HistoryEntry& entry, EvidenceStatus status,
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
    bool enqueue(const char* channel, const char* payload, bool retain = false);
    bool observedAt(char* output, std::size_t size);
    uint64_t nowMs() const;
    void makeConnectionId();
    void makeTopic(const char* channel, char* output, std::size_t size) const;
    const char* phaseName(WateringState state) const;
    const char* activityKind(const WateringStatus& status) const;
    const char* rejectionForStart(WateringStartResult result) const;

    IrrigationApp* app_ = nullptr;
    esp_mqtt_client_handle_t client_ = nullptr;
    Preferences preferences_;
    std::array<QueuedPacket, kCommandQueueDepth> commandQueue_{};
    std::array<HistoryEntry, kHistoryCapacity> history_{};
    ActiveRemoteCommand activeRemote_{};
    ActiveRemoteCommand pendingStop_{};
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
    uint32_t lastTrustedEpoch_ = 0;
    uint32_t lastTrustedMillis_ = 0;
    uint32_t nextWateringEventRecordId_ = 0;
    uint32_t lastWateringEventPollMs_ = 0;
    uint32_t pendingWateringEventRecordId_ = 0;
    int pendingWateringEventMessageId_ = -1;
    uint32_t nextAppEventRecordId_ = 0;
    uint32_t lastAppEventPollMs_ = 0;
    uint32_t pendingAppEventRecordId_ = 0;
    int pendingAppEventMessageId_ = -1;
    uint32_t seq_ = 0;
    std::array<char, 37> connectionId_{};
    std::array<char, 192> availabilityTopic_{};
    std::array<char, 256> lwtPayload_{};
};
