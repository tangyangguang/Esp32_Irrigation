#pragma once

#include <Esp32Base.h>
#include <ports/Esp32RecordStorage.h>

#include <cstddef>
#include <cstdint>

struct IrrigationAuditPayload {
    enum class Kind : uint8_t {
        AutomaticRun = 1,
        AutomaticStateChanged = 2,
        PlansChanged = 3,
        ZoneBaselineSaved = 5,
    };

    Kind kind = Kind::AutomaticRun;
    uint8_t reason = 0;
    uint8_t flags = 0;
    uint8_t objectId = 0;
    uint32_t value1 = 0;
    uint32_t value2 = 0;
    uint32_t value3 = 0;
};

struct StoredIrrigationAuditRecord {
    uint32_t recordId = 0;
    Esp32BaseRecordStore::RecordTiming timing{};
    IrrigationAuditPayload payload{};
};

class IrrigationAuditCodec {
public:
    static constexpr std::size_t kPayloadSize = 24;
    static bool encode(const IrrigationAuditPayload& payload,
                       uint8_t* output,
                       std::size_t outputSize);
    static bool decode(const uint8_t* data,
                       std::size_t dataSize,
                       IrrigationAuditPayload& payload);
};

class IrrigationAuditStore {
public:
    static constexpr const char* kRecordTypeName = "irrigation-audit";
    static constexpr uint16_t kStoreVersion = 2;
    static constexpr uint32_t kMaximumStoreBytes = 48UL * 1024UL;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 32UL * 1024UL;
    using ReadCallback = void (*)(const StoredIrrigationAuditRecord&, void*);

    bool begin();
    iot_device::RecordStream& recordStream() { return stream_; }
    static constexpr std::size_t kFactBytes = 4 + IrrigationAuditCodec::kPayloadSize;
    static constexpr std::size_t kStoredBytes = iot_device::RecordStream::HeaderBytes + kFactBytes;
    bool appendInstant(const IrrigationAuditPayload& payload);
    bool hasPending() const { return pending_; }
    bool flushPending();
    void discardPendingAfterFormat() { pending_ = false; }
    bool appendRecorded(
        const Esp32BaseRecordStore::RecordTiming& timing,
        const IrrigationAuditPayload& payload);
    bool readLatest(uint32_t offset, uint32_t limit,
                    ReadCallback callback, void* user = nullptr);
    Esp32BaseRecordStore::RecordReadResult readById(
        uint32_t recordId, StoredIrrigationAuditRecord& record);
    bool readStatus(Esp32BaseRecordStore::StoreStatus& status) const;
    bool isReady() const;
    bool isWritable() const;
    Esp32BaseRecordStore& baseStore();

private:
    struct ReadContext {
        ReadCallback callback = nullptr;
        void* user = nullptr;
        bool failed = false;
    };
    static void readAdapter(const Esp32BaseRecordStore::RecordView&, void*);

    bool appendFact(const Esp32BaseRecordStore::RecordTiming&, const IrrigationAuditPayload&);
    bool pending_ = false;
    Esp32BaseRecordStore::RecordTiming pendingTiming_{};
    IrrigationAuditPayload pendingPayload_{};
    Esp32BaseRecordStore store_;
    iot_device::Esp32RecordStorage sdkStorage_{store_};
    uint8_t scratch_[kStoredBytes]{};
    iot_device::RecordStream stream_{sdkStorage_, scratch_, sizeof(scratch_)};
    static bool decodeFact(const uint8_t*, std::size_t, StoredIrrigationAuditRecord&);
};
