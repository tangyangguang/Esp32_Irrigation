#include "storage/PlanResultStore.h"

#include <Esp32Base.h>
#include <stdio.h>
#include <time.h>

#include "storage/PlanStore.h"

namespace {

static constexpr const char* kNamespace = "irr_plan_res";
static constexpr const char* kKeyCount = "count";

struct Entry {
    uint8_t planIndex;
    uint8_t result;
    uint16_t reserved;
    uint32_t ymd;
};

Entry g_entries[PlanResultStore::Capacity] = {};
uint8_t g_count = 0;

void key(char* out, size_t len, uint8_t slot, const char* name) {
    snprintf(out, len, "r%u_%s", static_cast<unsigned>(slot), name);
}

bool validResult(PlanResultStore::Result result) {
    return result >= PlanResultStore::RESULT_STARTED && result <= PlanResultStore::RESULT_LEAK_ALERT;
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
    for (uint8_t i = 0; i < PlanResultStore::Capacity; ++i) {
        char k[16];
        key(k, sizeof(k), i, "idx");
        ok = Esp32BaseConfig::setInt(kNamespace, k, i < g_count ? g_entries[i].planIndex : 0) && ok;
        key(k, sizeof(k), i, "ymd");
        ok = Esp32BaseConfig::setInt(kNamespace, k, i < g_count ? static_cast<int32_t>(g_entries[i].ymd) : 0) && ok;
        key(k, sizeof(k), i, "res");
        ok = Esp32BaseConfig::setInt(kNamespace, k, i < g_count ? g_entries[i].result : 0) && ok;
    }
    return ok;
}

uint32_t currentYmd() {
    if (!Esp32BaseNtp::isTimeSynced()) {
        return 0;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    tm local = {};
    if (localtime_r(&now, &local) == nullptr) {
        return 0;
    }
    return static_cast<uint32_t>(local.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(local.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(local.tm_mday);
}

bool pruneExpired(uint32_t today) {
    if (today < 20000101UL) {
        return false;
    }
    uint8_t out = 0;
    bool changed = false;
    for (uint8_t i = 0; i < g_count; ++i) {
        if (g_entries[i].ymd < today) {
            changed = true;
            continue;
        }
        if (out != i) {
            g_entries[out] = g_entries[i];
        }
        ++out;
    }
    g_count = out;
    return changed;
}

}

namespace PlanResultStore {

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
        key(k, sizeof(k), i, "res");
        const Result result = static_cast<Result>(Esp32BaseConfig::getInt(kNamespace, k, RESULT_NONE));
        if (valid(planIndex, ymd) && validResult(result) && find(planIndex, ymd) < 0) {
            g_entries[g_count++] = {planIndex, static_cast<uint8_t>(result), 0, ymd};
        }
    }

    if (pruneExpired(currentYmd())) {
        (void)persist();
    }
    ESP32BASE_LOG_I("plan_result", "loaded count=%u", static_cast<unsigned>(g_count));
}

bool getResult(uint8_t planIndex, uint32_t ymd, Result* result) {
    if (result) {
        *result = RESULT_NONE;
    }
    if (!result || !valid(planIndex, ymd)) {
        return false;
    }
    const int pos = find(planIndex, ymd);
    if (pos < 0) {
        return false;
    }
    *result = static_cast<Result>(g_entries[pos].result);
    return true;
}

bool setResult(uint8_t planIndex, uint32_t ymd, Result result) {
    if (!valid(planIndex, ymd) || !validResult(result)) {
        return false;
    }
    const bool pruned = pruneExpired(currentYmd());
    const int pos = find(planIndex, ymd);
    if (pos >= 0) {
        g_entries[pos].result = static_cast<uint8_t>(result);
        return persist();
    }
    if (g_count >= Capacity) {
        for (uint8_t i = 1; i < g_count; ++i) {
            g_entries[i - 1] = g_entries[i];
        }
        --g_count;
    }
    g_entries[g_count++] = {planIndex, static_cast<uint8_t>(result), 0, ymd};
    (void)pruned;
    return persist();
}

bool clearResult(uint8_t planIndex, uint32_t ymd) {
    const bool pruned = pruneExpired(currentYmd());
    const int pos = find(planIndex, ymd);
    if (pos < 0) {
        return !pruned || persist();
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

const char* resultName(Result result) {
    switch (result) {
        case RESULT_STARTED: return "started";
        case RESULT_SKIPPED_MANUAL: return "skipped_manual";
        case RESULT_SKIPPED_ROAD_DISABLED: return "skipped_road_disabled";
        case RESULT_SKIPPED_ROAD_BUSY: return "skipped_road_busy";
        case RESULT_REJECTED: return "rejected";
        case RESULT_CONFIG_INVALID: return "config_invalid";
        case RESULT_FACTORY_RESET_PENDING: return "factory_reset_pending";
        case RESULT_LEAK_ALERT: return "leak_alert";
        case RESULT_NONE:
        default: return "none";
    }
}

const char* resultLabel(Result result) {
    switch (result) {
        case RESULT_STARTED: return "已启动";
        case RESULT_SKIPPED_MANUAL: return "已手动跳过";
        case RESULT_SKIPPED_ROAD_DISABLED: return "水路停用跳过";
        case RESULT_SKIPPED_ROAD_BUSY: return "水路忙跳过";
        case RESULT_REJECTED: return "启动被拒绝";
        case RESULT_CONFIG_INVALID: return "配置无效";
        case RESULT_FACTORY_RESET_PENDING: return "恢复出厂待处理";
        case RESULT_LEAK_ALERT: return "漏水告警中跳过";
        case RESULT_NONE:
        default: return "无结果";
    }
}

}
