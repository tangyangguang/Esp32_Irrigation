#include "storage/RecordStore.h"

#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>

namespace {

static constexpr const char* kPath = "/irr_records.bin";
static constexpr const char* kNamespace = "irr_rec";
static constexpr const char* kKeyHead = "head";
static constexpr const char* kKeyCount = "count";
static constexpr const char* kKeyNextId = "next_id";
static constexpr const char* kKeyInitialized = "init";
static constexpr uint32_t kMagic = 0x49525245UL;
static constexpr uint16_t kVersion = 2;

uint16_t g_head = 0;
uint16_t g_count = 0;
uint32_t g_nextId = 1;
bool g_ready = false;

uint32_t fileSizeBytes() {
    return static_cast<uint32_t>(sizeof(RecordStore::Record)) * RecordStore::Capacity;
}

uint16_t clampIndex(int32_t value) {
    if (value < 0 || value >= RecordStore::Capacity) {
        return 0;
    }
    return static_cast<uint16_t>(value);
}

uint16_t clampCount(int32_t value) {
    if (value < 0) {
        return 0;
    }
    if (value > RecordStore::Capacity) {
        return RecordStore::Capacity;
    }
    return static_cast<uint16_t>(value);
}

bool createEmptyStore() {
    const uint32_t total = fileSizeBytes();
    uint8_t zeros[256] = {};
    if (!Esp32BaseFs::writeBytes(kPath, nullptr, 0)) {
        ESP32BASE_LOG_W("records", "create store failed open");
        return false;
    }
    for (uint32_t offset = 0; offset < total; offset += sizeof(zeros)) {
        const uint32_t remaining = total - offset;
        const size_t chunk = remaining < sizeof(zeros) ? static_cast<size_t>(remaining) : sizeof(zeros);
        if (!Esp32BaseFs::appendBytes(kPath, zeros, chunk)) {
            ESP32BASE_LOG_W("records", "create store failed offset=%lu bytes=%u",
                            static_cast<unsigned long>(offset),
                            static_cast<unsigned>(chunk));
            return false;
        }
        delay(0);
    }
    return Esp32BaseFs::fileSize(kPath) == static_cast<int64_t>(total);
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

bool readAtIndex(uint16_t index, RecordStore::Record* record) {
    if (!record || index >= RecordStore::Capacity) {
        return false;
    }
    size_t readLen = 0;
    const uint32_t offset = static_cast<uint32_t>(index) * sizeof(RecordStore::Record);
    if (!Esp32BaseFs::readBytesAt(kPath, offset, reinterpret_cast<uint8_t*>(record), sizeof(*record), &readLen)) {
        return false;
    }
    return readLen == sizeof(*record) && record->magic == kMagic && record->version == kVersion && record->size == sizeof(*record);
}

}

namespace RecordStore {

void begin() {
    g_ready = ensureStoreFile();
    if (!g_ready) {
        ESP32BASE_LOG_W("records", "record store not ready");
        return;
    }

    g_head = clampIndex(Esp32BaseConfig::getInt(kNamespace, kKeyHead, 0));
    g_count = clampCount(Esp32BaseConfig::getInt(kNamespace, kKeyCount, 0));
    g_nextId = static_cast<uint32_t>(Esp32BaseConfig::getInt(kNamespace, kKeyNextId, 1));
    if (g_nextId == 0) {
        g_nextId = 1;
    }
    ESP32BASE_LOG_I("records", "ready count=%u head=%u next=%lu",
                    static_cast<unsigned>(g_count),
                    static_cast<unsigned>(g_head),
                    static_cast<unsigned long>(g_nextId));
}

bool append(const Record& record) {
    if (!g_ready && !ensureStoreFile()) {
        return false;
    }
    g_ready = true;

    Record stored = record;
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.id = g_nextId;

    const uint32_t offset = static_cast<uint32_t>(g_head) * sizeof(stored);
    if (!Esp32BaseFs::writeBytesAt(kPath, offset, reinterpret_cast<const uint8_t*>(&stored), sizeof(stored))) {
        ESP32BASE_LOG_W("records", "append failed id=%lu index=%u",
                        static_cast<unsigned long>(stored.id),
                        static_cast<unsigned>(g_head));
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

    const bool metaOk = Esp32BaseConfig::setInt(kNamespace, kKeyHead, g_head) &&
                        Esp32BaseConfig::setInt(kNamespace, kKeyCount, g_count) &&
                        Esp32BaseConfig::setInt(kNamespace, kKeyNextId, static_cast<int32_t>(g_nextId));
    ESP32BASE_LOG_I("records", "append id=%lu result=%s",
                    static_cast<unsigned long>(stored.id),
                    metaOk ? "ok" : "meta_failed");
    return metaOk;
}

bool clear() {
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
    g_nextId = 1;
    g_ready = true;
    ESP32BASE_LOG_W("records", "cleared");
    return true;
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

bool readLatest(uint16_t offset, uint8_t limit, ReadCallback callback, void* user) {
    if (!callback || !g_ready || offset >= g_count) {
        return false;
    }
    uint16_t remaining = g_count - offset;
    if (remaining > limit) {
        remaining = limit;
    }

    for (uint16_t i = 0; i < remaining; ++i) {
        const uint16_t reverseIndex = static_cast<uint16_t>(offset + i + 1);
        uint16_t index = g_head >= reverseIndex
            ? static_cast<uint16_t>(g_head - reverseIndex)
            : static_cast<uint16_t>(Capacity + g_head - reverseIndex);
        Record record = {};
        if (!readAtIndex(index, &record)) {
            continue;
        }
        callback(record, user);
    }
    return true;
}

const char* sourceName(Source source) {
    switch (source) {
        case SOURCE_BUTTON: return "button";
        case SOURCE_WEB: return "web";
        case SOURCE_PLAN: return "plan";
        case SOURCE_UNKNOWN:
        default: return "unknown";
    }
}

}
