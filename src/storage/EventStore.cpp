#include "storage/EventStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

namespace {

static constexpr const char* kPath = "/irr_events.bin";
static constexpr const char* kNamespace = "irr_evt";
static constexpr const char* kKeyInitialized = "init";
static constexpr const char* kKeyMeta = "meta";
static constexpr uint32_t kMagic = 0x49524556UL;
static constexpr uint32_t kMetaMagic = 0x4952454DUL;
static constexpr uint16_t kVersion = 2;
static constexpr uint16_t kMetaVersion = 1;

struct StoreMeta {
    uint32_t magic;
    uint16_t version;
    uint16_t head;
    uint16_t count;
    uint16_t reserved;
    uint32_t nextId;
};

static_assert(sizeof(StoreMeta) == 16, "EventStore::StoreMeta binary layout changed");

uint16_t g_head = 0;
uint16_t g_count = 0;
uint32_t g_nextId = 1;
bool g_ready = false;

uint32_t fileSizeBytes() {
    return static_cast<uint32_t>(sizeof(EventStore::Event)) * EventStore::Capacity;
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
           meta.head < EventStore::Capacity &&
           meta.count <= EventStore::Capacity &&
           meta.nextId != 0;
}

bool saveMeta() {
    const StoreMeta meta = makeMeta();
    return Esp32BaseConfig::setPod(kNamespace, kKeyMeta, meta);
}

void loadMeta() {
    StoreMeta meta = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKeyMeta, meta) && validMeta(meta)) {
        g_head = meta.head;
        g_count = meta.count;
        g_nextId = meta.nextId;
        return;
    }
    g_head = 0;
    g_count = 0;
    g_nextId = 1;
    (void)saveMeta();
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

bool readAtIndex(uint16_t index, EventStore::Event* event) {
    if (!event || index >= EventStore::Capacity) {
        return false;
    }
    size_t readLen = 0;
    const uint32_t offset = static_cast<uint32_t>(index) * sizeof(EventStore::Event);
    if (!Esp32BaseFs::readBytesAt(kPath, offset, reinterpret_cast<uint8_t*>(event), sizeof(*event), &readLen)) {
        return false;
    }
    return readLen == sizeof(*event) && event->magic == kMagic && event->version == kVersion && event->size == sizeof(*event);
}

uint32_t currentEpoch() {
#if ESP32BASE_ENABLE_NTP
    return Esp32BaseNtp::isTimeSynced() ? static_cast<uint32_t>(Esp32BaseNtp::timestamp()) : 0;
#else
    return 0;
#endif
}

}

namespace EventStore {

void begin() {
    g_ready = ensureStoreFile();
    if (!g_ready) {
        ESP32BASE_LOG_W("events", "event store not ready");
        return;
    }
    loadMeta();
}

bool append(Irrigation::EventType type, Irrigation::EventSource source, uint8_t zoneId, uint8_t code, int32_t value1, int32_t value2, const char* text) {
    if (!g_ready && !ensureStoreFile()) {
        return false;
    }
    g_ready = true;

    Event stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.id = g_nextId;
    stored.uptimeMs = millis();
    stored.epoch = currentEpoch();
    stored.type = static_cast<uint8_t>(type);
    stored.source = static_cast<uint8_t>(source);
    stored.zoneId = zoneId;
    stored.code = code;
    stored.value1 = value1;
    stored.value2 = value2;
    if (text) {
        strlcpy(stored.text, text, sizeof(stored.text));
    }

    const uint32_t offset = static_cast<uint32_t>(g_head) * sizeof(stored);
    if (!Esp32BaseFs::writeBytesAt(kPath, offset, reinterpret_cast<const uint8_t*>(&stored), sizeof(stored))) {
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
    return saveMeta();
}

bool clear() {
    const uint32_t nextId = g_nextId == 0 ? 1 : g_nextId;
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
    g_nextId = nextId;
    g_ready = saveMeta();
    return g_ready;
}

uint16_t count() {
    return g_count;
}

uint16_t capacity() {
    return Capacity;
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
        Event event = {};
        if (readAtIndex(index, &event)) {
            callback(event, user);
        }
    }
    return true;
}

const char* typeName(Irrigation::EventType type) {
    switch (type) {
        case Irrigation::EventType::BOOT: return "boot";
        case Irrigation::EventType::ZONE_CONFIG_CHANGED: return "zone_config_changed";
        case Irrigation::EventType::SYSTEM_CONFIG_CHANGED: return "system_config_changed";
        case Irrigation::EventType::PLAN_CHANGED: return "plan_changed";
        case Irrigation::EventType::PLAN_OBSERVED: return "plan_observed";
        case Irrigation::EventType::WATER_START: return "water_start";
        case Irrigation::EventType::WATER_FINISH: return "water_finish";
        case Irrigation::EventType::WATER_ERROR: return "water_error";
        case Irrigation::EventType::LEAK_ALERT: return "leak_alert";
        case Irrigation::EventType::ALERT_CLEARED: return "alert_cleared";
        case Irrigation::EventType::FACTORY_RESET_REQUESTED: return "factory_reset_requested";
        case Irrigation::EventType::FACTORY_RESET_EXECUTED: return "factory_reset_executed";
        case Irrigation::EventType::WIFI_STATUS_CHANGED: return "wifi_status_changed";
        case Irrigation::EventType::OTA_STATUS_CHANGED: return "ota_status_changed";
        default: return "unknown";
    }
}

const char* sourceName(Irrigation::EventSource source) {
    switch (source) {
        case Irrigation::EventSource::BUTTON: return "button";
        case Irrigation::EventSource::WEB: return "web";
        case Irrigation::EventSource::PLAN: return "plan";
        case Irrigation::EventSource::SYSTEM:
        default: return "system";
    }
}

}
