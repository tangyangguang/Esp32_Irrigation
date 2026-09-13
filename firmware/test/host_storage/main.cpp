// Actual Base storage with its in-memory FS fixture; no device or MQTT connection.
#define main baseHarnessMain
#include IRRIGATION_BASE_HARNESS
#undef main
#include "WateringRecordStore.h"
#include "WateringHistory.h"
#include "IrrigationEvents.h"
#include "IrrigationRecords.h"
#include <cassert>
#include <ctime>

bool Esp32BaseTime::formatEpoch(uint32_t epoch, char* out, size_t size, const char* format) {
    const std::time_t local = static_cast<std::time_t>(epoch) + 28800;
    std::tm date{};
    return out && size && gmtime_r(&local, &date) &&
        std::strftime(out, size, format ? format : "%Y-%m-%d %H:%M:%S", &date) != 0;
}


static std::vector<uint8_t> taskMarker;
static bool markerWriteFails = false, markerReadFails = false;
bool Esp32BaseConfig::setBlob(const char*, const char*, const void* data, size_t size) {
    if (markerWriteFails) return false;
    taskMarker.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    return true;
}
Esp32BaseConfig::BlobReadResult Esp32BaseConfig::readBlob(const char*, const char*, void* out, size_t size) {
    if (markerReadFails) return BlobReadResult::Error;
    if (taskMarker.empty()) return BlobReadResult::NotFound;
    if (taskMarker.size() != size) return BlobReadResult::Error;
    memcpy(out, taskMarker.data(), size); return BlobReadResult::Found;
}
static uint32_t count(WateringRecordStore& store) {
    Esp32BaseRecordStore::StoreStatus status{}; assert(store.readStatus(status)); return status.recordCount;
}
static StoredWateringRecord latest(WateringRecordStore& store) {
    StoredWateringRecord result{};
    assert(store.readLatest(0, 1, [](const StoredWateringRecord& r, void* user) {
        *static_cast<StoredWateringRecord*>(user) = r;
    }, &result));
    return result;
}
int main() {
    resetHarness(); g_totalBytes = 512U * 1024U;
    WateringRecordStore watering; IrrigationEvents events; auto& audit = events.auditStore();
    auto& records = IrrigationRecords::instance(); records.bind(watering, audit);
    Esp32BaseStorage::FormatResult formatted{};
    assert(Esp32BaseStorage::formatAndReload(formatted));
    assert(records.reloadAfterFormat());
    assert(watering.begin() && events.begin());
    assert(records.begin(watering, audit));
    assert(count(watering) == 0);
    WateringRequest request{}; request.stepCount = 1; request.steps[0] = {1, 60, 0};
    WateringSessionSummary summary{}; summary.source = WateringSource::Manual;
    summary.purpose = WateringPurpose::Normal; summary.result = WateringResult::Completed;
    summary.stopReason = WateringStopReason::Completed; summary.zoneCount = 1;
    summary.zones[0].zoneId = 1; summary.zones[0].result = ZoneWateringResult::Completed;
    summary.zones[0].plannedDurationSec = summary.zones[0].actualWateringSec = 60;
    summary.zones[0].pulseCount = 100;
    Esp32BaseRecordStore::RecordStartTime start{};
    assert(watering.prepareTask(request) && watering.captureStartTime(start));
    g_time.uptimeSec += 60;
    assert(watering.appendCompleted(start, summary));
    const auto first = latest(watering);
    assert(first.payload.startedEpoch == 0 && first.timing.completedEpochSec == 0);
    assert(first.timing.completedBootId == 1 && first.timing.durationSec == 60);
    // Committed fact + failed marker cleanup: reboot does not append a duplicate.
    markerWriteFails = true; assert(!watering.cancelPreparedTask());
    markerWriteFails = false; ++g_time.bootId; g_time.uptimeSec = 10;
    assert(watering.begin() && count(watering) == 1);
    // A durable start without final evidence becomes one incomplete task.
    g_time.synced = true; g_time.epochSec = 1800000000;
    assert(watering.prepareTask(request));
    ++g_time.bootId; g_time.uptimeSec = 10;
    assert(watering.begin() && count(watering) == 2);
    auto interrupted = latest(watering);
    assert(interrupted.payload.result == WateringResult::Incomplete);
    assert(interrupted.payload.zones[0].flags == WateringRecordCodec::kZoneFlagUnknown);
    assert(watering.begin() && count(watering) == 2);
    auto day = WateringHistory::localDay(1800000000);
    auto daily = WateringHistory::summarize(watering, day, WateringStatus{}, 0);
    assert(daily.readable && daily.zones[0].unknown == 1 && daily.zones[0].count == 0);
    assert(daily.unknownTimeCount == 1);
    // Read failure is not an absent marker and cannot permit a new task.
    markerReadFails = true; assert(!watering.begin() && !watering.isWritable());
    markerReadFails = false; assert(watering.begin());
    // Frozen pending audit time survives a temporary write suspension.
    IrrigationAuditPayload fact{}; fact.kind = IrrigationAuditPayload::Kind::PlansChanged; fact.value1 = 123;
    assert(Esp32BaseStorage::setOtaWriteSuspended(true));
    assert(!audit.appendInstant(fact) && audit.hasPending());
    const auto at = g_time.epochSec; g_time.epochSec += 100;
    fact.value1 = 999; assert(!audit.appendInstant(fact));
    assert(Esp32BaseStorage::setOtaWriteSuspended(false));
    assert(audit.flushPending());
    StoredIrrigationAuditRecord storedAudit{};
    assert(audit.readById(1, storedAudit) == Esp32BaseRecordStore::RecordReadResult::Found);
    assert(storedAudit.payload.value1 == 123 && storedAudit.timing.completedEpochSec == at);
    // NVS observation failure is irrelevant to RAM-only conditions.
    assert(Esp32BaseConditions::begin()); g_conditionStateWriteFails = true;
    events.observeRtcRollback(Esp32BaseConditions::ObservedState::Active);
    assert(!events.storageFault() && g_conditionStateWriteCount == 0);
    // No acknowledgement/release is ever issued: full histories still rotate.
    Esp32BaseRecordStore::StoreStatus state{}; assert(watering.readStatus(state));
    WateringRecordPayload payload{}; assert(WateringRecordCodec::fromSession(summary, payload));
    payload.startedEpoch = 1800000000;
    for (uint32_t i = 0; i < state.capacity + 2; ++i) {
        payload.taskId = 1000 + i; assert(watering.appendPayload(payload));
    }
    assert(watering.readStatus(state) && state.writable && state.oldestRecordId > 1);
    daily = WateringHistory::summarize(watering, day, WateringStatus{}, 0);
    assert(daily.readable && daily.truncated);
    // Corrupt business bytes cannot be treated as an empty successful read.
    uint8_t invalid[WateringRecordStore::kStoredBytes]{};
    assert(watering.baseStore().appendInstant(invalid, sizeof(invalid)));
    assert(!watering.readLatest(0, 1, [](const StoredWateringRecord&, void*) {}));
    assert(Esp32BaseStorage::formatAndReload(formatted) && records.reloadAfterFormat());
    assert(count(watering) == 0 && watering.isWritable());
    puts("Local retention, task recovery, unknown measurements and independent audit cases passed");
}
