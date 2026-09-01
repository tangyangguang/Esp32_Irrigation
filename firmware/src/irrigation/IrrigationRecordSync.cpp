#include "IrrigationRecordSync.h"

#include <Arduino.h>
#include <esp_system.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {
constexpr const char* kMetadataKey = "stream";
constexpr const char* kWateringNamespace = "irr_wtr_sync";
constexpr const char* kAuditNamespace = "irr_aud_sync";

uint32_t crc32(const uint8_t* data, std::size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1U) ^
                  (0xEDB88320UL & (0U - static_cast<uint32_t>(crc & 1U)));
    }
    return ~crc;
}

void makeUuid(char* output, std::size_t length) {
    uint8_t bytes[16]{};
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    std::snprintf(output, length,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                  "%02x%02x%02x%02x%02x%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                  bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                  bytes[12], bytes[13], bytes[14], bytes[15]);
}
}  // namespace

IrrigationRecordSync& IrrigationRecordSync::instance() {
    static IrrigationRecordSync sync;
    return sync;
}

bool IrrigationRecordSync::begin(WateringRecordStore& wateringStore,
                                 IrrigationAuditStore& auditStore) {
    if (ready_) return true;
    wateringStore_ = &wateringStore;
    auditStore_ = &auditStore;
    if (!wateringStore.isReady() || !auditStore.isReady()) return false;
    if (!registered_) {
        registered_ =
            Esp32BaseStorage::registerRecordStore(wateringStore.baseStore()) &&
            Esp32BaseStorage::registerRecordStore(auditStore.baseStore());
    }
    if (!registered_ || !loadMetadata(StreamKind::Watering, kWateringNamespace) ||
        !loadMetadata(StreamKind::Audit, kAuditNamespace)) return false;
    ready_ = true;
    return true;
}

bool IrrigationRecordSync::appendWatering(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary,
    const char* relatedCommandId) {
    if (!ready_ || !canAppend(StreamKind::Watering)) return false;
    const bool appended =
        wateringStore_->appendCompleted(startTime, summary, relatedCommandId);
    if (appended) watering_.backlogFull = false;
    return appended;
}

bool IrrigationRecordSync::appendAudit(const IrrigationAuditPayload& payload) {
    if (!ready_ || !canAppend(StreamKind::Audit)) return false;
    const bool appended = auditStore_->appendInstant(payload);
    if (appended) audit_.backlogFull = false;
    return appended;
}

bool IrrigationRecordSync::appendAudit(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const IrrigationAuditPayload& payload) {
    if (!ready_ || !canAppend(StreamKind::Audit)) return false;
    const bool appended = auditStore_->appendCompleted(startTime, payload);
    if (appended) audit_.backlogFull = false;
    return appended;
}

bool IrrigationRecordSync::readOldestPending(StreamKind streamKind,
                                             PendingRecord& record) {
    record = {};
    if (!ready_) return false;
    Esp32BaseRecordStore::StoreStatus status;
    if (!store(streamKind).readStatus(status) || status.recordCount == 0U ||
        status.nextRecordId == 0U) return false;
    const StreamState& streamState = state(streamKind);
    const uint32_t newest = status.nextRecordId - 1U;
    if (streamState.acknowledged >= newest) return false;
    const uint32_t target =
        std::max(status.oldestRecordId, streamState.acknowledged + 1U);
    record.stream = streamKind;
    record.sequence = target;
    if (streamKind == StreamKind::Watering) {
        StoredWateringRecord stored;
        const auto result = wateringStore_->readById(target, stored);
        if (result != Esp32BaseRecordStore::RecordReadResult::Found) return false;
        record.timing = stored.timing;
        record.watering = stored.payload;
        return true;
    }
    StoredIrrigationAuditRecord stored;
    const auto result = auditStore_->readById(target, stored);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) return false;
    record.timing = stored.timing;
    record.audit = stored.payload;
    return true;
}

