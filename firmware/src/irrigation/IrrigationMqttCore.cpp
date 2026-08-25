#include "IrrigationMqttCore.h"

#include <cstdio>

namespace IrrigationMqttCore {
namespace {

bool hexDigit(char value, uint8_t& digit) {
    if (value >= '0' && value <= '9') digit = static_cast<uint8_t>(value - '0');
    else if (value >= 'a' && value <= 'f') digit = static_cast<uint8_t>(value - 'a' + 10);
    else if (value >= 'A' && value <= 'F') digit = static_cast<uint8_t>(value - 'A' + 10);
    else return false;
    return true;
}

bool jsonStringEquals(const char* begin,
                      const char* end,
                      const char* expected,
                      const char*& after) {
    if (begin >= end || *begin != '"') return false;
    const char* cursor = begin + 1;
    const char* match = expected;
    bool equal = true;
    while (cursor < end) {
        char value = *cursor++;
        if (value == '"') {
            after = cursor;
            return equal && *match == '\0';
        }
        if (value == '\\') {
            if (cursor >= end) return false;
            const char escaped = *cursor++;
            switch (escaped) {
                case '"': value = '"'; break;
                case '\\': value = '\\'; break;
                case '/': value = '/'; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'n': value = '\n'; break;
                case 'r': value = '\r'; break;
                case 't': value = '\t'; break;
                case 'u': {
                    if (end - cursor < 4) return false;
                    uint16_t code = 0;
                    for (uint8_t index = 0; index < 4; ++index) {
                        uint8_t digit = 0;
                        if (!hexDigit(cursor[index], digit)) return false;
                        code = static_cast<uint16_t>((code << 4U) | digit);
                    }
                    cursor += 4;
                    if (code > 0x7FU) equal = false;
                    value = static_cast<char>(code);
                    break;
                }
                default: return false;
            }
        }
        if (*match == '\0' || value != *match) equal = false;
        else ++match;
    }
    return false;
}

const char* skipWhitespace(const char* cursor, const char* end) {
    while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                            *cursor == '\r' || *cursor == '\n')) ++cursor;
    return cursor;
}

void copyId(std::array<char, 37>& target, const char* source) {
    target = {};
    if (source) std::snprintf(target.data(), target.size(), "%s", source);
}

}  // namespace

const char* channelName(Channel channel) {
    switch (channel) {
        case Channel::Availability: return "availability";
        case Channel::State: return "state";
        case Channel::Event: return "event";
        case Channel::Command: return "command";
        case Channel::Receipt: return "receipt";
        case Channel::Progress: return "progress";
    }
    return "";
}

PublicationPolicy publicationPolicy(Channel channel) {
    return {1, channel == Channel::Availability};
}

bool acceptsCommandTransport(uint8_t qos, bool retain) {
    return qos == 1 && !retain;
}

bool matchesCommandTopic(const char* topic,
                         std::size_t length,
                         const char* deviceId) {
    if (!topic || !deviceId) return false;
    char expected[192]{};
    if (!IrrigationPlatformProtocol::formatTopic(
            deviceId, "command", expected, sizeof(expected))) return false;
    const std::size_t expectedLength = std::strlen(expected);
    return length == expectedLength &&
           std::memcmp(topic, expected, expectedLength) == 0;
}

void TrustedTimeAnchor::observe(bool synced,
                                uint32_t epochSec,
                                uint32_t currentMillis) {
    if (available()) {
        const uint32_t elapsedSec =
            static_cast<uint32_t>(currentMillis - anchorMillis_) / 1000U;
        epochSec_ += elapsedSec;
        anchorMillis_ += elapsedSec * 1000U;
    }
    if (synced && epochSec != 0) {
        if (!available() || epochSec > epochSec_) {
            epochSec_ = epochSec;
            anchorMillis_ = currentMillis;
        }
    }
}

bool TrustedTimeAnchor::available() const { return epochSec_ != 0; }

uint64_t TrustedTimeAnchor::currentTimeMs(uint32_t currentMillis) const {
    if (!available()) return 0;
    return static_cast<uint64_t>(epochSec_) * 1000ULL +
           static_cast<uint32_t>(currentMillis - anchorMillis_);
}

