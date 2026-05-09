#include "storage/PlanSkipStore.h"

#include <Esp32Base.h>
#include <stdio.h>

#include "storage/PlanStore.h"

namespace {

static constexpr const char* kNamespace = "irr_skip";
static constexpr const char* kKeyCount = "count";

struct Entry {
    uint8_t planIndex;
    uint32_t ymd;
};

Entry g_entries[PlanSkipStore::Capacity] = {};
uint8_t g_count = 0;

void key(char* out, size_t len, uint8_t slot, const char* name) {
    snprintf(out, len, "s%u_%s", static_cast<unsigned>(slot), name);
}

bool valid(uint8_t planIndex, uint32_t ymd) {
    return planIndex < PlanStore::MaxPlans && ymd >= 20000101UL;
}

int find(uint8_t planIndex, uint32_t ymd) {
    for (uint8_t i = 0; i < g_count; ++i) {
        if (g_entries[i].planIndex == planIndex && g_entries[i].ymd == ymd) {
            return i;
        }
    }
    return -1;
}

bool persist() {
    bool ok = Esp32BaseConfig::setInt(kNamespace, kKeyCount, g_count);
    for (uint8_t i = 0; i < PlanSkipStore::Capacity; ++i) {
        char k[16];
        key(k, sizeof(k), i, "idx");
        ok = Esp32BaseConfig::setInt(kNamespace, k, i < g_count ? g_entries[i].planIndex : 0) && ok;
        key(k, sizeof(k), i, "ymd");
        ok = Esp32BaseConfig::setInt(kNamespace, k, i < g_count ? static_cast<int32_t>(g_entries[i].ymd) : 0) && ok;
    }
    return ok;
}

}

namespace PlanSkipStore {

void begin() {
    g_count = 0;
    int32_t rawCount = Esp32BaseConfig::getInt(kNamespace, kKeyCount, 0);
    if (rawCount < 0 || rawCount > Capacity) {
        rawCount = 0;
    }
    for (uint8_t i = 0; i < static_cast<uint8_t>(rawCount); ++i) {
        char k[16];
        key(k, sizeof(k), i, "idx");
        const uint8_t planIndex = static_cast<uint8_t>(Esp32BaseConfig::getInt(kNamespace, k, 0));
        key(k, sizeof(k), i, "ymd");
        const uint32_t ymd = static_cast<uint32_t>(Esp32BaseConfig::getInt(kNamespace, k, 0));
        if (valid(planIndex, ymd) && find(planIndex, ymd) < 0) {
            g_entries[g_count++] = {planIndex, ymd};
        }
    }

    for (uint8_t i = 0; i < PlanStore::MaxPlans && g_count < Capacity; ++i) {
        char k[16];
        snprintf(k, sizeof(k), "p%u_skip", static_cast<unsigned>(i));
        const uint32_t legacy = static_cast<uint32_t>(Esp32BaseConfig::getInt("irr_plan", k, 0));
        if (valid(i, legacy) && find(i, legacy) < 0) {
            g_entries[g_count++] = {i, legacy};
        }
    }
    (void)persist();
    ESP32BASE_LOG_I("skips", "loaded count=%u", static_cast<unsigned>(g_count));
}

bool isSkipped(uint8_t planIndex, uint32_t ymd) {
    return valid(planIndex, ymd) && find(planIndex, ymd) >= 0;
}

bool setSkipped(uint8_t planIndex, uint32_t ymd) {
    if (!valid(planIndex, ymd)) {
        return false;
    }
    if (find(planIndex, ymd) >= 0) {
        return true;
    }
    if (g_count >= Capacity) {
        for (uint8_t i = 1; i < g_count; ++i) {
            g_entries[i - 1] = g_entries[i];
        }
        --g_count;
    }
    g_entries[g_count++] = {planIndex, ymd};
    return persist();
}

bool clearSkipped(uint8_t planIndex, uint32_t ymd) {
    const int pos = find(planIndex, ymd);
    if (pos < 0) {
        return true;
    }
    for (uint8_t i = static_cast<uint8_t>(pos + 1); i < g_count; ++i) {
        g_entries[i - 1] = g_entries[i];
    }
    --g_count;
    return persist();
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    g_count = 0;
    return true;
}

}
