#pragma once

#include <Esp32Base.h>
#include <ModelPublisher.h>
#include <ports/Esp32MqttPort.h>

#include <cstddef>
#include <cstdint>

#include "IrrigationCommandJournal.h"
#include "IrrigationIotProtocol.h"
#include "IrrigationRecordSync.h"

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
    IrrigationIot();

    enum StateBit : uint16_t {
        StateRuntime = 1U << 0U,
        StateOverview = 1U << 1U,
        StatePlans = 1U << 2U,
        StateAutomatic = 1U << 3U,
        StateZones = 1U << 4U,
        StateZoneMaintenance = 1U << 5U,
        StateCalibration = 1U << 6U,
        StateSystemParameters = 1U << 7U,
        StateDiagnostics = 1U << 8U,
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
        Evidence,
        State,
        Record,
    };

    static constexpr std::size_t kEvidenceCapacity = 32;

    static bool randomBytes(uint8_t output[16], void*);
    static bool utcNow(char output[25], void*);
    static uint16_t beforeNetworkStop(void* context);
    static void commandReceived(const char*, size_t, uint8_t, bool, const uint8_t*, size_t, void*);
    static void mqttEvent(const Esp32BaseMqtt::Event& event, void* context);

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
                    IrrigationApp& app) __attribute__((noinline));
    bool applyAutomatic(const IrrigationIotProtocol::Command& command,
                        IrrigationApp& app);
    bool startManual(const IrrigationIotProtocol::Command& command,
                     IrrigationApp& app);
    bool startSingleOutput(const IrrigationIotProtocol::Command& command,
                           IrrigationApp& app);

    IrrigationIotProtocol::BusinessContext businessContext(
        const IrrigationApp& app,
        uint64_t nowMs) const __attribute__((noinline));
    IrrigationIotProtocol::ActiveKind activeKind(
        const WateringStatus& status) const;
    void detectActivity(IrrigationApp& app, uint32_t nowMs);
    void beginCommandActivity(IrrigationApp& app, const IrrigationIotProtocol::Command& command,
                              std::size_t journalIndex) __attribute__((noinline));
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
    bool publishEvidence();
    static bool publishRecordFact(const uint8_t generation[16], const iot_device::RecordFactView&, void*);
    bool serializeRecord(const uint8_t generation[16], const iot_device::RecordFactView&,
                         char*, std::size_t, std::size_t&);
    bool publishState(IrrigationApp&, StateBit);
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
    bool journalReady_ = false;
    bool commandClockReady_ = false;
    bool lifecycleStopping_ = false;
    uint32_t stateSeq_ = 0;
    uint32_t lastStateScheduleMs_ = 0;
    uint32_t lastDiagnosticsScheduleMs_ = 0;
    uint32_t lastRunningEvidenceMs_ = 0;
    uint32_t lastActivityStateMs_ = 0;
    uint64_t lastStateFingerprint_ = 0;
    bool stateFingerprintSet_ = false;
    uint16_t pendingStateMask_ = 0;

    char deviceId_[48]{};
    char topicWork_[160]{};
    char eventTopic_[160]{};
    char lwtPayload_[512]{};
    char publishPayload_[ESP32BASE_MQTT_MAX_PAYLOAD_BYTES + 1U]{};
    iot_device::SessionIo io_;
    iot_device::ConnectionSession session_;
    iot_device::Esp32MqttPort port_;
    StaticJsonDocument<16> unusedRecordDocument_;
    iot_device::ModelPublisher publisher_;

    Evidence evidence_[kEvidenceCapacity]{};
    std::size_t evidenceRead_ = 0;
    std::size_t evidenceWrite_ = 0;
    std::size_t evidenceCount_ = 0;

    InFlightKind inFlightKind_ = InFlightKind::None;
    uint16_t inFlightPacketId_ = 0;
    uint16_t inFlightStateBit_ = 0;
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
