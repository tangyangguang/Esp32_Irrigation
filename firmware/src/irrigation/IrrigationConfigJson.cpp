#include "IrrigationConfigJson.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <limits>

#include "IrrigationConfig.h"

namespace {

template <typename T>
bool readUnsigned(JsonObjectConst object, const char* key, T& target) {
    const JsonVariantConst value = object[key];
    if (!value.is<uint32_t>()) {
        return false;
    }
    const uint32_t number = value.as<uint32_t>();
    if (number > static_cast<uint32_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    target = static_cast<T>(number);
    return true;
}

bool readBoolean(JsonObjectConst object, const char* key, bool& target) {
    const JsonVariantConst value = object[key];
    if (!value.is<bool>()) {
        return false;
    }
    target = value.as<bool>();
    return true;
}

template <std::size_t N>
bool readText(JsonObjectConst object, const char* key, std::array<char, N>& target) {
    const JsonVariantConst value = object[key];
    if (!value.is<const char*>()) {
        return false;
    }
    const char* text = value.as<const char*>();
    const std::size_t length = std::strlen(text);
    if (length >= N) {
        return false;
    }
    std::snprintf(target.data(), target.size(), "%s", text);
    return true;
}

bool readValveDrive(JsonObjectConst root, IrrigationConfig& config) {
    const JsonObjectConst object = root["valve_drive"].as<JsonObjectConst>();
    return !object.isNull() &&
           readUnsigned(object, "pull_in_time_ms", config.valveDrive.pullInTimeMs) &&
           readUnsigned(object, "pwm_frequency_hz", config.valveDrive.pwmFrequencyHz) &&
           readUnsigned(object, "hold_duty_percent", config.valveDrive.holdDutyPercent);
}

bool readPump(JsonObjectConst root, IrrigationConfig& config) {
    const JsonObjectConst object = root["pump"].as<JsonObjectConst>();
    return !object.isNull() &&
           readBoolean(object, "enabled", config.pump.enabled) &&
           readUnsigned(object, "start_delay_ms", config.pump.startDelayMs) &&
           readUnsigned(object, "stop_to_valve_close_delay_ms", config.pump.stopToValveCloseDelayMs);
}

bool readFlow(JsonObjectConst root, IrrigationConfig& config) {
    const JsonObjectConst meter = root["flow_meter"].as<JsonObjectConst>();
    const JsonObjectConst protection = root["flow_protection"].as<JsonObjectConst>();
    uint8_t lowAction = 0;
    uint8_t highAction = 0;
    if (meter.isNull() || protection.isNull() ||
        !readUnsigned(meter, "pulses_per_liter_x100", config.flowMeter.pulsesPerLiterX100) ||
        !readUnsigned(protection, "flow_start_timeout_sec", config.flowProtection.flowStartTimeoutSec) ||
        !readUnsigned(protection, "no_flow_timeout_sec", config.flowProtection.noFlowTimeoutSec) ||
        !readUnsigned(protection, "unexpected_flow_delay_sec", config.flowProtection.unexpectedFlowDelaySec) ||
        !readUnsigned(protection, "unexpected_flow_window_sec", config.flowProtection.unexpectedFlowWindowSec) ||
        !readUnsigned(protection, "unexpected_flow_pulse_count", config.flowProtection.unexpectedFlowPulseCount) ||
        !readUnsigned(protection, "flow_deviation_confirm_sec", config.flowProtection.flowDeviationConfirmSec) ||
        !readUnsigned(protection, "low_flow_percent", config.flowProtection.lowFlowPercent) ||
        !readUnsigned(protection, "high_flow_percent", config.flowProtection.highFlowPercent) ||
        !readUnsigned(protection, "low_flow_action", lowAction) ||
        !readUnsigned(protection, "high_flow_action", highAction)) {
        return false;
    }
    config.flowProtection.lowFlowAction = static_cast<FlowAlertAction>(lowAction);
    config.flowProtection.highFlowAction = static_cast<FlowAlertAction>(highAction);
    return true;
}

bool readTimeSafety(JsonObjectConst root, IrrigationConfig& config) {
    const JsonObjectConst object = root["time_safety"].as<JsonObjectConst>();
    return !object.isNull() &&
           readUnsigned(object, "rtc_rollback_threshold_minutes", config.timeSafety.rtcRollbackThresholdMinutes) &&
           readUnsigned(object, "alive_checkpoint_hours", config.timeSafety.aliveCheckpointHours);
}

bool readZones(JsonObjectConst root, IrrigationConfig& config) {
    const JsonArrayConst zones = root["zones"].as<JsonArrayConst>();
    if (zones.isNull() || zones.size() != config.zones.size()) {
        return false;
    }
    for (std::size_t index = 0; index < config.zones.size(); ++index) {
        const JsonObjectConst object = zones[index].as<JsonObjectConst>();
        ZoneConfig& zone = config.zones[index];
        if (object.isNull() ||
            !readUnsigned(object, "id", zone.id) ||
            !readBoolean(object, "enabled", zone.enabled) ||
            !readText(object, "name", zone.name) ||
            !readUnsigned(object, "learned_flow_ml_per_minute", zone.learnedFlowMlPerMinute)) {
            return false;
        }
    }
    return true;
}

bool readPlans(JsonObjectConst root, IrrigationConfig& config) {
    const JsonArrayConst plans = root["plans"].as<JsonArrayConst>();
    if (plans.isNull() || plans.size() != config.plans.size()) {
        return false;
    }
    for (std::size_t index = 0; index < config.plans.size(); ++index) {
        const JsonObjectConst object = plans[index].as<JsonObjectConst>();
        WateringPlan& plan = config.plans[index];
        if (object.isNull() ||
            !readUnsigned(object, "id", plan.id) ||
            !readBoolean(object, "configured", plan.configured) ||
            !readBoolean(object, "schedule_enabled", plan.scheduleEnabled) ||
            !readText(object, "name", plan.name)) {
            return false;
        }

        const JsonArrayConst starts = object["start_minutes"].as<JsonArrayConst>();
        const JsonArrayConst durations = object["zone_duration_minutes"].as<JsonArrayConst>();
        if (starts.isNull() || starts.size() != plan.startMinutes.size() ||
            durations.isNull() || durations.size() != plan.zoneDurationMinutes.size()) {
            return false;
        }
        for (std::size_t item = 0; item < plan.startMinutes.size(); ++item) {
            if (!starts[item].is<uint16_t>()) {
                return false;
            }
            plan.startMinutes[item] = starts[item].as<uint16_t>();
        }
        for (std::size_t item = 0; item < plan.zoneDurationMinutes.size(); ++item) {
            if (!durations[item].is<uint16_t>()) {
                return false;
            }
            plan.zoneDurationMinutes[item] = durations[item].as<uint16_t>();
        }
    }
    return true;
}

}  // namespace

bool IrrigationConfigJson::encode(const IrrigationConfig& config, std::string& json) {
    if (!IrrigationConfigRules::validate(config)) {
        return false;
    }

    JsonDocument document;
    document["schema_version"] = config.schemaVersion;
    document["revision"] = config.revision;

    JsonObject valve = document["valve_drive"].to<JsonObject>();
    valve["pull_in_time_ms"] = config.valveDrive.pullInTimeMs;
    valve["pwm_frequency_hz"] = config.valveDrive.pwmFrequencyHz;
    valve["hold_duty_percent"] = config.valveDrive.holdDutyPercent;

    JsonObject pump = document["pump"].to<JsonObject>();
    pump["enabled"] = config.pump.enabled;
    pump["start_delay_ms"] = config.pump.startDelayMs;
    pump["stop_to_valve_close_delay_ms"] = config.pump.stopToValveCloseDelayMs;

    JsonObject meter = document["flow_meter"].to<JsonObject>();
    meter["pulses_per_liter_x100"] = config.flowMeter.pulsesPerLiterX100;

    JsonObject protection = document["flow_protection"].to<JsonObject>();
    protection["flow_start_timeout_sec"] = config.flowProtection.flowStartTimeoutSec;
    protection["no_flow_timeout_sec"] = config.flowProtection.noFlowTimeoutSec;
    protection["unexpected_flow_delay_sec"] = config.flowProtection.unexpectedFlowDelaySec;
    protection["unexpected_flow_window_sec"] = config.flowProtection.unexpectedFlowWindowSec;
    protection["unexpected_flow_pulse_count"] = config.flowProtection.unexpectedFlowPulseCount;
    protection["flow_deviation_confirm_sec"] = config.flowProtection.flowDeviationConfirmSec;
    protection["low_flow_percent"] = config.flowProtection.lowFlowPercent;
    protection["high_flow_percent"] = config.flowProtection.highFlowPercent;
    protection["low_flow_action"] = static_cast<uint8_t>(config.flowProtection.lowFlowAction);
    protection["high_flow_action"] = static_cast<uint8_t>(config.flowProtection.highFlowAction);

    JsonObject time = document["time_safety"].to<JsonObject>();
    time["rtc_rollback_threshold_minutes"] = config.timeSafety.rtcRollbackThresholdMinutes;
    time["alive_checkpoint_hours"] = config.timeSafety.aliveCheckpointHours;

    JsonArray zones = document["zones"].to<JsonArray>();
    for (const ZoneConfig& zone : config.zones) {
        JsonObject object = zones.add<JsonObject>();
        object["id"] = zone.id;
        object["enabled"] = zone.enabled;
        object["name"] = zone.name.data();
        object["learned_flow_ml_per_minute"] = zone.learnedFlowMlPerMinute;
    }

    JsonArray plans = document["plans"].to<JsonArray>();
    for (const WateringPlan& plan : config.plans) {
        JsonObject object = plans.add<JsonObject>();
        object["id"] = plan.id;
        object["configured"] = plan.configured;
        object["schedule_enabled"] = plan.scheduleEnabled;
        object["name"] = plan.name.data();
        JsonArray starts = object["start_minutes"].to<JsonArray>();
        for (const uint16_t minute : plan.startMinutes) {
            starts.add(minute);
        }
        JsonArray durations = object["zone_duration_minutes"].to<JsonArray>();
        for (const uint16_t duration : plan.zoneDurationMinutes) {
            durations.add(duration);
        }
    }

    json.clear();
    return serializeJson(document, json) > 0;
}

bool IrrigationConfigJson::decode(const char* json, std::size_t length, IrrigationConfig& config) {
    if (!json || length == 0) {
        return false;
    }
    JsonDocument document;
    if (deserializeJson(document, json, length)) {
        return false;
    }
    const JsonObjectConst root = document.as<JsonObjectConst>();
    IrrigationConfig decoded{};
    if (root.isNull() ||
        !readUnsigned(root, "schema_version", decoded.schemaVersion) ||
        !readUnsigned(root, "revision", decoded.revision) ||
        !readValveDrive(root, decoded) ||
        !readPump(root, decoded) ||
        !readFlow(root, decoded) ||
        !readTimeSafety(root, decoded) ||
        !readZones(root, decoded) ||
        !readPlans(root, decoded) ||
        !IrrigationConfigRules::validate(decoded)) {
        return false;
    }
    config = decoded;
    return true;
}
