#pragma once

#include <stdint.h>

namespace irrigation {

constexpr uint16_t kIrrigationRecordVersion = 1;
constexpr uint16_t kIrrigationRunRecordSize = 128;

enum class IrrigationRecordSource : uint8_t {
    Unknown = 0,
    Manual = 1,
    Schedule = 2,
    Maintenance = 3,
};

enum class IrrigationRecordStopReason : uint8_t {
    Unknown = 0,
    Completed = 1,
    UserStop = 2,
    NoWater = 3,
    HighFlow = 4,
    LowFlow = 5,
    LowLevel = 6,
    FlowMeterFault = 7,
};

enum IrrigationRecordWarningFlag : uint8_t {
    IRRIGATION_RECORD_WARNING_LOW_FLOW = 1U << 0,
    IRRIGATION_RECORD_WARNING_HIGH_FLOW = 1U << 1,
};

struct IrrigationRecordStoreMeta {
    uint16_t version;
    uint16_t recordSize;
    uint16_t recordCapacity;
    uint16_t recordHead;
    uint16_t recordCount;
    uint16_t reserved;
    uint32_t recordNextId;
};

#pragma pack(push, 1)
struct IrrigationRunRecord {
    uint32_t recordId;
    uint16_t version;
    uint8_t source;
    uint8_t stopReason;
    uint8_t zoneId;
    uint8_t planGroupId;
    uint8_t startTimeIndex;
    uint8_t warningFlags;
    uint32_t scheduledAt;
    uint32_t startedAt;
    uint32_t endedAt;
    uint16_t targetDurationMin;
    uint16_t reserved16;
    uint32_t actualDurationSec;
    uint32_t pulseCount;
    uint32_t estimatedMl;
    uint32_t avgFlowMlPerMin;
    uint32_t minFlowMlPerMin;
    uint32_t maxFlowMlPerMin;
    uint32_t lastFlowMlPerMin;
    uint16_t faultCode;
    uint16_t reservedFlags;
    char zoneName[16];
    char planName[16];
    uint8_t reserved[36];
};
#pragma pack(pop)

static_assert(sizeof(IrrigationRunRecord) == kIrrigationRunRecordSize, "IrrigationRunRecord must stay fixed-size");

inline IrrigationRunRecord makeEmptyRunRecord() {
    IrrigationRunRecord record = {};
    record.version = kIrrigationRecordVersion;
    record.source = static_cast<uint8_t>(IrrigationRecordSource::Unknown);
    record.stopReason = static_cast<uint8_t>(IrrigationRecordStopReason::Unknown);
    return record;
}

inline const char* irrigationRecordSourceKey(IrrigationRecordSource source) {
    switch (source) {
        case IrrigationRecordSource::Manual:
            return "manual";
        case IrrigationRecordSource::Schedule:
            return "schedule";
        case IrrigationRecordSource::Maintenance:
            return "maintenance";
        case IrrigationRecordSource::Unknown:
        default:
            return "unknown";
    }
}

inline const char* irrigationRecordStopReasonKey(IrrigationRecordStopReason reason) {
    switch (reason) {
        case IrrigationRecordStopReason::Completed:
            return "completed";
        case IrrigationRecordStopReason::UserStop:
            return "user_stop";
        case IrrigationRecordStopReason::NoWater:
            return "no_water";
        case IrrigationRecordStopReason::HighFlow:
            return "high_flow";
        case IrrigationRecordStopReason::LowFlow:
            return "low_flow";
        case IrrigationRecordStopReason::LowLevel:
            return "low_level";
        case IrrigationRecordStopReason::FlowMeterFault:
            return "flow_meter_fault";
        case IrrigationRecordStopReason::Unknown:
        default:
            return "unknown";
    }
}

inline IrrigationRecordStoreMeta makeEmptyRecordStoreMeta(uint16_t capacity) {
    IrrigationRecordStoreMeta meta = {};
    meta.version = kIrrigationRecordVersion;
    meta.recordSize = kIrrigationRunRecordSize;
    meta.recordCapacity = capacity;
    meta.recordNextId = 1;
    return meta;
}

inline uint16_t nextRecordAppendSlot(const IrrigationRecordStoreMeta& meta) {
    if (meta.recordCapacity == 0) {
        return 0;
    }
    if (meta.recordCount < meta.recordCapacity) {
        return static_cast<uint16_t>((meta.recordHead + meta.recordCount) % meta.recordCapacity);
    }
    return meta.recordHead;
}

inline void advanceRecordStoreMetaAfterAppend(IrrigationRecordStoreMeta& meta) {
    if (meta.recordCapacity == 0) {
        return;
    }
    if (meta.recordCount < meta.recordCapacity) {
        ++meta.recordCount;
    } else {
        meta.recordHead = static_cast<uint16_t>((meta.recordHead + 1U) % meta.recordCapacity);
    }
    ++meta.recordNextId;
}

}  // namespace irrigation
