#include "PlanService.h"

#include <stdio.h>

#include "ConfigStore.h"
#include "IrrigationConfig.h"

namespace Irrigation {

namespace {
char g_lastError[40] = "ok";

void copyPlanName(char* target, size_t targetSize, const char* value, uint8_t planId) {
    if (value != nullptr && value[0] != '\0') {
        snprintf(target, targetSize, "%s", value);
    } else {
        snprintf(target, targetSize, "Plan %u", static_cast<unsigned>(planId));
    }
    target[targetSize - 1] = '\0';
}

void resetPlanSlot(WateringPlan& plan, uint8_t planId) {
    plan.id = planId;
    plan.used = false;
    plan.enabled = false;
    copyPlanName(plan.name, sizeof(plan.name), nullptr, planId);

    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        plan.startTimes[i].enabled = false;
        plan.startTimes[i].minuteOfDay = kInvalidMinuteOfDay;
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        plan.zoneDurationSec[i] = 0;
    }
}

bool validatePlanForSave(const IrrigationConfig& config, const WateringPlan& plan, const char** error) {
    if (plan.id == 0 || plan.id > kMaxPlans) {
        if (error != nullptr) {
            *error = "plan_id_invalid";
        }
        return false;
    }

    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        if (plan.startTimes[i].enabled && !isValidMinuteOfDay(plan.startTimes[i].minuteOfDay)) {
            if (error != nullptr) {
                *error = "plan_start_time_invalid";
            }
            return false;
        }
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (plan.zoneDurationSec[i] > config.valve.maxZoneDurationSec) {
            if (error != nullptr) {
                *error = "plan_zone_duration_too_long";
            }
            return false;
        }
    }
    return true;
}

} // namespace

const WateringPlan* PlanService::find(uint8_t planId) {
    if (planId == 0 || planId > kMaxPlans) {
        setLastError("plan_id_invalid");
        return nullptr;
    }

    const WateringPlan& plan = ConfigStore::config().plans[planId - 1];
    if (!plan.used) {
        setLastError("plan_unused");
        return nullptr;
    }

    setLastError("ok");
    return &plan;
}

bool PlanService::snapshot(uint8_t planId, PlanSnapshot& out) {
    const WateringPlan* plan = find(planId);
    if (plan == nullptr) {
        return false;
    }

    out.id = plan->id;
    out.used = plan->used;
    out.enabled = plan->enabled;
    out.name = plan->name;
    out.startTimeCount = 0;
    out.nextRunEpoch = 0;

    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        out.startTimes[i] = plan->startTimes[i].minuteOfDay;
        if (plan->startTimes[i].enabled && isValidMinuteOfDay(plan->startTimes[i].minuteOfDay)) {
            ++out.startTimeCount;
        }
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        out.zoneDurationSec[i] = plan->zoneDurationSec[i];
    }

    setLastError("ok");
    return true;
}

bool PlanService::createPlan(const char* name, uint8_t& planIdOut) {
    IrrigationConfig next = ConfigStore::config();

    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        WateringPlan& plan = next.plans[i];
        if (plan.used) {
            continue;
        }

        resetPlanSlot(plan, i + 1);
        plan.used = true;
        plan.enabled = false;
        copyPlanName(plan.name, sizeof(plan.name), name, plan.id);

        if (!ConfigStore::save(next)) {
            setLastError(ConfigStore::lastError());
            return false;
        }

        planIdOut = plan.id;
        setLastError("ok");
        return true;
    }

    setLastError("plan_slots_full");
    return false;
}

bool PlanService::savePlan(const WateringPlan& plan) {
    if (plan.id == 0 || plan.id > kMaxPlans) {
        setLastError("plan_id_invalid");
        return false;
    }

    IrrigationConfig next = ConfigStore::config();
    WateringPlan& target = next.plans[plan.id - 1];
    const char* error = nullptr;
    if (!validatePlanForSave(next, plan, &error)) {
        setLastError(error);
        return false;
    }

    target = plan;
    target.id = plan.id;
    target.used = true;
    copyPlanName(target.name, sizeof(target.name), plan.name, target.id);

    if (!ConfigStore::save(next)) {
        setLastError(ConfigStore::lastError());
        return false;
    }

    setLastError("ok");
    return true;
}

bool PlanService::deletePlan(uint8_t planId) {
    if (planId == 0 || planId > kMaxPlans) {
        setLastError("plan_id_invalid");
        return false;
    }

    IrrigationConfig next = ConfigStore::config();
    resetPlanSlot(next.plans[planId - 1], planId);

    if (!ConfigStore::save(next)) {
        setLastError(ConfigStore::lastError());
        return false;
    }

    setLastError("ok");
    return true;
}

bool PlanService::setEnabled(uint8_t planId, bool enabled) {
    if (planId == 0 || planId > kMaxPlans) {
        setLastError("plan_id_invalid");
        return false;
    }

    IrrigationConfig next = ConfigStore::config();
    WateringPlan& plan = next.plans[planId - 1];
    if (!plan.used) {
        setLastError("plan_unused");
        return false;
    }

    plan.enabled = enabled;
    if (!ConfigStore::save(next)) {
        setLastError(ConfigStore::lastError());
        return false;
    }

    setLastError("ok");
    return true;
}

bool PlanService::canRunNow(uint8_t planId, RunReason& reason) {
    const WateringPlan* plan = find(planId);
    if (plan == nullptr) {
        reason = RunReason::ConfigInvalid;
        return false;
    }

    if (!plan->enabled) {
        reason = RunReason::PlanDisabled;
        setLastError("plan_disabled");
        return false;
    }

    if (!planHasEffectiveStep(ConfigStore::config(), *plan)) {
        reason = RunReason::NoEffectiveStep;
        setLastError("no_effective_step");
        return false;
    }

    reason = RunReason::RunPlanNow;
    setLastError("ok");
    return true;
}

bool PlanService::buildSteps(uint8_t planId, WateringStep* out, uint8_t maxSteps, uint8_t& stepCount, RunReason& reason) {
    stepCount = 0;
    if (out == nullptr || maxSteps == 0) {
        reason = RunReason::ConfigInvalid;
        setLastError("step_buffer_invalid");
        return false;
    }

    if (!canRunNow(planId, reason)) {
        return false;
    }

    const WateringPlan& plan = ConfigStore::config().plans[planId - 1];
    for (uint8_t i = 0; i < kMaxZones && stepCount < maxSteps; ++i) {
        const ZoneConfig& zone = ConfigStore::config().zones[i];
        if (!zone.enabled || plan.zoneDurationSec[i] == 0) {
            continue;
        }
        out[stepCount].zoneId = zone.id;
        out[stepCount].targetDurationSec = plan.zoneDurationSec[i];
        ++stepCount;
    }

    if (stepCount == 0) {
        reason = RunReason::NoEffectiveStep;
        setLastError("no_effective_step");
        return false;
    }

    reason = RunReason::RunPlanNow;
    setLastError("ok");
    return true;
}

const char* PlanService::lastError() {
    return g_lastError;
}

void PlanService::setLastError(const char* error) {
    snprintf(g_lastError, sizeof(g_lastError), "%s", error != nullptr ? error : "unknown");
    g_lastError[sizeof(g_lastError) - 1] = '\0';
}

} // namespace Irrigation
