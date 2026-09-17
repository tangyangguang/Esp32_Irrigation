#pragma once
#include "WateringRecordStore.h"
struct WateringDayZone {
    uint32_t count = 0, seconds = 0, failures = 0, unknown = 0;
    uint64_t waterMl = 0;
    bool active = false;
};
struct WateringDaySummary {
    uint32_t day = 0, unknownTimeCount = 0, startFailed = 0;
    bool readable = false, truncated = false;
    std::array<WateringDayZone, BoardPins::kZoneCount> zones{};
};
namespace WateringHistory {
uint32_t localDay(uint32_t epoch);
uint32_t zoneEpoch(const WateringRecordPayload&, size_t zone);
bool hasWater(const ZoneWateringRecord&);
WateringDaySummary summarize(WateringRecordStore&, uint32_t day,
                            const WateringStatus&, uint32_t activeStartedEpoch);
}
