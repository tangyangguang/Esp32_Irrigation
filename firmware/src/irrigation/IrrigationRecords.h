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

}  // namespace irrigation
