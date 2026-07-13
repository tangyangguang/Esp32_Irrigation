#include "IrrigationConfig.h"

#include <cstdio>

namespace {

template <std::size_t N>
void setText(std::array<char, N>& target, const char* value) {
    std::snprintf(target.data(), target.size(), "%s", value);
}

template <std::size_t N>
bool isTerminated(const std::array<char, N>& value) {
    for (const char ch : value) {
        if (ch == '\0') {
            return true;
        }
    }
    return false;
}

}  // namespace

IrrigationConfig IrrigationConfigDefaults::create() {
    IrrigationConfig config{};
    config.schemaVersion = kIrrigationConfigSchemaVersion;
    config.revision = 1;

    config.valveDrive = {3000, 20000, 75};
    config.pump = {false, 0, 1000};
    config.flowMeter = {250};
    config.flowProtection = {
        20,
        10,
        30,
        30,
        3,
        20,
        30,
        200,
        FlowAlertAction::AlertOnly,
        FlowAlertAction::AlertOnly,
    };
    config.timeSafety = {5, 12};

    for (std::size_t index = 0; index < config.zones.size(); ++index) {
        ZoneConfig& zone = config.zones[index];
        zone.id = static_cast<uint8_t>(index + 1);
        zone.enabled = index < 2;
        char name[kObjectNameCapacity];
        std::snprintf(name, sizeof(name), "区域 %u", static_cast<unsigned>(zone.id));
        setText(zone.name, name);
        zone.learnedFlowMlPerMinute = 0;
    }

    for (std::size_t index = 0; index < config.plans.size(); ++index) {
        WateringPlan& plan = config.plans[index];
        plan.id = static_cast<uint8_t>(index + 1);
        plan.configured = false;
        plan.scheduleEnabled = false;
        plan.startMinutes.fill(kUnusedStartMinute);
        plan.zoneDurationMinutes.fill(0);
    }

    return config;
}

bool IrrigationConfigDefaults::validate(const IrrigationConfig& config) {
    if (config.schemaVersion != kIrrigationConfigSchemaVersion || config.revision == 0) {
        return false;
    }
    if (config.valveDrive.pullInTimeMs == 0 ||
        config.valveDrive.pwmFrequencyHz < 1000 ||
        config.valveDrive.pwmFrequencyHz > 25000 ||
        config.valveDrive.holdDutyPercent == 0 ||
        config.valveDrive.holdDutyPercent > 100) {
        return false;
    }
    if (config.flowMeter.pulsesPerLiter == 0 ||
        config.flowProtection.flowStartTimeoutSec == 0 ||
        config.flowProtection.noFlowTimeoutSec == 0 ||
        config.flowProtection.unexpectedFlowWindowSec == 0 ||
        config.flowProtection.unexpectedFlowPulseCount == 0 ||
        config.flowProtection.lowFlowPercent >= config.flowProtection.highFlowPercent) {
        return false;
    }
    if (config.timeSafety.rtcRollbackThresholdMinutes < 1 ||
        config.timeSafety.rtcRollbackThresholdMinutes > 60 ||
        config.timeSafety.aliveCheckpointHours > 168) {
        return false;
    }

    for (std::size_t index = 0; index < config.zones.size(); ++index) {
        const ZoneConfig& zone = config.zones[index];
        if (zone.id != index + 1 || !isTerminated(zone.name)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < config.plans.size(); ++index) {
        const WateringPlan& plan = config.plans[index];
        if (plan.id != index + 1 || !isTerminated(plan.name)) {
            return false;
        }
        for (const uint16_t minute : plan.startMinutes) {
            if (minute != kUnusedStartMinute && minute >= 24U * 60U) {
                return false;
            }
        }
        for (const uint16_t duration : plan.zoneDurationMinutes) {
            if (duration > 120) {
                return false;
            }
        }
    }
    return true;
}
