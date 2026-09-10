#include "IrrigationConfigJson.h"

#include "IrrigationJsonCapacity.h"

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
            !readUnsigned(object, "baseline_pulse_rate_x10000",
                          zone.baselinePulseRateX10000)) {
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

    DynamicJsonDocument document(IrrigationJsonCapacity::config);
    document["schema_version"] = config.schemaVersion;
    document["revision"] = config.revision;

    JsonArray zones = document["zones"].to<JsonArray>();
    for (const ZoneConfig& zone : config.zones) {
        JsonObject object = zones.createNestedObject();
        object["id"] = zone.id;
        object["enabled"] = zone.enabled;
        object["name"] = zone.name.data();
        object["baseline_pulse_rate_x10000"] = zone.baselinePulseRateX10000;
    }

    JsonArray plans = document["plans"].to<JsonArray>();
    for (const WateringPlan& plan : config.plans) {
        JsonObject object = plans.createNestedObject();
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

    if (document.overflowed()) return false;
    json.clear();
    return serializeJson(document, json) > 0;
}

bool IrrigationConfigJson::decode(const char* json, std::size_t length, IrrigationConfig& config) {
    if (!json || length == 0) {
        return false;
    }
    DynamicJsonDocument document(IrrigationJsonCapacity::config);
    if (deserializeJson(document, json, length)) {
        return false;
    }
    const JsonObjectConst root = document.as<JsonObjectConst>();
    IrrigationConfig decoded = IrrigationConfigRules::createDefault();
    if (root.isNull() || root.size() != 4 ||
        !readUnsigned(root, "schema_version", decoded.schemaVersion) ||
        !readUnsigned(root, "revision", decoded.revision) ||
        !readZones(root, decoded) ||
        !readPlans(root, decoded) ||
        !IrrigationConfigRules::validate(decoded)) {
        return false;
    }
    config = decoded;
    return true;
}