bool IrrigationRecordSync::acknowledge(
    const IrrigationIotProtocol::RecordAck& ack,
    uint32_t nowMs,
    StreamKind* acknowledgedStream) {
    if (!ready_) return false;
    StreamKind streamKind;
    if (std::strcmp(ack.recordStreamId, watering_.streamId) == 0)
        streamKind = StreamKind::Watering;
    else if (std::strcmp(ack.recordStreamId, audit_.streamId) == 0)
        streamKind = StreamKind::Audit;
    else return false;
    Esp32BaseRecordStore::StoreStatus status;
    if (!store(streamKind).readStatus(status) || status.nextRecordId == 0U ||
        ack.acknowledgedThroughSequence >= status.nextRecordId) return false;
    StreamState& streamState = state(streamKind);
    if (ack.acknowledgedThroughSequence > streamState.acknowledged) {
        streamState.acknowledged = ack.acknowledgedThroughSequence;
        streamState.checkpointDirty =
            streamState.acknowledged > streamState.persistedAcknowledged;
        if (streamState.acknowledged - streamState.persistedAcknowledged >=
            kCheckpointBatch) {
            if (!saveMetadata(streamKind)) {
                ready_ = false;
                return false;
            }
            streamState.lastCheckpointMs = nowMs;
        }
    }
    streamState.backlogFull = false;
    if (acknowledgedStream) *acknowledgedStream = streamKind;
    return true;
}

bool IrrigationRecordSync::resetGenerationsAfterFormat() {
    if (!registered_) return false;
    ready_ = false;
    return resetGeneration(StreamKind::Watering) &&
           resetGeneration(StreamKind::Audit) &&
           (ready_ = true);
}

void IrrigationRecordSync::handle(uint32_t nowMs) {
    if (!ready_) return;
    for (StreamKind streamKind : {StreamKind::Watering, StreamKind::Audit}) {
        StreamState& streamState = state(streamKind);
        if (streamState.checkpointDirty &&
            static_cast<uint32_t>(nowMs - streamState.lastCheckpointMs) >=
                kCheckpointIntervalMs) {
            if (!saveMetadata(streamKind)) {
                ready_ = false;
                return;
            }
            streamState.lastCheckpointMs = nowMs;
        }
    }
}

bool IrrigationRecordSync::ready() const { return ready_; }
bool IrrigationRecordSync::writable() const {
    return writable(StreamKind::Watering) && writable(StreamKind::Audit);
}
bool IrrigationRecordSync::writable(StreamKind streamKind) const {
    return ready_ && const_cast<IrrigationRecordSync*>(this)->canAppend(streamKind);
}
bool IrrigationRecordSync::backlogFull(StreamKind streamKind) const {
    return state(streamKind).backlogFull;
}
const char* IrrigationRecordSync::streamId(StreamKind streamKind) const {
    return state(streamKind).streamId;
}
uint32_t IrrigationRecordSync::acknowledgedThroughSequence(
    StreamKind streamKind) const {
    return state(streamKind).acknowledged;
}
uint32_t IrrigationRecordSync::pendingCount(StreamKind streamKind) const {
    if (!ready_) return 0U;
    Esp32BaseRecordStore::StoreStatus status;
    if (!store(streamKind).readStatus(status) || status.nextRecordId == 0U)
        return 0U;
    const uint32_t newest = status.nextRecordId - 1U;
    return newest > state(streamKind).acknowledged
               ? newest - state(streamKind).acknowledged
               : 0U;
}

IrrigationRecordSync::StreamState& IrrigationRecordSync::state(
    StreamKind streamKind) {
    return streamKind == StreamKind::Watering ? watering_ : audit_;
}
const IrrigationRecordSync::StreamState& IrrigationRecordSync::state(
    StreamKind streamKind) const {
    return streamKind == StreamKind::Watering ? watering_ : audit_;
}
Esp32BaseRecordStore& IrrigationRecordSync::store(StreamKind streamKind) {
    return streamKind == StreamKind::Watering ? wateringStore_->baseStore()
                                               : auditStore_->baseStore();
}
const Esp32BaseRecordStore& IrrigationRecordSync::store(
    StreamKind streamKind) const {
    return streamKind == StreamKind::Watering
               ? const_cast<WateringRecordStore*>(wateringStore_)->baseStore()
               : const_cast<IrrigationAuditStore*>(auditStore_)->baseStore();
}

