#include "WateringRecordStore.h"

#include <cstring>

#include <runtime/Esp32BaseTime.h>

#include "IrrigationRecordStoreRecovery.h"
#include "IrrigationStoredFact.h"
#include "IrrigationPlatform.h"

namespace {
constexpr uint32_t kCrcPolynomial = 0xedb88320U;


uint32_t markerCrc(const uint8_t* data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (kCrcPolynomial & (0U - (crc & 1U)));
    }
    return ~crc;
}

uint16_t resultTypeCode(WateringResult result) {
    switch (result) {
        case WateringResult::Completed:
            return IrrigationPlatform::FactWateringCompleted;
        case WateringResult::Stopped:
            return IrrigationPlatform::FactWateringStopped;
        default:
            return IrrigationPlatform::FactWateringFailed;
    }
}
}  // namespace

bool WateringRecordStore::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kRecordTypeName;
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = kStoredBytes;
    definition.retentionPolicy =
        Esp32BaseRecordStore::RetentionPolicy::PreserveUnreleased;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    pending_ = false;
    taskReady_ = false;
    startedEpoch_ = 0;
    if (!store_.begin(definition)) {
        if (!IrrigationRecordStoreRecovery::resetStructuralStore(
                store_, kRecordTypeName, kStoreVersion, definition)) {
            return false;
        }
    }
    if (!stream_.begin(millis())) return false;
    stream_.poll(millis(), nullptr, nullptr);  // bounded step toward Ready
    return recoverTask();
}

bool WateringRecordStore::captureStartTime(
    Esp32BaseRecordStore::RecordStartTime& startTime) const {
    return store_.captureStartTime(startTime);
}

bool WateringRecordStore::appendCompleted(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary) {
    if (!pending_) {
        const auto now = Esp32BaseTime::snapshot();
        if (!startTime.bootId || startTime.bootId != now.bootId ||
            startTime.uptimeSec > now.uptimeSec)
            return false;
        WateringRecordPayload payload{};
        if (!WateringRecordCodec::fromSession(summary, payload)) return false;
        payload.startedEpoch = startedEpoch_;
        const uint32_t durationSec = now.uptimeSec - startTime.uptimeSec;
        irrigation_fact::putDuration(pendingFact_, durationSec);
        if (!WateringRecordCodec::encode(
                payload, pendingFact_ + 4,
                WateringRecordCodec::kPayloadSize))
            return false;
        pendingObservedAt_ =
            now.synced ? uint64_t(now.epochSec) * 1000ULL
                       : iot_device::RecordStream::UnknownTime;
        pendingType_ = resultTypeCode(summary.result);
        pending_ = true;
    }
    if (!stream_.append(pendingType_, pendingObservedAt_, pendingFact_,
                        sizeof(pendingFact_)))
        return false;
    pending_ = false;
    // Caller keeps the finished task until the marker is cleared. A failed
    // cleanup never causes a second append, including after a reboot.
    return cancelPreparedTask();
}

bool WateringRecordStore::appendStartRejected(
    const WateringSessionSummary& summary,
    uint32_t startedEpoch) {
    if (summary.result != WateringResult::StartFailed ||
        stream_.state() != iot_device::StreamState::Ready) {
        return false;
    }
    WateringRecordPayload payload{};
    if (!WateringRecordCodec::fromSession(summary, payload)) return false;
    payload.startedEpoch = startedEpoch;
    uint8_t fact[kFactBytes]{};
    irrigation_fact::putDuration(fact, 0U);
    if (!WateringRecordCodec::encode(payload, fact + 4, WateringRecordCodec::kPayloadSize))
        return false;
    const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
    const uint64_t observedAtMs =
        now.synced ? uint64_t(now.epochSec) * 1000ULL
                   : iot_device::RecordStream::UnknownTime;
    return stream_.append(IrrigationPlatform::FactWateringFailed, observedAtMs,
                          fact, sizeof(fact));
}

