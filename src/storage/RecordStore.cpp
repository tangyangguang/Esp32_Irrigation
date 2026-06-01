#include "storage/RecordStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stddef.h>

#include "domain/BusinessEventLog.h"

namespace {

static constexpr const char* kPath = "/irr_records.bin";
static constexpr const char* kNamespace = "irr_rec";
static constexpr const char* kKeyInitialized = "init";
static constexpr const char* kKeyMeta = "meta";
static constexpr uint32_t kMagic = 0x49525245UL;
static constexpr uint32_t kCommitMagic = 0x4952434DUL;
static constexpr uint32_t kMetaMagic = 0x4952524DUL;
static constexpr uint16_t kVersion = 5;
static constexpr uint16_t kMetaVersion = 1;

struct StoreMeta {
    uint32_t magic;
    uint16_t version;
    uint16_t head;
    uint16_t count;
    uint16_t reserved;
    uint32_t nextId;
};

static_assert(sizeof(StoreMeta) == 16, "RecordStore::StoreMeta binary layout changed");

uint16_t g_head = 0;
uint16_t g_count = 0;
uint32_t g_nextId = 1;
bool g_ready = false;

uint32_t fileSizeBytes() {
    return static_cast<uint32_t>(sizeof(RecordStore::WateringRecord)) * RecordStore::Capacity;
}

StoreMeta makeMeta() {
    StoreMeta meta = {};
    meta.magic = kMetaMagic;
    meta.version = kMetaVersion;
    meta.head = g_head;
    meta.count = g_count;
    meta.nextId = g_nextId == 0 ? 1 : g_nextId;
    return meta;
}

bool validMeta(const StoreMeta& meta) {
    return meta.magic == kMetaMagic &&
           meta.version == kMetaVersion &&
           meta.head < RecordStore::Capacity &&
           meta.count <= RecordStore::Capacity &&
           meta.nextId != 0;
}

bool saveMeta() {
    const StoreMeta meta = makeMeta();
    return Esp32BaseConfig::setPod(kNamespace, kKeyMeta, meta);
}

uint32_t crc32Record(const RecordStore::WateringRecord& record) {
    RecordStore::WateringRecord copy = record;
    copy.commitMagic = 0;
    copy.crc32 = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&copy);
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < sizeof(copy); ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return ~crc;
}

bool loadMeta() {
    StoreMeta meta = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKeyMeta, meta) && validMeta(meta)) {
        g_head = meta.head;
        g_count = meta.count;
        g_nextId = meta.nextId;
        return true;
    }
    g_head = 0;
    g_count = 0;
    g_nextId = 1;
    return false;
}

bool createEmptyStore() {
    return Esp32BaseFs::createFixedFile(kPath, fileSizeBytes(), 0);
}

bool ensureStoreFile() {
    if (!Esp32BaseFs::isReady()) {
        return false;
    }
    if (Esp32BaseConfig::getInt(kNamespace, kKeyInitialized, 0) != 1) {
        const bool created = createEmptyStore();
        if (created) {
            (void)Esp32BaseConfig::setInt(kNamespace, kKeyInitialized, 1);
        }
        return created;
    }
    const int64_t size = Esp32BaseFs::fileSize(kPath);
    if (size == static_cast<int64_t>(fileSizeBytes())) {
        return true;
    }
    const bool created = createEmptyStore();
    if (created) {
        (void)Esp32BaseConfig::setInt(kNamespace, kKeyInitialized, 1);
    }
    return created;
}

bool readAtIndex(uint16_t index, RecordStore::WateringRecord* record) {
    if (!record || index >= RecordStore::Capacity) {
        return false;
    }
    size_t readLen = 0;
    const uint32_t offset = static_cast<uint32_t>(index) * sizeof(RecordStore::WateringRecord);
    if (!Esp32BaseFs::readBytesAt(kPath, offset, reinterpret_cast<uint8_t*>(record), sizeof(*record), &readLen)) {
        return false;
    }
    if (readLen != sizeof(*record) ||
        record->magic != kMagic ||
        record->version != kVersion ||
        record->size != sizeof(*record) ||
        record->recordId == 0 ||
        record->commitMagic != kCommitMagic) {
        return false;
    }
    return record->crc32 == crc32Record(*record);
}