uint32_t TrustedTimeAnchor::currentEpochSec(uint32_t currentMillis) const {
    return static_cast<uint32_t>(currentTimeMs(currentMillis) / 1000ULL);
}

CommandGuardRejection evaluateCommandGuard(const CommandGuardContext& context) {
    if (context.currentTimeMs == 0)
        return CommandGuardRejection::TimeUntrusted;
    if (context.expiresAtMs <= context.currentTimeMs)
        return CommandGuardRejection::Expired;
    if (!context.businessReady || !context.configurationReady)
        return CommandGuardRejection::NotReady;
    if (context.capability == IrrigationPlatformProtocol::Capability::Stop) {
        return context.wateringActive && !context.normalWatering
                   ? CommandGuardRejection::MaintenanceActivity
                   : CommandGuardRejection::None;
    }
    if (context.capability ==
            IrrigationPlatformProtocol::Capability::AutomaticWatering &&
        context.automaticMode == AutomaticWateringMode::PausedUntil) {
        if (!context.calendarTimeTrusted)
            return CommandGuardRejection::TimeUntrusted;
        if (context.resumeAtEpoch <= context.currentTimeMs / 1000ULL)
            return CommandGuardRejection::InvalidResumeTime;
    }
    return CommandGuardRejection::None;
}

const char* commandGuardReason(CommandGuardRejection rejection) {
    switch (rejection) {
        case CommandGuardRejection::None: return nullptr;
        case CommandGuardRejection::TimeUntrusted: return "time_untrusted";
        case CommandGuardRejection::Expired: return "expired";
        case CommandGuardRejection::NotReady: return "not_ready";
        case CommandGuardRejection::MaintenanceActivity:
            return "maintenance_activity";
        case CommandGuardRejection::InvalidResumeTime:
            return "invalid_resume_time";
    }
    return "not_ready";
}

bool shouldReplayProgress(EvidenceStatus status) {
    return status == EvidenceStatus::Running ||
           status == EvidenceStatus::Succeeded ||
           status == EvidenceStatus::Failed;
}

bool isStopCommandEnvelope(const char* payload, std::size_t length) {
    if (!payload || length == 0) return false;
    const char* cursor = payload;
    const char* end = payload + length;
    uint8_t depth = 0;
    while (cursor < end) {
        if (*cursor == '"') {
            const char* afterKey = nullptr;
            const bool capabilityKey = depth == 1 &&
                jsonStringEquals(cursor, end, "capabilityKey", afterKey);
            if (!afterKey) {
                // Find the end of any non-key string without allocating.
                const char* ignored = nullptr;
                jsonStringEquals(cursor, end, "", ignored);
                if (!ignored) return false;
                cursor = ignored;
                continue;
            }
            cursor = skipWhitespace(afterKey, end);
            if (!capabilityKey || cursor >= end || *cursor != ':') continue;
            cursor = skipWhitespace(cursor + 1, end);
            const char* afterValue = nullptr;
            return jsonStringEquals(cursor, end, "operation.stop", afterValue);
        }
        if (*cursor == '{' || *cursor == '[') ++depth;
        else if ((*cursor == '}' || *cursor == ']') && depth != 0) --depth;
        ++cursor;
    }
    return false;
}

AdmissionResult EvidenceStore::admit(
    const char* commandId,
    const std::array<uint8_t, 32>& signature,
    uint64_t expiresAtMs,
    IrrigationPlatformProtocol::Capability capability,
    uint64_t nowMs) {
    if (EvidenceEntry* previous = find(commandId)) {
        return {previous->signature == signature ? Admission::Replay
                                                 : Admission::Conflict,
                previous};
    }
    for (EvidenceEntry& entry : entries_) {
        if (entry.used && entry.expiresAtMs <= nowMs &&
            entry.progress != EvidenceStatus::Running) entry = {};
    }
    for (EvidenceEntry& entry : entries_) {
        if (entry.used) continue;
        entry = {};
        entry.used = true;
        copyId(entry.commandId, commandId);
        entry.signature = signature;
        entry.expiresAtMs = expiresAtMs;
        entry.capability = capability;
        return {Admission::New, &entry};
    }
    return {Admission::Full, nullptr};
}

