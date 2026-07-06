#include "IrrigationConfig.h"

#include <stdio.h>
#include <string.h>

namespace Irrigation {

namespace {

void setName(char* target, size_t targetSize, const char* prefix, uint8_t number) {
    if (targetSize == 0) {
        return;
    }
    snprintf(target, targetSize, "%s %u", prefix, static_cast<unsigned>(number));
    target[targetSize - 1] = '\0';
}

void clearPlan(WateringPlan& plan, uint8_t id) {
    plan.id = id;
    plan.used = false;
    plan.enabled = false;
    setName(plan.name, sizeof(plan.name), "Plan", id);

    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        plan.startTimes[i].enabled = false;
        plan.startTimes[i].minuteOfDay = kInvalidMinuteOfDay;
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        plan.zoneDurationSec[i] = 0;
    }
}

bool validateZones(const IrrigationConfig& config, const char** error) {
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        if (zone.id != i + 1) {
            if (error != nullptr) {
                *error = "zone_id_mismatch";
            }
            return false;
        }
        if (zone.valveIndex != i) {
            if (error != nullptr) {
                *error = "zone_valve_index_mismatch";
            }
            return false;
        }
        if (zone.defaultDurationSec > config.valve.maxZoneDurationSec) {
            if (error != nullptr) {
                *error = "zone_default_duration_too_long";
            }
            return false;
        }
    }
    return true;
}

bool validatePlans(const IrrigationConfig& config, const char** error) {
    for (uint8_t planIndex = 0; planIndex < kMaxPlans; ++planIndex) {
        const WateringPlan& plan = config.plans[planIndex];
        if (plan.id != planIndex + 1) {
            if (error != nullptr) {
                *error = "plan_id_mismatch";
            }
            return false;
        }

        for (uint8_t startIndex = 0; startIndex < kMaxPlanStartTimes; ++startIndex) {
            const StartTime& startTime = plan.startTimes[startIndex];
            if (startTime.enabled && !isValidMinuteOfDay(startTime.minuteOfDay)) {
                if (error != nullptr) {
                    *error = "plan_start_time_invalid";
                }
                return false;
            }
        }

        for (uint8_t zoneIndex = 0; zoneIndex < kMaxZones; ++zoneIndex) {
            if (plan.zoneDurationSec[zoneIndex] > config.valve.maxZoneDurationSec) {
                if (error != nullptr) {
                    *error = "plan_zone_duration_too_long";
                }
                return false;
            }
        }
    }
    return true;
}

} // namespace

void applyDefaultConfig(IrrigationConfig& config) {
    memset(&config, 0, sizeof(config));

    config.version = kConfigVersion;

    config.supply.pumpEnabled = false;
    config.supply.pumpStartDelayMs = 1000;
    config.supply.pumpStopDelayMs = 1000;
    config.supply.lowLevelEnabled = false;
    config.supply.lowLevelContactType = ContactType::NormallyOpen;
    config.supply.lowLevelDebounceMs = 1000;

    config.flow.pulsesPerLiter = 0;
    config.flow.startupGraceSec = 10;
    config.flow.noFlowConfirmSec = 10;
    config.flow.leakWindowSec = 30;
    config.flow.leakPulseThreshold = 3;
    config.flow.lowFlowPercent = 30;
    config.flow.highFlowPercent = 200;
    config.flow.lowHighFlowConfirmSec = 20;

    config.valve.pullInMs = 300;
    config.valve.holdPercent = 60;
    config.valve.maxZoneDurationSec = 120UL * 60UL;

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        ZoneConfig& zone = config.zones[i];
        zone.id = i + 1;
        zone.enabled = i < kDefaultEnabledZones;
        setName(zone.name, sizeof(zone.name), "Zone", zone.id);
        zone.defaultDurationSec = 0;
        zone.standardFlowMlPerMin = 0;
        zone.valveIndex = i;
    }

    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        clearPlan(config.plans[i], i + 1);
    }
}

bool isValidZoneId(uint8_t zoneId) {
    return zoneId >= 1 && zoneId <= kMaxZones;
}

bool isValidMinuteOfDay(uint16_t minuteOfDay) {
    return minuteOfDay < kMinutesPerDay;
}

bool isValidHoldPercent(uint8_t holdPercent) {
    return holdPercent >= 1 && holdPercent <= 100;
}

uint8_t zoneIndexFromId(uint8_t zoneId) {
    return isValidZoneId(zoneId) ? static_cast<uint8_t>(zoneId - 1) : kMaxZones;
}

uint8_t enabledZoneCount(const IrrigationConfig& config) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (config.zones[i].enabled) {
            ++count;
        }
    }
    return count;
}

uint8_t usedPlanCount(const IrrigationConfig& config) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        if (config.plans[i].used) {
            ++count;
        }
    }
    return count;
}

uint8_t enabledPlanCount(const IrrigationConfig& config) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        if (config.plans[i].used && config.plans[i].enabled) {
            ++count;
        }
    }
    return count;
}

uint8_t enabledStartTimeCount(const WateringPlan& plan) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kMaxPlanStartTimes; ++i) {
        if (plan.startTimes[i].enabled && isValidMinuteOfDay(plan.startTimes[i].minuteOfDay)) {
            ++count;
        }
    }
    return count;
}

bool planHasEffectiveStep(const IrrigationConfig& config, const WateringPlan& plan) {
    if (!plan.used) {
        return false;
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (config.zones[i].enabled && plan.zoneDurationSec[i] > 0) {
            return true;
        }
    }
    return false;
}

bool validateConfig(const IrrigationConfig& config, const char** error) {
    if (error != nullptr) {
        *error = nullptr;
    }

    if (config.version != kConfigVersion) {
        if (error != nullptr) {
            *error = "config_version_unsupported";
        }
        return false;
    }

    if (!isValidHoldPercent(config.valve.holdPercent)) {
        if (error != nullptr) {
            *error = "valve_hold_percent_invalid";
        }
        return false;
    }

    if (config.valve.maxZoneDurationSec == 0) {
        if (error != nullptr) {
            *error = "max_zone_duration_invalid";
        }
        return false;
    }

    if (!validateZones(config, error)) {
        return false;
    }

    if (!validatePlans(config, error)) {
        return false;
    }

    return true;
}

const char* runReasonToString(RunReason reason) {
    switch (reason) {
        case RunReason::None:
            return "none";
        case RunReason::ManualRequest:
            return "manual_request";
        case RunReason::PlanStartTime:
            return "plan_start_time";
        case RunReason::RunPlanNow:
            return "run_plan_now";
        case RunReason::CalibrationRequest:
            return "calibration_request";
        case RunReason::UserStop:
            return "user_stop";
        case RunReason::NoEffectiveStep:
            return "no_effective_step";
        case RunReason::ControllerBusy:
            return "controller_busy";
        case RunReason::PlanDisabled:
            return "plan_disabled";
        case RunReason::ZoneDisabled:
            return "zone_disabled";
        case RunReason::InvalidDuration:
            return "invalid_duration";
        case RunReason::ConfigInvalid:
            return "config_invalid";
        case RunReason::FlowNotCalibrated:
            return "flow_not_calibrated";
        case RunReason::TimeInvalid:
            return "time_invalid";
        case RunReason::NoFlow:
            return "no_flow";
        case RunReason::LowLevel:
            return "low_level";
        case RunReason::RebootRecoveredSafe:
            return "reboot_recovered_safe";
    }
    return "unknown";
}

} // namespace Irrigation