bool IrrigationRecordSync::loadMetadata(StreamKind streamKind,
                                        const char* nvsNamespace) {
    StreamState& streamState = state(streamKind);
    if (!streamState.preferences.begin(nvsNamespace, false)) return false;
    Esp32BaseRecordStore::StoreStatus status;
    if (!store(streamKind).readStatus(status) || status.nextRecordId == 0U)
        return false;
    Metadata metadata;
    const std::size_t length = streamState.preferences.getBytesLength(kMetadataKey);
    if (length == 0U) {
        if (status.recordCount != 0U) return false;
        makeUuid(streamState.streamId, sizeof(streamState.streamId));
        streamState.acknowledged = 0U;
        streamState.persistedAcknowledged = 0U;
        streamState.metadataReady = true;
        return saveMetadata(streamKind);
    }
    if (length != sizeof(metadata) ||
        streamState.preferences.getBytes(kMetadataKey, &metadata,
                                         sizeof(metadata)) != sizeof(metadata) ||
        metadata.magic != kMetadataMagic ||
        metadata.version != kMetadataVersion ||
        metadata.crc32 != metadataCrc(metadata) ||
        !IrrigationIotProtocol::isValidUuid(metadata.streamId) ||
        metadata.acknowledged >= status.nextRecordId) return false;
    std::memcpy(streamState.streamId, metadata.streamId,
                sizeof(streamState.streamId));
    streamState.persistedAcknowledged = metadata.acknowledged;
    streamState.acknowledged = metadata.acknowledged;
    streamState.lastCheckpointMs = millis();
    streamState.checkpointDirty = false;
    streamState.metadataReady = true;
    return true;
}

bool IrrigationRecordSync::saveMetadata(StreamKind streamKind) {
    StreamState& streamState = state(streamKind);
    if (!streamState.metadataReady) return false;
    Metadata metadata;
    metadata.magic = kMetadataMagic;
    metadata.version = kMetadataVersion;
    std::memcpy(metadata.streamId, streamState.streamId,
                sizeof(metadata.streamId));
    metadata.acknowledged = streamState.acknowledged;
    metadata.crc32 = metadataCrc(metadata);
    if (streamState.preferences.putBytes(kMetadataKey, &metadata,
                                         sizeof(metadata)) != sizeof(metadata))
        return false;
    streamState.persistedAcknowledged = streamState.acknowledged;
    streamState.checkpointDirty = false;
    return true;
}

bool IrrigationRecordSync::canAppend(StreamKind streamKind) {
    if (!ready_ && !state(streamKind).metadataReady) return false;
    Esp32BaseRecordStore::StoreStatus status;
    if (!store(streamKind).readStatus(status) || !status.ready ||
        !status.writable || status.nextRecordId == 0U ||
        status.nextRecordId == UINT32_MAX) return false;
    StreamState& streamState = state(streamKind);
    if (status.recordCount < status.capacity) {
        streamState.backlogFull = false;
        return true;
    }
    const uint32_t newest = status.nextRecordId - 1U;
    streamState.backlogFull = streamState.acknowledged < newest;
    return !streamState.backlogFull;
}

bool IrrigationRecordSync::resetGeneration(StreamKind streamKind) {
    Esp32BaseRecordStore::StoreStatus status;
    StreamState& streamState = state(streamKind);
    if (!store(streamKind).readStatus(status) || !status.ready ||
        status.recordCount != 0U ||
        !streamState.preferences.remove(kMetadataKey)) return false;
    streamState.preferences.end();
    std::memset(streamState.streamId, 0, sizeof(streamState.streamId));
    streamState.persistedAcknowledged = 0U;
    streamState.acknowledged = 0U;
    streamState.lastCheckpointMs = 0U;
    streamState.checkpointDirty = false;
    streamState.backlogFull = false;
    streamState.metadataReady = false;
    return loadMetadata(streamKind,
                        streamKind == StreamKind::Watering
                            ? kWateringNamespace
                            : kAuditNamespace);
}

uint32_t IrrigationRecordSync::metadataCrc(const Metadata& metadata) {
    return crc32(reinterpret_cast<const uint8_t*>(&metadata),
                 offsetof(Metadata, crc32));
}
