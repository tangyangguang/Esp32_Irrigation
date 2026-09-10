#include "IrrigationIotProtocol.h"

#include "IrrigationJsonCapacity.h"
#include <CommandInput.h>
#include "generated/IrrigationModel.h"

#include <cstdio>
#include <cstring>

namespace IrrigationIotProtocol {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void civilFromDays(int64_t days, int32_t& year, uint32_t& month, uint32_t& day) {
    days += 719468LL;
    const int64_t era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    const uint32_t dayOfEra = static_cast<uint32_t>(days - era * 146097LL);
    const uint32_t yearOfEra =
        (dayOfEra - dayOfEra / 1460U + dayOfEra / 36524U -
         dayOfEra / 146096U) /
        365U;
    year = static_cast<int32_t>(yearOfEra) + static_cast<int32_t>(era * 400LL);
    const uint32_t dayOfYear =
        dayOfEra - (365U * yearOfEra + yearOfEra / 4U - yearOfEra / 100U);
    const uint32_t monthPrime = (5U * dayOfYear + 2U) / 153U;
    day = dayOfYear - (153U * monthPrime + 2U) / 5U + 1U;
    month = monthPrime + (monthPrime < 10U ? 3U : static_cast<uint32_t>(-9));
    year += month <= 2U;
}

bool validBusinessName(const char* value) {
    return IrrigationConfigRules::validateName(value, kObjectNameCapacity);
}

// Called only after SDK schema validation. Keep the destination byte bound;
// Unicode/type/character-count checks belong to the generated model validator.
bool copyName(JsonVariantConst value, char* output, std::size_t outputLength) {
    const JsonString text = value.as<JsonString>();
    if (!text.c_str() || text.size() >= outputLength ||
        std::strlen(text.c_str()) != text.size()) return false;
    std::memcpy(output, text.c_str(), text.size() + 1);
    return true;
}

bool readUint(JsonVariantConst value, uint32_t minimum, uint32_t maximum,
              uint32_t& output) {
    if (!value.is<uint32_t>()) {
        return false;
    }
    output = value.as<uint32_t>();
    return output >= minimum && output <= maximum;
}

bool parseZoneDuration(JsonObjectConst object, ZoneDuration& zone) {
    uint32_t zoneId = 0;
    uint32_t duration = 0;
    if (!readUint(object["zoneId"], 1, BoardPins::kZoneCount, zoneId) ||
        !readUint(object["durationMinutes"], 1,
                  kMaximumConfigurableZoneDurationMinutes, duration)) {
        return false;
    }
    zone.zoneId = static_cast<uint8_t>(zoneId);
    zone.durationMinutes = static_cast<uint16_t>(duration);
    return true;
}

bool parsePlans(JsonObjectConst parameters, Command& command) {
    if (!parameters["revision"].is<uint32_t>() ||
        !parameters["plans"].is<JsonArrayConst>()) {
        return false;
    }
    const JsonArrayConst plans = parameters["plans"].as<JsonArrayConst>();
    if (plans.size() > kWateringPlanCount) {
        return false;
    }
    command.expectedRevision = parameters["revision"].as<uint32_t>();
    for (JsonVariantConst planValue : plans) {
        if (!planValue.is<JsonObjectConst>()) {
            return false;
        }
        const JsonObjectConst object = planValue.as<JsonObjectConst>();
        uint32_t id = 0;
        if (!readUint(object["id"], 1, kWateringPlanCount, id) ||
            !object["automaticEnabled"].is<bool>() ||
            !object["startMinutes"].is<JsonArrayConst>() ||
            !object["zones"].is<JsonArrayConst>()) {
            return false;
        }
        PlanValue& plan = command.plans[command.planCount];
        plan.id = static_cast<uint8_t>(id);
        if (!copyName(object["name"], plan.name, sizeof(plan.name))) {
            return false;
        }
        plan.automaticEnabled = object["automaticEnabled"].as<bool>();
        const JsonArrayConst starts = object["startMinutes"].as<JsonArrayConst>();
        if (starts.size() > kPlanStartTimeCount) {
            return false;
        }
        for (JsonVariantConst startValue : starts) {
            uint32_t minute = 0;
            if (!readUint(startValue, 0, 1439, minute)) {
                return false;
            }
            for (uint8_t index = 0; index < plan.startMinuteCount; ++index) {
                if (plan.startMinutes[index] == minute) {
                    return false;
                }
            }
            plan.startMinutes[plan.startMinuteCount++] =
                static_cast<uint16_t>(minute);
        }
        const JsonArrayConst zones = object["zones"].as<JsonArrayConst>();
        if (zones.size() > BoardPins::kZoneCount) {
            return false;
        }
        for (JsonVariantConst zoneValue : zones) {
            if (!zoneValue.is<JsonObjectConst>()) {
                return false;
            }
            ZoneDuration zone;
            if (!parseZoneDuration(zoneValue.as<JsonObjectConst>(), zone)) {
                return false;
            }
            plan.zones[plan.zoneCount++] = zone;
        }
        ++command.planCount;
    }
    return true;
}

bool parseAutomatic(JsonObjectConst parameters, Command& command) {
    if (!parameters["mode"].is<const char*>()) {
        return false;
    }
    const char* mode = parameters["mode"].as<const char*>();
    if (std::strcmp(mode, "enabled") == 0) {
        command.automaticMode = AutomaticMode::Enabled;
        return parameters["resumeAtEpoch"].isNull();
    }
    if (std::strcmp(mode, "paused-indefinitely") == 0) {
        command.automaticMode = AutomaticMode::PausedIndefinitely;
        return parameters["resumeAtEpoch"].isNull();
    }
    if (std::strcmp(mode, "paused-until") == 0 &&
        parameters["resumeAtEpoch"].is<uint32_t>()) {
        command.automaticMode = AutomaticMode::PausedUntil;
        command.resumeAtEpoch = parameters["resumeAtEpoch"].as<uint32_t>();
        return true;
    }
    return false;
}

bool parseManual(JsonObjectConst parameters, Command& command) {
    if (!parameters["zones"].is<JsonArrayConst>()) {
        return false;
    }
    const JsonArrayConst zones = parameters["zones"].as<JsonArrayConst>();
    if (zones.size() < 1U || zones.size() > BoardPins::kZoneCount) {
        return false;
    }
    for (JsonVariantConst zoneValue : zones) {
        if (!zoneValue.is<JsonObjectConst>()) {
            return false;
        }
        ZoneDuration zone;
        if (!parseZoneDuration(zoneValue.as<JsonObjectConst>(), zone)) {
            return false;
        }
        command.zones[command.zoneCount++] = zone;
    }
    return true;
}

bool parseStop(JsonObjectConst parameters) {
    return parameters.size() == 0;
}

bool parseSingleOutput(JsonObjectConst parameters, Command& command) {
    if (!parameters["zoneId"].is<uint32_t>() ||
        !parameters["mode"].is<const char*>()) {
        return false;
    }
    uint32_t zoneId = 0;
    if (!readUint(parameters["zoneId"], 1, BoardPins::kZoneCount, zoneId)) {
        return false;
    }
    command.zoneId = static_cast<uint8_t>(zoneId);
    const char* mode = parameters["mode"].as<const char*>();
    if (std::strcmp(mode, "duration") == 0) {
        command.singleOutputMode = SingleOutputMode::Duration;
        return readUint(parameters["durationSeconds"], 1, 43200,
                        command.durationSeconds);
    }
    if (std::strcmp(mode, "volume") == 0) {
        command.singleOutputMode = SingleOutputMode::Volume;
        return readUint(parameters["targetWaterMl"], 100, 1000000,
                        command.targetWaterMl);
    }
    return false;
}

void hashBytes(uint64_t& hash, const void* data, std::size_t length) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (std::size_t index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

template <typename T>
void hashValue(uint64_t& hash, const T& value) {
    hashBytes(hash, &value, sizeof(value));
}

void calculateSignature(Command& command) {
    uint64_t hash = kFnvOffset;
    hashBytes(hash, command.commandId, std::strlen(command.commandId));
    hashValue(hash, command.kind);
    hashValue(hash, command.issuedAtMs);
    hashValue(hash, command.expiresAtMs);
    hashValue(hash, command.expectedRevision);
    hashValue(hash, command.planCount);
    for (uint8_t index = 0; index < command.planCount; ++index) {
        const PlanValue& plan = command.plans[index];
        hashValue(hash, plan.id);
        hashBytes(hash, plan.name, std::strlen(plan.name));
        hashValue(hash, plan.automaticEnabled);
        hashValue(hash, plan.startMinuteCount);
        for (uint8_t start = 0; start < plan.startMinuteCount; ++start) {
            hashValue(hash, plan.startMinutes[start]);
        }
        hashValue(hash, plan.zoneCount);
        for (uint8_t zone = 0; zone < plan.zoneCount; ++zone) {
            hashValue(hash, plan.zones[zone].zoneId);
            hashValue(hash, plan.zones[zone].durationMinutes);
        }
    }
    hashValue(hash, command.automaticMode);
    hashValue(hash, command.resumeAtEpoch);
    hashValue(hash, command.zoneCount);
    for (uint8_t zone = 0; zone < command.zoneCount; ++zone) {
        hashValue(hash, command.zones[zone].zoneId);
        hashValue(hash, command.zones[zone].durationMinutes);
    }
    hashValue(hash, command.zoneId);
    hashValue(hash, command.singleOutputMode);
    hashValue(hash, command.durationSeconds);
    hashValue(hash, command.targetWaterMl);
    command.signature = hash;
}

CommandKind commandKind(const char* capability, bool& valid) {
    valid = true;
    if (std::strcmp(capability, "parameter.plans") == 0) {
        return CommandKind::Plans;
    }
    if (std::strcmp(capability, "parameter.automatic-watering") == 0) {
        return CommandKind::AutomaticWatering;
    }
    if (std::strcmp(capability, "operation.start-manual") == 0) {
        return CommandKind::StartManual;
    }
    if (std::strcmp(capability, "operation.stop") == 0) {
        return CommandKind::Stop;
    }
    if (std::strcmp(capability, "operation.single-output") == 0) {
        return CommandKind::SingleOutput;
    }
    valid = false;
    return CommandKind::Stop;
}

bool parseParameters(JsonObjectConst parameters, Command& command) {
    switch (command.kind) {
        case CommandKind::Plans:
            return parsePlans(parameters, command);
        case CommandKind::AutomaticWatering:
            return parseAutomatic(parameters, command);
        case CommandKind::StartManual:
            return parseManual(parameters, command);
        case CommandKind::Stop:
            return parseStop(parameters);
        case CommandKind::SingleOutput:
            return parseSingleOutput(parameters, command);
    }
    return false;
}

}  // namespace

ParseError parseCommand(const CommandPacket& packet,
                        const iot_device::PlatformIdentity& identity,
                        Command& command) {
    command = {};
    if (!packet.topic || !iot_device::platformTopicMatches(identity, iot_device::Channel::Command,
                                                          packet.topic, std::strlen(packet.topic)))
        return ParseError::UnexpectedTopic;
    if (packet.qos != 1U) return ParseError::InvalidQos;
    if (packet.retain) return ParseError::RetainedCommand;
    if (!packet.payload || !packet.payloadLength || packet.payloadLength > 4096U)
        return ParseError::InvalidJson;

    DynamicJsonDocument document(IrrigationJsonCapacity::command);
    iot_device::CommandView input;
    const auto error = iot_device::parseCommand(
        iot_device::model_irrigation_controller_6_zone::contract, identity,
        packet.topic, std::strlen(packet.topic), packet.qos, packet.retain,
        packet.payload, packet.payloadLength, 0, document, input);
    switch (error) {
        case iot_device::CommandError::None: break;
        case iot_device::CommandError::UnknownCapability: return ParseError::UnknownCapability;
        case iot_device::CommandError::InvalidExpiry:
        case iot_device::CommandError::Expired: return ParseError::InvalidExpiry;
        case iot_device::CommandError::SchemaRejected: return ParseError::SchemaMismatch;
        case iot_device::CommandError::InvalidFields: return ParseError::InvalidFields;
        case iot_device::CommandError::InvalidProtocol: return ParseError::InvalidProtocol;
        case iot_device::CommandError::InvalidCommandId: return ParseError::InvalidCommandId;
        case iot_device::CommandError::InvalidTimestamp: return ParseError::InvalidTimestamp;
        case iot_device::CommandError::InvalidUtf8: return ParseError::InvalidUtf8;
        default: return ParseError::InvalidJson;
    }
    // The SDK owns the platform envelope and model validation. Conversion and
    // cross-field irrigation constraints stay here; no second envelope parser.
    std::strcpy(command.commandId, input.commandId);
    bool validKind = false;
    command.kind = commandKind(input.capability->key, validKind);
    if (!validKind) return ParseError::UnknownCapability;
    command.issuedAtMs = input.issuedAtMs;
    command.expiresAtMs = input.expiresAtMs;
    if (command.issuedAtMs / 1000ULL > UINT32_MAX || command.expiresAtMs / 1000ULL > UINT32_MAX)
        return ParseError::InvalidTimestamp;
    if (!parseParameters(input.parameters, command)) return ParseError::SchemaMismatch;
    calculateSignature(command);
    return ParseError::None;
}

const char* parseErrorName(ParseError error) {
    switch (error) {
        case ParseError::None: return "none";
        case ParseError::UnexpectedTopic: return "unexpected_topic";
        case ParseError::InvalidQos: return "invalid_qos";
        case ParseError::RetainedCommand: return "retained_command";
        case ParseError::InvalidUtf8: return "invalid_utf8";
        case ParseError::InvalidJson: return "invalid_json";
        case ParseError::InvalidFields: return "invalid_fields";
        case ParseError::InvalidProtocol: return "invalid_protocol";
        case ParseError::InvalidCommandId: return "invalid_command_id";
        case ParseError::UnknownCapability: return "unknown_capability";
        case ParseError::InvalidTimestamp: return "invalid_timestamp";
        case ParseError::InvalidExpiry: return "invalid_expiry";
        case ParseError::SchemaMismatch: return "schema_mismatch";
    }
    return "unknown";
}

const char* capabilityKey(CommandKind kind) {
    switch (kind) {
        case CommandKind::Plans: return "parameter.plans";
        case CommandKind::AutomaticWatering:
            return "parameter.automatic-watering";
        case CommandKind::StartManual: return "operation.start-manual";
        case CommandKind::Stop: return "operation.stop";
        case CommandKind::SingleOutput: return "operation.single-output";
    }
    return "";
}

uint32_t commandTtlMs(CommandKind kind) {
    return kind == CommandKind::StartManual || kind == CommandKind::SingleOutput
               ? 30000U
               : 10000U;
}

Rejection evaluateCommand(const Command& command,
                          const BusinessContext& context) {
    if (command.expiresAtMs <= context.nowMs) {
        return Rejection::Expired;
    }
    if (!context.ready) {
        return Rejection::NotReady;
    }
    if (command.kind == CommandKind::Stop) {
        return context.activeKind == ActiveKind::Learning
                   ? Rejection::MaintenanceActivity
                   : Rejection::None;
    }
    if (!context.recordWritable) {
        return Rejection::NotReady;
    }
    if (command.kind == CommandKind::Plans) {
        if (command.expectedRevision != context.plansRevision) {
            return Rejection::RevisionConflict;
        }
        bool planIds[kWateringPlanCount]{};
        uint16_t scheduledMinutes[kWateringPlanCount * kPlanStartTimeCount]{};
        uint8_t scheduledMinuteCount = 0;
        for (uint8_t planIndex = 0; planIndex < command.planCount; ++planIndex) {
            const PlanValue& plan = command.plans[planIndex];
            if (planIds[plan.id - 1U] || !validBusinessName(plan.name)) {
                return Rejection::PlanConflict;
            }
            planIds[plan.id - 1U] = true;
            bool zoneIds[BoardPins::kZoneCount]{};
            for (uint8_t zoneIndex = 0; zoneIndex < plan.zoneCount; ++zoneIndex) {
                const ZoneDuration& zone = plan.zones[zoneIndex];
                if (zoneIds[zone.zoneId - 1U]) {
                    return Rejection::PlanConflict;
                }
                zoneIds[zone.zoneId - 1U] = true;
                if (!context.enabledZones[zone.zoneId - 1U]) {
                    return Rejection::ZoneUnavailable;
                }
                if (zone.durationMinutes > context.maximumZoneDurationMinutes) {
                    return Rejection::DurationLimit;
                }
            }
            if (plan.automaticEnabled &&
                (plan.startMinuteCount == 0U || plan.zoneCount == 0U)) {
                return Rejection::PlanConflict;
            }
            if (plan.automaticEnabled) {
                for (uint8_t start = 0; start < plan.startMinuteCount; ++start) {
                    const uint16_t minute = plan.startMinutes[start];
                    for (uint8_t used = 0; used < scheduledMinuteCount; ++used) {
                        if (scheduledMinutes[used] == minute) {
                            return Rejection::PlanConflict;
                        }
                    }
                    scheduledMinutes[scheduledMinuteCount++] = minute;
                }
            }
        }
        return Rejection::None;
    }
    if (command.kind == CommandKind::AutomaticWatering) {
        if (command.automaticMode == AutomaticMode::PausedUntil) {
            if (!context.timeTrusted) {
                return Rejection::TimeUntrusted;
            }
            if (static_cast<uint64_t>(command.resumeAtEpoch) * 1000ULL <=
                context.nowMs) {
                return Rejection::InvalidResumeTime;
            }
        }
        return Rejection::None;
    }
    if (context.activeKind != ActiveKind::Idle) {
        return Rejection::Busy;
    }
    if (command.kind == CommandKind::StartManual) {
        bool zoneIds[BoardPins::kZoneCount]{};
        for (uint8_t index = 0; index < command.zoneCount; ++index) {
            const ZoneDuration& zone = command.zones[index];
            if (!context.enabledZones[zone.zoneId - 1U]) {
                return Rejection::ZoneUnavailable;
            }
            if (zoneIds[zone.zoneId - 1U] ||
                zone.durationMinutes > context.maximumZoneDurationMinutes) {
                return Rejection::DurationLimit;
            }
            zoneIds[zone.zoneId - 1U] = true;
        }
        return Rejection::None;
    }
    if (!context.enabledZones[command.zoneId - 1U]) {
        return Rejection::ZoneUnavailable;
    }
    if (command.singleOutputMode == SingleOutputMode::Duration &&
        command.durationSeconds >
            static_cast<uint32_t>(context.maximumZoneDurationMinutes) * 60U) {
        return Rejection::DurationLimit;
    }
    if (command.singleOutputMode == SingleOutputMode::Volume &&
        command.targetWaterMl >
            static_cast<uint32_t>(context.maximumSingleOutputLiters) * 1000U) {
        return Rejection::VolumeLimit;
    }
    return Rejection::None;
}

bool buildPlanReplacement(const Command& command,
                          const IrrigationConfig& current,
                          IrrigationConfig& replacement) {
    if (command.kind != CommandKind::Plans ||
        command.planCount > kWateringPlanCount) {
        return false;
    }
    replacement = current;
    for (WateringPlan& plan : replacement.plans) {
        plan.configured = false;
        plan.scheduleEnabled = false;
        plan.name.fill('\0');
        plan.startMinutes.fill(kUnusedStartMinute);
        plan.zoneDurationMinutes.fill(0);
    }
    bool usedPlanIds[kWateringPlanCount]{};
    for (uint8_t planIndex = 0; planIndex < command.planCount; ++planIndex) {
        const PlanValue& requested = command.plans[planIndex];
        if (requested.id == 0U || requested.id > kWateringPlanCount ||
            usedPlanIds[requested.id - 1U] ||
            requested.startMinuteCount > kPlanStartTimeCount ||
            requested.zoneCount > BoardPins::kZoneCount) {
            return false;
        }
        usedPlanIds[requested.id - 1U] = true;
        WateringPlan& target = replacement.plans[requested.id - 1U];
        const WateringPlan& previous = current.plans[requested.id - 1U];
        target = previous;
        target.id = requested.id;
        target.configured = true;
        target.scheduleEnabled = requested.automaticEnabled;
        std::snprintf(target.name.data(), target.name.size(), "%s",
                      requested.name);
        target.startMinutes.fill(kUnusedStartMinute);
        for (uint8_t start = 0; start < requested.startMinuteCount; ++start) {
            target.startMinutes[start] = requested.startMinutes[start];
        }
        for (uint8_t zoneIndex = 0; zoneIndex < current.zones.size();
             ++zoneIndex) {
            if (!current.zones[zoneIndex].enabled) continue;
            target.zoneDurationMinutes[zoneIndex] = 0;
            for (uint8_t zone = 0; zone < requested.zoneCount; ++zone) {
                if (requested.zones[zone].zoneId == zoneIndex + 1U) {
                    target.zoneDurationMinutes[zoneIndex] =
                        requested.zones[zone].durationMinutes;
                    break;
                }
            }
        }
    }
    return true;
}

const char* rejectionName(Rejection rejection) {
    switch (rejection) {
        case Rejection::None: return "none";
        case Rejection::Expired: return "expired";
        case Rejection::NotReady: return "not_ready";
        case Rejection::Busy: return "busy";
        case Rejection::MaintenanceActivity: return "maintenance_activity";
        case Rejection::ZoneUnavailable: return "zone_unavailable";
        case Rejection::DurationLimit: return "duration_limit";
        case Rejection::VolumeLimit: return "volume_limit";
        case Rejection::RevisionConflict: return "revision_conflict";
        case Rejection::PlanConflict: return "plan_conflict";
        case Rejection::TimeUntrusted: return "time_untrusted";
        case Rejection::InvalidResumeTime: return "invalid_resume_time";
    }
    return "unknown";
}

bool parseCanonicalTimestamp(const char* value, uint64_t& epochMs) {
    if (!value) return false;
    uint64_t parsed = 0;
    if (!iot_device::utcEpochMilliseconds(value, std::strlen(value), parsed) ||
        parsed / 1000ULL > UINT32_MAX) return false;
    epochMs = parsed;
    return true;
}

bool formatTimestamp(uint32_t epochSec,
                     uint16_t milliseconds,
                     char* output,
                     std::size_t outputLength) {
    if (!output || outputLength < 25U || milliseconds > 999U) {
        return false;
    }
    const int64_t days = epochSec / 86400U;
    const uint32_t daySeconds = epochSec % 86400U;
    int32_t year = 0;
    uint32_t month = 0;
    uint32_t day = 0;
    civilFromDays(days, year, month, day);
    if (year < 1970 || year > 2106) {
        return false;
    }
    const int written = std::snprintf(
        output,
        outputLength,
        "%04ld-%02lu-%02luT%02lu:%02lu:%02lu.%03uZ",
        static_cast<long>(year),
        static_cast<unsigned long>(month),
        static_cast<unsigned long>(day),
        static_cast<unsigned long>(daySeconds / 3600U),
        static_cast<unsigned long>((daySeconds % 3600U) / 60U),
        static_cast<unsigned long>(daySeconds % 60U),
        static_cast<unsigned>(milliseconds));
    return written == 24;
}

}  // namespace IrrigationIotProtocol
