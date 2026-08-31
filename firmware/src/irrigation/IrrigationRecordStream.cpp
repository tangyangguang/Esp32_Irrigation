#include "IrrigationRecordStream.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kPayloadMagic = 0x31525249UL;  // IRR1
constexpr uint16_t kPayloadVersion = 1;
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kKindOffset = 6;
constexpr std::size_t kObservedOffset = 8;
constexpr std::size_t kStartedOffset = 12;
constexpr std::size_t kCommandOffset = 16;
constexpr std::size_t kCommandBytes = IrrigationIotProtocol::kUuidBufferSize;
constexpr std::size_t kLengthOffset = kCommandOffset + kCommandBytes;
constexpr std::size_t kDataOffset = kLengthOffset + 2;
static_assert(IrrigationRecordStream::kPayloadSize - kDataOffset ==
                  IrrigationRecordStream::kDataSize,
              "record stream payload layout changed");
static_assert(WateringRecordCodec::kPayloadSize <=
                  IrrigationRecordStream::kDataSize,
              "watering payload exceeds record stream slot");

void writeU16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFFU);
    output[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void writeU32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFFU);
    output[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    output[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    output[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint16_t readU16(const uint8_t* input) {
    return static_cast<uint16_t>(input[0]) |
           static_cast<uint16_t>(input[1]) << 8U;
}

uint32_t readU32(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           static_cast<uint32_t>(input[1]) << 8U |
           static_cast<uint32_t>(input[2]) << 16U |
           static_cast<uint32_t>(input[3]) << 24U;
}

uint32_t crc32(const uint8_t* data, std::size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^
                  (0xEDB88320UL & (0U - static_cast<uint32_t>(crc & 1U)));
        }
    }
    return ~crc;
}

void makeUuid(char* output, std::size_t length) {
    uint8_t bytes[16]{};
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3FU) | 0x80U);
    std::snprintf(output,
                  length,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                  "%02x%02x%02x%02x%02x%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                  bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                  bytes[12], bytes[13], bytes[14], bytes[15]);
}

struct ReadContext {
    uint32_t target = 0;
    IrrigationRecordStream::Record* output = nullptr;
    bool found = false;
};

}  // namespace

IrrigationRecordStream& IrrigationRecordStream::instance() {
    static IrrigationRecordStream stream;
    return stream;
}

bool IrrigationRecordStream::begin() {
    if (ready_) return true;
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = "irrigation-iot";
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = kPayloadSize;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    if (!store_.begin(definition)) return false;

    Esp32BaseRecordStore::StoreStatus status;
    if (!store_.readStatus(status) || status.capacity != kCapacity ||
        status.state != Esp32BaseRecordStore::StoreState::Ready ||
        status.damagedRecordCount != 0U ||
        !preferences_.begin(kNamespace, false) ||
        !loadMetadata(status.recordCount)) {
        return false;
    }
    if (!registered_) {
#if ESP32BASE_ENABLE_WEB
        registered_ = Esp32BaseWeb::registerBusinessRecordStore(store_);
#else
        registered_ = true;
#endif
    }
    if (!registered_) return false;
    if (status.nextRecordId == 0U ||
        acknowledgedInRam_ >= status.nextRecordId ||
        !rebuildWateringIndex(status.recordCount)) {
        return false;
    }
    lastCheckpointMs_ = millis();
    ready_ = true;
    return true;
}

