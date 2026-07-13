#include "WateringRecordCodec.h"

namespace {

constexpr uint32_t kMagic = 0x31525249U;  // "IRR1" in little-endian byte order.
constexpr uint8_t kHeaderSize = 35;
constexpr uint8_t kTimeSyncedFlag = 1U << 0U;
constexpr std::size_t kZoneSize = 20;
constexpr std::size_t kTrailerSize = 6;

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

uint32_t crc32(const uint8_t* data, std::size_t size) {
    uint32_t crc = UINT32_MAX;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

bool validSource(WateringSource value) {
    return value == WateringSource::ManualZones || value == WateringSource::ManualPlan ||
           value == WateringSource::AutomaticPlan;
}

bool validResult(WateringResult value) {
    return value == WateringResult::Completed || value == WateringResult::Stopped ||
           value == WateringResult::Failed;
}

bool validReason(WateringStopReason value) {
    return value >= WateringStopReason::Completed && value <= WateringStopReason::HardwareFailure;
}

bool validZoneResult(ZoneWateringResult value) {
    return value >= ZoneWateringResult::NotStarted && value <= ZoneWateringResult::Failed;
}

bool validRecord(const WateringRecord& record) {
    if (record.id == 0 || record.startTime.bootId == 0 ||
        (record.startTime.synced && record.startTime.epochSec == 0) ||
        (!record.startTime.synced && record.startTime.epochSec != 0) ||
        !validSource(record.source) || !validResult(record.result) ||
        !validReason(record.stopReason) ||
        record.zoneCount == 0 || record.zoneCount > record.zones.size()) {
        return false;
    }
    if ((record.source == WateringSource::ManualZones && record.planId != 0) ||
        (record.source != WateringSource::ManualZones &&
         (record.planId == 0 || record.planId > kWateringPlanCount))) {
        return false;
    }
    uint8_t previousZoneId = 0;
    for (uint8_t index = 0; index < record.zoneCount; ++index) {
        const ZoneWateringRecord& zone = record.zones[index];
        if (!BoardPins::isValidZoneId(zone.zoneId) || zone.zoneId <= previousZoneId ||
            !validZoneResult(zone.result) ||
            (zone.flags & ~WateringRecordCodec::kZoneFlagWaterEstimateCapped) != 0) {
            return false;
        }
        previousZoneId = zone.zoneId;
    }
    return true;
}

}  // namespace

bool WateringRecordCodec::fromSession(const WateringSessionSummary& summary,
                                      uint32_t recordId,
                                      const WateringRecordTime& startTime,
                                      WateringRecord& record) {
    if (summary.purpose != WateringPurpose::Normal || !summary.anyFlowEstablished) {
        record = {};
        return false;
    }
    record = {};
    record.id = recordId;
    record.startTime = startTime;
    record.elapsedSec = summary.elapsedSec;
    record.source = summary.source;
    record.planId = summary.planId;
    record.result = summary.result;
    record.stopReason = summary.stopReason;
    record.zoneCount = summary.zoneCount;
    for (uint8_t index = 0; index < summary.zoneCount && index < record.zones.size(); ++index) {
        const ZoneWateringSummary& source = summary.zones[index];
        ZoneWateringRecord& target = record.zones[index];
        target.zoneId = source.zoneId;
        target.result = source.result;
        target.flags = source.waterEstimateCapped ? kZoneFlagWaterEstimateCapped : 0;
        target.plannedDurationSec = source.plannedDurationSec;
        target.actualWateringSec = source.actualWateringSec;
        target.pulseCount = source.pulseCount;
        target.estimatedWaterMl = source.estimatedWaterMl;
    }
    return validRecord(record);
}

bool WateringRecordCodec::encode(const WateringRecord& record,
                                 uint8_t* output,
                                 std::size_t capacity,
                                 std::size_t& encodedSize) {
    encodedSize = 0;
    if (!output || !validRecord(record)) {
        return false;
    }
    const std::size_t size = kHeaderSize + kZoneSize * record.zoneCount + kTrailerSize;
    if (capacity < size || size > UINT16_MAX) {
        return false;
    }

    uint8_t* cursor = output;
    put32(cursor, kMagic);
    *cursor++ = kVersion;
    *cursor++ = kHeaderSize;
    put16(cursor, static_cast<uint16_t>(size));
    put32(cursor, record.id);
    put32(cursor, record.startTime.epochSec);
    put32(cursor, record.startTime.bootId);
    put32(cursor, record.startTime.uptimeSec);
    put32(cursor, record.elapsedSec);
    *cursor++ = static_cast<uint8_t>(record.source);
    *cursor++ = record.planId;
    *cursor++ = static_cast<uint8_t>(record.result);
    *cursor++ = static_cast<uint8_t>(record.stopReason);
    *cursor++ = record.zoneCount;
    put16(cursor, record.startTime.synced ? kTimeSyncedFlag : 0);

    for (uint8_t index = 0; index < record.zoneCount; ++index) {
        const ZoneWateringRecord& zone = record.zones[index];
        *cursor++ = zone.zoneId;
        *cursor++ = static_cast<uint8_t>(zone.result);
        put16(cursor, zone.flags);
        put32(cursor, zone.plannedDurationSec);
        put32(cursor, zone.actualWateringSec);
        put32(cursor, zone.pulseCount);
        put32(cursor, zone.estimatedWaterMl);
    }

    put32(cursor, crc32(output, static_cast<std::size_t>(cursor - output)));
    put16(cursor, static_cast<uint16_t>(size));
    encodedSize = static_cast<std::size_t>(cursor - output);
    return encodedSize == size;
}

bool WateringRecordCodec::decode(const uint8_t* data, std::size_t size, WateringRecord& record) {
    record = {};
    if (!data || size < kHeaderSize + kZoneSize + kTrailerSize || size > UINT16_MAX) {
        return false;
    }
    const uint8_t* cursor = data;
    if (get32(cursor) != kMagic || *cursor++ != kVersion || *cursor++ != kHeaderSize ||
        get16(cursor) != size) {
        return false;
    }

    record.id = get32(cursor);
    record.startTime.epochSec = get32(cursor);
    record.startTime.bootId = get32(cursor);
    record.startTime.uptimeSec = get32(cursor);
    record.elapsedSec = get32(cursor);
    record.source = static_cast<WateringSource>(*cursor++);
    record.planId = *cursor++;
    record.result = static_cast<WateringResult>(*cursor++);
    record.stopReason = static_cast<WateringStopReason>(*cursor++);
    record.zoneCount = *cursor++;
    const uint16_t flags = get16(cursor);
    record.startTime.synced = (flags & kTimeSyncedFlag) != 0;

    const std::size_t expectedSize = kHeaderSize + kZoneSize * record.zoneCount + kTrailerSize;
    if ((flags & ~kTimeSyncedFlag) != 0 || expectedSize != size ||
        record.zoneCount > record.zones.size()) {
        return false;
    }
    for (uint8_t index = 0; index < record.zoneCount; ++index) {
        ZoneWateringRecord& zone = record.zones[index];
        zone.zoneId = *cursor++;
        zone.result = static_cast<ZoneWateringResult>(*cursor++);
        zone.flags = get16(cursor);
        zone.plannedDurationSec = get32(cursor);
        zone.actualWateringSec = get32(cursor);
        zone.pulseCount = get32(cursor);
        zone.estimatedWaterMl = get32(cursor);
    }
    const uint32_t storedCrc = get32(cursor);
    const uint16_t trailingSize = get16(cursor);
    return cursor == data + size && trailingSize == size &&
           storedCrc == crc32(data, size - kTrailerSize) && validRecord(record);
}
