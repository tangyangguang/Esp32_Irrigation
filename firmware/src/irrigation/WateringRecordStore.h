#pragma once

#include <Esp32Base.h>
#include <RecordStream.h>
#include <ports/Esp32RecordStorage.h>

#include <cstddef>
#include <cstdint>

#include "WateringRecordCodec.h"
#include "IrrigationPlatform.h"

struct StoredWateringRecord {
    uint32_t recordId;
    Esp32BaseRecordStore::RecordTiming timing;
    WateringRecordPayload payload;
};

// Compact task intent persisted to NVS before outputs start, used only to
// rebuild an Incomplete/RebootInterrupted fact after an unexpected reboot.
#pragma pack(push, 1)
struct WateringTaskMarker {
    char magic[2] = {'I', 'T'};
    uint8_t version = 2;
    uint8_t active = 0;
    uint8_t generation[16] = {};
    uint32_t taskId = 0;
    uint32_t startedEpoch = 0;
    uint8_t source = 0;
    uint8_t targetMode = 0;
    uint8_t planId = 0;
    uint8_t stepCount = 0;
    char commandId[kCommandIdTextLength] = {};
    struct Step {
        uint8_t zoneId;
        uint8_t reserved;
        uint32_t targetDurationSec;
        uint32_t targetWaterMl;
    } steps[BoardPins::kZoneCount] = {};
};
#pragma pack(pop)

class WateringRecordStore {
public:
    static constexpr const char* kRecordTypeName = "watering";
    static constexpr uint16_t kStoreVersion = 9;
    static constexpr uint32_t kMaximumStoreBytes = 160UL * 1024UL;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 32UL * 1024UL;

    using ReadCallback = void (*)(const StoredWateringRecord&, void*);

    bool begin();
    void setConnectionReady(bool ready) { stream_.setConnectionReady(ready); }
    iot_device::RecordStream& recordStream() { return stream_; }
    void poll(iot_device::RecordStream::PublishFact publish, void* user) {
        stream_.poll(millis(), publish, user);
    }

    bool prepareTask(const WateringRequest& request);
    bool cancelPreparedTask();
    uint32_t taskStartedEpoch() const { return startedEpoch_; }
    bool taskReady() const { return taskReady_; }
    bool resetTaskAfterFormat() { pending_ = false; return cancelPreparedTask(); }

    // 4 duration bytes + fixed business payload.
    static constexpr std::size_t kFactBytes =
        4 + WateringRecordCodec::kPayloadSize;
    static constexpr std::size_t kStoredBytes =
        iot_device::RecordStream::HeaderBytes + kFactBytes;

    Esp32BaseRecordStore& baseStore() { return store_; }
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
    iot_device::StreamState streamState() const {
        return stream_.state();
    }
    bool backlogFull() const {
        return stream_.error() == iot_device::StreamError::BacklogFull;
    }
    Esp32BaseRecordStore::StoreState state() const;
    Esp32BaseRecordStore::StoreError lastError() const;
    const char* lastErrorReason() const;

private:
    struct ReadContext {
        ReadCallback callback = nullptr;
        void* user = nullptr;
        bool decodeFailed = false;
    };

    static void readAdapter(const Esp32BaseRecordStore::RecordView& view,
                            void* user);

    Esp32BaseRecordStore store_;
    iot_device::Esp32RecordStorage sdkStorage_{store_};
    uint8_t scratch_[kStoredBytes]{};
    iot_device::RecordStream stream_{sdkStorage_, scratch_,
                                     sizeof(scratch_)};
    uint8_t pendingFact_[kFactBytes]{};
    uint64_t pendingObservedAt_ = iot_device::RecordStream::UnknownTime;
    uint16_t pendingType_ = IrrigationPlatform::FactWateringCompleted;
    bool pending_ = false;
    bool taskReady_ = false;
    uint32_t startedEpoch_ = 0;

    bool recoverTask();
    bool writeTaskMarker(const WateringTaskMarker& marker);
    static bool decodeFact(const uint8_t*, std::size_t, StoredWateringRecord&);
};