bool recoverMetaFromRecords(bool metaValid) {
    const uint16_t oldHead = g_head;
    const uint16_t oldCount = g_count;
    const uint32_t oldNextId = g_nextId;

    uint16_t recoveredCount = 0;
    uint32_t maxRecordId = 0;
    uint16_t maxRecordIndex = 0;
    for (uint16_t index = 0; index < RecordStore::Capacity; ++index) {
        RecordStore::WateringRecord record = {};
        if (!readAtIndex(index, &record)) {
            continue;
        }
        ++recoveredCount;
        if (record.recordId > maxRecordId) {
            maxRecordId = record.recordId;
            maxRecordIndex = index;
        }
    }

    if (maxRecordId == 0) {
        g_head = 0;
        g_count = 0;
        g_nextId = 1;
    } else {
        g_head = static_cast<uint16_t>((maxRecordIndex + 1U) % RecordStore::Capacity);
        g_count = recoveredCount > RecordStore::Capacity ? RecordStore::Capacity : recoveredCount;
        g_nextId = maxRecordId + 1U;
        if (g_nextId == 0) {
            g_nextId = 1;
        }
    }

    const bool changed = !metaValid || oldHead != g_head || oldCount != g_count || oldNextId != g_nextId;
    if (!changed) {
        return true;
    }
    if (!saveMeta()) {
        BusinessEventLog::appendRecordMetaSaveFailed(maxRecordId, maxRecordIndex);
        return false;
    }
    BusinessEventLog::appendRecordStoreRecovered(g_count, g_nextId);
    return true;
}

}

namespace RecordStore {

void begin() {
    g_ready = ensureStoreFile();
    if (!g_ready) {
        ESP32BASE_LOG_W("records", "record store not ready");
        return;
    }
    const bool metaValid = loadMeta();
    if (!recoverMetaFromRecords(metaValid)) {
        ESP32BASE_LOG_W("records", "record meta recovery save failed head=%u count=%u next=%lu",
                        static_cast<unsigned>(g_head),
                        static_cast<unsigned>(g_count),
                        static_cast<unsigned long>(g_nextId));
    }
}

bool append(const WateringRecord& record) {
    if (!g_ready && !ensureStoreFile()) {
        return false;
    }
    g_ready = true;
    WateringRecord stored = record;
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.recordId = g_nextId;
    stored.commitMagic = 0;
    stored.crc32 = crc32Record(stored);
    const uint16_t writtenSlot = g_head;
    const uint32_t offset = static_cast<uint32_t>(writtenSlot) * sizeof(stored);
    const uint32_t commitOffset = offset + offsetof(RecordStore::WateringRecord, commitMagic);
    uint32_t pendingCommit = 0;
    if (!Esp32BaseFs::writeBytesAt(kPath, commitOffset, reinterpret_cast<const uint8_t*>(&pendingCommit), sizeof(pendingCommit))) {
        return false;
    }
    if (!Esp32BaseFs::writeBytesAt(kPath, offset, reinterpret_cast<const uint8_t*>(&stored), sizeof(stored))) {
        return false;
    }
    const uint32_t committed = kCommitMagic;
    if (!Esp32BaseFs::writeBytesAt(kPath, commitOffset, reinterpret_cast<const uint8_t*>(&committed), sizeof(committed))) {
        return false;
    }
    g_head = static_cast<uint16_t>((g_head + 1) % Capacity);
    if (g_count < Capacity) {
        ++g_count;
    }
    ++g_nextId;
    if (g_nextId == 0) {
        g_nextId = 1;
    }
    const bool metaSaved = saveMeta();
    if (!metaSaved) {
        BusinessEventLog::appendRecordMetaSaveFailed(stored.recordId, writtenSlot);
    }
    return metaSaved;
}

bool clear() {
    const uint32_t next = g_nextId == 0 ? 1 : g_nextId;
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    if (!createEmptyStore()) {
        g_ready = false;
        return false;
    }
    (void)Esp32BaseConfig::setInt(kNamespace, kKeyInitialized, 1);
    g_head = 0;
    g_count = 0;
    g_nextId = next;
    g_ready = saveMeta();
    return g_ready;
}

uint16_t count() {
    return g_count;
}

uint16_t capacity() {
    return Capacity;
}

uint32_t nextId() {
    return g_nextId;
}

bool readLatest(uint16_t offset, uint16_t limit, ReadCallback callback, void* user) {
    if (!callback || !g_ready || offset >= g_count) {
        return false;
    }
    uint16_t remaining = g_count - offset;
    if (remaining > limit) {
        remaining = limit;
    }
    for (uint16_t i = 0; i < remaining; ++i) {
        const uint16_t reverseIndex = static_cast<uint16_t>(offset + i + 1);
        const uint16_t index = g_head >= reverseIndex
            ? static_cast<uint16_t>(g_head - reverseIndex)
            : static_cast<uint16_t>(Capacity + g_head - reverseIndex);
        WateringRecord record = {};
        if (readAtIndex(index, &record)) {
            callback(record, user);
        }
    }
    return true;
}

}