bool IrrigationRecordStream::appendWatering(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary,
    const char* relatedCommandId) {
    if (!ready_ || summary.purpose != WateringPurpose::Normal) return false;
    uint32_t startedAtEpoch = 0;
    const Esp32BaseTime::Snapshot completed = Esp32BaseTime::snapshot();
    if (!completed.synced || completed.epochSec == 0U ||
        !Esp32BaseTime::resolveCurrentBootEvent(startTime.bootId,
                                               startTime.uptimeSec,
                                               &startedAtEpoch) ||
        startedAtEpoch == 0U || startedAtEpoch > completed.epochSec) {
        return false;
    }
    if (!canAppend(summary.source == WateringSource::AutomaticPlan ? 2U : 1U)) {
        return false;
    }
    WateringRecordPayload watering{};
    constexpr std::size_t kBaselineBytes = BoardPins::kZoneCount * 4U;
    constexpr std::size_t kIotWateringBytes =
        WateringRecordCodec::kPayloadSize + kBaselineBytes +
        kObjectNameCapacity;
    uint8_t encoded[kIotWateringBytes]{};
    if (!WateringRecordCodec::fromSession(summary, watering) ||
        !WateringRecordCodec::encode(watering, encoded,
                                     WateringRecordCodec::kPayloadSize)) {
        return false;
    }
    uint8_t* baseline = encoded + WateringRecordCodec::kPayloadSize;
    for (uint8_t zoneId = 1; zoneId <= BoardPins::kZoneCount; ++zoneId) {
        uint32_t pulseRate = 0;
        for (uint8_t index = 0; index < summary.zoneCount; ++index) {
            if (summary.zones[index].zoneId == zoneId &&
                summary.zones[index].result != ZoneWateringResult::NotStarted &&
                summary.zones[index].flowBaselineAvailable) {
                pulseRate = summary.zones[index].baselinePulseRateX10000;
                break;
            }
        }
        writeU32(baseline, pulseRate);
        baseline += 4;
    }
    std::memcpy(encoded + WateringRecordCodec::kPayloadSize + kBaselineBytes,
                summary.planName.data(), summary.planName.size());
    if (!appendEncoded(RecordKind::Watering,
                       completed.epochSec,
                       startedAtEpoch,
                       relatedCommandId,
                       encoded,
                       sizeof(encoded),
                       &startTime)) {
        return false;
    }
    if (summary.source != WateringSource::AutomaticPlan) return true;

    char startedAt[25]{};
    char endedAt[25]{};
    if (!IrrigationIotProtocol::formatTimestamp(startedAtEpoch, 0, startedAt,
                                                sizeof(startedAt)) ||
        !IrrigationIotProtocol::formatTimestamp(completed.epochSec, 0, endedAt,
                                                sizeof(endedAt))) {
        return false;
    }
    JsonDocument operation;
    operation["actionKey"] = "automatic.plan-run";
    operation["sourceKey"] = "device_schedule";
    operation["status"] = summary.result == WateringResult::Completed
                              ? "succeeded"
                              : summary.result == WateringResult::Stopped
                                    ? "canceled"
                                    : "failed";
    operation["reason"] =
        summary.stopReason == WateringStopReason::Completed
            ? "completed"
            : summary.stopReason == WateringStopReason::UserStopped
                  ? "user_stopped"
                  : summary.stopReason == WateringStopReason::FlowStartTimeout
                        ? "flow_start_timeout"
                        : summary.stopReason == WateringStopReason::NoFlowTimeout
                              ? "no_flow_timeout"
                              : summary.stopReason == WateringStopReason::LowFlow
                                    ? "low_flow"
                                    : summary.stopReason == WateringStopReason::HighFlow
                                          ? "high_flow"
                                          : summary.stopReason ==
                                                    WateringStopReason::TargetVolumeTimeout
                                                ? "target_volume_timeout"
                                                : "hardware_failure";
    operation["startedAt"] = startedAt;
    operation["endedAt"] = endedAt;
    operation["durationSeconds"] = completed.epochSec - startedAtEpoch;
    JsonObject parameters = operation["parameters"].to<JsonObject>();
    parameters["planId"] = summary.planId;
    parameters["planName"] = summary.planName.data();
    char json[512]{};
    const std::size_t jsonLength = serializeJson(operation, json, sizeof(json));
    return jsonLength != 0U && jsonLength < sizeof(json) &&
           appendJson("operation.automatic-run.completed", json,
                      completed.epochSec);
}

