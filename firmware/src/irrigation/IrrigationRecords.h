#pragma once
#include "IrrigationAuditStore.h"
#include "WateringRecordStore.h"
class IrrigationRecords {
public:
    enum class StoreKind : uint8_t { Watering, Audit };
    static IrrigationRecords& instance();
    void bind(WateringRecordStore&, IrrigationAuditStore&);
    bool begin(WateringRecordStore&, IrrigationAuditStore&);
    bool reloadAfterFormat();
    void handle(uint32_t nowMs);
    bool writable(StoreKind) const;
    bool appendAudit(const IrrigationAuditPayload&);
private:
    WateringRecordStore* watering_ = nullptr;
    IrrigationAuditStore* audit_ = nullptr;
    bool registered_[2]{};
    uint32_t lastRetryMs_ = 0;
};