EvidenceEntry* EvidenceStore::find(const char* commandId) {
    for (EvidenceEntry& entry : entries_) {
        if (entry.used && commandId &&
            std::strcmp(entry.commandId.data(), commandId) == 0) return &entry;
    }
    return nullptr;
}

const EvidenceEntry* EvidenceStore::find(const char* commandId) const {
    for (const EvidenceEntry& entry : entries_) {
        if (entry.used && commandId &&
            std::strcmp(entry.commandId.data(), commandId) == 0) return &entry;
    }
    return nullptr;
}

bool EvidenceStore::markAccepted(EvidenceEntry& entry) {
    if (!entry.used || entry.receipt != EvidenceStatus::None) return false;
    entry.receipt = EvidenceStatus::Accepted;
    entry.reason = {};
    return true;
}

bool EvidenceStore::markRejected(EvidenceEntry& entry, const char* reason) {
    if (!entry.used || entry.receipt != EvidenceStatus::None || !reason) return false;
    entry.receipt = EvidenceStatus::Rejected;
    std::snprintf(entry.reason.data(), entry.reason.size(), "%s", reason);
    return true;
}

bool EvidenceStore::markRunning(EvidenceEntry& entry) {
    if (!entry.used || entry.receipt != EvidenceStatus::Accepted ||
        entry.progress != EvidenceStatus::None) return false;
    entry.progress = EvidenceStatus::Running;
    entry.reason = {};
    return true;
}

bool EvidenceStore::markTerminal(EvidenceEntry& entry,
                                 EvidenceStatus terminal,
                                 const char* reason) {
    if (!entry.used || entry.receipt != EvidenceStatus::Accepted ||
        entry.progress != EvidenceStatus::Running ||
        (terminal != EvidenceStatus::Succeeded && terminal != EvidenceStatus::Failed)) {
        return false;
    }
    entry.progress = terminal;
    entry.reason = {};
    if (reason) std::snprintf(entry.reason.data(), entry.reason.size(), "%s", reason);
    return true;
}

EvidenceCommitResult EvidenceStore::commitReceipt(
    EvidenceEntry& entry,
    EvidenceStatus receipt,
    const char* reason,
    EvidencePersistCallback persist,
    void* context) {
    const bool changed = receipt == EvidenceStatus::Accepted
                             ? markAccepted(entry)
                             : receipt == EvidenceStatus::Rejected
                                   ? markRejected(entry, reason)
                                   : false;
    if (!changed) return EvidenceCommitResult::InvalidTransition;
    if (persist && persist(context)) return EvidenceCommitResult::Persisted;
    entry = {};
    return EvidenceCommitResult::PersistenceFailed;
}

EvidenceCommitResult EvidenceStore::commitProgress(
    EvidenceEntry& entry,
    EvidenceStatus progress,
    const char* reason,
    EvidencePersistCallback persist,
    void* context) {
    const EvidenceEntry previous = entry;
    const bool changed = progress == EvidenceStatus::Running
                             ? markRunning(entry)
                             : markTerminal(entry, progress, reason);
    if (!changed) return EvidenceCommitResult::InvalidTransition;
    if (persist && persist(context)) return EvidenceCommitResult::Persisted;
    entry = previous;
    return EvidenceCommitResult::PersistenceFailed;
}

bool EvidenceStore::validate() const {
    for (const EvidenceEntry& entry : entries_) {
        if (!entry.used) continue;
        if (!IrrigationPlatformProtocol::isUuid(entry.commandId.data()) ||
            entry.receipt == EvidenceStatus::None ||
            entry.receipt > EvidenceStatus::Rejected ||
            entry.progress > EvidenceStatus::Failed ||
            (entry.receipt == EvidenceStatus::Rejected &&
             entry.progress != EvidenceStatus::None)) return false;
    }
    return true;
}

bool EvidenceStore::reconcileAfterRestart() {
    bool changed = false;
    for (EvidenceEntry& entry : entries_) {
        if (entry.used && entry.progress == EvidenceStatus::Running) {
            entry.progress = EvidenceStatus::None;
            entry.reason = {};
            changed = true;
        }
    }
    return changed;
}

