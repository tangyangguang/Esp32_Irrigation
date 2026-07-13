#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

struct WateringRecordTime {
    bool synced;
    uint32_t epochSec;
    uint32_t bootId;
    uint32_t uptimeSec;
};

struct ZoneWateringRecord {
    uint8_t zoneId;
    ZoneWateringResult result;
    uint16_t flags;
    uint32_t plannedDurationSec;
    uint32_t actualWateringSec;
    uint32_t pulseCount;
    uint32_t estimatedWaterMl;
};

struct WateringRecord {
    uint32_t id;
    WateringRecordTime startTime;
    uint32_t elapsedSec;
    WateringSource source;
    uint8_t planId;
    WateringResult result;
    WateringStopReason stopReason;
    uint8_t zoneCount;
    std::array<ZoneWateringRecord, BoardPins::kZoneCount> zones;
};

class WateringRecordCodec {
public:
    static constexpr uint8_t kVersion = 1;
    static constexpr uint16_t kZoneFlagWaterEstimateCapped = 1U << 0U;
    static constexpr std::size_t kMaximumEncodedSize = 41U + 20U * BoardPins::kZoneCount;

    static bool fromSession(const WateringSessionSummary& summary,
                            uint32_t recordId,
                            const WateringRecordTime& startTime,
                            WateringRecord& record);
    static bool encode(const WateringRecord& record,
                       uint8_t* output,
                       std::size_t capacity,
                       std::size_t& encodedSize);
    static bool decode(const uint8_t* data, std::size_t size, WateringRecord& record);
};
