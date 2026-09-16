#pragma once

#include <Esp32Base.h>
#include <RecordStream.h>
#include <ports/Esp32RecordStorage.h>

#include "IrrigationAuditPayload.h"

struct StoredIrrigationAuditRecord {
    uint32_t recordId = 0;
    Esp32BaseRecordStore::RecordTiming timing{};
    IrrigationAuditPayload payload{};
};

class IrrigationAuditStore {
public:
    static constexpr const char* kRecordTypeName = "irrigation-audit";
    static constexpr uint16_t kStoreVersion = 5;
    static constexpr uint32_t kMaximumStoreBytes = 48UL * 1024UL;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 32UL * 1024UL;
    using ReadCallback = void (*)(const StoredIrrigationAuditRecord&, void*);

    bool begin();
    static constexpr std::size_t kFactBytes = IrrigationAuditCodec::kPayloadSize;
    static constexpr std::size_t kStoredBytes =
        iot_device::RecordStream::HeaderBytes + kFactBytes;

    void setConnectionReady(bool ready) { stream_.setConnectionReady(ready); }
    iot_device::RecordStream& recordStream() { return stream_; }
    void poll(iot_device::RecordStream::PublishFact publish, void* user) {
        stream_.poll(millis(), publish, user);
    }

    bool appendInstant(const IrrigationAuditPayload& payload);
    bool hasPending() const { return pending_; }
    bool flushPending();
    void discardPendingAfterFormat() { pending_ = false; }

    bool readLatest(uint32_t offset, uint32_t limit,
                    ReadCallback callback, void* user = nullptr);
    Esp32BaseRecordStore::RecordReadResult readById(
        uint32_t recordId, StoredIrrigationAuditRecord& record);
    bool readStatus(Esp32BaseRecordStore::StoreStatus& status) const;
    bool isReady() const;
    bool isWritable() const;
    bool backlogFull() const {
        return stream_.error() == iot_device::StreamError::BacklogFull;
    }
    Esp32BaseRecordStore& baseStore();

private:
    struct ReadContext {
        ReadCallback callback = nullptr;
        void* user = nullptr;
        bool failed = false;
    };

    static void readAdapter(const Esp32BaseRecordStore::RecordView&, void*);

    bool appendFact(const Esp32BaseRecordStore::RecordTiming&,
                    const IrrigationAuditPayload&);
    bool pending_ = false;
    Esp32BaseRecordStore::RecordTiming pendingTiming_{};
    IrrigationAuditPayload pendingPayload_{};

    Esp32BaseRecordStore store_;
    iot_device::Esp32RecordStorage sdkStorage_{store_};
    uint8_t scratch_[kStoredBytes]{};
    iot_device::RecordStream stream_{sdkStorage_, scratch_,
                                     sizeof(scratch_)};
    static bool decodeFact(const uint8_t*, std::size_t,
                           StoredIrrigationAuditRecord&);
    static uint16_t factTypeCode(const IrrigationAuditPayload&);
};