bool IrrigationRecordStream::appendJson(const char* eventKey,
                                        const char* dataJson,
                                        uint32_t observedAtEpoch) {
    if (!eventKey || eventKey[0] == '\0' || std::strlen(eventKey) >= 48U ||
        !dataJson) {
        return false;
    }
    if (observedAtEpoch == 0U) {
        const Esp32BaseTime::Snapshot now = Esp32BaseTime::snapshot();
        observedAtEpoch = now.synced ? now.epochSec : 0U;
    }
    const std::size_t keyLength = std::strlen(eventKey);
    const std::size_t jsonLength = std::strlen(dataJson);
    if (keyLength + 1U + jsonLength > kDataSize) return false;
    uint8_t data[kDataSize]{};
    std::memcpy(data, eventKey, keyLength + 1U);
    std::memcpy(data + keyLength + 1U, dataJson, jsonLength);
    return appendEncoded(RecordKind::Json,
                         observedAtEpoch,
                         0,
                         nullptr,
                         data,
                         static_cast<uint16_t>(keyLength + 1U + jsonLength),
                         nullptr);
}

bool IrrigationRecordStream::appendEncoded(
    RecordKind kind,
    uint32_t observedAtEpoch,
    uint32_t startedAtEpoch,
    const char* relatedCommandId,
    const uint8_t* data,
    uint16_t dataLength,
    const Esp32BaseRecordStore::RecordStartTime* startTime) {
    if (!ready_ || !data || dataLength == 0U || dataLength > kDataSize ||
        !canAppend() ||
        !encodePayload(kind, observedAtEpoch, startedAtEpoch, relatedCommandId,
                       data, dataLength, scratch_, sizeof(scratch_))) {
        return false;
    }
    const bool appended = startTime
                              ? store_.appendCompleted(*startTime, scratch_,
                                                       sizeof(scratch_))
                              : store_.appendInstant(scratch_, sizeof(scratch_));
    if (!appended) return false;
    if (!updateWateringIndexAfterAppend(kind == RecordKind::Watering)) {
        ready_ = false;
        return false;
    }
    backlogFull_ = false;
    return true;
}

bool IrrigationRecordStream::canAppend(uint8_t requiredSlots) {
    Esp32BaseRecordStore::StoreStatus status;
    if (requiredSlots == 0U || requiredSlots > 10U ||
        !store_.readStatus(status) || !status.writable ||
        status.state != Esp32BaseRecordStore::StoreState::Ready ||
        status.damagedRecordCount != 0U ||
        status.nextRecordId >
            kLocalWateringRecordIdMask - requiredSlots) {
        return false;
    }
    if (status.recordCount <= kCapacity - requiredSlots) {
        backlogFull_ = false;
        return true;
    }
    // A full store rotates an entire 10-slot segment. Every record removed by
    // the append group must already be cumulatively acknowledged before the
    // first immutable fact is written.
    const uint32_t rotationEnd = status.oldestRecordId + 9U;
    backlogFull_ = acknowledgedInRam_ < rotationEnd;
    return !backlogFull_;
}

bool IrrigationRecordStream::readOldestPending(Record& record) {
    record = {};
    if (!ready_) return false;
    Esp32BaseRecordStore::StoreStatus status;
    if (!store_.readStatus(status) || status.recordCount == 0U ||
        acknowledgedInRam_ >= status.nextRecordId - 1U) {
        return false;
    }
    const uint32_t target =
        std::max(status.oldestRecordId, acknowledgedInRam_ + 1U);
    return readRecord(target, record);
}

bool IrrigationRecordStream::readLatestWatering(uint32_t offset,
                                                  WateringView& record) {
    record = {};
    if (!ready_ || offset >= wateringSequenceCount_) return false;
    const uint32_t sequence =
        wateringSequences_[wateringSequenceCount_ - 1U - offset];
    return readWateringByLocalId(sequence | kLocalWateringRecordIdMask,
                                 record) ==
           Esp32BaseRecordStore::RecordReadResult::Found;
}

