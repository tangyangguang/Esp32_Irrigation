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
    if (!watering.isReady() || !audit.isReady()) return false;
    registered_=Esp32BaseStorage::registerRecordStore(watering.baseStore()) &&
                Esp32BaseStorage::registerRecordStore(audit.baseStore());
    return registered_;
}
bool IrrigationRecordSync::appendWatering(const Esp32BaseRecordStore::RecordStartTime& start,
    const WateringSessionSummary& summary,const char* command) {
    return registered_ && wateringStore_->appendCompleted(start,summary,command);
}
bool IrrigationRecordSync::appendAudit(const IrrigationAuditPayload& payload) {
    return registered_ && auditStore_->appendInstant(payload);
}
bool IrrigationRecordSync::appendAudit(const Esp32BaseRecordStore::RecordTiming& timing,
                                      const IrrigationAuditPayload& payload) {
    return writable(StreamKind::Audit) && auditStore_->appendRecorded(timing,payload);
}
bool IrrigationRecordSync::resetGenerationsAfterFormat() {
    if (!registered_) return false;
    wateringStore_->discardPendingAfterFormat();
    auditStore_->discardPendingAfterFormat();
    const bool watering=streams_[0]->begin(millis());
    const bool audit=streams_[1]->begin(millis());
    return watering && audit;
}
void IrrigationRecordSync::handle(uint32_t nowMs) {
    for (auto* stream : streams_) if (stream) stream->poll(nowMs,nullptr,nullptr);
    if (registered_ && auditStore_->hasPending() && uint32_t(nowMs-lastAuditRetryMs_) >= 1000U) {
        lastAuditRetryMs_=nowMs;
        auditStore_->flushPending();
    }
}
void IrrigationRecordSync::publish(uint32_t nowMs,iot_device::RecordStream::PublishFact publisher,void* context) {
    // One stream per turn; the SDK keeps its own head/retry and ACK state.
    auto* current=streams_[nextStream_]; nextStream_^=1;
    if (current) current->poll(nowMs,publisher,context);
}
bool IrrigationRecordSync::ready() const {
    return registered_ && streams_[0]->state()==iot_device::StreamState::Ready &&
                          streams_[1]->state()==iot_device::StreamState::Ready;
}
bool IrrigationRecordSync::writable() const { return writable(StreamKind::Watering) && writable(StreamKind::Audit); }
bool IrrigationRecordSync::writable(StreamKind kind) const {
    if (!registered_ || stream(kind)->state()!=iot_device::StreamState::Ready ||
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
