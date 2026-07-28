#include "IrrigationMqttProtocol.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <limits>

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

template <std::size_t N>
bool readText(JsonObjectConst object,
              const char* key,
              std::array<char, N>& target) {
    const JsonVariantConst value = object[key];
    if (!value.is<const char*>()) {
        return false;
    }
    const char* text = value.as<const char*>();
    const std::size_t length = std::strlen(text);
    if (length == 0 || length >= N) {
        return false;
    }
    std::snprintf(target.data(), target.size(), "%s", text);
    return true;
}

bool actionIs(const char* actual, const char* expected) {
    return actual && std::strcmp(actual, expected) == 0;
}

bool readPlanId(JsonObjectConst args, uint8_t& planId) {
    return !args.isNull() &&
           readUnsigned(args, "id", planId) &&
           planId >= 1 &&
           planId <= kWateringPlanCount;
}

bool readDurations(JsonObjectConst args,
                   std::array<uint16_t, BoardPins::kZoneCount>& durations) {
    const JsonArrayConst values =
        args["durations"].as<JsonArrayConst>();
    if (values.isNull() || values.size() != durations.size()) {
        return false;
    }
    for (std::size_t index = 0; index < durations.size(); ++index) {
        if (!values[index].is<uint16_t>()) {
            return false;
        }
        durations[index] = values[index].as<uint16_t>();
    }
    return true;
}

bool readSetPlan(JsonObjectConst root,
                 JsonObjectConst args,
                 IrrigationMqttProtocol::Command& command) {
    const JsonVariantConst enabled = args["enabled"];
    const JsonArrayConst starts = args["starts"].as<JsonArrayConst>();
    if (!readUnsigned(root, "revision", command.revision) ||
        command.revision == 0 ||
        !readPlanId(args, command.planId) ||
        !readText(args, "name", command.planName) ||
        !enabled.is<bool>() ||
        starts.isNull() ||
        starts.size() > command.startMinutes.size() ||
        !readDurations(args, command.zoneDurationMinutes)) {
        return false;
    }
    command.planEnabled = enabled.as<bool>();
    command.startMinutes.fill(kUnusedStartMinute);
    for (std::size_t index = 0; index < starts.size(); ++index) {
        if (!starts[index].is<uint16_t>()) {
            return false;
        }
        const uint16_t minute = starts[index].as<uint16_t>();
        if (minute >= 24U * 60U) {
            return false;
        }
        command.startMinutes[index] = minute;
    }
    return true;
}

}  // namespace

bool IrrigationMqttProtocol::parse(const uint8_t* payload,
                                   std::size_t payloadLength,
                                   Command& command,
                                   ParseError& error) {
    error = ParseError::None;
    command = {};
    if (!payload || payloadLength == 0) {
        error = ParseError::MalformedJson;
        return false;
    }

    JsonDocument document;
    if (deserializeJson(document, payload, payloadLength)) {
        error = ParseError::MalformedJson;
        return false;
    }
    const JsonObjectConst root = document.as<JsonObjectConst>();
    uint8_t version = 0;
    if (root.isNull() ||
        !readUnsigned(root, "v", version) ||
        !readText(root, "id", command.id)) {
        error = ParseError::InvalidEnvelope;
        return false;
    }
    if (version != kVersion) {
        error = ParseError::UnsupportedVersion;
        return false;
    }

    const JsonVariantConst actionValue = root["action"];
    if (!actionValue.is<const char*>()) {
        error = ParseError::InvalidEnvelope;
        return false;
    }
    const char* action = actionValue.as<const char*>();
    const JsonObjectConst args = root["args"].as<JsonObjectConst>();

    if (actionIs(action, "set_plan")) {
        command.action = Action::SetPlan;
        if (!readSetPlan(root, args, command)) {
            error = ParseError::InvalidArguments;
            return false;
        }
    } else if (actionIs(action, "delete_plan")) {
        command.action = Action::DeletePlan;
        if (!readUnsigned(root, "revision", command.revision) ||
            command.revision == 0 ||
            !readPlanId(args, command.planId)) {
            error = ParseError::InvalidArguments;
            return false;
        }
    } else if (actionIs(action, "pause_automatic")) {
        command.action = Action::PauseAutomatic;
        if (args.isNull() ||
            !readUnsigned(args, "resume_at", command.resumeAtEpoch)) {
            error = ParseError::InvalidArguments;
            return false;
        }
    } else if (actionIs(action, "resume_automatic")) {
        command.action = Action::ResumeAutomatic;
    } else if (actionIs(action, "start_plan")) {
        command.action = Action::StartPlan;
        if (!readPlanId(args, command.planId)) {
            error = ParseError::InvalidArguments;
            return false;
        }
    } else if (actionIs(action, "start_manual")) {
        command.action = Action::StartManual;
        if (args.isNull() ||
            !readDurations(args, command.zoneDurationMinutes)) {
            error = ParseError::InvalidArguments;
            return false;
        }
    } else if (actionIs(action, "stop")) {
        command.action = Action::Stop;
    } else {
        error = ParseError::UnsupportedAction;
        return false;
    }
    return true;
}

const char* IrrigationMqttProtocol::parseErrorName(ParseError error) {
    switch (error) {
        case ParseError::None: return "none";
        case ParseError::MalformedJson: return "malformed_json";
        case ParseError::InvalidEnvelope: return "invalid_envelope";
        case ParseError::UnsupportedVersion: return "unsupported_version";
        case ParseError::UnsupportedAction: return "unsupported_action";
        case ParseError::InvalidArguments: return "invalid_arguments";
    }
    return "unknown";
}