Esp32BaseRecordStore::RecordReadResult
IrrigationRecordStream::readWateringByLocalId(uint32_t localRecordId,
                                               WateringView& record) {
    record = {};
    if (!ready_ ||
        (localRecordId & kLocalWateringRecordIdMask) == 0U) {
        return Esp32BaseRecordStore::RecordReadResult::InvalidArgument;
    }
    const uint32_t sequence = localRecordId & ~kLocalWateringRecordIdMask;
    Record decoded;
    Esp32BaseRecordStore::RecordMetadata metadata;
    const Esp32BaseRecordStore::RecordReadResult result =
        store_.readById(sequence, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) return result;
    if (!decodePayload(scratch_, sizeof(scratch_), decoded) ||
        decoded.kind != RecordKind::Watering ||
        !WateringRecordCodec::decode(decoded.data,
                                     WateringRecordCodec::kPayloadSize,
                                     record.payload)) {
        return Esp32BaseRecordStore::RecordReadResult::Corrupt;
    }
    record.localRecordId = localRecordId;
    record.timing = metadata.timing;
    return Esp32BaseRecordStore::RecordReadResult::Found;
}

uint32_t IrrigationRecordStream::wateringRecordCount() {
    return ready_ ? wateringSequenceCount_ : 0U;
}

bool IrrigationRecordStream::readRecord(uint32_t sequence, Record& record) {
    Esp32BaseRecordStore::RecordMetadata metadata;
    const Esp32BaseRecordStore::RecordReadResult result =
        store_.readById(sequence, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found ||
        !decodePayload(scratch_, sizeof(scratch_), record)) {
        return false;
    }
    record.sequence = sequence;
    return true;
}

bool IrrigationRecordStream::acknowledge(
    const IrrigationIotProtocol::RecordAck& ack,
    uint32_t nowMs) {
    if (!ready_ || std::strcmp(ack.recordStreamId, metadata_.streamId) != 0) {
        return false;
    }
    Esp32BaseRecordStore::StoreStatus status;
    if (!store_.readStatus(status) ||
        ack.acknowledgedThroughSequence >= status.nextRecordId) {
        return false;
    }
    if (ack.acknowledgedThroughSequence <= acknowledgedInRam_) return true;
    acknowledgedInRam_ = ack.acknowledgedThroughSequence;
    checkpointDirty_ = acknowledgedInRam_ > metadata_.acknowledgedThroughSequence;
    if (acknowledgedInRam_ - metadata_.acknowledgedThroughSequence >=
        kAckCheckpointBatch) {
        if (!saveMetadata()) {
            ready_ = false;
            return false;
        }
        lastCheckpointMs_ = nowMs;
    }
    backlogFull_ = false;
    return true;
}

bool IrrigationRecordStream::resetGenerationAfterFormat() {
    Esp32BaseRecordStore::StoreStatus status;
    if (!registered_ || !store_.readStatus(status) || !status.ready ||
        status.recordCount != 0U || !preferences_.remove(kMetadataKey)) {
        ready_ = false;
        return false;
    }
    metadata_ = {};
    acknowledgedInRam_ = 0;
    wateringSequenceCount_ = 0;
    std::memset(wateringSequences_, 0, sizeof(wateringSequences_));
    checkpointDirty_ = false;
    backlogFull_ = false;
    if (!loadMetadata(0)) {
        ready_ = false;
        return false;
    }
    lastCheckpointMs_ = millis();
    ready_ = true;
    return true;
}

void IrrigationRecordStream::handle(uint32_t nowMs) {
    if (!ready_ || !checkpointDirty_ ||
        static_cast<uint32_t>(nowMs - lastCheckpointMs_) <
            kAckCheckpointIntervalMs) {
        return;
    }
    if (!saveMetadata()) {
        ready_ = false;
        return;
    }
    lastCheckpointMs_ = nowMs;
}

