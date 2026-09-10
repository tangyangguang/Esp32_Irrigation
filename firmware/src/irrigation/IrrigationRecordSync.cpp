#include "IrrigationRecordSync.h"
IrrigationRecordSync& IrrigationRecordSync::instance() {
    static IrrigationRecordSync sync;
    return sync;
}
void IrrigationRecordSync::bind(WateringRecordStore& watering,IrrigationAuditStore& audit) {
    wateringStore_=&watering; auditStore_=&audit;
    streams_[0]=&watering.recordStream(); streams_[1]=&audit.recordStream();
}
bool IrrigationRecordSync::begin(WateringRecordStore& watering,IrrigationAuditStore& audit) {
    bind(watering,audit);
    // Register definitions even when loading one store failed: managed paths,
    // reserved capacity and explicit format/reload must remain available.
    // Do not short-circuit the other store's registration.
    registered_[0] = Esp32BaseStorage::registerRecordStore(watering.baseStore());
    registered_[1] = Esp32BaseStorage::registerRecordStore(audit.baseStore());
    return registered_[0] && registered_[1];
}
bool IrrigationRecordSync::appendWatering(const Esp32BaseRecordStore::RecordStartTime& start,
    const WateringSessionSummary& summary,const char* command) {
    return registered_[0] && wateringStore_->appendCompleted(start,summary,command);
}
bool IrrigationRecordSync::appendAudit(const IrrigationAuditPayload& payload) {
    return registered_[1] && auditStore_->appendInstant(payload);
}
bool IrrigationRecordSync::appendAudit(const Esp32BaseRecordStore::RecordTiming& timing,
                                      const IrrigationAuditPayload& payload) {
    return writable(StreamKind::Audit) && auditStore_->appendRecorded(timing,payload);
}
bool IrrigationRecordSync::resetGenerationsAfterFormat() {
    wateringStore_->discardPendingAfterFormat();
    auditStore_->discardPendingAfterFormat();
    // Early startup may have stopped at invalid configuration/FS, before these
    // stores were initialized. Base only reloads already registered objects.
    bool watering;
    if (registered_[0]) watering = streams_[0]->begin(millis());
    else {
        watering = wateringStore_->begin();
        registered_[0] = Esp32BaseStorage::registerRecordStore(wateringStore_->baseStore());
    }
    bool audit;
    if (registered_[1]) audit = streams_[1]->begin(millis());
    else {
        audit = auditStore_->begin();
        registered_[1] = Esp32BaseStorage::registerRecordStore(auditStore_->baseStore());
    }
    return watering && audit && registered_[0] && registered_[1];
}
void IrrigationRecordSync::handle(uint32_t nowMs) {
    for (uint8_t i = 0; i < 2; ++i)
        if (registered_[i]) streams_[i]->poll(nowMs,nullptr,nullptr);
    if (registered_[1] && auditStore_->hasPending() && uint32_t(nowMs-lastAuditRetryMs_) >= 1000U) {
        lastAuditRetryMs_=nowMs;
        auditStore_->flushPending();
    }
}
void IrrigationRecordSync::publish(uint32_t nowMs,iot_device::RecordStream::PublishFact publisher,void* context) {
    // One stream per turn; the SDK keeps its own head/retry and ACK state.
    const uint8_t index = nextStream_; nextStream_ ^= 1;
    if (registered_[index]) streams_[index]->poll(nowMs,publisher,context);
}
bool IrrigationRecordSync::ready(StreamKind kind) const {
    const uint8_t index = kind == StreamKind::Watering ? 0 : 1;
    return registered_[index] && streams_[index]->state() == iot_device::StreamState::Ready &&
        (index == 0 ? wateringStore_->isReady() : auditStore_->isReady());
}
bool IrrigationRecordSync::ready() const {
    return ready(StreamKind::Watering) && ready(StreamKind::Audit);
}
bool IrrigationRecordSync::writable() const { return writable(StreamKind::Watering) && writable(StreamKind::Audit); }
bool IrrigationRecordSync::writable(StreamKind kind) const {
    if (!ready(kind) ||
        (kind==StreamKind::Audit && auditStore_->hasPending())) return false;
    Esp32BaseRecordStore::StoreStatus status;
    const bool read=kind==StreamKind::Watering ? wateringStore_->readStatus(status) : auditStore_->readStatus(status);
    return read && status.writable && (status.recordCount<status.capacity ||
                                       status.releasedThroughRecordId>=status.newestRecordId);
}
bool IrrigationRecordSync::backlogFull(StreamKind kind) const {
    return stream(kind) && stream(kind)->error()==iot_device::StreamError::BacklogFull;
}
uint64_t IrrigationRecordSync::acknowledgedThroughSequence(StreamKind kind) const {
    return stream(kind) ? stream(kind)->acknowledgedSequence() : 0;
}
uint64_t IrrigationRecordSync::pendingCount(StreamKind kind) const {
    return stream(kind) ? stream(kind)->lastSequence()-stream(kind)->acknowledgedSequence() : 0;
}
