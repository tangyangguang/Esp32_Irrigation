// Use the Base's in-memory filesystem fixture and actual storage engine.
#define main baseHarnessMain
#include IRRIGATION_BASE_HARNESS
#undef main
#include "WateringRecordStore.h"
#include "IrrigationAuditStore.h"
#include <cassert>

static void countWatering(const StoredWateringRecord&, void* count) {
    ++*static_cast<unsigned*>(count);
}
static void countAudit(const StoredIrrigationAuditRecord&, void* count) {
    ++*static_cast<unsigned*>(count);
}
int main() {
    resetHarness();
    WateringRecordStore watering;
    IrrigationAuditStore audit;
    assert(watering.begin() && audit.begin());
    unsigned count = 0;
    // An empty successful read must not inherit a stack garbage failure flag.
    assert(watering.readLatest(0, 10, countWatering, &count) && count == 0);
    assert(audit.readLatest(0, 10, countAudit, &count) && count == 0);
    IrrigationAuditPayload fact;
    fact.kind = IrrigationAuditPayload::Kind::PlansChanged;
    assert(audit.appendInstant(fact));
    assert(audit.readLatest(0, 10, countAudit, &count) && count == 1);
    WateringSessionSummary summary{};
    summary.purpose = WateringPurpose::Normal;
    summary.source = WateringSource::ManualZones;
    summary.result = WateringResult::Completed;
    summary.stopReason = WateringStopReason::Completed;
    summary.zoneCount = 1;
    summary.zones[0].zoneId = 1;
    summary.zones[0].result = ZoneWateringResult::Completed;
    summary.zones[0].plannedDurationSec = 60;
    summary.zones[0].actualWateringSec = 60;
    summary.zones[0].pulseCount = 100;
    Esp32BaseRecordStore::RecordStartTime start;
    assert(watering.captureStartTime(start));
    g_time.uptimeSec += 60;
    assert(watering.appendCompleted(start, summary));
    count = 0;
    assert(watering.readLatest(0, 10, countWatering, &count) && count == 1);
    assert(watering.readLatest(1, 10, countWatering, &count) && count == 1);
    // Valid Base container but invalid application payload must report failure.
    uint8_t invalid[WateringRecordCodec::kPayloadSize]{};
    assert(watering.baseStore().appendInstant(invalid, sizeof(invalid)));
    count = 0;
    assert(!watering.readLatest(0, 1, countWatering, &count) && count == 0);
    assert(!watering.readLatest(0, 0, countWatering, &count));
    assert(!watering.readLatest(0, 1, nullptr));
    puts("Actual Base Store + irrigation: empty, valid, pagination and invalid payload reads passed");
}
