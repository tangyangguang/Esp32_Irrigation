#include "WateringRecordStore.h"

bool WateringRecordStore::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kRecordTypeName;
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = WateringRecordCodec::kPayloadSize;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    return store_.begin(definition);
}

bool WateringRecordStore::reload() {
    return store_.reload();
}

bool WateringRecordStore::captureStartTime(
    Esp32BaseRecordStore::RecordStartTime& startTime) const {
    return store_.captureStartTime(startTime);
}

bool WateringRecordStore::appendCompleted(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary) {
    WateringRecordPayload payload{};
    uint8_t encoded[WateringRecordCodec::kPayloadSize]{};
    return WateringRecordCodec::fromSession(summary, payload) &&
           WateringRecordCodec::encode(payload, encoded, sizeof(encoded)) &&
           store_.appendCompleted(startTime, encoded, sizeof(encoded));
}

bool WateringRecordStore::readLatest(uint32_t offset,
                                     uint32_t limit,
                                     ReadCallback callback,
                                     void* user) {
    if (!callback) {
        return false;
    }
    ReadContext context{callback, user, false};
    const bool read = store_.readLatest(offset,
                                        limit,
                                        scratch_,
                                        sizeof(scratch_),
                                        readAdapter,
                                        &context);
    return read && !context.decodeFailed;
}

Esp32BaseRecordStore::RecordReadResult WateringRecordStore::readById(
    uint32_t recordId,
    StoredWateringRecord& record) {
    record = {};
    Esp32BaseRecordStore::RecordMetadata metadata;
    const Esp32BaseRecordStore::RecordReadResult result =
        store_.readById(recordId, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) {
        return result;
    }
    if (!WateringRecordCodec::decode(scratch_, sizeof(scratch_), record.payload)) {
        return Esp32BaseRecordStore::RecordReadResult::Corrupt;
    }
    record.recordId = metadata.recordId;
    record.timing = metadata.timing;
    return Esp32BaseRecordStore::RecordReadResult::Found;
}

bool WateringRecordStore::clear(bool userConfirmed) {
    return userConfirmed && store_.clear();
}

bool WateringRecordStore::readStatus(Esp32BaseRecordStore::StoreStatus& status) const {
    return store_.readStatus(status);
}

bool WateringRecordStore::isReady() const {
    return store_.isReady();
}

bool WateringRecordStore::isWritable() const {
    return store_.isWritable();
}

Esp32BaseRecordStore::StoreState WateringRecordStore::state() const {
    return store_.state();
}

Esp32BaseRecordStore::StoreError WateringRecordStore::lastError() const {
    return store_.lastError();
}

const char* WateringRecordStore::lastErrorReason() const {
    return store_.lastErrorReason();
}

void WateringRecordStore::readAdapter(const Esp32BaseRecordStore::RecordView& view,
                                      void* user) {
    ReadContext* context = static_cast<ReadContext*>(user);
    StoredWateringRecord record{};
    if (!context || view.payloadSizeBytes != WateringRecordCodec::kPayloadSize ||
        !WateringRecordCodec::decode(view.payload, view.payloadSizeBytes, record.payload)) {
        if (context) {
            context->decodeFailed = true;
        }
        return;
    }
    record.recordId = view.recordId;
    record.timing = view.timing;
    context->callback(record, context->user);
}
