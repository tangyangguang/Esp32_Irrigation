#include "storage/PlanStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "domain/BusinessEventLog.h"
#include "storage/ZoneConfigStore.h"

namespace {

static constexpr const char* kNamespace = "irr_plan";
static constexpr const char* kKeyMeta = "meta";
static constexpr uint32_t kMetaMagic = 0x49504D45UL;
static constexpr uint16_t kMetaVersion = 1;
static constexpr uint32_t kPlanMagic = 0x49504C4EUL;
static constexpr uint16_t kPlanVersion = 1;

struct PlanMeta {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t nextPlanId;
};

struct StoredPlan {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Irrigation::PlanDefinition data;
};

Irrigation::PlanDefinition g_plans[Irrigation::TotalPlanSlots] = {};
uint32_t g_nextPlanId = 1;
Irrigation::PlanDefinition g_invalid = {};
bool g_schemaResetDetected = false;

void key(char* out, size_t len, uint8_t index) {
    snprintf(out, len, "p%u", static_cast<unsigned>(index));
}

bool flatIndex(uint8_t zoneId, uint8_t slotIndex, uint8_t* index) {
    if (!index || !Irrigation::validZoneId(zoneId) || slotIndex >= Irrigation::MaxPlansPerZone) {
        return false;
    }
    *index = static_cast<uint8_t>((zoneId - 1) * Irrigation::MaxPlansPerZone + slotIndex);
    return true;
}

Irrigation::PlanDefinition defaultPlan(uint8_t zoneId, uint8_t slotIndex) {
    Irrigation::PlanDefinition plan = {};
    plan.exists = false;
    plan.planId = 0;
    plan.zoneId = zoneId;
    plan.slotIndex = slotIndex;
    plan.enabled = false;
    plan.timeHour = 7;
    plan.timeMinute = 0;
    plan.durationSec = 300;
    plan.cycleDays = 1;
    plan.cycleMask = 0x01;
    plan.cycleStartYmd = PlanStore::DefaultCycleStartYmd;
    plan.createdAt = 0;
    return plan;
}

PlanMeta makeMeta() {
    PlanMeta meta = {};
    meta.magic = kMetaMagic;
    meta.version = kMetaVersion;
    meta.size = sizeof(meta);
    meta.nextPlanId = g_nextPlanId == 0 ? 1 : g_nextPlanId;
    return meta;
}

bool validMeta(const PlanMeta& meta) {
    return meta.magic == kMetaMagic &&
           meta.version == kMetaVersion &&
           meta.size == sizeof(meta) &&
           meta.nextPlanId != 0;
}

bool saveMeta() {
    const PlanMeta meta = makeMeta();
    return Esp32BaseConfig::setPod(kNamespace, kKeyMeta, meta);
}

StoredPlan wrap(const Irrigation::PlanDefinition& plan) {
    StoredPlan stored = {};
    stored.magic = kPlanMagic;
    stored.version = kPlanVersion;
    stored.size = sizeof(stored);
    stored.data = plan;
    return stored;
}

bool validStored(const StoredPlan& stored) {
    return stored.magic == kPlanMagic &&
           stored.version == kPlanVersion &&
           stored.size == sizeof(stored) &&
           PlanStore::validate(stored.data);
}

uint32_t currentEpoch() {
#if ESP32BASE_ENABLE_NTP
    return Esp32BaseNtp::isTimeSynced() ? static_cast<uint32_t>(Esp32BaseNtp::timestamp()) : 0;
#else
    return 0;
#endif
}

uint32_t currentYmd() {
#if ESP32BASE_ENABLE_NTP
    if (!Esp32BaseNtp::isTimeSynced()) {
        return PlanStore::DefaultCycleStartYmd;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    tm local = {};
    if (localtime_r(&now, &local) == nullptr) {
        return PlanStore::DefaultCycleStartYmd;
    }
    return static_cast<uint32_t>(local.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(local.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(local.tm_mday);
#else
    return PlanStore::DefaultCycleStartYmd;
#endif
}

bool ymdToTime(uint32_t ymd, time_t* out) {
    if (!out || ymd < 20000101UL) {
        return false;
    }
    tm value = {};
    value.tm_year = static_cast<int>(ymd / 10000UL) - 1900;
    value.tm_mon = static_cast<int>((ymd / 100UL) % 100UL) - 1;
    value.tm_mday = static_cast<int>(ymd % 100UL);
    value.tm_hour = 12;
    *out = mktime(&value);
    if (*out <= 0) {
        return false;
    }
    tm normalized = {};
    if (localtime_r(out, &normalized) == nullptr) {
        return false;
    }
    return normalized.tm_year == value.tm_year &&
           normalized.tm_mon == value.tm_mon &&
           normalized.tm_mday == value.tm_mday;
}

uint32_t validCycleMask(uint8_t days) {
    return days >= 32 ? 0xFFFFFFFFUL : ((1UL << days) - 1UL);
}

bool planIdExists(uint32_t planId) {
    if (planId == 0) {
        return false;
    }
    for (uint8_t i = 0; i < Irrigation::TotalPlanSlots; ++i) {
        if (g_plans[i].exists && g_plans[i].planId == planId) {
            return true;
        }
    }
    return false;
}

uint32_t allocatePlanId() {
    if (g_nextPlanId == 0) {
        g_nextPlanId = 1;
    }
    while (planIdExists(g_nextPlanId)) {
        ++g_nextPlanId;
        if (g_nextPlanId == 0) {
            g_nextPlanId = 1;
        }
    }
    return g_nextPlanId++;
}

bool saveSlot(uint8_t index, const Irrigation::PlanDefinition& plan) {
    char k[8];
    key(k, sizeof(k), index);
    const StoredPlan stored = wrap(plan);
    return Esp32BaseConfig::setPod(kNamespace, k, stored);
}

}

namespace PlanStore {

void begin() {
    g_nextPlanId = 1;
    uint16_t invalidCount = 0;
    g_schemaResetDetected = false;
    PlanMeta meta = {};
    if (Esp32BaseConfig::getPod(kNamespace, kKeyMeta, meta)) {
        if (validMeta(meta)) {
            g_nextPlanId = meta.nextPlanId;
        } else {
            ++invalidCount;
        }
    }
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            uint8_t index = 0;
            (void)flatIndex(zoneId, slot, &index);
            g_plans[index] = defaultPlan(zoneId, slot);
            char k[8];
            key(k, sizeof(k), index);
            StoredPlan stored = {};
            if (Esp32BaseConfig::getPod(kNamespace, k, stored)) {
                if (validStored(stored)) {
                    g_plans[index] = stored.data;
                } else {
                    ++invalidCount;
                }
            }
        }
    }
    if (invalidCount > 0) {
        g_schemaResetDetected = true;
        BusinessEventLog::appendConfigSchemaReset("plans", invalidCount);
    }
    (void)saveMeta();
}

bool clear() {
    if (!Esp32BaseConfig::clearNamespace(kNamespace)) {
        return false;
    }
    g_nextPlanId = 1;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            uint8_t index = 0;
            (void)flatIndex(zoneId, slot, &index);
            g_plans[index] = defaultPlan(zoneId, slot);
        }
    }
    return saveMeta();
}

