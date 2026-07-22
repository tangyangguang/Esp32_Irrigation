#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

struct ZoneWateringRecord {
    ZoneWateringResult result;
    uint8_t flags;
    uint16_t plannedDurationSec;
    uint16_t actualWateringSec;
    uint32_t targetWaterMl;
    uint32_t pulseCount;
    uint32_t estimatedWaterMl;
    uint32_t averageFlowMlPerMinute;
    uint32_t baselineFlowMlPerMinute;
    uint32_t terminalFlowMlPerMinute;
    uint32_t terminalMinimumFlowMlPerMinute;
    uint32_t terminalMaximumFlowMlPerMinute;
};

struct WateringRecordPayload {
    WateringSource source;
    uint8_t planId;
    WateringResult result;
    WateringStopReason stopReason;
    std::array<ZoneWateringRecord, BoardPins::kZoneCount> zones;
};

struct WateringRecordTotals {
    uint32_t plannedDurationSec;
    uint32_t actualWateringSec;
    uint64_t pulseCount;
    uint64_t estimatedWaterMl;
    uint32_t averageFlowMlPerMinute;
};

class WateringRecordCodec {
public:
    static constexpr std::size_t kPayloadSize = 232;
    static constexpr uint8_t kZoneFlagWaterEstimateCapped = 1U << 0U;
    static constexpr uint8_t kZoneFlagLowFlow = 1U << 1U;
    static constexpr uint8_t kZoneFlagHighFlow = 1U << 2U;
    static constexpr uint8_t kZoneFlagFlowBaselineAvailable = 1U << 3U;
    static constexpr uint8_t kZoneFlagTerminalFlowAvailable = 1U << 4U;
    static constexpr uint8_t kZoneFlagTerminalFlowStable = 1U << 5U;

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
