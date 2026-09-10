#pragma once

#include <Esp32Base.h>
#include <ports/Esp32RecordStorage.h>

#include <cstddef>
#include <cstdint>

#include "WateringRecordCodec.h"

struct StoredWateringRecord {
    uint32_t recordId;
    Esp32BaseRecordStore::RecordTiming timing;
    WateringRecordPayload payload;
};

class WateringRecordStore {
public:
    static constexpr const char* kRecordTypeName = "watering";
    static constexpr uint16_t kStoreVersion = 7;
    static constexpr uint32_t kMaximumStoreBytes = 384UL * 1024UL;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 32UL * 1024UL;

    using ReadCallback = void (*)(const StoredWateringRecord& record, void* user);

    bool begin();
    iot_device::RecordStream& recordStream() { return stream_; }
    static constexpr std::size_t kFactBytes = 4 + WateringRecordCodec::kPayloadSize;
    static constexpr std::size_t kStoredBytes = iot_device::RecordStream::HeaderBytes + kFactBytes;
    Esp32BaseRecordStore& baseStore();
    bool captureStartTime(Esp32BaseRecordStore::RecordStartTime& startTime) const;
    bool appendCompleted(const Esp32BaseRecordStore::RecordStartTime& startTime,
                         const WateringSessionSummary& summary,
                         const char* relatedCommandId = nullptr);
    Esp32BaseRecordStore::RecordTiming completionTiming() const;
    bool readLatest(uint32_t offset,
                    uint32_t limit,
                    ReadCallback callback,
                    void* user = nullptr);
    Esp32BaseRecordStore::RecordReadResult readById(uint32_t recordId,
                                                    StoredWateringRecord& record);
    bool readStatus(Esp32BaseRecordStore::StoreStatus& status) const;
    bool isReady() const;
    bool isWritable() const;
    Esp32BaseRecordStore::StoreState state() const;
    Esp32BaseRecordStore::StoreError lastError() const;
    const char* lastErrorReason() const;

private:
    struct ReadContext {
        ReadCallback callback = nullptr;
        void* user = nullptr;
        bool decodeFailed = false;
    };

    static void readAdapter(const Esp32BaseRecordStore::RecordView& view, void* user);

    Esp32BaseRecordStore store_;
    uint8_t pendingFact_[kFactBytes]{};
    uint64_t pendingObservedAt_=iot_device::RecordStream::UnknownTime;
    bool pending_=false;
    iot_device::Esp32RecordStorage sdkStorage_{store_};
    uint8_t scratch_[kStoredBytes]{};
    iot_device::RecordStream stream_{sdkStorage_, scratch_, sizeof(scratch_)};
    static bool decodeFact(const uint8_t*, std::size_t, StoredWateringRecord&);
};
