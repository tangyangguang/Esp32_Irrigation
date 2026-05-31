#include "storage/ScheduleSkipStore.h"

#include <Esp32Base.h>
#include <stdio.h>
#include <time.h>

namespace {

static constexpr const char* kNamespace = "irr_skip";
static constexpr const char* kKeyMeta = "meta";
static constexpr uint32_t kMetaMagic = 0x49534B4DUL;
static constexpr uint16_t kMetaVersion = 1;

struct Meta {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t count;
    uint8_t head;
    uint8_t reserved[2];
};

ScheduleSkipStore::SkipEntry g_entries[ScheduleSkipStore::Capacity] = {};
uint8_t g_count = 0;
uint8_t g_head = 0;

void key(char* out, size_t len, uint8_t index) {
    snprintf(out, len, "s%u", static_cast<unsigned>(index));
}

bool validYmd(uint32_t ymd) {
    return ymd >= 20000101UL;
}

int find(uint32_t planId, uint32_t ymd) {
    if (planId == 0 || !validYmd(ymd)) {
        return -1;
    }
    for (uint8_t i = 0; i < g_count; ++i) {
        if (g_entries[i].planId == planId && g_entries[i].ymd == ymd) {
            return i;
        }
    }
    return -1;
}

Meta makeMeta() {
    Meta meta = {};
    meta.magic = kMetaMagic;
    meta.version = kMetaVersion;
    meta.size = sizeof(meta);
    meta.count = g_count;
    meta.head = g_head;
    return meta;
}

bool validMeta(const Meta& meta) {
    return meta.magic == kMetaMagic &&
           meta.version == kMetaVersion &&
           meta.size == sizeof(meta) &&
           meta.count <= ScheduleSkipStore::Capacity &&
           meta.head < ScheduleSkipStore::Capacity;
}

bool saveMeta() {
    const Meta meta = makeMeta();
    return Esp32BaseConfig::setPod(kNamespace, kKeyMeta, meta);
}

bool persistAll() {
    bool ok = saveMeta();
    for (uint8_t i = 0; i < ScheduleSkipStore::Capacity; ++i) {
        char k[8];
        key(k, sizeof(k), i);
        ok = Esp32BaseConfig::setPod(kNamespace, k, g_entries[i]) && ok;
    }
    return ok;
}

}

namespace ScheduleSkipStore {

void begin() {
    g_count = 0;
    g_head = 0;
    Meta meta = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKeyMeta, meta) && validMeta(meta)) {
        g_count = meta.count;
        g_head = meta.head;
    }
    for (uint8_t i = 0; i < g_count; ++i) {
        char k[8];
        key(k, sizeof(k), i);
        SkipEntry entry = {};
        if (Esp32BaseConfig::getPod(kNamespace, k, entry) && entry.planId != 0 && validYmd(entry.ymd) && find(entry.planId, entry.ymd) < 0) {
            g_entries[i] = entry;
        }
    }
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    for (uint8_t i = 0; i < Capacity; ++i) {
        g_entries[i] = {};
    }
    g_count = 0;
    g_head = 0;
    return true;
}

bool isSkipped(uint32_t planId, uint32_t ymd) {
    return find(planId, ymd) >= 0;
}

bool skip(uint32_t planId, uint32_t ymd, Irrigation::SkipReason reason) {
    if (planId == 0 || !validYmd(ymd)) {
        return false;
    }
    int pos = find(planId, ymd);
    if (pos >= 0) {
        g_entries[pos].reason = reason;
        return persistAll();
    }
    if (g_count < Capacity) {
        pos = g_count++;
    } else {
        pos = g_head;
        g_head = static_cast<uint8_t>((g_head + 1) % Capacity);
    }
    g_entries[pos] = {planId, ymd, reason, {0, 0, 0}};
    return persistAll();
}

bool unskip(uint32_t planId, uint32_t ymd) {
    const int pos = find(planId, ymd);
    if (pos < 0) {
        return true;
    }
    for (uint8_t i = static_cast<uint8_t>(pos + 1); i < g_count; ++i) {
        g_entries[i - 1] = g_entries[i];
    }
    --g_count;
    if (g_count < Capacity) {
        g_entries[g_count] = {};
    }
    g_head = 0;
    return persistAll();
}

bool read(uint8_t offset, uint8_t limit, SkipEntry* out, uint8_t* outCount) {
    if (!out || !outCount || offset >= g_count) {
        if (outCount) *outCount = 0;
        return false;
    }
    uint8_t remaining = static_cast<uint8_t>(g_count - offset);
    if (remaining > limit) {
        remaining = limit;
    }
    for (uint8_t i = 0; i < remaining; ++i) {
        out[i] = g_entries[offset + i];
    }
    *outCount = remaining;
    return true;
}

const char* reasonName(Irrigation::SkipReason reason) {
    switch (reason) {
        case Irrigation::SkipReason::MANUAL: return "manual";
        case Irrigation::SkipReason::WEATHER: return "weather";
        case Irrigation::SkipReason::OTHER:
        default: return "other";
    }
}

}
