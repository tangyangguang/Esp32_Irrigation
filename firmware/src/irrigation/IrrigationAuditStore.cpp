#include "IrrigationAuditStore.h"
#include "IrrigationStoredFact.h"
#include <runtime/Esp32BaseTime.h>

namespace {
constexpr uint32_t kMagic = 0x31545541UL;  // AUT1
constexpr uint8_t kVersion = 1;

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
    return kind >= IrrigationAuditPayload::Kind::AutomaticRun &&
           kind <= IrrigationAuditPayload::Kind::ZoneBaselineSaved;
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
    put32(cursor, payload.value3);
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
    payload.value3 = get32(cursor);
    return cursor == data + dataSize && validKind(payload.kind);
}

bool IrrigationAuditStore::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kRecordTypeName;
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = kStoredBytes;
    definition.retentionPolicy = Esp32BaseRecordStore::RetentionPolicy::PreserveUnreleased;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    if (!store_.begin(definition) || !stream_.begin(millis())) return false;
    stream_.poll(millis(), nullptr, nullptr); // Empty stores become ready in one bounded step.
    return true;
}

bool IrrigationAuditStore::appendInstant(const IrrigationAuditPayload& payload) {
    const auto now=Esp32BaseTime::snapshot();
    Esp32BaseRecordStore::RecordTiming timing{};
    timing.completedEpochSec=now.synced ? now.epochSec : 0;
    return appendRecorded(timing,payload);
}

bool IrrigationAuditStore::appendRecorded(
    const Esp32BaseRecordStore::RecordTiming& timing,
    const IrrigationAuditPayload& payload) {
    uint8_t fact[kFactBytes]{};
    irrigation_fact::putDuration(fact,timing.durationSec);
    return IrrigationAuditCodec::encode(payload,fact+4,IrrigationAuditCodec::kPayloadSize) &&
           stream_.append(2,timing.completedEpochSec ? uint64_t(timing.completedEpochSec)*1000 : iot_device::RecordStream::UnknownTime,
                          fact,sizeof(fact));
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

    context->callback(record, context->user);
}

bool IrrigationAuditStore::decodeFact(const uint8_t* bytes, std::size_t length, StoredIrrigationAuditRecord& record) {
    iot_device::RecordFactView fact{};
    return irrigation_fact::decode(bytes, length, kFactBytes, record.timing, fact) && fact.typeCode == 2 &&
           IrrigationAuditCodec::decode(fact.data + 4, fact.dataBytes - 4, record.payload);
}
