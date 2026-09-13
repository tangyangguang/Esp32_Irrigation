#include "IrrigationRecords.h"
IrrigationRecords& IrrigationRecords::instance() { static IrrigationRecords records; return records; }
void IrrigationRecords::bind(WateringRecordStore& watering, IrrigationAuditStore& audit) { watering_ = &watering; audit_ = &audit; }
bool IrrigationRecords::begin(WateringRecordStore& watering, IrrigationAuditStore& audit) {
    watering_ = &watering; audit_ = &audit;
    registered_[0] = Esp32BaseStorage::registerRecordStore(watering.baseStore());
    registered_[1] = Esp32BaseStorage::registerRecordStore(audit.baseStore());
    return registered_[0] && registered_[1];
}
bool IrrigationRecords::reloadAfterFormat() {
    if (!watering_ || !audit_) return false;
    audit_->discardPendingAfterFormat();
    // Base already reloaded registered objects. A startup failure may have left
    // the wrappers uninitialized; only those require first-time begin here.
    const bool w = registered_[0] ? watering_->resetTaskAfterFormat() : watering_->begin();
    const bool a = registered_[1] ? audit_->isWritable() : audit_->begin();
    return begin(*watering_, *audit_) && w && a;
}
void IrrigationRecords::handle(uint32_t nowMs) {
    if (audit_ && audit_->hasPending() && uint32_t(nowMs - lastRetryMs_) >= 1000) {
        lastRetryMs_ = nowMs; audit_->flushPending();
    }
}
bool IrrigationRecords::writable(StoreKind kind) const {
    return kind == StoreKind::Watering
        ? registered_[0] && watering_ && watering_->isWritable()
        : registered_[1] && audit_ && audit_->isWritable() && !audit_->hasPending();
}
bool IrrigationRecords::appendAudit(const IrrigationAuditPayload& payload) {
    return registered_[1] && audit_ && audit_->appendInstant(payload);
}
