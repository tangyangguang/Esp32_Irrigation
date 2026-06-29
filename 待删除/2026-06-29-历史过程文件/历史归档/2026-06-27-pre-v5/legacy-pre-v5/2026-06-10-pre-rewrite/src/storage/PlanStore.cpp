#include "storage/PlanStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "domain/BusinessEventLog.h"
#include "storage/ZoneConfigStore.h"

namespace {

static constexpr const char* kNamespace = "irr_plan_v1";
static constexpr const char* kKeyNext = "next";
static constexpr uint32_t kMagic = 0x49504C4EUL;
static constexpr uint16_t kVersion = 1;

struct StoredPlan {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Irrigation::PlanDefinition data;
};

static_assert(sizeof(StoredPlan) <= Esp32BaseConfig::CONFIG_BLOB_MAX_LEN,
              "StoredPlan must fit Esp32BaseConfig blob storage");

Irrigation::PlanDefinition g_plans[Irrigation::TotalPlanSlots] = {};
uint32_t g_nextPlanId = 1;
Irrigation::PlanDefinition g_invalid = {};
bool g_schemaResetDetected = false;

bool flatIndex(uint8_t zoneId, uint8_t slotIndex, uint8_t* index) {
    if (!index || !Irrigation::validZoneId(zoneId) || slotIndex >= Irrigation::MaxPlansPerZone) {
        return false;
    }
    *index = static_cast<uint8_t>((zoneId - 1) * Irrigation::MaxPlansPerZone + slotIndex);
    return true;
}

void planKey(char* out, size_t len, uint8_t zoneId, uint8_t slotIndex) {
    snprintf(out, len, "p%u_%u", static_cast<unsigned>(zoneId), static_cast<unsigned>(slotIndex));
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
    plan.cycleStartYmd = 0;
    plan.createdAt = 0;
    return plan;
}

StoredPlan wrap(const Irrigation::PlanDefinition& plan) {
    StoredPlan stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.data = plan;
    return stored;
}

uint32_t currentEpoch() {
#if ESP32BASE_ENABLE_NTP
    return Esp32BaseNtp::isTimeSynced() ? static_cast<uint32_t>(Esp32BaseNtp::timestamp()) : 0;
#else
    return 0;
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

bool validStored(const StoredPlan& stored, uint8_t zoneId, uint8_t slotIndex) {
    return stored.magic == kMagic &&
           stored.version == kVersion &&
           stored.size == sizeof(stored) &&
           stored.data.zoneId == zoneId &&
           stored.data.slotIndex == slotIndex &&
           PlanStore::validate(stored.data);
}

uint32_t recoverNextPlanId(uint32_t storedNext) {
    uint32_t next = storedNext == 0 ? 1 : storedNext;
    for (uint8_t i = 0; i < Irrigation::TotalPlanSlots; ++i) {
        if (g_plans[i].exists && g_plans[i].planId >= next) {
            next = g_plans[i].planId + 1UL;
            if (next == 0) {
                next = 1;
            }
        }
    }
    return next;
}

bool saveNextPlanId() {
    return Esp32BaseConfig::setInt(kNamespace, kKeyNext, static_cast<int32_t>(g_nextPlanId == 0 ? 1 : g_nextPlanId));
}

bool savePlanSlot(uint8_t zoneId, uint8_t slotIndex, const Irrigation::PlanDefinition& plan) {
    if (!PlanStore::validate(plan)) {
        return false;
    }
    char key[12];
    planKey(key, sizeof(key), zoneId, slotIndex);
    return Esp32BaseConfig::setPod(kNamespace, key, wrap(plan));
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

uint32_t nextAvailablePlanId() {
    uint32_t candidate = g_nextPlanId == 0 ? 1 : g_nextPlanId;
    while (planIdExists(candidate)) {
        ++candidate;
        if (candidate == 0) {
            candidate = 1;
        }
    }
    return candidate;
}

bool currentYmdValue(uint32_t* out) {
#if ESP32BASE_ENABLE_NTP
    if (!out || !Esp32BaseNtp::isTimeSynced()) {
        return false;
    }
    const time_t now = static_cast<time_t>(Esp32BaseNtp::timestamp());
    tm local = {};
    if (localtime_r(&now, &local) == nullptr) {
        return false;
    }
    *out = static_cast<uint32_t>(local.tm_year + 1900) * 10000UL +
           static_cast<uint32_t>(local.tm_mon + 1) * 100UL +
           static_cast<uint32_t>(local.tm_mday);
    return PlanStore::validYmd(*out);
#else
    (void)out;
    return false;
#endif
}

uint32_t validCycleMask(uint8_t days) {
    return days >= 32 ? 0xFFFFFFFFUL : ((1UL << days) - 1UL);
}

}

namespace PlanStore {

void begin() {
    uint16_t invalidCount = 0;
    g_schemaResetDetected = false;
    g_nextPlanId = static_cast<uint32_t>(Esp32BaseConfig::getInt(kNamespace, kKeyNext, 1));
    if (g_nextPlanId == 0) {
        g_nextPlanId = 1;
        ++invalidCount;
    }
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            uint8_t index = 0;
            (void)flatIndex(zoneId, slot, &index);
            g_plans[index] = defaultPlan(zoneId, slot);
            char key[12];
            planKey(key, sizeof(key), zoneId, slot);
            StoredPlan stored = {};
            if (!Esp32BaseConfig::getPod(kNamespace, key, stored)) {
                continue;
            }
            if (validStored(stored, zoneId, slot)) {
                g_plans[index] = stored.data;
            } else {
                ++invalidCount;
            }
        }
    }
    g_nextPlanId = recoverNextPlanId(g_nextPlanId);
    (void)saveNextPlanId();
    if (invalidCount > 0) {
        g_schemaResetDetected = true;
        BusinessEventLog::appendConfigSchemaReset("plans", invalidCount);
    }
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
    return saveNextPlanId();
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
        if (!getBySlot(zoneId, slot).exists) {
            return createAt(zoneId, slot, draft, out);
        }
    }
    return false;
}

