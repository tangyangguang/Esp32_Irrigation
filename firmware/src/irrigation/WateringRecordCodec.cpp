#include "WateringRecordCodec.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kMagic = 0x31525457UL;  // WTR1
constexpr uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 25;
constexpr std::size_t kZoneSize = 28;
constexpr uint8_t kKnownZoneFlags =
    WateringRecordCodec::kZoneFlagWaterEstimateCapped |
    WateringRecordCodec::kZoneFlagLowFlow |
    WateringRecordCodec::kZoneFlagHighFlow |
    WateringRecordCodec::kZoneFlagFlowBaselineAvailable;
constexpr uint16_t kMaximumZoneDurationSec =
    kMaximumConfigurableZoneDurationMinutes * 60U;

void put16(uint8_t*& cursor, uint16_t value) {
    *cursor++ = static_cast<uint8_t>(value);
    *cursor++ = static_cast<uint8_t>(value >> 8U);
}

void put24(uint8_t*& cursor, uint32_t value) {
    *cursor++ = static_cast<uint8_t>(value);
    *cursor++ = static_cast<uint8_t>(value >> 8U);
    *cursor++ = static_cast<uint8_t>(value >> 16U);
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

uint32_t get24(const uint8_t*& cursor) {
    const uint32_t value = static_cast<uint32_t>(cursor[0]) |
                           static_cast<uint32_t>(cursor[1]) << 8U |
                           static_cast<uint32_t>(cursor[2]) << 16U;
    cursor += 3;
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

int hexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool parseUuid(const char* value, std::array<uint8_t, 16>& output) {
    output.fill(0);
    if (!value || value[0] == '\0') return true;
    if (std::strlen(value) != 36U || value[14] < '1' || value[14] > '8' ||
        (value[19] != '8' && value[19] != '9' && value[19] != 'a' &&
         value[19] != 'A' && value[19] != 'b' && value[19] != 'B'))
        return false;
    std::size_t source = 0;
    std::size_t target = 0;
    while (source < 36U && target < output.size()) {
        if (source == 8U || source == 13U || source == 18U || source == 23U) {
            if (value[source++] != '-') return false;
        }
        const int high = hexNibble(value[source++]);
        const int low = hexNibble(value[source++]);
        if (high < 0 || low < 0) return false;
        output[target++] = static_cast<uint8_t>((high << 4) | low);
    }
    return source == 36U && target == output.size() && value[source] == '\0';
}

bool emptyUuid(const std::array<uint8_t, 16>& value) {
    for (uint8_t byte : value) {
        if (byte != 0U) return false;
    }
    return true;
}

bool validSource(WateringSource value) {
    return value == WateringSource::ManualZones ||
           value == WateringSource::SingleOutput ||
           value == WateringSource::AutomaticPlan;
}

bool validResult(WateringResult value) {
    return value == WateringResult::Completed ||
           value == WateringResult::Stopped ||
           value == WateringResult::Failed;
}

bool validReason(WateringStopReason value) {
    return value >= WateringStopReason::Completed &&
           value <= WateringStopReason::TargetVolumeTimeout;
}

bool validResultPair(WateringResult result, WateringStopReason reason) {
    if (result == WateringResult::Completed)
        return reason == WateringStopReason::Completed;
    if (result == WateringResult::Stopped)
        return reason == WateringStopReason::UserStopped;
    return result == WateringResult::Failed &&
           reason != WateringStopReason::None &&
           reason != WateringStopReason::Completed &&
           reason != WateringStopReason::UserStopped;
}

bool validPayload(const WateringRecordPayload& payload) {
    if (!validSource(payload.source) || !validResult(payload.result) ||
        !validReason(payload.stopReason) ||
        !validResultPair(payload.result, payload.stopReason) ||
        ((payload.source == WateringSource::ManualZones ||
          payload.source == WateringSource::SingleOutput) &&
         payload.planId != 0U) ||
        (payload.source == WateringSource::AutomaticPlan &&
         (payload.planId == 0U || payload.planId > kWateringPlanCount))) {
        return false;
    }
    bool included = false;
    bool started = false;
    uint64_t pulses = 0;
    for (const ZoneWateringRecord& zone : payload.zones) {
        if (zone.result < ZoneWateringResult::NotStarted ||
            zone.result > ZoneWateringResult::Failed ||
            (zone.flags & ~kKnownZoneFlags) != 0U ||
            zone.plannedDurationSec > kMaximumZoneDurationSec ||
            zone.actualWateringSec > zone.plannedDurationSec ||
            zone.targetWaterMl > 0xFFFFFFUL) {
            return false;
        }
        if (zone.plannedDurationSec == 0U) {
            if (zone.result != ZoneWateringResult::NotStarted || zone.flags != 0U ||
                zone.actualWateringSec != 0U || zone.targetWaterMl != 0U ||
                zone.pulseCount != 0U || zone.estimatedWaterMl != 0U ||
                zone.averageFlowMlPerMinute != 0U ||
                zone.baselinePulseRateX10000 != 0U ||
                zone.baselineFlowMlPerMinute != 0U) return false;
            continue;
        }
        included = true;
        started |= zone.result != ZoneWateringResult::NotStarted;
        if (zone.result == ZoneWateringResult::NotStarted &&
            (zone.flags != 0U || zone.actualWateringSec != 0U ||
             zone.targetWaterMl != 0U || zone.pulseCount != 0U ||
             zone.estimatedWaterMl != 0U ||
             zone.averageFlowMlPerMinute != 0U ||
             zone.baselinePulseRateX10000 != 0U ||
             zone.baselineFlowMlPerMinute != 0U)) return false;
        if ((zone.flags & WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0U &&
            zone.estimatedWaterMl != UINT32_MAX) return false;
        const bool baseline =
            (zone.flags & WateringRecordCodec::kZoneFlagFlowBaselineAvailable) != 0U;
        if (!baseline &&
            (zone.baselinePulseRateX10000 != 0U ||
             zone.baselineFlowMlPerMinute != 0U)) return false;
        if ((zone.flags & (WateringRecordCodec::kZoneFlagLowFlow |
                           WateringRecordCodec::kZoneFlagHighFlow)) != 0U &&
            !baseline) return false;
        pulses += zone.pulseCount;
    }
    if (payload.source != WateringSource::SingleOutput) {
        for (const ZoneWateringRecord& zone : payload.zones)
            if (zone.targetWaterMl != 0U) return false;
    }
    return included && started &&
           (payload.result != WateringResult::Completed || pulses != 0U);
}

}  // namespace

static_assert(WateringRecordCodec::kPayloadSize ==
                  kHeaderSize + kZoneSize * BoardPins::kZoneCount,
              "watering record payload layout changed");

bool WateringRecordCodec::fromSession(const WateringSessionSummary& summary,
                                      const char* relatedCommandId,
                                      WateringRecordPayload& payload) {
    payload = {};
    for (ZoneWateringRecord& zone : payload.zones)
        zone.result = ZoneWateringResult::NotStarted;
    if (summary.purpose != WateringPurpose::Normal || summary.zoneCount == 0U ||
        summary.zoneCount > summary.zones.size() ||
        !parseUuid(relatedCommandId, payload.relatedCommandId)) return false;
    payload.source = summary.source;
    payload.planId = summary.planId;
    payload.result = summary.result;
    payload.stopReason = summary.stopReason;
    uint8_t previousZoneId = 0;
    for (uint8_t index = 0; index < summary.zoneCount; ++index) {
        const ZoneWateringSummary& source = summary.zones[index];
        if (!BoardPins::isValidZoneId(source.zoneId) ||
            source.zoneId <= previousZoneId || source.plannedDurationSec == 0U ||
            source.plannedDurationSec > kMaximumZoneDurationSec ||
            source.actualWateringSec > UINT16_MAX) return false;
        previousZoneId = source.zoneId;
        ZoneWateringRecord& target =
            payload.zones[BoardPins::zoneIndex(source.zoneId)];
        target.result = source.result;
        target.flags = source.waterEstimateCapped ? kZoneFlagWaterEstimateCapped : 0U;
        target.flags |= source.lowFlowDetected ? kZoneFlagLowFlow : 0U;
        target.flags |= source.highFlowDetected ? kZoneFlagHighFlow : 0U;
        target.flags |= source.result != ZoneWateringResult::NotStarted &&
                                source.flowBaselineAvailable
                            ? kZoneFlagFlowBaselineAvailable
                            : 0U;
        target.plannedDurationSec = static_cast<uint16_t>(source.plannedDurationSec);
        target.actualWateringSec = static_cast<uint16_t>(source.actualWateringSec);
        target.targetWaterMl = source.targetWaterMl;
        target.pulseCount = source.pulseCount;
        target.estimatedWaterMl = source.estimatedWaterMl;
        target.averageFlowMlPerMinute = source.averageFlowMlPerMinute;
        if ((target.flags & kZoneFlagFlowBaselineAvailable) != 0U) {
            target.baselinePulseRateX10000 = source.baselinePulseRateX10000;
            target.baselineFlowMlPerMinute = source.baselineFlowMlPerMinute;
        }
    }
    return validPayload(payload);
}

bool WateringRecordCodec::encode(const WateringRecordPayload& payload,
                                 uint8_t* output,
                                 std::size_t outputSize) {
    if (!output || outputSize != kPayloadSize || !validPayload(payload)) return false;
    uint8_t* cursor = output;
    put32(cursor, kMagic);
    *cursor++ = kVersion;
    *cursor++ = static_cast<uint8_t>(payload.source);
    *cursor++ = payload.planId;
    *cursor++ = static_cast<uint8_t>(payload.result);
    *cursor++ = static_cast<uint8_t>(payload.stopReason);
    for (uint8_t byte : payload.relatedCommandId) *cursor++ = byte;
    for (const ZoneWateringRecord& zone : payload.zones) {
        *cursor++ = static_cast<uint8_t>(zone.result) |
                    static_cast<uint8_t>(zone.flags << 2U);
        put16(cursor, zone.plannedDurationSec);
        put16(cursor, zone.actualWateringSec);
        put24(cursor, zone.targetWaterMl);
        put32(cursor, zone.pulseCount);
        put32(cursor, zone.estimatedWaterMl);
        put32(cursor, zone.averageFlowMlPerMinute);
        put32(cursor, zone.baselinePulseRateX10000);
        put32(cursor, zone.baselineFlowMlPerMinute);
    }
    return cursor == output + outputSize;
}

bool WateringRecordCodec::decode(const uint8_t* data,
                                 std::size_t dataSize,
                                 WateringRecordPayload& payload) {
    payload = {};
    if (!data || dataSize != kPayloadSize) return false;
    const uint8_t* cursor = data;
    if (get32(cursor) != kMagic || *cursor++ != kVersion) return false;
    payload.source = static_cast<WateringSource>(*cursor++);
    payload.planId = *cursor++;
    payload.result = static_cast<WateringResult>(*cursor++);
    payload.stopReason = static_cast<WateringStopReason>(*cursor++);
    for (uint8_t& byte : payload.relatedCommandId) byte = *cursor++;
    for (ZoneWateringRecord& zone : payload.zones) {
        const uint8_t resultAndFlags = *cursor++;
        zone.result = static_cast<ZoneWateringResult>(resultAndFlags & 0x03U);
        zone.flags = resultAndFlags >> 2U;
        zone.plannedDurationSec = get16(cursor);
        zone.actualWateringSec = get16(cursor);
        zone.targetWaterMl = get24(cursor);
        zone.pulseCount = get32(cursor);
        zone.estimatedWaterMl = get32(cursor);
        zone.averageFlowMlPerMinute = get32(cursor);
        zone.baselinePulseRateX10000 = get32(cursor);
        zone.baselineFlowMlPerMinute = get32(cursor);
    }
    if (cursor != data + dataSize || !validPayload(payload)) {
        payload = {};
        return false;
    }
    return true;
}

bool WateringRecordCodec::formatRelatedCommandId(
    const WateringRecordPayload& payload,
    char* output,
    std::size_t outputSize) {
    if (!output || outputSize < 37U) return false;
    if (emptyUuid(payload.relatedCommandId)) {
        output[0] = '\0';
        return true;
    }
    const uint8_t* b = payload.relatedCommandId.data();
    return std::snprintf(output, outputSize,
                         "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                         "%02x%02x%02x%02x%02x%02x",
                         b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                         b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]) == 36;
}

WateringRecordTotals WateringRecordCodec::calculateTotals(
    const WateringRecordPayload& payload) {
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
    if (totals.actualWateringSec != 0U) {
        const uint64_t average =
            (weightedFlow + totals.actualWateringSec / 2U) /
            totals.actualWateringSec;
        totals.averageFlowMlPerMinute =
            average > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(average);
    }
    return totals;
}
