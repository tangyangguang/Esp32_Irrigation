#include "WateringRecordStore.h"
#include <runtime/Esp32BaseTime.h>
#include <cstring>

bool WateringRecordStore::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kRecordTypeName;
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = kStoredBytes;
    definition.retentionPolicy = Esp32BaseRecordStore::RetentionPolicy::RotateOldest;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    taskReady_ = false; taskId_ = 0; startedEpoch_ = 0; pending_ = false;
    return store_.begin(definition) && (taskReady_ = recoverTask());
}

Esp32BaseRecordStore& WateringRecordStore::baseStore() { return store_; }

bool WateringRecordStore::captureStartTime(
    Esp32BaseRecordStore::RecordStartTime& startTime) const {
    return store_.captureStartTime(startTime);
}

bool WateringRecordStore::appendCompleted(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary) {
    if (!pending_) {
        if (!taskId_ || !WateringRecordCodec::fromSession(summary, pendingPayload_)) return false;
        const auto now = Esp32BaseTime::snapshot();
        if (!startTime.bootId || startTime.bootId != now.bootId || startTime.uptimeSec > now.uptimeSec) return false;
        pendingPayload_.taskId = taskId_;
        pendingPayload_.startedEpoch = startedEpoch_;
        pendingTiming_ = {now.synced ? now.epochSec : 0, now.bootId, now.uptimeSec, now.uptimeSec - startTime.uptimeSec};
        pending_ = true;
    }
    if (!WateringRecordCodec::encode(pendingPayload_, scratch_, sizeof(scratch_)) ||
        !store_.appendRecorded(pendingTiming_, scratch_, sizeof(scratch_))) return false;
    pending_ = false;
    // Caller keeps the completed task until the marker can be cleared. A failed
    // cleanup never causes a second append, including after a reboot.
    return true;
}

bool WateringRecordStore::appendPayload(const WateringRecordPayload& payload) {
    return WateringRecordCodec::encode(payload, scratch_, sizeof(scratch_)) &&
           store_.appendInstant(scratch_, sizeof(scratch_));
}

bool WateringRecordStore::readLatest(uint32_t offset,
                                     uint32_t limit,
                                     ReadCallback callback,
                                     void* user) {
    if (!callback || limit == 0U) return false;
    ReadContext context;
    context.callback = callback;
    context.user = user;
    const bool read = store_.readLatest(offset, limit, scratch_, sizeof(scratch_),
                                        readAdapter, &context);
    return read && !context.decodeFailed;
}

Esp32BaseRecordStore::RecordReadResult WateringRecordStore::readById(
    uint32_t recordId,
    StoredWateringRecord& record) {
    record = {};
    Esp32BaseRecordStore::RecordMetadata metadata;
    const Esp32BaseRecordStore::RecordReadResult result =
        store_.readById(recordId, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) return result;
    if (!decodeFact(scratch_, sizeof(scratch_), record))
        return Esp32BaseRecordStore::RecordReadResult::Corrupt;
    record.recordId = metadata.recordId;
    record.timing = metadata.timing;

    return Esp32BaseRecordStore::RecordReadResult::Found;
}

bool WateringRecordStore::readStatus(
    Esp32BaseRecordStore::StoreStatus& status) const {
    return store_.readStatus(status);
}

bool WateringRecordStore::isReady() const { return store_.isReady(); }
bool WateringRecordStore::isWritable() const { return taskReady_ && store_.isWritable(); }
Esp32BaseRecordStore::StoreState WateringRecordStore::state() const {
    return store_.state();
}
Esp32BaseRecordStore::StoreError WateringRecordStore::lastError() const {
    return store_.lastError();
}
const char* WateringRecordStore::lastErrorReason() const {
    return store_.lastErrorReason();
}

void WateringRecordStore::readAdapter(
    const Esp32BaseRecordStore::RecordView& view,
    void* user) {
    ReadContext* context = static_cast<ReadContext*>(user);
    StoredWateringRecord record{};
    if (!context || !decodeFact(view.payload, view.payloadSizeBytes, record)) {
        if (context) context->decodeFailed = true;
        return;
    }
    record.recordId = view.recordId;
    record.timing = view.timing;

    context->callback(record, context->user);
}

bool WateringRecordStore::decodeFact(const uint8_t* bytes, std::size_t length, StoredWateringRecord& record) {
    return WateringRecordCodec::decode(bytes, length, record.payload);
}

