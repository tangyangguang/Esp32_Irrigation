#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

struct ZoneWateringRecord {
    ZoneWateringResult result = ZoneWateringResult::NotStarted;
    uint8_t flags = 0;
    uint32_t startedOffsetSec = 0;
    uint16_t plannedDurationSec = 0;
    uint16_t actualWateringSec = 0;
    uint32_t targetWaterMl = 0;
    uint32_t pulseCount = 0;
    uint32_t estimatedWaterMl = 0;
    uint32_t averageFlowMlPerMinute = 0;
    uint32_t baselinePulseRateX10000 = 0;
    uint32_t baselineFlowMlPerMinute = 0;
};

struct WateringRecordPayload {
    WateringSource source = WateringSource::LocalWeb;
    WateringTargetMode targetMode = WateringTargetMode::Duration;
    uint8_t planId = 0;
    WateringResult result = WateringResult::Failed;
    WateringStopReason stopReason = WateringStopReason::None;
    uint32_t taskId = 0;
    uint32_t startedEpoch = 0;
    // Platform command UUID text for WeChat-started watering; all zero otherwise.
    std::array<char, kCommandIdTextLength> commandId{};
    std::array<ZoneWateringRecord, BoardPins::kZoneCount> zones{};
};

struct WateringRecordTotals {
    uint32_t plannedDurationSec = 0;
    uint32_t actualWateringSec = 0;
    uint64_t pulseCount = 0;
    uint64_t estimatedWaterMl = 0;
    uint32_t averageFlowMlPerMinute = 0;
};

class WateringRecordCodec {
public:
    static constexpr std::size_t kPayloadSize = 246;
    static constexpr uint8_t kZoneFlagUnknown = 1U << 4U;
    static constexpr uint8_t kZoneFlagWaterEstimateCapped = 1U << 0U;
    static constexpr uint8_t kZoneFlagLowFlow = 1U << 1U;
    static constexpr uint8_t kZoneFlagHighFlow = 1U << 2U;
    static constexpr uint8_t kZoneFlagFlowBaselineAvailable = 1U << 3U;

    static bool fromSession(const WateringSessionSummary& summary,
                            WateringRecordPayload& payload);
    static bool encode(const WateringRecordPayload& payload,
                       uint8_t* output,
                       std::size_t outputSize);
    static bool decode(const uint8_t* data,
                       std::size_t dataSize,
                       WateringRecordPayload& payload);
    static WateringRecordTotals calculateTotals(const WateringRecordPayload& payload);
};
