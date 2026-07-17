#include "WateringRecordCodec.h"

namespace {

constexpr std::size_t kHeaderSize = 4;
constexpr std::size_t kZoneSize = 18;
constexpr uint8_t kKnownZoneFlags = WateringRecordCodec::kZoneFlagWaterEstimateCapped |
                                    WateringRecordCodec::kZoneFlagLowFlow |
                                    WateringRecordCodec::kZoneFlagHighFlow |
                                    WateringRecordCodec::kZoneFlagFlowBaselineAvailable;
constexpr uint16_t kMaximumZoneDurationSec = 120U * 60U;

void put16(uint8_t*& cursor, uint16_t value) {
    *cursor++ = static_cast<uint8_t>(value);
    *cursor++ = static_cast<uint8_t>(value >> 8U);
}

void put32(uint8_t*& cursor, uint32_t value) {
    for (uint8_t shift = 0; shift < 32; shift += 8) {
        *cursor++ = static_cast<uint8_t>(value >> shift);
    }
}

uint16_t get16(const uint8_t*& cursor) {
    const uint16_t value = static_cast<uint16_t>(cursor[0]) |
                           static_cast<uint16_t>(cursor[1]) << 8U;
    cursor += 2;
    return value;
}

uint32_t get32(const uint8_t*& cursor) {
    const uint32_t value = static_cast<uint32_t>(cursor[0]) |
                           static_cast<uint32_t>(cursor[1]) << 8U |
                           static_cast<uint32_t>(cursor[2]) << 16U |
                           static_cast<uint32_t>(cursor[3]) << 24U;
    cursor += 4;
    return value;
}

bool validSource(WateringSource value) {
    return value == WateringSource::ManualZones || value == WateringSource::AutomaticPlan;
}

bool validResult(WateringResult value) {
    return value == WateringResult::Completed || value == WateringResult::Stopped ||
           value == WateringResult::Failed;
}

bool validReason(WateringStopReason value) {
    return value >= WateringStopReason::Completed &&
           value <= WateringStopReason::MaintenanceInterrupted;
}

bool validZoneResult(ZoneWateringResult value) {
    return value >= ZoneWateringResult::NotStarted && value <= ZoneWateringResult::Failed;
}

bool validResultPair(WateringResult result, WateringStopReason reason) {
    if (result == WateringResult::Completed) {
        return reason == WateringStopReason::Completed;
    }
    if (result == WateringResult::Stopped) {
        return reason == WateringStopReason::UserStopped;
    }
    return result == WateringResult::Failed &&
           reason != WateringStopReason::None &&
           reason != WateringStopReason::Completed &&
           reason != WateringStopReason::UserStopped;
}

bool validPayload(const WateringRecordPayload& payload) {
    if (!validSource(payload.source) || !validResult(payload.result) ||
        !validReason(payload.stopReason) || !validResultPair(payload.result, payload.stopReason) ||
        (payload.source == WateringSource::ManualZones && payload.planId != 0) ||
        (payload.source != WateringSource::ManualZones &&
         (payload.planId == 0 || payload.planId > kWateringPlanCount))) {
        return false;
    }

    bool hasIncludedZone = false;
    uint64_t totalPulses = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        if (!validZoneResult(zone.result) || (zone.flags & ~kKnownZoneFlags) != 0 ||
            zone.plannedDurationSec > kMaximumZoneDurationSec ||
            zone.actualWateringSec > zone.plannedDurationSec) {
            return false;
        }
        if (zone.plannedDurationSec == 0) {
            if (zone.result != ZoneWateringResult::NotStarted || zone.flags != 0 ||
                zone.actualWateringSec != 0 || zone.pulseCount != 0 ||
                zone.estimatedWaterMl != 0 || zone.averageFlowMlPerMinute != 0) {
                return false;
            }
            continue;
        }
        hasIncludedZone = true;
        if (zone.result == ZoneWateringResult::NotStarted &&
            (zone.flags != 0 || zone.actualWateringSec != 0 ||
             zone.pulseCount != 0 || zone.estimatedWaterMl != 0 ||
             zone.averageFlowMlPerMinute != 0)) {
            return false;
        }
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0 &&
            zone.estimatedWaterMl != UINT32_MAX) {
            return false;
        }
        if ((zone.flags & (WateringRecordCodec::kZoneFlagLowFlow |
                           WateringRecordCodec::kZoneFlagHighFlow)) != 0 &&
            (zone.flags &
             WateringRecordCodec::kZoneFlagFlowBaselineAvailable) == 0) {
            return false;
        }
        totalPulses += zone.pulseCount;
    }
    return hasIncludedZone && totalPulses > 0;
}

}  // namespace

static_assert(WateringRecordCodec::kPayloadSize ==
                  kHeaderSize + kZoneSize * BoardPins::kZoneCount,
              "watering record payload layout changed");