std::array<EvidenceEntry, kEvidenceCapacity>& EvidenceStore::entries() {
    return entries_;
}

const std::array<EvidenceEntry, kEvidenceCapacity>& EvidenceStore::entries() const {
    return entries_;
}

void RemoteOperationTracker::start(const char* commandId) { copyId(active_, commandId); }
void RemoteOperationTracker::requestStop(const char* commandId) { copyId(stop_, commandId); }
bool RemoteOperationTracker::hasActive() const { return active_[0] != '\0'; }
bool RemoteOperationTracker::hasPendingStop() const { return stop_[0] != '\0'; }
const char* RemoteOperationTracker::activeCommandId() const { return active_.data(); }
const char* RemoteOperationTracker::pendingStopCommandId() const { return stop_.data(); }

StopCompletion RemoteOperationTracker::completeIdleStop() {
    StopCompletion result{};
    if (!hasPendingStop()) return result;
    copyId(result.updates[0].commandId, stop_.data());
    result.updates[0].status = EvidenceStatus::Succeeded;
    result.count = 1;
    stop_ = {};
    return result;
}

StopCompletion RemoteOperationTracker::completeStoppedOperation() {
    StopCompletion result{};
    if (hasActive()) {
        copyId(result.updates[result.count].commandId, active_.data());
        result.updates[result.count].status = EvidenceStatus::Failed;
        result.updates[result.count].reason = "stopped";
        ++result.count;
        active_ = {};
    }
    if (hasPendingStop()) {
        copyId(result.updates[result.count].commandId, stop_.data());
        result.updates[result.count].status = EvidenceStatus::Succeeded;
        ++result.count;
        stop_ = {};
    }
    return result;
}

TerminalUpdate RemoteOperationTracker::completeOperation(EvidenceStatus status,
                                                         const char* reason) {
    TerminalUpdate result{};
    if (!hasActive()) return result;
    copyId(result.commandId, active_.data());
    result.status = status;
    result.reason = reason;
    active_ = {};
    return result;
}

void EventCursor::prepare(uint32_t storedCursor,
                          uint32_t oldestRecordId,
                          uint32_t newestRecordId,
                          uint32_t recordCount) {
    pendingMessageId_ = -1;
    pendingRecordId_ = 0;
    if (recordCount == 0) {
        nextRecordId_ = 0;
        return;
    }
    nextRecordId_ = storedCursor == UINT32_MAX ? 0 : storedCursor + 1U;
    if (nextRecordId_ == 0 || nextRecordId_ < oldestRecordId)
        nextRecordId_ = oldestRecordId;
    if (nextRecordId_ > newestRecordId) nextRecordId_ = 0;
}

uint32_t EventCursor::nextRecordId() const { return nextRecordId_; }
bool EventCursor::awaitingAck() const { return pendingMessageId_ >= 0; }

void EventCursor::enqueued(int messageId, uint32_t recordId) {
    if (messageId < 0 || recordId == 0) return;
    pendingMessageId_ = messageId;
    pendingRecordId_ = recordId;
}

uint32_t EventCursor::matchingAckRecord(int messageId) const {
    return messageId == pendingMessageId_ ? pendingRecordId_ : 0;
}

bool EventCursor::commitAck(int messageId, bool cursorPersisted) {
    if (messageId != pendingMessageId_ || pendingRecordId_ == 0 || !cursorPersisted)
        return false;
    nextRecordId_ = pendingRecordId_ == UINT32_MAX ? 0 : pendingRecordId_ + 1U;
    pendingMessageId_ = -1;
    pendingRecordId_ = 0;
    return true;
}

bool EventCursor::skipRecord(uint32_t recordId,
                             uint32_t newestRecordId,
                             bool cursorPersisted) {
    if (recordId == 0 || recordId > newestRecordId) {
        nextRecordId_ = 0;
        return false;
    }
    if (!cursorPersisted) return false;
    nextRecordId_ = recordId == UINT32_MAX ? 0 : recordId + 1U;
    return true;
}

void EventCursor::disconnected() {
    pendingMessageId_ = -1;
    pendingRecordId_ = 0;
}

}  // namespace IrrigationMqttCore
