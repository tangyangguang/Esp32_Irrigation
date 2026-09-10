#pragma once

#include <CompactCommandLedger.h>

#include <cstddef>
#include <cstdint>

#include "IrrigationIotProtocol.h"

class IrrigationCommandJournal {
public:
    static constexpr std::size_t kCapacity = 16;

    using ReceiptStatus = iot_device::LedgerReceipt;
    using ProgressStatus = iot_device::LedgerProgress;

    enum class Reason : uint8_t {
        None,
        Expired,
        NotReady,
        Busy,
        MaintenanceActivity,
        ZoneUnavailable,
        DurationLimit,
        VolumeLimit,
        RevisionConflict,
        PlanConflict,
        TimeUntrusted,
        InvalidResumeTime,
        PersistenceError,
        HardwareFailure,
        InternalState,
        FlowStartTimeout,
        NoFlowTimeout,
        LowFlow,
        HighFlow,
        TargetVolumeTimeout,
    };

    struct StoredCommand {
        uint8_t id[16]{};
        uint64_t signature = 0, expires = 0;
        IrrigationIotProtocol::CommandKind kind = IrrigationIotProtocol::CommandKind::Stop;
        bool assign(const IrrigationIotProtocol::Command&);
        bool idEquals(const char*) const;
        bool sameCommand(const IrrigationIotProtocol::Command&) const;
        uint64_t expiresAtMs() const { return expires; }
        void formatId(char output[37]) const;
    };
    // SDK ledger record shape; running evidence is transient, so no unused
    // per-slot running timestamp is retained.
    struct Entry {
        StoredCommand command;
        uint64_t receiptAtMs = 0, progressAtMs = 0;
        uint16_t receiptOrder = 0, runningOrder = 0, progressOrder = 0;
        bool used = false, processOpen = false;
        ReceiptStatus receipt = ReceiptStatus::NONE;
        ProgressStatus progress = ProgressStatus::NONE;
        Reason reason = Reason::None;
    };

    enum class LookupResult : uint8_t {
        NotFound,
        SameCommand,
        ConflictingCommand,
    };

    bool begin();
    bool observeTime(uint64_t nowMs);
    bool admit(const IrrigationIotProtocol::Command&, uint64_t nowMs);
    void replay();
    LookupResult lookup(const IrrigationIotProtocol::Command& command,
                        uint64_t nowMs,
                        std::size_t& index);
    bool storeReceipt(const IrrigationIotProtocol::Command& command,
                      ReceiptStatus status,
                      Reason reason,
                      uint64_t observedAtMs,
                      bool processOpen,
                      uint64_t nowMs,
                      std::size_t& index);
    bool storeFinal(std::size_t index,
                    ProgressStatus status,
                    Reason reason,
                    uint64_t observedAtMs);
    bool closeWithoutFinal(std::size_t index);
    Entry* entry(std::size_t index);
    bool ready() const;

    static Reason fromRejection(IrrigationIotProtocol::Rejection rejection);
    static const char* reasonName(Reason reason);
    static const char* progressName(ProgressStatus status);

private:
    iot_device::CompactCommandLedger<Entry, kCapacity> ledger_;
    bool ready_ = false;
};
