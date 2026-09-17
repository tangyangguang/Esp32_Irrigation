#include "IrrigationAuditStore.h"

#include <cstring>

#include <runtime/Esp32BaseTime.h>

#include "IrrigationEvents.h"
#include "IrrigationPlatform.h"
#include "IrrigationRecordStoreRecovery.h"
#include "IrrigationStoredFact.h"

// ---- store -----------------------------------------------------------------

bool IrrigationAuditStore::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kRecordTypeName;
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = kStoredBytes;
    definition.retentionPolicy =
        Esp32BaseRecordStore::RetentionPolicy::PreserveUnreleased;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    pending_ = false;
    if (!store_.begin(definition) &&
        !IrrigationRecordStoreRecovery::resetStructuralStore(
            store_, kRecordTypeName, kStoreVersion, definition)) {
        return false;
    }
    if (!stream_.begin(millis())) return false;
    stream_.poll(millis(), nullptr, nullptr);
    return true;
}

Esp32BaseRecordStore& IrrigationAuditStore::baseStore() { return store_; }

uint16_t IrrigationAuditStore::factTypeCode(
    const IrrigationAuditPayload& payload) {
    using Kind = IrrigationAuditPayload::Kind;
    using Reason = IrrigationEvents::ReasonCode;
    switch (payload.kind) {
        case Kind::PlanSkipped:
            return IrrigationPlatform::FactAutomaticRunCompleted;
        case Kind::AutomaticStateChanged: {
            const auto reason =
                static_cast<Reason>(payload.reason);
            return (reason == Reason::PausedIndefinitely ||
                    reason == Reason::PausedUntil)
                       ? IrrigationPlatform::FactAutomaticPaused
                       : IrrigationPlatform::FactAutomaticResumed;
        }
        case Kind::PlansChanged:
            return IrrigationPlatform::FactPlansChanged;
        case Kind::ZoneBaselineSaved:
            return IrrigationPlatform::FactZoneBaselineSaved;
        case Kind::ZoneChanged:
            return IrrigationPlatform::FactZoneChanged;
        case Kind::SystemFieldChanged:
            return IrrigationPlatform::FactSystemFieldChanged;
        case Kind::ClosedFlowChanged:
        default:
            // Closed-flow changes drive only a local condition; they are not
            // part of the platform audit record set.
            return 0;
    }
}

bool IrrigationAuditStore::appendFact(
    const Esp32BaseRecordStore::RecordTiming& timing,
    const IrrigationAuditPayload& payload) {
    const uint16_t typeCode = factTypeCode(payload);
    if (!typeCode ||
        stream_.state() != iot_device::StreamState::Ready)
        return false;
    uint8_t fact[kFactBytes]{};
    if (!IrrigationAuditCodec::encode(payload, fact, sizeof(fact))) return false;
    const uint64_t observedAtMs =
        timing.completedEpochSec ? uint64_t(timing.completedEpochSec) * 1000ULL
                                 : iot_device::RecordStream::UnknownTime;
    return stream_.append(typeCode, observedAtMs, fact, sizeof(fact));
}

bool IrrigationAuditStore::appendInstant(const IrrigationAuditPayload& payload) {
    if (stream_.state() != iot_device::StreamState::Ready) return false;
    const auto now = Esp32BaseTime::snapshot();
    Esp32BaseRecordStore::RecordTiming timing{};
    timing.completedEpochSec = now.synced ? now.epochSec : 0;
    timing.completedBootId = now.bootId;
    timing.completedUptimeSec = now.uptimeSec;
    return appendFact(timing, payload);
}

bool IrrigationAuditStore::flushPending() {
    if (!pending_) return true;
    if (!appendFact(pendingTiming_, pendingPayload_)) return false;
    pending_ = false;
    return true;
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
                             readAdapter, &context) &&
           !context.failed;
}

Esp32BaseRecordStore::RecordReadResult IrrigationAuditStore::readById(
    uint32_t recordId,
    StoredIrrigationAuditRecord& record) {
    record = {};
    Esp32BaseRecordStore::RecordMetadata metadata;
    const Esp32BaseRecordStore::RecordReadResult result =
        store_.readById(recordId, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) return result;
    if (!decodeFact(scratch_, sizeof(scratch_), record))
        return Esp32BaseRecordStore::RecordReadResult::Corrupt;
    record.recordId = metadata.recordId;
    record.timing = metadata.timing;
    return Esp32BaseRecordStore::RecordReadResult::Found;
}

bool IrrigationAuditStore::readStatus(
    Esp32BaseRecordStore::StoreStatus& status) const {
    return store_.readStatus(status);
}

bool IrrigationAuditStore::isReady() const { return store_.isReady(); }
bool IrrigationAuditStore::isWritable() const {
    return store_.isWritable() &&
           stream_.state() == iot_device::StreamState::Ready;
}

void IrrigationAuditStore::readAdapter(
    const Esp32BaseRecordStore::RecordView& view,
    void* user) {
    ReadContext* context = static_cast<ReadContext*>(user);
    StoredIrrigationAuditRecord record{};
    if (!context || !decodeFact(view.payload, view.payloadSizeBytes, record)) {
        if (context) context->failed = true;
        return;
    }
    record.recordId = view.recordId;
    record.timing = view.timing;
    context->callback(record, context->user);
}

bool IrrigationAuditStore::decodeFact(const uint8_t* bytes,
                                      std::size_t length,
                                      StoredIrrigationAuditRecord& record) {
    iot_device::RecordFactView fact{};
    if (!iot_device::linearRecordEncoding().decode(bytes, length, fact) ||
        fact.dataBytes != kFactBytes || !fact.sequence ||
        fact.sequence > iot_device::RecordStream::MaxSequence)
        return false;
    record.timing = {};
    if (fact.observedAtMs != iot_device::RecordStream::UnknownTime) {
        if (fact.observedAtMs % 1000 ||
            fact.observedAtMs / 1000 > UINT32_MAX)
            return false;
        record.timing.completedEpochSec = uint32_t(fact.observedAtMs / 1000);
    }
    return IrrigationAuditCodec::decode(fact.data, fact.dataBytes,
                                        record.payload);
}
