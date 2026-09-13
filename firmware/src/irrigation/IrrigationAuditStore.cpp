#include "IrrigationAuditStore.h"
#include <runtime/Esp32BaseTime.h>

namespace {
constexpr uint32_t kMagic = 0x31545541UL;  // AUT1
constexpr uint8_t kVersion = 2;

void put32(uint8_t*& cursor, uint32_t value) {
    for (uint8_t shift = 0; shift < 32; shift += 8)
        *cursor++ = static_cast<uint8_t>(value >> shift);
}
uint32_t get32(const uint8_t*& cursor) {
    const uint32_t value = static_cast<uint32_t>(cursor[0]) |
                           static_cast<uint32_t>(cursor[1]) << 8U |
                           static_cast<uint32_t>(cursor[2]) << 16U |
                           static_cast<uint32_t>(cursor[3]) << 24U;
    cursor += 4;
    return value;
}
bool validKind(IrrigationAuditPayload::Kind kind) {
    return kind == IrrigationAuditPayload::Kind::PlanSkipped ||
           kind == IrrigationAuditPayload::Kind::AutomaticStateChanged ||
           kind == IrrigationAuditPayload::Kind::PlansChanged ||
           kind == IrrigationAuditPayload::Kind::ZoneBaselineSaved ||
           kind == IrrigationAuditPayload::Kind::ClosedFlowChanged;
}
}  // namespace

bool IrrigationAuditCodec::encode(const IrrigationAuditPayload& payload,
                                  uint8_t* output,
                                  std::size_t outputSize) {
    if (!output || outputSize != kPayloadSize || !validKind(payload.kind))
        return false;
    uint8_t* cursor = output;
    put32(cursor, kMagic);
    *cursor++ = kVersion;
    *cursor++ = static_cast<uint8_t>(payload.kind);
    *cursor++ = payload.reason;
    *cursor++ = payload.flags;
    *cursor++ = payload.objectId;
    *cursor++ = 0;
    *cursor++ = 0;
    *cursor++ = 0;
    put32(cursor, payload.value1);
    put32(cursor, payload.value2);
    return cursor == output + outputSize;
}

bool IrrigationAuditCodec::decode(const uint8_t* data,
                                  std::size_t dataSize,
                                  IrrigationAuditPayload& payload) {
    payload = {};
    if (!data || dataSize != kPayloadSize) return false;
    const uint8_t* cursor = data;
    if (get32(cursor) != kMagic || *cursor++ != kVersion) return false;
    payload.kind = static_cast<IrrigationAuditPayload::Kind>(*cursor++);
    payload.reason = *cursor++;
    payload.flags = *cursor++;
    payload.objectId = *cursor++;
    if (*cursor++ != 0U || *cursor++ != 0U || *cursor++ != 0U) return false;
    payload.value1 = get32(cursor);
    payload.value2 = get32(cursor);
    return cursor == data + dataSize && validKind(payload.kind);
}

bool IrrigationAuditStore::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kRecordTypeName;
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = kStoredBytes;
    definition.retentionPolicy = Esp32BaseRecordStore::RetentionPolicy::RotateOldest;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    return store_.begin(definition);
}

bool IrrigationAuditStore::appendInstant(const IrrigationAuditPayload& payload) {
    if (pending_) return false; // Never overwrite the first fact that failed to commit.
    const auto now = Esp32BaseTime::snapshot();
    pendingTiming_ = {};
    pendingTiming_.completedEpochSec = now.synced ? now.epochSec : 0;
    pendingTiming_.completedBootId = now.bootId;
    pendingTiming_.completedUptimeSec = now.uptimeSec;
    pendingPayload_ = payload;
    pending_ = true;
    return flushPending();
}

bool IrrigationAuditStore::flushPending() {
    if (!pending_) return true;
    const auto storageState = Esp32BaseStorage::state();
    if (storageState == Esp32BaseStorage::StorageState::OtaWriteSuspended ||
        storageState == Esp32BaseStorage::StorageState::Maintenance) return false;
    // Fault means the commit is uncertain: do not blindly append it again.
    if (!store_.isWritable() ||
        !appendFact(pendingTiming_, pendingPayload_)) return false;
    pending_ = false;
    return true;
}

bool IrrigationAuditStore::appendFact(
    const Esp32BaseRecordStore::RecordTiming& timing,
    const IrrigationAuditPayload& payload) {
    return IrrigationAuditCodec::encode(payload, scratch_, sizeof(scratch_)) &&
           store_.appendRecorded(timing, scratch_, sizeof(scratch_));
}

bool IrrigationAuditStore::readLatest(uint32_t offset,
                                      uint32_t limit,
                                      ReadCallback callback,
                                      void* user) {
    if (!callback || limit == 0U) return false;
    ReadContext context;
    context.callback = callback;
    context.user = user;
    return store_.readLatest(offset, limit, scratch_, sizeof(scratch_),
                             readAdapter, &context) && !context.failed;
}

Esp32BaseRecordStore::RecordReadResult IrrigationAuditStore::readById(
    uint32_t recordId,
    StoredIrrigationAuditRecord& record) {
    record = {};
    Esp32BaseRecordStore::RecordMetadata metadata;
    const auto result = store_.readById(recordId, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) return result;
    if (!decodeFact(scratch_, sizeof(scratch_), record))
        return Esp32BaseRecordStore::RecordReadResult::Corrupt;
    record.recordId = metadata.recordId;
    record.timing = metadata.timing;

    return result;
}

bool IrrigationAuditStore::readStatus(
    Esp32BaseRecordStore::StoreStatus& status) const {
    return store_.readStatus(status);
}
bool IrrigationAuditStore::isReady() const { return store_.isReady(); }
bool IrrigationAuditStore::isWritable() const { return store_.isWritable(); }
Esp32BaseRecordStore& IrrigationAuditStore::baseStore() { return store_; }

void IrrigationAuditStore::readAdapter(
    const Esp32BaseRecordStore::RecordView& view,
    void* user) {
    auto* context = static_cast<ReadContext*>(user);
    StoredIrrigationAuditRecord record;
    if (!context || !decodeFact(view.payload, view.payloadSizeBytes, record)) {
        if (context) context->failed = true;
        return;
    }
    record.recordId = view.recordId;
    record.timing = view.timing;

    context->callback(record, context->user);
}

bool IrrigationAuditStore::decodeFact(const uint8_t* bytes, std::size_t length, StoredIrrigationAuditRecord& record) {
    return IrrigationAuditCodec::decode(bytes, length, record.payload);
}