bool WateringRecordStore::readLatest(uint32_t offset,
                                     uint32_t limit,
                                     ReadCallback callback,
                                     void* user) {
    if (!callback || limit == 0U) return false;
    ReadContext context;
    context.callback = callback;
    context.user = user;
    const bool read =
        store_.readLatest(offset, limit, scratch_, sizeof(scratch_),
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
bool WateringRecordStore::isWritable() const {
    return taskReady_ && store_.isWritable() &&
           stream_.state() == iot_device::StreamState::Ready;
}
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

bool WateringRecordStore::decodeFact(const uint8_t* bytes,
                                     std::size_t length,
                                     StoredWateringRecord& record) {
    iot_device::RecordFactView fact{};
    return irrigation_fact::decode(bytes, length, kFactBytes, record.timing,
                                   fact) &&
           fact.typeCode >= IrrigationPlatform::FactWateringCompleted &&
           fact.typeCode <= IrrigationPlatform::FactWateringFailed &&
           WateringRecordCodec::decode(fact.data + 4, fact.dataBytes - 4,
                                       record.payload);
}

// ---- task marker (reboot incomplete recovery) ----------------------------

bool WateringRecordStore::writeTaskMarker(const WateringTaskMarker& marker) {
    uint8_t bytes[sizeof(WateringTaskMarker) + 4]{};
    memcpy(bytes, &marker, sizeof(WateringTaskMarker));
    const uint32_t crc = markerCrc(bytes, sizeof(WateringTaskMarker));
    for (unsigned n = 0; n < 4; ++n)
        bytes[sizeof(WateringTaskMarker) + n] =
            uint8_t(crc >> (8 * n));
    return Esp32BaseConfig::setBlob("irrigation", "task", bytes, sizeof(bytes));
}

bool WateringRecordStore::prepareTask(const WateringRequest& request) {
    // Do NOT gate on isWritable(): it requires taskReady_, which is only set
    // by a successful prepareTask — that deadlocks every first start. Check
    // the underlying store and stream readiness directly.
    if (!store_.isWritable() || stream_.state() != iot_device::StreamState::Ready || taskReady_)
        return false;
    Esp32BaseRecordStore::StoreStatus status{};
    if (!store_.readStatus(status) || !status.nextRecordId) return false;
    WateringTaskMarker marker{};
    marker.active = 1;
    memcpy(marker.generation, status.storageGeneration, 16);
    marker.taskId = status.nextRecordId;
    const auto now = Esp32BaseTime::snapshot();
    marker.startedEpoch = now.synced ? now.epochSec : 0;
    marker.source = static_cast<uint8_t>(request.source);
    marker.targetMode = static_cast<uint8_t>(request.targetMode);
    marker.planId = request.planId;
    marker.stepCount = request.stepCount;
    memcpy(marker.commandId, request.commandId.data(),
           request.commandId.size());
    for (uint8_t i = 0; i < request.stepCount; ++i) {
        auto& target = marker.steps[i];
        target.zoneId = request.steps[i].zoneId;
        target.targetDurationSec = request.steps[i].targetDurationSec;
        target.targetWaterMl = request.steps[i].targetWaterMl;
    }
    if (!writeTaskMarker(marker)) {
        taskReady_ = false;
        return false;
    }
    taskReady_ = true;
    startedEpoch_ = marker.startedEpoch;
    return true;
}

bool WateringRecordStore::cancelPreparedTask() {
    if (!writeTaskMarker(WateringTaskMarker{})) {
        taskReady_ = false;
        return false;
    }
    taskReady_ = true;
    startedEpoch_ = 0;
    return true;
}

bool WateringRecordStore::resetCorruptTask() {
    WateringTaskMarker empty{};
    uint8_t out[sizeof(WateringTaskMarker) + 4]{};
    memcpy(out, &empty, sizeof(empty));
    const uint32_t crc = markerCrc(out, sizeof(empty));
    for (unsigned n = 0; n < 4; ++n) out[sizeof(empty) + n] = (crc >> (8 * n)) & 0xFF;
    if (!Esp32BaseConfig::setBlob("irrigation", "task", out, sizeof(out))) return false;
    taskReady_ = true;
    return true;
}

bool WateringRecordStore::recoverTask() {
    uint8_t bytes[sizeof(WateringTaskMarker) + 4]{};
    const auto read =
        Esp32BaseConfig::readBlob("irrigation", "task", bytes, sizeof(bytes));
    if (read == Esp32BaseConfig::BlobReadResult::NotFound) {
        taskReady_ = true;
        return true;
    }
    WateringTaskMarker marker{};
    uint32_t storedCrc = 0;
    for (unsigned n = 0; n < 4; ++n)
        storedCrc |= uint32_t(bytes[sizeof(WateringTaskMarker) + n]) << (8 * n);
    // A corrupted marker is almost always a write interrupted by a reset
    // (e.g. the former I2C-stall watchdog reset). Treat it like a stale
    // inactive task: seal an empty inactive marker and become ready instead
    // of refusing forever, which locked the device out of every watering.
    if (read != Esp32BaseConfig::BlobReadResult::Found ||
        bytes[0] != 'I' || bytes[1] != 'T' || bytes[2] != 2 || bytes[3] > 1 ||
        storedCrc != markerCrc(bytes, sizeof(WateringTaskMarker))) {
        return resetCorruptTask();
    }
    memcpy(&marker, bytes, sizeof(WateringTaskMarker));

    Esp32BaseRecordStore::StoreStatus status{};
    if (!store_.readStatus(status)) return false;
    if (!marker.active) {
        taskReady_ = true;
        return true;
    }
    if (memcmp(marker.generation, status.storageGeneration, 16))
        return cancelPreparedTask();

    // Rebuild the Incomplete fact the marker promised before the reboot.
    WateringRecordPayload intent{};
    intent.taskId = marker.taskId;
    intent.startedEpoch = marker.startedEpoch;
    intent.source = static_cast<WateringSource>(marker.source);
    intent.targetMode = static_cast<WateringTargetMode>(marker.targetMode);
    intent.planId = marker.planId;
    memcpy(intent.commandId.data(), marker.commandId,
           intent.commandId.size());
    intent.result = WateringResult::Incomplete;
    intent.stopReason = WateringStopReason::RebootInterrupted;
    for (uint8_t i = 0; i < marker.stepCount && i < BoardPins::kZoneCount;
         ++i) {
        const auto& step = marker.steps[i];
        if (!BoardPins::isValidZoneId(step.zoneId)) return false;
        auto& zone = intent.zones[step.zoneId - 1];
        zone.plannedDurationSec = uint16_t(step.targetDurationSec);
        zone.targetWaterMl = step.targetWaterMl;
        zone.flags = WateringRecordCodec::kZoneFlagUnknown;
    }
    if (!marker.taskId) return false;

    // No next task can start before this marker is cleared. The latest
    // committed payload identifies this task even when a torn slot consumed
    // an ID, preventing a duplicate Incomplete fact.
    struct Latest {
        uint32_t taskId = 0;
    } latest;
    auto remember = [](const StoredWateringRecord& record, void* user) {
        static_cast<Latest*>(user)->taskId = record.payload.taskId;
    };
    if (status.recordCount && !readLatest(0, 1, remember, &latest)) return false;
    if (latest.taskId != intent.taskId) {
        uint8_t fact[kFactBytes]{};
        irrigation_fact::putDuration(fact, 0);
        if (!WateringRecordCodec::encode(
                intent, fact + 4, WateringRecordCodec::kPayloadSize))
            return false;
        if (!stream_.append(IrrigationPlatform::FactWateringFailed,
                            iot_device::RecordStream::UnknownTime, fact,
                            sizeof(fact)))
            return false;
    }
    return cancelPreparedTask();
}
