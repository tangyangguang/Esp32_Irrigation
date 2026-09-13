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
    static constexpr uint16_t kStoreVersion = 8;
    static constexpr uint32_t kMaximumStoreBytes = 160UL * 1024UL;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 32UL * 1024UL;

    using ReadCallback = void (*)(const StoredWateringRecord& record, void* user);

    bool begin();
    bool prepareTask(const WateringRequest& request);
    bool cancelPreparedTask();
    uint32_t taskStartedEpoch() const { return startedEpoch_; }
    bool taskReady() const { return taskReady_; }
    bool resetTaskAfterFormat() { pending_ = false; return cancelPreparedTask(); }
    static constexpr std::size_t kStoredBytes = WateringRecordCodec::kPayloadSize;
    Esp32BaseRecordStore& baseStore();
    bool captureStartTime(Esp32BaseRecordStore::RecordStartTime& startTime) const;
    bool appendCompleted(const Esp32BaseRecordStore::RecordStartTime& startTime,
                         const WateringSessionSummary& summary);
    bool appendPayload(const WateringRecordPayload& payload);
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
    uint8_t scratch_[kStoredBytes]{};
    bool pending_ = false;
    bool taskReady_ = false;
    uint32_t taskId_ = 0;
    uint32_t startedEpoch_ = 0;
    WateringRecordPayload pendingPayload_{};
    Esp32BaseRecordStore::RecordTiming pendingTiming_{};
    bool recoverTask();
    bool writeTaskMarker(const WateringRecordPayload* payload);

    static bool decodeFact(const uint8_t*, std::size_t, StoredWateringRecord&);
};