bool create(uint8_t zoneId, Irrigation::PlanDefinition* out) {
    Irrigation::PlanDefinition draft = {};
    return create(zoneId, draft, out);
}

bool create(uint8_t zoneId, const Irrigation::PlanDefinition& draft, Irrigation::PlanDefinition* out) {
    if (!Irrigation::validZoneId(zoneId)) {
        return false;
    }
    for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
        uint8_t index = 0;
        (void)flatIndex(zoneId, slot, &index);
        if (g_plans[index].exists) {
            continue;
        }
        Irrigation::PlanDefinition plan = defaultPlan(zoneId, slot);
        plan.exists = true;
        plan.planId = allocatePlanId();
        snprintf(plan.name, sizeof(plan.name), "计划 %u", static_cast<unsigned>(slot + 1));
        plan.enabled = false;
        if (draft.exists && draft.name[0] != '\0') {
            strlcpy(plan.name, draft.name, sizeof(plan.name));
        }
        if (draft.exists) {
            plan.enabled = draft.enabled;
        }
        if (draft.exists && draft.timeHour <= 23 && draft.timeMinute <= 59) {
            plan.timeHour = draft.timeHour;
            plan.timeMinute = draft.timeMinute;
        }
        if (draft.exists && draft.durationSec > 0) {
            plan.durationSec = draft.durationSec;
        }
        if (draft.exists && draft.cycleDays > 0) {
            plan.cycleDays = draft.cycleDays;
        }
        if (draft.exists && draft.cycleMask > 0) {
            plan.cycleMask = draft.cycleMask;
        }
        plan.cycleStartYmd = draft.exists && draft.cycleStartYmd >= 20000101UL ? draft.cycleStartYmd : currentYmd();
        plan.createdAt = currentEpoch();
        if (!validate(plan)) {
            return false;
        }
        if (!saveSlot(index, plan) || !saveMeta()) {
            return false;
        }
        g_plans[index] = plan;
        if (out) {
            *out = plan;
        }
        return true;
    }
    return false;
}

