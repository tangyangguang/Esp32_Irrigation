#pragma once

#include <Preferences.h>

#include <cstddef>
#include <cstdint>

#include "IrrigationIotProtocol.h"

class IrrigationCommandJournal {
public:
    static constexpr std::size_t kCapacity = 16;

    enum class ReceiptStatus : uint8_t {
        None,
        Accepted,
        Rejected,
    };

    enum class ProgressStatus : uint8_t {
        None,
        Succeeded,
        Canceled,
        Failed,
    };

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

    struct Entry {
        char commandId[IrrigationIotProtocol::kUuidBufferSize]{};
        uint64_t signature = 0;
        uint64_t expiresAtMs = 0;
        uint64_t receiptObservedAtMs = 0;
        uint64_t progressObservedAtMs = 0;
        IrrigationIotProtocol::CommandKind kind =
            IrrigationIotProtocol::CommandKind::Stop;
        ReceiptStatus receipt = ReceiptStatus::None;
        Reason receiptReason = Reason::None;
        ProgressStatus progress = ProgressStatus::None;
        Reason progressReason = Reason::None;
        bool processOpen = false;
    };

    enum class LookupResult : uint8_t {
        NotFound,
        SameCommand,
        ConflictingCommand,
    };

    bool begin();
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
    const Entry* entry(std::size_t index) const;
    Entry* entry(std::size_t index);
    bool ready() const;

    static Reason fromRejection(IrrigationIotProtocol::Rejection rejection);
    static const char* reasonName(Reason reason);
    static const char* progressName(ProgressStatus status);

private:
    struct PersistentState {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t reserved = 0;
        Entry entries[kCapacity]{};
        uint32_t crc32 = 0;
    };

    static constexpr uint32_t kMagic = 0x494F5443UL;  // IOTC
    static constexpr uint16_t kVersion = 1;
    static constexpr const char* kNamespace = "irr_iot_cmd";
    static constexpr const char* kKey = "journal";

    bool save();
    bool valid(const PersistentState& state) const;
    void initialize();
    void clearExpired(uint64_t nowMs);
    std::size_t findReusableSlot(uint64_t nowMs);
    static uint32_t calculateCrc(const PersistentState& state);

    Preferences preferences_;
    PersistentState state_{};
    bool ready_ = false;
};