bool createAt(uint8_t zoneId, uint8_t slotIndex, const Irrigation::PlanDefinition& draft, Irrigation::PlanDefinition* out) {
    uint8_t index = 0;
    if (!flatIndex(zoneId, slotIndex, &index) || g_plans[index].exists) {
        return false;
    }
    Irrigation::PlanDefinition plan = defaultPlan(zoneId, slotIndex);
    const Irrigation::PlanDefinition oldPlan = g_plans[index];
    const uint32_t oldNextPlanId = g_nextPlanId;
    plan.exists = true;
    plan.planId = nextAvailablePlanId();
    snprintf(plan.name, sizeof(plan.name), "计划 %u", static_cast<unsigned>(slotIndex + 1));
    if (draft.exists && draft.name[0] != '\0') {
        strlcpy(plan.name, draft.name, sizeof(plan.name));
    }
    if (draft.exists) {
        plan.enabled = draft.enabled;
        if (draft.timeHour <= 23 && draft.timeMinute <= 59) {
            plan.timeHour = draft.timeHour;
            plan.timeMinute = draft.timeMinute;
        }
        if (draft.durationSec > 0) {
            plan.durationSec = draft.durationSec;
        }
        if (draft.cycleDays > 0) {
            plan.cycleDays = draft.cycleDays;
        }
        if (draft.cycleMask > 0) {
            plan.cycleMask = draft.cycleMask;
        }
        if (validYmd(draft.cycleStartYmd)) {
            plan.cycleStartYmd = draft.cycleStartYmd;
        }
    }
    if (!validYmd(plan.cycleStartYmd) && !currentYmdValue(&plan.cycleStartYmd)) {
        return false;
    }
    plan.createdAt = currentEpoch();
    if (!validate(plan)) {
        return false;
    }
    g_plans[index] = plan;
    g_nextPlanId = plan.planId + 1UL;
    if (g_nextPlanId == 0) {
        g_nextPlanId = 1;
    }
    if (!savePlanSlot(zoneId, slotIndex, plan) || !saveNextPlanId()) {
        g_plans[index] = oldPlan;
        g_nextPlanId = oldNextPlanId;
        return false;
    }
    if (out) {
        *out = plan;
    }
    return true;
}

bool remove(uint32_t planId) {
    for (uint8_t i = 0; i < Irrigation::TotalPlanSlots; ++i) {
        if (!g_plans[i].exists || g_plans[i].planId != planId) {
            continue;
        }
        const uint8_t zoneId = g_plans[i].zoneId;
        const uint8_t slot = g_plans[i].slotIndex;
        const Irrigation::PlanDefinition oldPlan = g_plans[i];
        const Irrigation::PlanDefinition plan = defaultPlan(zoneId, slot);
        g_plans[i] = plan;
        if (!savePlanSlot(zoneId, slot, plan)) {
            g_plans[i] = oldPlan;
            return false;
        }
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
        const Irrigation::PlanDefinition oldPlan = g_plans[i];
        g_plans[i] = plan;
        if (!savePlanSlot(plan.zoneId, plan.slotIndex, plan)) {
            g_plans[i] = oldPlan;
            return false;
        }
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

bool validYmd(uint32_t ymd) {
    time_t ignored = 0;
    return ymdToTime(ymd, &ignored);
}

bool currentYmd(uint32_t* out) {
    return currentYmdValue(out);
}

uint32_t nextPlanId() {
    return g_nextPlanId;
}

bool schemaResetDetected() {
    return g_schemaResetDetected;
}

}
