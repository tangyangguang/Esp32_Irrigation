#include "IrrigationCommandJournal.h"

#include <cstddef>
#include <cstring>

namespace {

uint32_t updateCrc32(uint32_t crc, const uint8_t* data, std::size_t length) {
    crc = ~crc;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320UL &
                                 (0U - static_cast<uint32_t>(crc & 1U)));
        }
    }
    return ~crc;
}

}  // namespace

bool IrrigationCommandJournal::begin() {
    ready_ = false;
    if (!preferences_.begin(kNamespace, false)) {
        return false;
    }
    const std::size_t storedLength = preferences_.getBytesLength(kKey);
    if (storedLength == 0U) {
        initialize();
        ready_ = true;
        return true;
    }
    if (storedLength != sizeof(state_) ||
        preferences_.getBytes(kKey, &state_, sizeof(state_)) != sizeof(state_) ||
        !valid(state_)) {
        return false;
    }
    ready_ = true;
    bool interrupted = false;
    for (Entry& entry : state_.entries) {
        if (entry.commandId[0] != '\0' && entry.processOpen) {
            entry.processOpen = false;
            interrupted = true;
        }
    }
    if (interrupted && !save()) {
        ready_ = false;
    }
    return ready_;
}

IrrigationCommandJournal::LookupResult IrrigationCommandJournal::lookup(
    const IrrigationIotProtocol::Command& command,
    uint64_t nowMs,
    std::size_t& index) {
    index = kCapacity;
    if (!ready_) {
        return LookupResult::NotFound;
    }
    clearExpired(nowMs);
    for (std::size_t candidate = 0; candidate < kCapacity; ++candidate) {
        const Entry& stored = state_.entries[candidate];
        if (stored.commandId[0] != '\0' &&
            std::strcmp(stored.commandId, command.commandId) == 0) {
            index = candidate;
            return stored.signature == command.signature
                       ? LookupResult::SameCommand
                       : LookupResult::ConflictingCommand;
        }
    }
    return LookupResult::NotFound;
}

bool IrrigationCommandJournal::storeReceipt(
    const IrrigationIotProtocol::Command& command,
    ReceiptStatus status,
    Reason reason,
    uint64_t observedAtMs,
    bool processOpen,
    uint64_t nowMs,
    std::size_t& index) {
    index = kCapacity;
    if (!ready_ || status == ReceiptStatus::None) {
        return false;
    }
    clearExpired(nowMs);
    index = findReusableSlot(nowMs);
    if (index >= kCapacity) {
        return false;
    }
    Entry& stored = state_.entries[index];
    stored = {};
    std::strcpy(stored.commandId, command.commandId);
    stored.signature = command.signature;
    stored.expiresAtMs = command.expiresAtMs;
    stored.receiptObservedAtMs = observedAtMs;
    stored.kind = command.kind;
    stored.receipt = status;
    stored.receiptReason = reason;
    stored.processOpen = processOpen;
    if (!save()) {
        ready_ = false;
        return false;
    }
    return true;
}

bool IrrigationCommandJournal::storeFinal(std::size_t index,
                                          ProgressStatus status,
                                          Reason reason,
                                          uint64_t observedAtMs) {
    if (!ready_ || index >= kCapacity || status == ProgressStatus::None ||
        state_.entries[index].commandId[0] == '\0') {
        return false;
    }
    Entry& stored = state_.entries[index];
    stored.progress = status;
    stored.progressReason = reason;
    stored.progressObservedAtMs = observedAtMs;
    stored.processOpen = false;
    if (!save()) {
        ready_ = false;
        return false;
    }
    return true;
}

bool IrrigationCommandJournal::closeWithoutFinal(std::size_t index) {
    if (!ready_ || index >= kCapacity ||
        state_.entries[index].commandId[0] == '\0') {
        return false;
    }
    Entry& stored = state_.entries[index];
    stored.processOpen = false;
    stored.progress = ProgressStatus::None;
    stored.progressReason = Reason::None;
    stored.progressObservedAtMs = 0;
    if (!save()) {
        ready_ = false;
        return false;
    }
    return true;
}

const IrrigationCommandJournal::Entry* IrrigationCommandJournal::entry(
    std::size_t index) const {
    return index < kCapacity && state_.entries[index].commandId[0] != '\0'
               ? &state_.entries[index]
               : nullptr;
}

