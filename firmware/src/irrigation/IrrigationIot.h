#pragma once

#include <Esp32Base.h>

#include <cstddef>
#include <cstdint>

#include "IrrigationCommandJournal.h"
#include "IrrigationIotProtocol.h"

class IrrigationApp;

class IrrigationIot {
public:
    static IrrigationIot& instance();

    // Must be called before Esp32Base::begin(). Missing private MQTT settings
    // leave the adapter safely unconfigured without blocking local operation.
    bool configure();
    bool begin();
    void handle(IrrigationApp& app);

    bool configured() const;
    const char* deviceId() const;
    const char* activeCommandId() const;

private:
    IrrigationIot() = default;

    enum StateBit : uint16_t {
        StateRuntime = 1U << 0U,
        StateOverview = 1U << 1U,
        StatePlans = 1U << 2U,
        StateAutomatic = 1U << 3U,
        StateZones = 1U << 4U,
        StateZoneMaintenance = 1U << 5U,
        StateCalibration = 1U << 6U,
        StateSystemParameters = 1U << 7U,
        StateAll = (1U << 8U) - 1U,
    };

    enum class EvidenceType : uint8_t {
        ReceiptAccepted,
        ReceiptRejected,
        ProgressRunning,
        ProgressSucceeded,
        ProgressCanceled,
        ProgressFailed,
    };

    struct Evidence {
        char commandId[IrrigationIotProtocol::kUuidBufferSize]{};
        IrrigationIotProtocol::CommandKind kind =
            IrrigationIotProtocol::CommandKind::Stop;
        EvidenceType type = EvidenceType::ReceiptAccepted;
        IrrigationCommandJournal::Reason reason =
            IrrigationCommandJournal::Reason::None;
        uint64_t observedAtMs = 0;
    };

    enum class InFlightKind : uint8_t {
        None,
        Availability,
        Evidence,
        State,
        Record,
    };

    static constexpr std::size_t kEvidenceCapacity = 32;

    static void beforeConnect(void* context);
    static uint16_t beforeNetworkStop(void* context);
    static void mqttMessage(const Esp32BaseMqtt::MessageView& message,
                            void* context);
    static void mqttEvent(const Esp32BaseMqtt::Event& event, void* context);

    void prepareConnectionCycle();
    bool publishShutdown();
    void onMessage(const Esp32BaseMqtt::MessageView& message);
    void onEvent(const Esp32BaseMqtt::Event& event);
    void handleCommand(const IrrigationIotProtocol::Command& command,
                       IrrigationApp& app,
                       uint64_t nowMs);
    void executeAcceptedCommand(const IrrigationIotProtocol::Command& command,
                                IrrigationApp& app,
                                std::size_t journalIndex,
                                uint64_t nowMs);
    bool applyPlans(const IrrigationIotProtocol::Command& command,
                    IrrigationApp& app);
    bool applyAutomatic(const IrrigationIotProtocol::Command& command,
                        IrrigationApp& app);
    bool startManual(const IrrigationIotProtocol::Command& command,
                     IrrigationApp& app);
    bool startSingleOutput(const IrrigationIotProtocol::Command& command,
                           IrrigationApp& app);

    IrrigationIotProtocol::BusinessContext businessContext(
        const IrrigationApp& app,
        uint64_t nowMs) const;
    IrrigationIotProtocol::ActiveKind activeKind(
        const WateringStatus& status) const;
    void detectActivity(IrrigationApp& app, uint32_t nowMs);
    void beginActivity(const WateringStatus& status,
                       const IrrigationIotProtocol::Command* command,
                       std::size_t journalIndex,
                       uint32_t nowMs);
    void finishActivity(const WateringStatus& status, uint64_t observedAtMs);
    void finishStopCommands(uint64_t observedAtMs, bool succeeded);
    bool addPendingStopCommand(const char* commandId, std::size_t journalIndex);
    IrrigationCommandJournal::Reason wateringFailureReason(
        WateringStopReason reason) const;

