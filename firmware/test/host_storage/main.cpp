// Use the Base's in-memory filesystem fixture and actual storage engine.
#define main baseHarnessMain
#include IRRIGATION_BASE_HARNESS
#undef main
#include "WateringRecordStore.h"
#include "IrrigationAuditStore.h"
#include "IrrigationEvents.h"
#include "IrrigationRecordSync.h"
#include "runtime/Esp32BaseFileLog.h"
#include <cassert>

static void countWatering(const StoredWateringRecord&, void* count) {
    ++*static_cast<unsigned*>(count);
}
static void countAudit(const StoredIrrigationAuditRecord&, void* count) {
    ++*static_cast<unsigned*>(count);
}
int main() {
    resetHarness();
    g_totalBytes = 512U * 1024U;
    WateringRecordStore watering;
    IrrigationEvents events;
    auto& audit = events.auditStore();
    assert(watering.begin() && events.begin());
    assert(IrrigationRecordSync::instance().begin(watering, audit));
    const uint32_t historyBudget = WateringRecordStore::kMaximumStoreBytes +
                                   IrrigationAuditStore::kMaximumStoreBytes;
    const uint32_t logBudget = ESP32BASE_EB_FILELOG_MAX_BYTES * ESP32BASE_EB_FILELOG_ROTATE_FILES;
    assert(g_totalBytes - historyBudget - logBudget - storageSafetyReserve(g_totalBytes) >= 48U * 1024U);

    for (const auto layout : {std::pair<uint32_t,uint32_t>{384U*1024U, 193U+24U},
                              {WateringRecordStore::kMaximumStoreBytes, WateringRecordStore::kStoredBytes+24U},
                              {128U*1024U, 24U+24U},
                              {IrrigationAuditStore::kMaximumStoreBytes, IrrigationAuditStore::kStoredBytes+24U}}) {
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

    // One instant audit can wait through bounded recovery without being
    // overwritten by another operation or acquiring the retry time.
    assert(auditStream.begin(30));
    auto pendingAudit = fact;
    pendingAudit.value1 = 123;
    const uint32_t auditAt = g_time.epochSec;
    assert(!audit.appendInstant(pendingAudit));
    assert(audit.hasPending());
    assert(events.storageFault());
    pendingAudit.value1 = 999;
    assert(!audit.appendInstant(pendingAudit));
    g_time.epochSec += 60;
    for (unsigned n=0;n<4;++n) auditStream.poll(31+n, nullptr, nullptr);
    assert(audit.flushPending() && !audit.hasPending());
    assert(!events.storageFault());
    StoredIrrigationAuditRecord storedAudit{};
    assert(audit.readById(2, storedAudit) == Esp32BaseRecordStore::RecordReadResult::Found);
    assert(storedAudit.payload.value1 == 123 && storedAudit.timing.completedEpochSec == auditAt);

    // A failed audit append faults only that stream; a repeated attempt cannot
    // blindly duplicate an uncertain write. Watering remains usable.
    g_fileSystemWriteFails = true;
    assert(!audit.appendInstant(fact));
    assert(auditStream.state() == iot_device::StreamState::Fault);
    g_fileSystemWriteFails = false;
    assert(!audit.appendInstant(fact));
    assert(!audit.flushPending() && audit.hasPending());
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
    // Explicit formatting, unlike clear(), may discard protected history.
    uint8_t oldGeneration[16];
    memcpy(oldGeneration, auditStream.generation(), 16);
    Esp32BaseStorage::FormatResult formatted;
    assert(Esp32BaseStorage::formatAndReload(formatted));
    assert(formatted.formatSuccess && formatted.mountSuccess);
    watering.discardPendingAfterFormat();
    audit.discardPendingAfterFormat();
    assert(auditStream.begin(40));
    auditStream.poll(41, nullptr, nullptr);
    assert(!audit.hasPending() && auditStream.lastSequence() == 0);
    assert(memcmp(oldGeneration, auditStream.generation(), 16) != 0);

    // Failed condition persistence stays visible despite another healthy
    // condition, then clears after that specific condition commits.
    assert(Esp32BaseConditions::begin());
    assert(events.resetConditionHistory());
    g_conditionStateWriteFails = true;
    events.observeRtcRollback(Esp32BaseConditions::ObservedState::Active);
    assert(events.storageFault());
    events.observeTrustedTime(true);
    assert(events.storageFault());
    g_conditionStateWriteFails = false;
    events.observeRtcRollback(Esp32BaseConditions::ObservedState::Active);
    assert(!events.storageFault());
    puts("Actual Base Store + irrigation: reads, immutable time, recovery retry, independent ACK and checkpoint passed");
}