bool IrrigationRecordStream::rebuildWateringIndex(uint32_t recordCount) {
    wateringSequenceCount_ = 0;
    std::memset(wateringSequences_, 0, sizeof(wateringSequences_));
    if (recordCount == 0U) return true;
    struct Context {
        uint32_t* sequences;
        uint16_t count;
        bool failed;
    } context{wateringSequences_, 0, false};
    auto callback = [](const Esp32BaseRecordStore::RecordView& view, void* user) {
        auto* context = static_cast<Context*>(user);
        if (!context || context->failed ||
            view.payloadSizeBytes != kPayloadSize) {
            if (context) context->failed = true;
            return;
        }
        Record decoded;
        if (!decodePayload(view.payload, view.payloadSizeBytes, decoded)) {
            context->failed = true;
            return;
        }
        if (decoded.kind == RecordKind::Watering) {
            if (context->count >= kCapacity) {
                context->failed = true;
                return;
            }
            context->sequences[context->count++] = view.recordId;
        }
    };
    if (!store_.readLatest(0, recordCount, scratch_, sizeof(scratch_), callback,
                           &context) ||
        context.failed) {
        wateringSequenceCount_ = 0;
        return false;
    }
    for (uint16_t left = 0, right = context.count == 0U ? 0U : context.count - 1U;
         left < right; ++left, --right) {
        const uint32_t value = wateringSequences_[left];
        wateringSequences_[left] = wateringSequences_[right];
        wateringSequences_[right] = value;
    }
    wateringSequenceCount_ = context.count;
    return true;
}

bool IrrigationRecordStream::updateWateringIndexAfterAppend(
    bool appendedWatering) {
    Esp32BaseRecordStore::StoreStatus status;
    if (!store_.readStatus(status)) return false;
    uint16_t keepFrom = 0;
    while (keepFrom < wateringSequenceCount_ &&
           wateringSequences_[keepFrom] < status.oldestRecordId) {
        ++keepFrom;
    }
    if (keepFrom != 0U) {
        const uint16_t remaining = wateringSequenceCount_ - keepFrom;
        std::memmove(wateringSequences_, wateringSequences_ + keepFrom,
                     remaining * sizeof(wateringSequences_[0]));
        std::memset(wateringSequences_ + remaining, 0,
                    keepFrom * sizeof(wateringSequences_[0]));
        wateringSequenceCount_ = remaining;
    }
    if (!appendedWatering) return true;
    if (wateringSequenceCount_ >= kCapacity ||
        status.newestRecordId >= kLocalWateringRecordIdMask) {
        return false;
    }
    wateringSequences_[wateringSequenceCount_++] = status.newestRecordId;
    return true;
}

bool IrrigationRecordStream::loadMetadata(uint32_t recordCount) {
    const std::size_t length = preferences_.getBytesLength(kMetadataKey);
    if (length == 0U) {
        if (recordCount != 0U) return false;
        metadata_ = {};
        metadata_.magic = kMagic;
        metadata_.version = kVersion;
        makeUuid(metadata_.streamId, sizeof(metadata_.streamId));
        acknowledgedInRam_ = 0;
        return saveMetadata();
    }
    if (length != sizeof(metadata_) ||
        preferences_.getBytes(kMetadataKey, &metadata_, sizeof(metadata_)) !=
            sizeof(metadata_) ||
        metadata_.magic != kMagic || metadata_.version != kVersion ||
        !IrrigationIotProtocol::isValidUuid(metadata_.streamId) ||
        metadata_.crc32 != calculateMetadataCrc(metadata_)) {
        return false;
    }
    acknowledgedInRam_ = metadata_.acknowledgedThroughSequence;
    checkpointDirty_ = false;
    return true;
}

bool IrrigationRecordStream::saveMetadata() {
    metadata_.magic = kMagic;
    metadata_.version = kVersion;
    metadata_.reserved = 0;
    metadata_.acknowledgedThroughSequence = acknowledgedInRam_;
    metadata_.crc32 = calculateMetadataCrc(metadata_);
    const bool saved = preferences_.putBytes(kMetadataKey, &metadata_,
                                             sizeof(metadata_)) ==
                       sizeof(metadata_);
    if (saved) checkpointDirty_ = false;
    return saved;
}