IrrigationCommandJournal::Entry* IrrigationCommandJournal::entry(
    std::size_t index) {
    return index < kCapacity && state_.entries[index].commandId[0] != '\0'
               ? &state_.entries[index]
               : nullptr;
}

bool IrrigationCommandJournal::ready() const {
    return ready_;
}

IrrigationCommandJournal::Reason IrrigationCommandJournal::fromRejection(
    IrrigationIotProtocol::Rejection rejection) {
    using Rejection = IrrigationIotProtocol::Rejection;
    switch (rejection) {
        case Rejection::None: return Reason::None;
        case Rejection::Expired: return Reason::Expired;
        case Rejection::NotReady: return Reason::NotReady;
        case Rejection::Busy: return Reason::Busy;
        case Rejection::MaintenanceActivity: return Reason::MaintenanceActivity;
        case Rejection::ZoneUnavailable: return Reason::ZoneUnavailable;
        case Rejection::DurationLimit: return Reason::DurationLimit;
        case Rejection::VolumeLimit: return Reason::VolumeLimit;
        case Rejection::RevisionConflict: return Reason::RevisionConflict;
        case Rejection::PlanConflict: return Reason::PlanConflict;
        case Rejection::TimeUntrusted: return Reason::TimeUntrusted;
        case Rejection::InvalidResumeTime: return Reason::InvalidResumeTime;
    }
    return Reason::InternalState;
}

const char* IrrigationCommandJournal::reasonName(Reason reason) {
    switch (reason) {
        case Reason::None: return "none";
        case Reason::Expired: return "expired";
        case Reason::NotReady: return "not_ready";
        case Reason::Busy: return "busy";
        case Reason::MaintenanceActivity: return "maintenance_activity";
        case Reason::ZoneUnavailable: return "zone_unavailable";
        case Reason::DurationLimit: return "duration_limit";
        case Reason::VolumeLimit: return "volume_limit";
        case Reason::RevisionConflict: return "revision_conflict";
        case Reason::PlanConflict: return "plan_conflict";
        case Reason::TimeUntrusted: return "time_untrusted";
        case Reason::InvalidResumeTime: return "invalid_resume_time";
        case Reason::PersistenceError: return "persistence_error";
        case Reason::HardwareFailure: return "hardware_failure";
        case Reason::InternalState: return "internal_state";
        case Reason::FlowStartTimeout: return "flow_start_timeout";
        case Reason::NoFlowTimeout: return "no_flow_timeout";
        case Reason::LowFlow: return "low_flow";
        case Reason::HighFlow: return "high_flow";
        case Reason::TargetVolumeTimeout: return "target_volume_timeout";
    }
    return "internal_state";
}

const char* IrrigationCommandJournal::progressName(ProgressStatus status) {
    switch (status) {
        case ProgressStatus::None: return "none";
        case ProgressStatus::Succeeded: return "succeeded";
        case ProgressStatus::Canceled: return "canceled";
        case ProgressStatus::Failed: return "failed";
    }
    return "failed";
}

bool IrrigationCommandJournal::save() {
    state_.magic = kMagic;
    state_.version = kVersion;
    state_.reserved = 0;
    state_.crc32 = calculateCrc(state_);
    return preferences_.putBytes(kKey, &state_, sizeof(state_)) == sizeof(state_);
}

bool IrrigationCommandJournal::valid(const PersistentState& state) const {
    return state.magic == kMagic && state.version == kVersion &&
           state.crc32 == calculateCrc(state);
}

void IrrigationCommandJournal::initialize() {
    state_ = {};
    state_.magic = kMagic;
    state_.version = kVersion;
    state_.crc32 = calculateCrc(state_);
}

void IrrigationCommandJournal::clearExpired(uint64_t nowMs) {
    for (Entry& entry : state_.entries) {
        if (entry.commandId[0] != '\0' && !entry.processOpen &&
            entry.expiresAtMs <= nowMs) {
            entry = {};
        }
    }
}

std::size_t IrrigationCommandJournal::findReusableSlot(uint64_t nowMs) {
    for (std::size_t index = 0; index < kCapacity; ++index) {
        Entry& entry = state_.entries[index];
        if (entry.commandId[0] == '\0' ||
            (!entry.processOpen && entry.expiresAtMs <= nowMs)) {
            return index;
        }
    }
    return kCapacity;
}

uint32_t IrrigationCommandJournal::calculateCrc(const PersistentState& state) {
    return updateCrc32(0,
                       reinterpret_cast<const uint8_t*>(&state),
                       offsetof(PersistentState, crc32));
}
