#pragma once

#include <Esp32Base.h>

#include <cstddef>
#include <cstdint>

struct IrrigationAuditPayload {
    enum class Kind : uint8_t {
        AutomaticRun = 1,
        AutomaticStateChanged = 2,
        PlansChanged = 3,
        CalibrationSaved = 4,
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
    static constexpr uint16_t kStoreVersion = 1;
    static constexpr uint32_t kMaximumStoreBytes = 128UL * 1024UL;
    static constexpr uint32_t kMinimumFileSystemFreeBytes = 32UL * 1024UL;
    using ReadCallback = void (*)(const StoredIrrigationAuditRecord&, void*);

    bool begin();
    bool appendInstant(const IrrigationAuditPayload& payload);
    bool appendCompleted(
        const Esp32BaseRecordStore::RecordStartTime& startTime,
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

    Esp32BaseRecordStore store_;
    uint8_t scratch_[IrrigationAuditCodec::kPayloadSize]{};
};
