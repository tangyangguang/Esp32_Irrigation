#pragma once
#include <Esp32Base.h>
#include "IrrigationAuditStore.h"
#include "WateringRecordStore.h"

class IrrigationRecordSync {
public:
    enum class StreamKind : uint8_t { Watering, Audit };
    struct PendingRecord {
        StreamKind stream = StreamKind::Watering;
        uint64_t sequence = 0;
        Esp32BaseRecordStore::RecordTiming timing{};
        WateringRecordPayload watering{};
        IrrigationAuditPayload audit{};
    };
    static IrrigationRecordSync& instance();
    void bind(WateringRecordStore&, IrrigationAuditStore&);
    bool begin(WateringRecordStore&, IrrigationAuditStore&);
    bool appendWatering(const Esp32BaseRecordStore::RecordStartTime&,
                        const WateringSessionSummary&,const char* relatedCommandId);
    bool appendAudit(const IrrigationAuditPayload&);
    bool appendAudit(const Esp32BaseRecordStore::RecordTiming&,const IrrigationAuditPayload&);
    bool resetGenerationsAfterFormat();
    void handle(uint32_t nowMs);
    void publish(uint32_t nowMs, iot_device::RecordStream::PublishFact, void*);
    iot_device::RecordStream* const* streams() { return streams_; }
    bool ready() const;
    bool writable() const;
    bool writable(StreamKind) const;
    bool backlogFull(StreamKind) const;
    uint64_t acknowledgedThroughSequence(StreamKind) const;
    uint64_t pendingCount(StreamKind) const;
private:
    iot_device::RecordStream* stream(StreamKind kind) const { return streams_[kind==StreamKind::Watering ? 0 : 1]; }
    WateringRecordStore* wateringStore_=nullptr;
    IrrigationAuditStore* auditStore_=nullptr;
    iot_device::RecordStream* streams_[2]{};
    bool registered_=false;
    uint8_t nextStream_=0;
};
