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
    for (const auto layout : {std::pair<uint32_t,uint32_t>{384U*1024U, 193U+24U},
                              {384U*1024U, WateringRecordStore::kStoredBytes+24U},
                              {128U*1024U, 24U+24U},
                              {128U*1024U, IrrigationAuditStore::kStoredBytes+24U}}) {
        const auto segment = recordStoreChooseSegmentLimit(layout.first, layout.second);
        printf("Store budget=%u slot=%u capacity=%u\n", layout.first, layout.second,
               recordStoreCalculateCapacity(layout.first, segment, layout.second));
    }
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
    // The fact stays unknown even when the clock later becomes trustworthy.
    g_time.synced = true;
    g_time.epochSec = 1800000000;
    StoredWateringRecord stored{};
    assert(watering.readById(1, stored) == Esp32BaseRecordStore::RecordReadResult::Found);
    assert(stored.timing.completedEpochSec == 0 && stored.timing.completedBootId == 0);
    assert(stored.timing.durationSec == 60);

    auto& stream = watering.recordStream();
    auto& auditStream = audit.recordStream();
    unsigned published = 0;
    auto publish = [](const uint8_t*, const iot_device::RecordFactView& value, void* user) {
        assert(value.sequence == 1 && value.typeCode == 1);
        assert(value.observedAtMs == iot_device::RecordStream::UnknownTime);
        ++*static_cast<unsigned*>(user);
        return true;
    };
    stream.setConnectionReady(true);
    stream.poll(1, publish, &published);
    assert(published == 1);
    assert(!stream.acknowledge(auditStream.generation(), 1));
    assert(stream.acknowledge(stream.generation(), 1));
    for (unsigned n=0;n<4;++n) stream.poll(2+n, nullptr, nullptr);
    assert(stream.acknowledgedSequence() == 1 && auditStream.acknowledgedSequence() == 0);
    assert(stream.checkpoint(10));

    // A safe retry during bounded recovery preserves its original completion.
    assert(stream.begin(11));
    Esp32BaseRecordStore::RecordStartTime secondStart;
    assert(watering.captureStartTime(secondStart));
    g_time.uptimeSec += 5;
    assert(!watering.appendCompleted(secondStart, summary));
    const auto frozen = watering.completionTiming();
    g_time.uptimeSec += 100;
    g_time.epochSec += 100;
    for (unsigned n=0;n<4;++n) stream.poll(12+n, nullptr, nullptr);
    assert(stream.state() == iot_device::StreamState::Ready);
    assert(stream.lastSequence() == 1 && stream.acknowledgedSequence() == 1);
    assert(watering.appendCompleted(secondStart, summary));
    assert(stream.lastSequence() == 2);
    assert(watering.readById(2, stored) == Esp32BaseRecordStore::RecordReadResult::Found);
    assert(stored.timing.completedEpochSec == frozen.completedEpochSec);
    assert(stored.timing.durationSec == 5);

    // A failed audit append faults only that stream; a repeated attempt cannot
    // blindly duplicate an uncertain write. Watering remains usable.
    g_fileSystemWriteFails = true;
    assert(!audit.appendInstant(fact));
    assert(auditStream.state() == iot_device::StreamState::Fault);
    g_fileSystemWriteFails = false;
    assert(!audit.appendInstant(fact));
    assert(stream.state() == iot_device::StreamState::Ready);
    assert(watering.captureStartTime(secondStart));
    g_time.uptimeSec += 1;
    assert(watering.appendCompleted(secondStart, summary));
    assert(stream.lastSequence() == 3);

    // Valid Base container but invalid application payload must report failure.
    uint8_t invalid[WateringRecordStore::kStoredBytes]{};
    assert(watering.baseStore().appendInstant(invalid, sizeof(invalid)));
    count = 0;
    assert(!watering.readLatest(0, 1, countWatering, &count) && count == 0);
    assert(!watering.readLatest(0, 0, countWatering, &count));
    assert(!watering.readLatest(0, 1, nullptr));
    puts("Actual Base Store + irrigation: reads, immutable time, recovery retry, independent ACK and checkpoint passed");
}
