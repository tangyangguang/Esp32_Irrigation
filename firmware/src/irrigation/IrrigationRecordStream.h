#pragma once

#include <Esp32Base.h>
#include <Preferences.h>

#include <cstddef>
#include <cstdint>

#include "IrrigationIotProtocol.h"
#include "WateringRecordCodec.h"

class IrrigationRecordStream {
public:
    static constexpr uint32_t kCapacity = 200;
    static constexpr std::size_t kPayloadSize = 728;
    static constexpr std::size_t kDataSize = 673;

    enum class RecordKind : uint8_t {
        Watering = 1,
        Json = 2,
    };

    static constexpr uint32_t kLocalWateringRecordIdMask = 0x80000000UL;

    struct Record {
        uint32_t sequence = 0;
        uint32_t observedAtEpoch = 0;
        uint32_t startedAtEpoch = 0;
        RecordKind kind = RecordKind::Json;
        char relatedCommandId[IrrigationIotProtocol::kUuidBufferSize]{};
        char eventKey[48]{};
        uint16_t dataLength = 0;
        uint8_t data[kDataSize]{};
    };

    static IrrigationRecordStream& instance();

    bool begin();
    bool appendWatering(
        const Esp32BaseRecordStore::RecordStartTime& startTime,
        const WateringSessionSummary& summary,
        const char* relatedCommandId);
    bool appendJson(const char* eventKey,
                    const char* dataJson,
                    uint32_t observedAtEpoch = 0);
    struct WateringView {
        uint32_t localRecordId = 0;
        Esp32BaseRecordStore::RecordTiming timing;
        WateringRecordPayload payload;
    };

    bool readOldestPending(Record& record);
    bool readLatestWatering(uint32_t offset, WateringView& record);
    Esp32BaseRecordStore::RecordReadResult readWateringByLocalId(
        uint32_t localRecordId,
        WateringView& record);
    uint32_t wateringRecordCount();
    bool acknowledge(const IrrigationIotProtocol::RecordAck& ack,
                     uint32_t nowMs);
    bool resetGenerationAfterFormat();
    void handle(uint32_t nowMs);

    bool ready() const;
    bool writable();
    bool backlogFull() const;
    const char* streamId() const;
    uint32_t acknowledgedThroughSequence() const;
    uint32_t pendingCount() const;
    Esp32BaseRecordStore& baseStore();

private:
    struct Metadata {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t reserved = 0;
        char streamId[IrrigationIotProtocol::kUuidBufferSize]{};
        uint32_t acknowledgedThroughSequence = 0;
        uint32_t crc32 = 0;
    };

    static constexpr uint32_t kMagic = 0x52545349UL;  // ISTR
    static constexpr uint16_t kVersion = 1;
    static constexpr uint16_t kStoreVersion = 1;
    static constexpr uint32_t kMaximumStoreBytes = 151168;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 32UL * 1024UL;
    static constexpr uint32_t kAckCheckpointBatch = 32;
    static constexpr uint32_t kAckCheckpointIntervalMs = 24UL * 60UL * 60UL * 1000UL;
    static constexpr const char* kNamespace = "irr_iot_rec";
    static constexpr const char* kMetadataKey = "stream";

    bool appendEncoded(RecordKind kind,
                       uint32_t observedAtEpoch,
                       uint32_t startedAtEpoch,
                       const char* relatedCommandId,
                       const uint8_t* data,
                       uint16_t dataLength,
                       const Esp32BaseRecordStore::RecordStartTime* startTime);
    bool canAppend(uint8_t requiredSlots = 1);
    bool readRecord(uint32_t sequence, Record& record);
    bool loadMetadata(uint32_t recordCount);
    bool saveMetadata();
    bool rebuildWateringIndex(uint32_t recordCount);
    bool updateWateringIndexAfterAppend(bool appendedWatering);
    static bool decodePayload(const uint8_t* payload,
                              std::size_t length,
                              Record& record);
    static bool encodePayload(RecordKind kind,
                              uint32_t observedAtEpoch,
                              uint32_t startedAtEpoch,
                              const char* relatedCommandId,
                              const uint8_t* data,
                              uint16_t dataLength,
                              uint8_t* payload,
                              std::size_t payloadLength);
    static uint32_t calculateMetadataCrc(const Metadata& metadata);

    Esp32BaseRecordStore store_;
    Preferences preferences_;
    Metadata metadata_{};
    uint32_t acknowledgedInRam_ = 0;
    uint32_t lastCheckpointMs_ = 0;
    bool checkpointDirty_ = false;
    bool registered_ = false;
    bool ready_ = false;
    bool backlogFull_ = false;
    uint32_t wateringSequences_[kCapacity]{};
    uint16_t wateringSequenceCount_ = 0;
    uint8_t scratch_[kPayloadSize]{};
};
