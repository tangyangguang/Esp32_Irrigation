#pragma once

#include "domain/ZoneTypes.h"

namespace PlanStore {

static constexpr uint8_t MaxPlansPerZone = 6;
static_assert(MaxPlansPerZone == Irrigation::MaxPlansPerZone, "PlanStore slot count must match domain model");
static constexpr uint8_t TotalPlans = Irrigation::TotalPlanSlots;

void begin();
bool clear();
bool create(uint8_t zoneId, Irrigation::PlanDefinition* out);
bool create(uint8_t zoneId, const Irrigation::PlanDefinition& draft, Irrigation::PlanDefinition* out);
bool remove(uint32_t planId);
bool set(uint32_t planId, const Irrigation::PlanDefinition& plan);
bool getById(uint32_t planId, Irrigation::PlanDefinition* out);
const Irrigation::PlanDefinition& getBySlot(uint8_t zoneId, uint8_t slotIndex);
uint8_t countForZone(uint8_t zoneId);
uint8_t count();
bool validate(const Irrigation::PlanDefinition& plan);
bool shouldRunOnDate(const Irrigation::PlanDefinition& plan, uint32_t ymd);
bool validYmd(uint32_t ymd);
bool currentYmd(uint32_t* out);
uint32_t nextPlanId();
bool schemaResetDetected();

}
