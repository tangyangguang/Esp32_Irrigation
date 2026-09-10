#include "IrrigationCommandJournal.h"
#include <cstring>

bool IrrigationCommandJournal::StoredCommand::assign(const IrrigationIotProtocol::Command& input) {
    if (!iot_device::parseUuid(input.commandId, std::strlen(input.commandId), id)) return false;
    signature=input.signature; expires=input.expiresAtMs; kind=input.kind;
    return true;
}
bool IrrigationCommandJournal::StoredCommand::idEquals(const char* text) const {
    uint8_t candidate[16];
    return text && iot_device::parseUuid(text,std::strlen(text),candidate) && !std::memcmp(id,candidate,16);
}
bool IrrigationCommandJournal::StoredCommand::sameCommand(const IrrigationIotProtocol::Command& input) const {
    return idEquals(input.commandId) && signature==input.signature && expires==input.expiresAtMs && kind==input.kind;
}
void IrrigationCommandJournal::StoredCommand::formatId(char output[37]) const { iot_device::uuidText(id,output); }
bool IrrigationCommandJournal::begin() { ready_=true; return true; }
bool IrrigationCommandJournal::ready() const { return ready_; }
bool IrrigationCommandJournal::observeTime(uint64_t nowMs) { return ready_ && ledger_.observeTime(nowMs); }
bool IrrigationCommandJournal::admit(const IrrigationIotProtocol::Command& command,uint64_t nowMs) {
    return observeTime(nowMs) && !ledger_.beforeBoot(command.issuedAtMs);
}
IrrigationCommandJournal::LookupResult IrrigationCommandJournal::lookup(
    const IrrigationIotProtocol::Command& command,uint64_t nowMs,std::size_t& index) {
    index=kCapacity;
    if (!ready_) return LookupResult::NotFound;
    ledger_.purge(nowMs);
    for (std::size_t n=0;n<kCapacity;++n) {
        const auto* value=ledger_.at(n);
        if (value && value->command.idEquals(command.commandId)) {
            index=n;
            return value->command.sameCommand(command) ? LookupResult::SameCommand : LookupResult::ConflictingCommand;
        }
    }
    return LookupResult::NotFound;
}
bool IrrigationCommandJournal::storeReceipt(const IrrigationIotProtocol::Command& command,
    ReceiptStatus status,Reason reason,uint64_t observedAtMs,bool processOpen,uint64_t nowMs,std::size_t& index) {
    index=kCapacity;
    if (status==ReceiptStatus::NONE || !observedAtMs || !admit(command,nowMs) || ledger_.find(command.commandId)) return false;
    auto* value=ledger_.remember(command,nowMs);
    if (!value) return false;
    value->receipt=status; value->reason=reason; value->receiptAtMs=observedAtMs;
    value->receiptOrder=1; value->processOpen=processOpen;
    for (std::size_t n=0;n<kCapacity;++n) if (ledger_.at(n)==value) { index=n; return true; }
    return false;
}
bool IrrigationCommandJournal::storeFinal(std::size_t index,ProgressStatus status,Reason reason,uint64_t observedAtMs) {
    auto* value=entry(index);
    if (!value || status==ProgressStatus::NONE || status==ProgressStatus::RUNNING || !observedAtMs ||
        value->receipt!=ReceiptStatus::ACCEPTED || value->progress!=ProgressStatus::NONE ||
        observedAtMs<value->receiptAtMs) return false;
    value->progress=status; value->reason=reason; value->progressAtMs=observedAtMs;
    value->progressOrder=1; value->processOpen=false;
    return true;
}
bool IrrigationCommandJournal::closeWithoutFinal(std::size_t index) {
    auto* value=entry(index);
    if (!value || value->progress!=ProgressStatus::NONE) return false;
    value->processOpen=false; // Keep a nonterminal slot protected; never invent an outcome.
    return true;
}
IrrigationCommandJournal::Entry* IrrigationCommandJournal::entry(std::size_t index) { return ready_ ? ledger_.at(index) : nullptr; }
void IrrigationCommandJournal::replay() {
    for (std::size_t n=0;n<kCapacity;++n) if (auto* value=entry(n)) {
        value->receiptOrder=1;
        if (value->progress!=ProgressStatus::NONE) value->progressOrder=1;
    }
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
        case ProgressStatus::NONE: return "none";
        case ProgressStatus::RUNNING: return "running";
        case ProgressStatus::SUCCEEDED: return "succeeded";
        case ProgressStatus::CANCELED: return "canceled";
        case ProgressStatus::FAILED: return "failed";
    }
    return "failed";
}
