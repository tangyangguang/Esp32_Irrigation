#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "IrrigationPlatformProtocol.h"

namespace IrrigationMqttCore {

constexpr uint8_t kEvidenceCapacity = 8;

enum class Channel : uint8_t {
    Availability,
    State,
    Event,
    Command,
    Receipt,
    Progress,
};

struct PublicationPolicy {
    uint8_t qos;
    bool retain;
    constexpr PublicationPolicy(uint8_t valueQos, bool valueRetain)
        : qos(valueQos), retain(valueRetain) {}
};

const char* channelName(Channel channel);
PublicationPolicy publicationPolicy(Channel channel);
bool acceptsCommandTransport(uint8_t qos, bool retain);
bool isStopCommandEnvelope(const char* payload, std::size_t length);

enum class EvidenceStatus : uint8_t {
    None,
    Accepted,
    Rejected,
    Running,
    Succeeded,
    Failed,
};

bool shouldReplayProgress(EvidenceStatus status);

struct EvidenceEntry {
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

enum class Admission : uint8_t {
    New,
    Replay,
    Conflict,
    Full,
};

struct AdmissionResult {
    Admission result = Admission::Full;
    EvidenceEntry* entry = nullptr;
    AdmissionResult(Admission valueResult, EvidenceEntry* valueEntry)
        : result(valueResult), entry(valueEntry) {}
};

class EvidenceStore {
public:
    AdmissionResult admit(const char* commandId,
                          const std::array<uint8_t, 32>& signature,
                          uint64_t expiresAtMs,
                          IrrigationPlatformProtocol::Capability capability,
                          uint64_t nowMs);
    EvidenceEntry* find(const char* commandId);
    const EvidenceEntry* find(const char* commandId) const;
    bool markAccepted(EvidenceEntry& entry);
    bool markRejected(EvidenceEntry& entry, const char* reason);
    bool markRunning(EvidenceEntry& entry);
    bool markTerminal(EvidenceEntry& entry,
                      EvidenceStatus terminal,
                      const char* reason = nullptr);
    bool validate() const;
    bool reconcileAfterRestart();
    std::array<EvidenceEntry, kEvidenceCapacity>& entries();
    const std::array<EvidenceEntry, kEvidenceCapacity>& entries() const;

private:
    std::array<EvidenceEntry, kEvidenceCapacity> entries_{};
};

struct ConnectionDecision {
    bool publishOnlineAvailability;
    bool publishFullState;
    bool replayKnownProgress;
    constexpr ConnectionDecision(bool online, bool fullState, bool progress)
        : publishOnlineAvailability(online),
          publishFullState(fullState),
          replayKnownProgress(progress) {}
};

constexpr ConnectionDecision connectedDecision() {
    return {true, true, true};
}

struct TerminalUpdate {
    std::array<char, 37> commandId{};
    EvidenceStatus status = EvidenceStatus::None;
    const char* reason = nullptr;
};

struct StopCompletion {
    std::array<TerminalUpdate, 2> updates{};
    uint8_t count = 0;
};

class RemoteOperationTracker {
public:
    void start(const char* commandId);
    void requestStop(const char* commandId);
    bool hasActive() const;
    bool hasPendingStop() const;
    const char* activeCommandId() const;
    const char* pendingStopCommandId() const;
    StopCompletion completeIdleStop();
    StopCompletion completeStoppedOperation();
    TerminalUpdate completeOperation(EvidenceStatus status,
                                     const char* reason = nullptr);

private:
    std::array<char, 37> active_{};
    std::array<char, 37> stop_{};
};

enum class QueuePushResult : uint8_t {
    AcceptedNormal,
    AcceptedStop,
    InvalidTransport,
    NormalSlotFull,
    StopSlotFull,
    InvalidPayload,
};

template <std::size_t MaximumBytes>
class CommandQueue {
public:
    struct Packet {
        uint16_t length = 0;
        uint8_t qos = 0;
        bool retain = false;
        bool ready = false;
        std::array<char, MaximumBytes + 1U> payload{};
    };

    QueuePushResult push(const char* payload,
                         std::size_t length,
                         uint8_t qos,
                         bool retain) {
        if (!acceptsCommandTransport(qos, retain))
            return QueuePushResult::InvalidTransport;
        if (!payload || length == 0 || length > MaximumBytes)
            return QueuePushResult::InvalidPayload;
        return pushClassified(payload, length, qos, retain,
                              isStopCommandEnvelope(payload, length));
    }

    QueuePushResult pushClassified(const char* payload,
                                   std::size_t length,
                                   uint8_t qos,
                                   bool retain,
                                   bool stop) {
        if (!acceptsCommandTransport(qos, retain))
            return QueuePushResult::InvalidTransport;
        if (!payload || length == 0 || length > MaximumBytes)
            return QueuePushResult::InvalidPayload;
        Packet& slot = stop ? stop_ : normal_;
        if (slot.ready)
            return stop ? QueuePushResult::StopSlotFull
                        : QueuePushResult::NormalSlotFull;
        slot.length = static_cast<uint16_t>(length);
        slot.qos = qos;
        slot.retain = retain;
        std::memcpy(slot.payload.data(), payload, length);
        slot.payload[length] = '\0';
        slot.ready = true;
        return stop ? QueuePushResult::AcceptedStop
                    : QueuePushResult::AcceptedNormal;
    }

    bool pop(Packet& packet) {
        Packet* slot = stop_.ready ? &stop_ : normal_.ready ? &normal_ : nullptr;
        if (!slot) return false;
        packet = *slot;
        slot->ready = false;
        return true;
    }

    bool normalPending() const { return normal_.ready; }
    bool stopPending() const { return stop_.ready; }

private:
    Packet normal_{};
    Packet stop_{};
};

class EventCursor {
public:
    void prepare(uint32_t storedCursor,
                 uint32_t oldestRecordId,
                 uint32_t newestRecordId,
                 uint32_t recordCount);
    uint32_t nextRecordId() const;
    bool awaitingAck() const;
    void enqueued(int messageId, uint32_t recordId);
    uint32_t matchingAckRecord(int messageId) const;
    bool commitAck(int messageId, bool cursorPersisted);
    bool skipRecord(uint32_t recordId,
                    uint32_t newestRecordId,
                    bool cursorPersisted);
    void disconnected();

private:
    uint32_t nextRecordId_ = 0;
    uint32_t pendingRecordId_ = 0;
    int pendingMessageId_ = -1;
};

}  // namespace IrrigationMqttCore