namespace {
constexpr size_t kTaskMarkerBytes = 4 + 16 + WateringRecordCodec::kPayloadSize + 4;
static_assert(kTaskMarkerBytes <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN, "task marker exceeds NVS blob budget");
uint32_t taskCrc(const uint8_t* data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
}
bool WateringRecordStore::writeTaskMarker(const WateringRecordPayload* payload) {
    uint8_t bytes[kTaskMarkerBytes]{};
    bytes[0] = 'I'; bytes[1] = 'T'; bytes[2] = 1; bytes[3] = payload ? 1 : 0;
    Esp32BaseRecordStore::StoreStatus status{};
    if (!store_.readStatus(status)) return false;
    memcpy(bytes + 4, status.storageGeneration, 16);
    if (payload && !WateringRecordCodec::encode(*payload, bytes + 20, WateringRecordCodec::kPayloadSize)) return false;
    const uint32_t crc = taskCrc(bytes, sizeof(bytes) - 4);
    for (unsigned n = 0; n < 4; ++n) bytes[sizeof(bytes) - 4 + n] = uint8_t(crc >> (8 * n));
    return Esp32BaseConfig::setBlob("irrigation", "task", bytes, sizeof(bytes));
}
bool WateringRecordStore::prepareTask(const WateringRequest& request) {
    if (!isWritable() || taskId_) return false;
    Esp32BaseRecordStore::StoreStatus status{};
    if (!store_.readStatus(status) || !status.nextRecordId) return false;
    WateringRecordPayload intent{};
    intent.taskId = status.nextRecordId;
    const auto now = Esp32BaseTime::snapshot();
    intent.startedEpoch = now.synced ? now.epochSec : 0;
    intent.source = request.source; intent.targetMode = request.targetMode; intent.planId = request.planId;
    intent.result = WateringResult::Incomplete; intent.stopReason = WateringStopReason::RebootInterrupted;
    for (uint8_t i = 0; i < request.stepCount; ++i) {
        const auto& step = request.steps[i];
        auto& zone = intent.zones[step.zoneId - 1];
        zone.plannedDurationSec = uint16_t(step.targetDurationSec);
        zone.targetWaterMl = step.targetWaterMl;
        zone.flags = WateringRecordCodec::kZoneFlagUnknown;
    }
    if (!writeTaskMarker(&intent)) { taskReady_ = false; return false; }
    taskId_ = intent.taskId; startedEpoch_ = intent.startedEpoch;
    return true;
}
bool WateringRecordStore::cancelPreparedTask() {
    if (!writeTaskMarker(nullptr)) { taskReady_ = false; return false; }
    taskId_ = 0; startedEpoch_ = 0; taskReady_ = true;
    return true;
}
bool WateringRecordStore::recoverTask() {
    uint8_t bytes[kTaskMarkerBytes]{};
    const auto read = Esp32BaseConfig::readBlob("irrigation", "task", bytes, sizeof(bytes));
    if (read == Esp32BaseConfig::BlobReadResult::NotFound) return true;
    if (read != Esp32BaseConfig::BlobReadResult::Found || bytes[0] != 'I' || bytes[1] != 'T' || bytes[2] != 1 || bytes[3] > 1) return false;
    uint32_t storedCrc = 0;
    for (unsigned n = 0; n < 4; ++n) storedCrc |= uint32_t(bytes[sizeof(bytes) - 4 + n]) << (8 * n);
    if (storedCrc != taskCrc(bytes, sizeof(bytes) - 4)) return false;
    Esp32BaseRecordStore::StoreStatus status{};
    if (!store_.readStatus(status)) return false;
    // Explicit history initialization/clear changes generation. Do not resurrect
    // a task from the discarded generation.
    if (!bytes[3]) return true;
    if (memcmp(bytes + 4, status.storageGeneration, 16)) return cancelPreparedTask();
    WateringRecordPayload intent{};
    if (!WateringRecordCodec::decode(bytes + 20, WateringRecordCodec::kPayloadSize, intent) || !intent.taskId) return false;
    // No next task can start before this marker is cleared. The latest committed
    // payload therefore identifies this task even when a torn slot consumed an ID.
    struct Latest { uint32_t taskId = 0; } latest;
    auto remember = [](const StoredWateringRecord& record, void* user) {
        static_cast<Latest*>(user)->taskId = record.payload.taskId;
    };
    if (status.recordCount && !readLatest(0, 1, remember, &latest)) return false;
    if (latest.taskId != intent.taskId && !appendPayload(intent)) return false;
    return cancelPreparedTask();
}
