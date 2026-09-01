#pragma once

#include <Esp32Base.h>
#include <Preferences.h>

#include <cstdint>

#include "IrrigationAuditStore.h"
#include "IrrigationIotProtocol.h"
#include "WateringRecordStore.h"

class IrrigationRecordSync {
public:
    enum class StreamKind : uint8_t { Watering, Audit };
    struct PendingRecord {
        StreamKind stream = StreamKind::Watering;
        uint32_t sequence = 0;
        Esp32BaseRecordStore::RecordTiming timing{};
        WateringRecordPayload watering{};
        IrrigationAuditPayload audit{};
    };

    static IrrigationRecordSync& instance();

    bool begin(WateringRecordStore& wateringStore,
               IrrigationAuditStore& auditStore);
    bool appendWatering(const Esp32BaseRecordStore::RecordStartTime& startTime,
                        const WateringSessionSummary& summary,
                        const char* relatedCommandId);
    bool appendAudit(const IrrigationAuditPayload& payload);
    bool appendAudit(
        const Esp32BaseRecordStore::RecordStartTime& startTime,
        const IrrigationAuditPayload& payload);
    bool readOldestPending(StreamKind stream, PendingRecord& record);
    bool acknowledge(const IrrigationIotProtocol::RecordAck& ack,
                     uint32_t nowMs,
                     StreamKind* acknowledgedStream = nullptr);
    bool resetGenerationsAfterFormat();
    void handle(uint32_t nowMs);

    bool ready() const;
    bool writable() const;
    bool writable(StreamKind stream) const;
    bool backlogFull(StreamKind stream) const;
    const char* streamId(StreamKind stream) const;
    uint32_t acknowledgedThroughSequence(StreamKind stream) const;
    uint32_t pendingCount(StreamKind stream) const;

private:
    struct StreamState {
        Preferences preferences;
        char streamId[IrrigationIotProtocol::kUuidBufferSize]{};
        uint32_t persistedAcknowledged = 0;
        uint32_t acknowledged = 0;
        uint32_t lastCheckpointMs = 0;
        bool checkpointDirty = false;
        bool backlogFull = false;
        bool metadataReady = false;
    };
    struct Metadata {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t reserved = 0;
        char streamId[IrrigationIotProtocol::kUuidBufferSize]{};
        uint32_t acknowledged = 0;
        uint32_t crc32 = 0;
    };

    static constexpr uint32_t kMetadataMagic = 0x32525349UL;  // ISR2
    static constexpr uint16_t kMetadataVersion = 1;
    static constexpr uint32_t kCheckpointBatch = 32;
    static constexpr uint32_t kCheckpointIntervalMs =
        24UL * 60UL * 60UL * 1000UL;

    StreamState& state(StreamKind stream);
    const StreamState& state(StreamKind stream) const;
    Esp32BaseRecordStore& store(StreamKind stream);
    const Esp32BaseRecordStore& store(StreamKind stream) const;
    bool loadMetadata(StreamKind stream, const char* nvsNamespace);
    bool saveMetadata(StreamKind stream);
    bool canAppend(StreamKind stream);
    bool resetGeneration(StreamKind stream);
    static uint32_t metadataCrc(const Metadata& metadata);

    WateringRecordStore* wateringStore_ = nullptr;
    IrrigationAuditStore* auditStore_ = nullptr;
    StreamState watering_{};
    StreamState audit_{};
    bool registered_ = false;
    bool ready_ = false;
};