bool remove(uint32_t planId) {
    for (uint8_t i = 0; i < Irrigation::TotalPlanSlots; ++i) {
        if (!g_plans[i].exists || g_plans[i].planId != planId) {
            continue;
        }
        const uint8_t zoneId = g_plans[i].zoneId;
        const uint8_t slot = g_plans[i].slotIndex;
        Irrigation::PlanDefinition plan = defaultPlan(zoneId, slot);
        if (!saveSlot(i, plan)) {
            return false;
        }
        g_plans[i] = plan;
        return true;
    }
    return false;
}

bool set(uint32_t planId, const Irrigation::PlanDefinition& plan) {
    if (planId == 0 || plan.planId != planId || !validate(plan)) {
        return false;
    }
    for (uint8_t i = 0; i < Irrigation::TotalPlanSlots; ++i) {
        if (!g_plans[i].exists || g_plans[i].planId != planId) {
            continue;
        }
        if (g_plans[i].zoneId != plan.zoneId || g_plans[i].slotIndex != plan.slotIndex) {
            return false;
        }
        if (!saveSlot(i, plan)) {
            return false;
        }
        g_plans[i] = plan;
        return true;
    }
    return false;
}

bool getById(uint32_t planId, Irrigation::PlanDefinition* out) {
    if (!out || planId == 0) {
        return false;
    }
    for (uint8_t i = 0; i < Irrigation::TotalPlanSlots; ++i) {
        if (g_plans[i].exists && g_plans[i].planId == planId) {
            *out = g_plans[i];
            return true;
        }
    }
    return false;
}

const Irrigation::PlanDefinition& getBySlot(uint8_t zoneId, uint8_t slotIndex) {
    uint8_t index = 0;
    if (!flatIndex(zoneId, slotIndex, &index)) {
        return g_invalid;
    }
    return g_plans[index];
}

uint8_t countForZone(uint8_t zoneId) {
    if (!Irrigation::validZoneId(zoneId)) {
        return 0;
    }
    uint8_t total = 0;
    for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
        if (getBySlot(zoneId, slot).exists) {
            ++total;
        }
    }
    return total;
}

uint8_t count() {
    uint8_t total = 0;
    for (uint8_t i = 0; i < Irrigation::TotalPlanSlots; ++i) {
        if (g_plans[i].exists) {
            ++total;
        }
    }
    return total;
}

bool validate(const Irrigation::PlanDefinition& plan) {
    time_t ignored = 0;
    if (!Irrigation::validZoneId(plan.zoneId) || plan.slotIndex >= Irrigation::MaxPlansPerZone) {
        return false;
    }
    if (!plan.exists) {
        return plan.planId == 0 && !plan.enabled;
    }
    if (plan.planId == 0 || !ZoneConfigStore::validateName(plan.name)) {
        return false;
    }
    if (plan.timeHour > 23 || plan.timeMinute > 59 || plan.durationSec < 1) {
        return false;
    }
    if (plan.cycleDays < 1 || plan.cycleDays > 30 || plan.cycleMask == 0) {
        return false;
    }
    if ((plan.cycleMask & ~validCycleMask(plan.cycleDays)) != 0) {
        return false;
    }
    return ymdToTime(plan.cycleStartYmd, &ignored);
}

bool shouldRunOnDate(const Irrigation::PlanDefinition& plan, uint32_t ymd) {
    time_t start = 0;
    time_t target = 0;
    if (!plan.exists || !plan.enabled || !ymdToTime(plan.cycleStartYmd, &start) || !ymdToTime(ymd, &target) || target < start) {
        return false;
    }
    const uint32_t days = static_cast<uint32_t>((target - start) / 86400L);
    const uint8_t dayInCycle = static_cast<uint8_t>(days % plan.cycleDays);
    return (plan.cycleMask & (1UL << dayInCycle)) != 0;
}

uint32_t nextPlanId() {
    return g_nextPlanId;
}

bool schemaResetDetected() {
    return g_schemaResetDetected;
}

}