bool IrrigationRecordStream::decodePayload(const uint8_t* payload,
                                           std::size_t length,
                                           Record& record) {
    record = {};
    if (!payload || length != kPayloadSize ||
        readU32(payload + kMagicOffset) != kPayloadMagic ||
        readU16(payload + kVersionOffset) != kPayloadVersion) {
        return false;
    }
    const uint8_t rawKind = payload[kKindOffset];
    if (rawKind != static_cast<uint8_t>(RecordKind::Watering) &&
        rawKind != static_cast<uint8_t>(RecordKind::Json)) {
        return false;
    }
    const uint16_t dataLength = readU16(payload + kLengthOffset);
    if (dataLength == 0U || dataLength > kDataSize) return false;
    record.kind = static_cast<RecordKind>(rawKind);
    record.observedAtEpoch = readU32(payload + kObservedOffset);
    record.startedAtEpoch = readU32(payload + kStartedOffset);
    std::memcpy(record.relatedCommandId, payload + kCommandOffset, kCommandBytes);
    record.relatedCommandId[kCommandBytes - 1U] = '\0';
    const uint8_t* storedData = payload + kDataOffset;
    if (record.kind == RecordKind::Json) {
        const void* terminator = std::memchr(storedData, '\0', dataLength);
        if (!terminator) return false;
        const std::size_t keyLength =
            static_cast<const uint8_t*>(terminator) - storedData;
        if (keyLength == 0U || keyLength >= sizeof(record.eventKey)) return false;
        std::memcpy(record.eventKey, storedData, keyLength);
        const std::size_t jsonOffset = keyLength + 1U;
        record.dataLength = static_cast<uint16_t>(dataLength - jsonOffset);
        std::memcpy(record.data, storedData + jsonOffset, record.dataLength);
    } else {
        if (dataLength != WateringRecordCodec::kPayloadSize +
                              BoardPins::kZoneCount * 4U +
                              kObjectNameCapacity) {
            return false;
        }
        record.dataLength = dataLength;
        std::memcpy(record.data, storedData, dataLength);
    }
    return true;
}

bool IrrigationRecordStream::encodePayload(
    RecordKind kind,
    uint32_t observedAtEpoch,
    uint32_t startedAtEpoch,
    const char* relatedCommandId,
    const uint8_t* data,
    uint16_t dataLength,
    uint8_t* payload,
    std::size_t payloadLength) {
    if (!payload || payloadLength != kPayloadSize || !data ||
        dataLength == 0U || dataLength > kDataSize) {
        return false;
    }
    if (relatedCommandId && relatedCommandId[0] != '\0' &&
        !IrrigationIotProtocol::isValidUuid(relatedCommandId)) {
        return false;
    }
    std::memset(payload, 0, payloadLength);
    writeU32(payload + kMagicOffset, kPayloadMagic);
    writeU16(payload + kVersionOffset, kPayloadVersion);
    payload[kKindOffset] = static_cast<uint8_t>(kind);
    writeU32(payload + kObservedOffset, observedAtEpoch);
    writeU32(payload + kStartedOffset, startedAtEpoch);
    if (relatedCommandId) {
        std::memcpy(payload + kCommandOffset, relatedCommandId,
                    std::strlen(relatedCommandId));
    }
    writeU16(payload + kLengthOffset, dataLength);
    std::memcpy(payload + kDataOffset, data, dataLength);
    return true;
}

uint32_t IrrigationRecordStream::calculateMetadataCrc(
    const Metadata& metadata) {
    return crc32(reinterpret_cast<const uint8_t*>(&metadata),
                 offsetof(Metadata, crc32));
}

bool IrrigationRecordStream::ready() const { return ready_; }
bool IrrigationRecordStream::writable() {
    return ready_ && canAppend();
}
bool IrrigationRecordStream::backlogFull() const { return backlogFull_; }
const char* IrrigationRecordStream::streamId() const { return metadata_.streamId; }
uint32_t IrrigationRecordStream::acknowledgedThroughSequence() const {
    return acknowledgedInRam_;
}
uint32_t IrrigationRecordStream::pendingCount() const {
    if (!ready_) return 0;
    Esp32BaseRecordStore::StoreStatus status;
    if (!store_.readStatus(status) || status.nextRecordId == 0U) return 0;
    const uint32_t newest = status.nextRecordId - 1U;
    return newest > acknowledgedInRam_ ? newest - acknowledgedInRam_ : 0U;
}
Esp32BaseRecordStore& IrrigationRecordStream::baseStore() { return store_; }