    void scheduleAllState();
    void scheduleRuntimeState();
    void detectStateChanges(const IrrigationApp& app, uint32_t nowMs);
    uint64_t stateFingerprint(const IrrigationApp& app) const;
    void queueEvidence(const Evidence& evidence);
    void queueJournalReceipt(const IrrigationCommandJournal::Entry& entry);
    void queueJournalProgress(const IrrigationCommandJournal::Entry& entry);
    void queuePendingJournalEvidence();
    void markJournalEvidenceDelivered(const Evidence& evidence);
    bool evidenceQueued(const Evidence& evidence) const;
    void removeEvidenceHead();
    void pump(IrrigationApp& app);
    bool publishAvailability();
    bool publishEvidence();
    bool publishRecord();
    bool serializeRecord(char* output,
                         std::size_t outputLength,
                         std::size_t& payloadLength);
    bool publishState(IrrigationApp& app, StateBit state);
    bool serializeState(IrrigationApp& app, StateBit state,
                        char* output, std::size_t outputLength,
                        std::size_t& payloadLength);
    bool publishBuffer(const char* topic,
                       const char* payload,
                       std::size_t payloadLength,
                       bool retain,
                       InFlightKind kind,
                       uint16_t stateBit = 0);
    bool currentObservedAt(char* output,
                           std::size_t outputLength,
                           uint64_t* epochMs = nullptr) const;
    uint64_t currentEpochMs() const;
    void markPublishAcknowledged(uint16_t packetId);
    void resetConnectionDelivery();

    bool configured_ = false;
    bool begun_ = false;
    bool connected_ = false;
    bool subscriptionsReady_ = false;
    bool journalReady_ = false;
    bool lifecycleStopping_ = false;
    uint8_t subscriptionAckMask_ = 0;
    uint32_t stateSeq_ = 0;
    uint32_t lastRecordPublishMs_ = 0;
    uint32_t lastStateScheduleMs_ = 0;
    uint32_t lastRunningEvidenceMs_ = 0;
    uint32_t lastActivityStateMs_ = 0;
    uint64_t lastStateFingerprint_ = 0;
    bool stateFingerprintSet_ = false;
    uint16_t pendingStateMask_ = 0;
    bool availabilityPending_ = false;

    char deviceId_[48]{};
    char topicPrefix_[128]{};
    char availabilityTopic_[160]{};
    char stateTopic_[160]{};
    char eventTopic_[160]{};
    char commandTopic_[160]{};
    char recordAckTopic_[160]{};
    char receiptTopic_[160]{};
    char progressTopic_[160]{};
    char connectionId_[IrrigationIotProtocol::kUuidBufferSize]{};
    char lwtPayload_[256]{};
    // State and record payloads can reach 4096 bytes. Keep the single serialized
    // publish buffer in static storage rather than consuming loopTask's stack.
    char publishPayload_[ESP32BASE_MQTT_MAX_PAYLOAD_BYTES + 1U]{};
    Esp32BaseMqtt::LastWill lastWill_{};

    Evidence evidence_[kEvidenceCapacity]{};
    std::size_t evidenceRead_ = 0;
    std::size_t evidenceWrite_ = 0;
    std::size_t evidenceCount_ = 0;
    bool journalReceiptDelivered_[IrrigationCommandJournal::kCapacity]{};
    bool journalProgressDelivered_[IrrigationCommandJournal::kCapacity]{};

    InFlightKind inFlightKind_ = InFlightKind::None;
    uint16_t inFlightPacketId_ = 0;
    uint16_t inFlightStateBit_ = 0;
    uint32_t inFlightRecordSequence_ = 0;

    bool activityTracked_ = false;
    char activityId_[IrrigationIotProtocol::kUuidBufferSize]{};
    char activityCommandId_[IrrigationIotProtocol::kUuidBufferSize]{};
    IrrigationIotProtocol::CommandKind activityCommandKind_ =
        IrrigationIotProtocol::CommandKind::Stop;
    std::size_t activityJournalIndex_ = IrrigationCommandJournal::kCapacity;
    uint32_t activityStartedMs_ = 0;
    uint32_t activityRequestedDurationMs_ = 0;
    uint32_t activityDeadlineEpoch_ = 0;
    bool activityHasRequestedDuration_ = false;
    bool activityHasDeadline_ = false;

    struct PendingStopCommand {
        char commandId[IrrigationIotProtocol::kUuidBufferSize]{};
        std::size_t journalIndex = IrrigationCommandJournal::kCapacity;
    };
    PendingStopCommand pendingStops_[IrrigationCommandJournal::kCapacity]{};
    uint8_t pendingStopCount_ = 0;

    IrrigationCommandJournal journal_;
    IrrigationApp* app_ = nullptr;
};
