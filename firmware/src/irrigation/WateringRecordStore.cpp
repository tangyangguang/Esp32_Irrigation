#include "WateringRecordStore.h"
#include "IrrigationStoredFact.h"
#include <runtime/Esp32BaseTime.h>

bool WateringRecordStore::begin() {
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

Esp32BaseRecordStore& WateringRecordStore::baseStore() { return store_; }

bool WateringRecordStore::captureStartTime(
    Esp32BaseRecordStore::RecordStartTime& startTime) const {
    return store_.captureStartTime(startTime);
}

bool WateringRecordStore::appendCompleted(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary,
    const char* relatedCommandId) {
    // One producer owns the controller's finished summary until this append commits.
    // Freeze completion time, duration and correlation before a safe full/busy retry.
    if (!pending_) {
        const auto now=Esp32BaseTime::snapshot();
        if (!startTime.bootId || startTime.bootId!=now.bootId || startTime.uptimeSec>now.uptimeSec) return false;
        WateringRecordPayload payload{};
        if (!WateringRecordCodec::fromSession(summary,relatedCommandId,payload) ||
            !WateringRecordCodec::encode(payload,pendingFact_+4,WateringRecordCodec::kPayloadSize)) return false;
        irrigation_fact::putDuration(pendingFact_,now.uptimeSec-startTime.uptimeSec);
        pendingObservedAt_=now.synced ? uint64_t(now.epochSec)*1000 : iot_device::RecordStream::UnknownTime;
        pending_=true;
    }
    if (!stream_.append(1,pendingObservedAt_,pendingFact_,sizeof(pendingFact_))) return false;
    pending_=false;
    return true;
}

bool WateringRecordStore::readLatest(uint32_t offset,
                                     uint32_t limit,
                                     ReadCallback callback,
                                     void* user) {
    if (!callback || limit == 0U) return false;
    ReadContext context;
    context.callback = callback;
    context.user = user;
    const bool read = store_.readLatest(offset, limit, scratch_, sizeof(scratch_),
                                        readAdapter, &context);
    return read && !context.decodeFailed;
}

Esp32BaseRecordStore::RecordReadResult WateringRecordStore::readById(
    uint32_t recordId,
    StoredWateringRecord& record) {
    record = {};
    Esp32BaseRecordStore::RecordMetadata metadata;
    const Esp32BaseRecordStore::RecordReadResult result =
        store_.readById(recordId, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) return result;
    if (!decodeFact(scratch_, sizeof(scratch_), record))
        return Esp32BaseRecordStore::RecordReadResult::Corrupt;
    record.recordId = metadata.recordId;

    return Esp32BaseRecordStore::RecordReadResult::Found;
}

bool WateringRecordStore::readStatus(
    Esp32BaseRecordStore::StoreStatus& status) const {
    return store_.readStatus(status);
}

bool WateringRecordStore::isReady() const { return store_.isReady(); }
bool WateringRecordStore::isWritable() const { return store_.isWritable(); }
Esp32BaseRecordStore::StoreState WateringRecordStore::state() const {
    return store_.state();
}
Esp32BaseRecordStore::StoreError WateringRecordStore::lastError() const {
    return store_.lastError();
}
const char* WateringRecordStore::lastErrorReason() const {
    return store_.lastErrorReason();
}

void WateringRecordStore::readAdapter(
    const Esp32BaseRecordStore::RecordView& view,
    void* user) {
    ReadContext* context = static_cast<ReadContext*>(user);
    StoredWateringRecord record{};
    if (!context || !decodeFact(view.payload, view.payloadSizeBytes, record)) {
        if (context) context->decodeFailed = true;
        return;
    }
    record.recordId = view.recordId;

    context->callback(record, context->user);
}

bool WateringRecordStore::decodeFact(const uint8_t* bytes, std::size_t length, StoredWateringRecord& record) {
    iot_device::RecordFactView fact{};
    return irrigation_fact::decode(bytes, length, kFactBytes, record.timing, fact) && fact.typeCode == 1 &&
           WateringRecordCodec::decode(fact.data + 4, fact.dataBytes - 4, record.payload);
}

Esp32BaseRecordStore::RecordTiming WateringRecordStore::completionTiming() const {
    Esp32BaseRecordStore::RecordTiming timing{};
    if (pendingObservedAt_!=iot_device::RecordStream::UnknownTime) timing.completedEpochSec=uint32_t(pendingObservedAt_/1000);
    for (unsigned n=0;n<4;++n) timing.durationSec|=uint32_t(pendingFact_[n])<<(8*n);
    return timing;
}
