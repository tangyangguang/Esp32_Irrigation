#pragma once

#include <Esp32Base.h>

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
    static constexpr uint16_t kStoreVersion = 1;
    static constexpr uint32_t kMaximumStoreBytes = 512UL * 1024UL;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 96UL * 1024UL;

    using ReadCallback = void (*)(const StoredWateringRecord& record, void* user);

    bool begin();
    Esp32BaseRecordStore& baseStore();
    bool captureStartTime(Esp32BaseRecordStore::RecordStartTime& startTime) const;
    bool appendCompleted(const Esp32BaseRecordStore::RecordStartTime& startTime,
                         const WateringSessionSummary& summary);
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
        ReadCallback callback;
        void* user;
        bool decodeFailed;
    };

    static void readAdapter(const Esp32BaseRecordStore::RecordView& view, void* user);

    Esp32BaseRecordStore store_;
    uint8_t scratch_[WateringRecordCodec::kPayloadSize]{};
};