bool WateringRecordCodec::fromSession(const WateringSessionSummary& summary,
                                      WateringRecordPayload& payload) {
    payload = {};
    if (summary.purpose != WateringPurpose::Normal || !summary.anyFlowEstablished ||
        summary.zoneCount == 0 || summary.zoneCount > summary.zones.size()) {
        return false;
    }
    payload.source = summary.source;
    payload.planId = summary.planId;
    payload.result = summary.result;
    payload.stopReason = summary.stopReason;
    uint8_t previousZoneId = 0;
    for (uint8_t index = 0; index < summary.zoneCount; ++index) {
        const ZoneWateringSummary& source = summary.zones[index];
        if (!BoardPins::isValidZoneId(source.zoneId) || source.zoneId <= previousZoneId ||
            source.plannedDurationSec == 0 || source.plannedDurationSec > kMaximumZoneDurationSec ||
            source.actualWateringSec > UINT16_MAX) {
            payload = {};
            return false;
        }
        previousZoneId = source.zoneId;
        ZoneWateringRecord& target = payload.zones[BoardPins::zoneIndex(source.zoneId)];
        target.result = source.result;
        target.flags = source.waterEstimateCapped ? kZoneFlagWaterEstimateCapped : 0;
        target.flags |= source.lowFlowDetected ? kZoneFlagLowFlow : 0;
        target.flags |= source.highFlowDetected ? kZoneFlagHighFlow : 0;
        target.flags |= source.result != ZoneWateringResult::NotStarted &&
                                source.flowBaselineAvailable
                            ? kZoneFlagFlowBaselineAvailable
                            : 0;
        target.plannedDurationSec = static_cast<uint16_t>(source.plannedDurationSec);
        target.actualWateringSec = static_cast<uint16_t>(source.actualWateringSec);
        target.pulseCount = source.pulseCount;
        target.estimatedWaterMl = source.estimatedWaterMl;
        target.averageFlowMlPerMinute = source.averageFlowMlPerMinute;
    }
    return validPayload(payload);
}

bool WateringRecordCodec::encode(const WateringRecordPayload& payload,
                                 uint8_t* output,
                                 std::size_t outputSize) {
    if (!output || outputSize != kPayloadSize || !validPayload(payload)) {
        return false;
    }
    uint8_t* cursor = output;
    *cursor++ = static_cast<uint8_t>(payload.source);
    *cursor++ = payload.planId;
    *cursor++ = static_cast<uint8_t>(payload.result);
    *cursor++ = static_cast<uint8_t>(payload.stopReason);
    for (const ZoneWateringRecord& zone : payload.zones) {
        *cursor++ = static_cast<uint8_t>(zone.result);
        *cursor++ = zone.flags;
        put16(cursor, zone.plannedDurationSec);
        put16(cursor, zone.actualWateringSec);
        put32(cursor, zone.pulseCount);
        put32(cursor, zone.estimatedWaterMl);
        put32(cursor, zone.averageFlowMlPerMinute);
    }
    return cursor == output + outputSize;
}

bool WateringRecordCodec::decode(const uint8_t* data,
                                 std::size_t dataSize,
                                 WateringRecordPayload& payload) {
    payload = {};
    if (!data || dataSize != kPayloadSize) {
        return false;
    }
    const uint8_t* cursor = data;
    payload.source = static_cast<WateringSource>(*cursor++);
    payload.planId = *cursor++;
    payload.result = static_cast<WateringResult>(*cursor++);
    payload.stopReason = static_cast<WateringStopReason>(*cursor++);
    for (ZoneWateringRecord& zone : payload.zones) {
        zone.result = static_cast<ZoneWateringResult>(*cursor++);
        zone.flags = *cursor++;
        zone.plannedDurationSec = get16(cursor);
        zone.actualWateringSec = get16(cursor);
        zone.pulseCount = get32(cursor);
        zone.estimatedWaterMl = get32(cursor);
        zone.averageFlowMlPerMinute = get32(cursor);
    }
    if (cursor != data + dataSize || !validPayload(payload)) {
        payload = {};
        return false;
    }
    return true;
}

WateringRecordTotals WateringRecordCodec::calculateTotals(const WateringRecordPayload& payload) {
    WateringRecordTotals totals{};
    uint64_t weightedFlow = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        totals.plannedDurationSec += zone.plannedDurationSec;
        totals.actualWateringSec += zone.actualWateringSec;
        totals.pulseCount += zone.pulseCount;
        totals.estimatedWaterMl += zone.estimatedWaterMl;
        weightedFlow += static_cast<uint64_t>(zone.averageFlowMlPerMinute) *
                        zone.actualWateringSec;
    }
    if (totals.actualWateringSec != 0) {
        const uint64_t average =
            (weightedFlow + totals.actualWateringSec / 2U) /
            totals.actualWateringSec;
        totals.averageFlowMlPerMinute =
            average > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(average);
    }
    return totals;
}
