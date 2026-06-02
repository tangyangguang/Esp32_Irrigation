#include "storage/WeatherSnapshotStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stddef.h>
#include <string.h>

namespace {

static constexpr const char* kNamespace = "irr_weather";
static constexpr const char* kKeyBlob = "snapshot";
static constexpr uint32_t kMagic = 0x49575448UL;
static constexpr uint16_t kVersion = 1;
static constexpr uint32_t kFreshWindowSec = 6UL * 60UL * 60UL;

struct StoredSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    WeatherSnapshotStore::Snapshot data;
};

WeatherSnapshotStore::Snapshot g_snapshot = {};

bool validUtf8NoControl(const char* text, size_t maxLen, bool allowEmpty) {
    if (!text) return false;
    const size_t len = strnlen(text, maxLen);
    if (len >= maxLen) return false;
    if (!allowEmpty && len == 0) return false;
    size_t i = 0;
    while (i < len) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7F) return false;
        if (c < 0x80) {
            ++i;
            continue;
        }
        uint8_t needed = 0;
        uint32_t codepoint = 0;
        if ((c & 0xE0) == 0xC0) {
            needed = 1;
            codepoint = c & 0x1F;
            if (codepoint == 0) return false;
        } else if ((c & 0xF0) == 0xE0) {
            needed = 2;
            codepoint = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            needed = 3;
            codepoint = c & 0x07;
        } else {
            return false;
        }
        if (i + needed >= len) return false;
        for (uint8_t j = 1; j <= needed; ++j) {
            const unsigned char cc = static_cast<unsigned char>(text[i + j]);
            if ((cc & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (cc & 0x3F);
        }
        if ((needed == 1 && codepoint < 0x80) ||
            (needed == 2 && codepoint < 0x800) ||
            (needed == 3 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF ||
            (codepoint >= 0x80 && codepoint <= 0x9F) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
        i += static_cast<size_t>(needed) + 1U;
    }
    return true;
}

StoredSnapshot wrap(const WeatherSnapshotStore::Snapshot& snapshot) {
    StoredSnapshot stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.data = snapshot;
    return stored;
}

bool validStored(const StoredSnapshot& stored) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored) &&
           WeatherSnapshotStore::validate(stored.data);
}

}

namespace WeatherSnapshotStore {

void begin() {
    g_snapshot = {};
    StoredSnapshot stored = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKeyBlob, stored) && validStored(stored)) {
        g_snapshot = stored.data;
    }
}

bool get(Snapshot* out) {
    if (!out) return false;
    *out = g_snapshot;
    return g_snapshot.exists;
}

bool set(const Snapshot& snapshot) {
    Snapshot next = snapshot;
    next.exists = true;
    if (!validate(next)) return false;
    if (!Esp32BaseConfig::setPod(kNamespace, kKeyBlob, wrap(next))) {
        return false;
    }
    g_snapshot = next;
    return true;
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    g_snapshot = {};
    return true;
}

bool validate(const Snapshot& snapshot) {
    if (!snapshot.exists) return true;
    if (!validUtf8NoControl(snapshot.condition, ConditionMaxBytes, false)) return false;
    if (snapshot.currentTempC < -50 || snapshot.currentTempC > 80) return false;
    if (snapshot.rainProbability24hPercent > 100) return false;
    if (snapshot.windLevel > 17) return false;
    for (uint8_t i = 0; i < 3; ++i) {
        const ForecastDay& day = snapshot.days[i];
        if (!validUtf8NoControl(day.label, DayLabelMaxBytes, false)) return false;
        if (day.lowTempC < -50 || day.lowTempC > 80) return false;
        if (day.highTempC < -50 || day.highTempC > 80) return false;
        if (day.lowTempC > day.highTempC) return false;
        if (day.rainProbabilityPercent > 100) return false;
    }
    return true;
}

bool isStale(const Snapshot& snapshot, uint32_t nowEpoch) {
    if (!snapshot.exists || snapshot.updatedEpoch == 0 || nowEpoch == 0) return false;
    return nowEpoch > snapshot.updatedEpoch && (nowEpoch - snapshot.updatedEpoch) > kFreshWindowSec;
}

}
