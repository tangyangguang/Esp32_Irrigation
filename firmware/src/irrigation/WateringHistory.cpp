#include "WateringHistory.h"
namespace WateringHistory {
uint32_t localDay(uint32_t epoch) { return uint32_t((uint64_t(epoch) + 8U * 3600U) / 86400U); }
uint32_t zoneEpoch(const WateringRecordPayload& p, size_t i) {
    if (!p.startedEpoch || i >= p.zones.size()) return 0;
    const uint64_t epoch = uint64_t(p.startedEpoch) + p.zones[i].startedOffsetSec;
    return epoch <= UINT32_MAX ? uint32_t(epoch) : 0;
}
bool hasWater(const ZoneWateringRecord& z) {
    return !(z.flags & WateringRecordCodec::kZoneFlagUnknown) &&
           z.result != ZoneWateringResult::NotStarted && z.pulseCount > 0;
}
WateringDaySummary summarize(WateringRecordStore& store, uint32_t day,
                            const WateringStatus& active, uint32_t activeStartedEpoch) {
    WateringDaySummary result{}; result.day = day;
    Esp32BaseRecordStore::StoreStatus state{};
    if (!store.readStatus(state) || !state.ready) return result;
    struct Context { WateringDaySummary* result; uint32_t oldestDay; } ctx{&result, UINT32_MAX};
    const auto visit = [](const StoredWateringRecord& record, void* user) {
        auto& ctx = *static_cast<Context*>(user); auto& r = *ctx.result;
        if (!record.payload.startedEpoch) { ++r.unknownTimeCount; return; }
        const uint32_t startDay = localDay(record.payload.startedEpoch);
        if (startDay < ctx.oldestDay) ctx.oldestDay = startDay;
        for (size_t i = 0; i < record.payload.zones.size(); ++i) {
            const auto& z = record.payload.zones[i];
            if (!z.plannedDurationSec || localDay(zoneEpoch(record.payload, i)) != r.day) continue;
            auto& out = r.zones[i];
            if (z.flags & WateringRecordCodec::kZoneFlagUnknown) { ++out.unknown; continue; }
            if (hasWater(z)) ++out.count;
            out.seconds += z.actualWateringSec; out.waterMl += z.estimatedWaterMl;
            if (z.result == ZoneWateringResult::Failed || z.result == ZoneWateringResult::Stopped ||
                (z.flags & (WateringRecordCodec::kZoneFlagLowFlow | WateringRecordCodec::kZoneFlagHighFlow))) ++out.failures;
        }
        if (record.payload.result == WateringResult::StartFailed && startDay == r.day)
            ++r.startFailed;
    };
    result.readable = state.recordCount == 0 || store.readLatest(0, state.recordCount, visit, &ctx);
    result.truncated = state.oldestRecordId > 1 && (ctx.oldestDay == UINT32_MAX || day <= ctx.oldestDay);
    if (active.active && active.purpose == WateringPurpose::Normal && activeStartedEpoch) {
        for (size_t i = 0; i < active.stepCount; ++i) {
            const auto& z = active.zones[i];
            const uint64_t start = uint64_t(activeStartedEpoch) + z.startedOffsetSec;
            if (!BoardPins::isValidZoneId(z.zoneId) || i > active.currentStepIndex ||
                start > UINT32_MAX || localDay(uint32_t(start)) != day) continue;
            auto& out = result.zones[z.zoneId - 1];
            if (z.pulseCount) ++out.count;
            out.seconds += z.actualWateringSec; out.waterMl += z.estimatedWaterMl;
            out.active = i == active.currentStepIndex;
        }
    }
    return result;
}
}
