#include "WateringRecordStore.h"

#include "IrrigationRecordStream.h"

bool WateringRecordStore::begin() {
    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = kRecordTypeName;
    definition.storeVersion = kStoreVersion;
    definition.payloadSizeBytes = WateringRecordCodec::kPayloadSize;
    definition.maximumStoreBytes = kMaximumStoreBytes;
    definition.minimumFileSystemFreeBytes = kMinimumFileSystemFreeBytes;
    return store_.begin(definition);
}

Esp32BaseRecordStore& WateringRecordStore::baseStore() {
    return store_;
}

bool WateringRecordStore::captureStartTime(
    Esp32BaseRecordStore::RecordStartTime& startTime) const {
    return store_.captureStartTime(startTime);
}

bool WateringRecordStore::appendCompleted(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary) {
    WateringRecordPayload payload{};
    uint8_t encoded[WateringRecordCodec::kPayloadSize]{};
    return WateringRecordCodec::fromSession(summary, payload) &&
           WateringRecordCodec::encode(payload, encoded, sizeof(encoded)) &&
           store_.appendCompleted(startTime, encoded, sizeof(encoded));
}

bool WateringRecordStore::readLatest(uint32_t offset,
                                     uint32_t limit,
                                     ReadCallback callback,
                                     void* user) {
    if (!callback || limit == 0U) return false;
    Esp32BaseRecordStore::StoreStatus localStatus;
    if (!store_.readStatus(localStatus)) return false;
    IrrigationRecordStream& stream = IrrigationRecordStream::instance();
    const uint32_t streamCount = stream.wateringRecordCount();
    uint32_t localOffset = 0;
    uint32_t streamOffset = 0;
    uint32_t mergedOffset = 0;
    uint32_t emitted = 0;
    while (emitted < limit &&
           (localOffset < localStatus.recordCount || streamOffset < streamCount)) {
        StoredWateringRecord local{};
        IrrigationRecordStream::WateringView remote{};
        bool haveLocal = false;
        bool haveRemote = false;
        if (localOffset < localStatus.recordCount) {
            struct Capture {
                StoredWateringRecord* output;
                bool decoded;
            } capture{&local, false};
            auto adapter = [](const Esp32BaseRecordStore::RecordView& view,
                              void* contextValue) {
                auto* capture = static_cast<Capture*>(contextValue);
                if (!capture || view.payloadSizeBytes !=
                                    WateringRecordCodec::kPayloadSize ||
                    !WateringRecordCodec::decode(view.payload,
                                                 view.payloadSizeBytes,
                                                 capture->output->payload)) {
                    return;
                }
                capture->output->recordId = view.recordId;
                capture->output->timing = view.timing;
                capture->decoded = true;
            };
            haveLocal = store_.readLatest(localOffset, 1, scratch_,
                                          sizeof(scratch_), adapter, &capture) &&
                        capture.decoded;
            if (!haveLocal) return false;
        }
        if (streamOffset < streamCount) {
            haveRemote = stream.readLatestWatering(streamOffset, remote);
            if (!haveRemote) return false;
        }
        bool selectRemote = haveRemote && !haveLocal;
        if (haveRemote && haveLocal) {
            uint32_t localEpoch = 0;
            uint32_t remoteEpoch = 0;
            const bool localTimed = Esp32BaseRecordStore::resolveCompletedEpoch(
                local.timing, localEpoch);
            const bool remoteTimed = Esp32BaseRecordStore::resolveCompletedEpoch(
                remote.timing, remoteEpoch);
            if (localTimed && remoteTimed) {
                selectRemote = remoteEpoch >= localEpoch;
            } else if (remoteTimed != localTimed) {
                selectRemote = remoteTimed;
            } else {
                selectRemote = remote.timing.completedUptimeSec >=
                               local.timing.completedUptimeSec;
            }
        }
        if (mergedOffset++ >= offset) {
            if (selectRemote) {
                StoredWateringRecord selected{};
                selected.recordId = remote.localRecordId;
                selected.timing = remote.timing;
                selected.payload = remote.payload;
                callback(selected, user);
            } else {
                callback(local, user);
            }
            ++emitted;
        }
        if (selectRemote) ++streamOffset;
        else ++localOffset;
    }
    return true;
}

Esp32BaseRecordStore::RecordReadResult WateringRecordStore::readById(
    uint32_t recordId,
    StoredWateringRecord& record) {
    record = {};
    if ((recordId & IrrigationRecordStream::kLocalWateringRecordIdMask) != 0U) {
        IrrigationRecordStream::WateringView streamed;
        const Esp32BaseRecordStore::RecordReadResult result =
            IrrigationRecordStream::instance().readWateringByLocalId(recordId,
                                                                      streamed);
        if (result == Esp32BaseRecordStore::RecordReadResult::Found) {
            record.recordId = streamed.localRecordId;
            record.timing = streamed.timing;
            record.payload = streamed.payload;
        }
        return result;
    }
    Esp32BaseRecordStore::RecordMetadata metadata;
    const Esp32BaseRecordStore::RecordReadResult result =
        store_.readById(recordId, scratch_, sizeof(scratch_), metadata);
    if (result != Esp32BaseRecordStore::RecordReadResult::Found) {
        return result;
    }
    if (!WateringRecordCodec::decode(scratch_, sizeof(scratch_), record.payload)) {
        return Esp32BaseRecordStore::RecordReadResult::Corrupt;
    }
    record.recordId = metadata.recordId;
    record.timing = metadata.timing;
    return Esp32BaseRecordStore::RecordReadResult::Found;
}

bool WateringRecordStore::readStatus(Esp32BaseRecordStore::StoreStatus& status) const {
    if (!store_.readStatus(status)) return false;
    IrrigationRecordStream& stream = IrrigationRecordStream::instance();
    if (stream.ready()) {
        const uint32_t streamCount = stream.wateringRecordCount();
        status.recordCount += streamCount;
        status.capacity += IrrigationRecordStream::kCapacity;
        status.writable = status.writable && stream.writable();
    }
    return true;
}

bool WateringRecordStore::isReady() const {
    return store_.isReady();
}

bool WateringRecordStore::isWritable() const {
    return store_.isWritable();
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

void WateringRecordStore::readAdapter(const Esp32BaseRecordStore::RecordView& view,
                                      void* user) {
    ReadContext* context = static_cast<ReadContext*>(user);
    StoredWateringRecord record{};
    if (!context || view.payloadSizeBytes != WateringRecordCodec::kPayloadSize ||
        !WateringRecordCodec::decode(view.payload, view.payloadSizeBytes, record.payload)) {
        if (context) {
            context->decodeFailed = true;
        }
        return;
    }
    record.recordId = view.recordId;
    record.timing = view.timing;
    context->callback(record, context->user);
}
