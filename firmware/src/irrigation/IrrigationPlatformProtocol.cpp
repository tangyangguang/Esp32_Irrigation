#include "IrrigationPlatformProtocol.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace IrrigationPlatformProtocol {
namespace {

bool isLeap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool parseTimestamp(const char* text, uint64_t& epochMs) {
    if (!text || std::strlen(text) != 24 || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != '.' ||
        text[23] != 'Z') {
        return false;
    }
    constexpr uint8_t digits[] = {
        0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18, 20, 21, 22,
    };
    for (uint8_t index : digits) {
        if (text[index] < '0' || text[index] > '9') return false;
    }
    const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                     (text[2] - '0') * 10 + text[3] - '0';
    const unsigned month = (text[5] - '0') * 10U + text[6] - '0';
    const unsigned day = (text[8] - '0') * 10U + text[9] - '0';
    const unsigned hour = (text[11] - '0') * 10U + text[12] - '0';
    const unsigned minute = (text[14] - '0') * 10U + text[15] - '0';
    const unsigned second = (text[17] - '0') * 10U + text[18] - '0';
    const unsigned millis = (text[20] - '0') * 100U +
                            (text[21] - '0') * 10U + text[22] - '0';
    static constexpr uint8_t daysPerMonth[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (year < 1970 || year > 2106 || month < 1 || month > 12 || hour > 23 ||
        minute > 59 || second > 59) {
        return false;
    }
    const unsigned maximumDay = month == 2 && isLeap(year)
                                    ? 29U
                                    : daysPerMonth[month - 1U];
    if (day < 1 || day > maximumDay) return false;
    const int adjustedYear = year - (month <= 2 ? 1 : 0);
    const int era = adjustedYear / 400;
    const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3U : month + 9U;
    const unsigned dayOfYear = (153U * adjustedMonth + 2U) / 5U + day - 1U;
    const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U -
                              yearOfEra / 100U + dayOfYear;
    const int64_t days = static_cast<int64_t>(era) * 146097LL + dayOfEra - 719468LL;
    if (days < 0) return false;
    epochMs = static_cast<uint64_t>(days) * 86400000ULL + hour * 3600000ULL +
              minute * 60000ULL + second * 1000ULL + millis;
    return true;
}

bool onlyKeys(JsonObjectConst object,
              std::initializer_list<const char*> allowed,
              std::initializer_list<const char*> required) {
    if (object.isNull()) return false;
    for (JsonPairConst pair : object) {
        bool known = false;
        for (const char* key : allowed) {
            if (std::strcmp(pair.key().c_str(), key) == 0) {
                known = true;
                break;
            }
        }
        if (!known) return false;
    }
    for (const char* key : required) {
        bool present = false;
        for (JsonPairConst pair : object) {
            if (std::strcmp(pair.key().c_str(), key) == 0) {
                present = true;
                break;
            }
        }
        if (!present) return false;
    }
    return true;
}

bool readUnsigned(JsonVariantConst value, uint32_t minimum, uint32_t maximum,
                  uint32_t& output) {
    if (!value.is<uint32_t>()) return false;
    output = value.as<uint32_t>();
    return output >= minimum && output <= maximum;
}

bool copyName(JsonVariantConst value, std::array<char, kObjectNameCapacity>& output) {
    if (!value.is<const char*>()) return false;
    const char* text = value.as<const char*>();
    const std::size_t length = std::strlen(text);
    if (length == 0 || length >= output.size() || text[0] == ' ' ||
        text[length - 1] == ' ' || !isValidUtf8(
            reinterpret_cast<const uint8_t*>(text), length)) {
        return false;
    }
    std::size_t characters = 0;
    for (std::size_t index = 0; index < length; ++index) {
        if ((static_cast<uint8_t>(text[index]) & 0xC0U) != 0x80U) ++characters;
        const uint8_t ch = static_cast<uint8_t>(text[index]);
        if (ch < 0x20U || ch == 0x7FU) return false;
    }
    if (characters > 20 || length > 63) return false;
    std::snprintf(output.data(), output.size(), "%s", text);
    return true;
}

bool parseZones(JsonArrayConst input,
                std::array<ZoneDuration, BoardPins::kZoneCount>& output,
                uint8_t& count,
                uint16_t maximumDuration) {
    if (input.isNull() || input.size() > output.size()) return false;
    count = 0;
    uint8_t used = 0;
    for (JsonVariantConst item : input) {
        const JsonObjectConst object = item.as<JsonObjectConst>();
        if (!onlyKeys(object, {"zoneId", "durationMinutes"},
                      {"zoneId", "durationMinutes"})) return false;
        uint32_t zoneId = 0;
        uint32_t duration = 0;
        if (!readUnsigned(object["zoneId"], 1, BoardPins::kZoneCount, zoneId) ||
            !readUnsigned(object["durationMinutes"], 1, maximumDuration, duration)) {
            return false;
        }
        const uint8_t bit = static_cast<uint8_t>(1U << (zoneId - 1U));
        if ((used & bit) != 0) return false;
        used |= bit;
        output[count].zoneId = static_cast<uint8_t>(zoneId);
        output[count].durationMinutes = static_cast<uint16_t>(duration);
        ++count;
    }
    return true;
}

bool parsePlans(JsonObjectConst parameters, Command& command) {
    if (!onlyKeys(parameters, {"revision", "plans"}, {"revision", "plans"}) ||
        !readUnsigned(parameters["revision"], 0, UINT32_MAX, command.revision)) {
        return false;
    }
    const JsonArrayConst plans = parameters["plans"].as<JsonArrayConst>();
    if (plans.isNull() || plans.size() > command.plans.size()) return false;
    uint16_t usedPlanIds = 0;
    uint16_t usedScheduledMinutes[32]{};
    uint8_t scheduledCount = 0;
    for (JsonVariantConst item : plans) {
        const JsonObjectConst object = item.as<JsonObjectConst>();
        if (!onlyKeys(object,
                      {"id", "name", "automaticEnabled", "startMinutes", "zones"},
                      {"id", "name", "automaticEnabled", "startMinutes", "zones"})) {
            return false;
        }
        PlanValue& plan = command.plans[command.planCount];
        uint32_t planId = 0;
        if (!readUnsigned(object["id"], 1, kWateringPlanCount, planId) ||
            !object["automaticEnabled"].is<bool>() ||
            !copyName(object["name"], plan.name)) return false;
        const uint16_t bit = static_cast<uint16_t>(1U << (planId - 1U));
        if ((usedPlanIds & bit) != 0) return false;
        usedPlanIds |= bit;
        plan.id = static_cast<uint8_t>(planId);
        plan.automaticEnabled = object["automaticEnabled"].as<bool>();
        const JsonArrayConst starts = object["startMinutes"].as<JsonArrayConst>();
        if (starts.isNull() || starts.size() > plan.startMinutes.size()) return false;
        for (JsonVariantConst start : starts) {
            uint32_t minute = 0;
            if (!readUnsigned(start, 0, 1439, minute)) return false;
            for (uint8_t index = 0; index < plan.startCount; ++index) {
                if (plan.startMinutes[index] == minute) return false;
            }
            plan.startMinutes[plan.startCount++] = static_cast<uint16_t>(minute);
            if (plan.automaticEnabled) {
                for (uint8_t index = 0; index < scheduledCount; ++index) {
                    if (usedScheduledMinutes[index] == minute) return false;
                }
                usedScheduledMinutes[scheduledCount++] = static_cast<uint16_t>(minute);
            }
        }
        if (!parseZones(object["zones"].as<JsonArrayConst>(), plan.zones,
                        plan.zoneCount, kMaximumConfigurableZoneDurationMinutes)) {
            return false;
        }
        if (plan.automaticEnabled && (plan.startCount == 0 || plan.zoneCount == 0)) {
            return false;
        }
        ++command.planCount;
    }
    return true;
}

bool parseAutomatic(JsonObjectConst parameters, Command& command) {
    if (!onlyKeys(parameters, {"mode", "resumeAtEpoch"},
                  {"mode", "resumeAtEpoch"}) || !parameters["mode"].is<const char*>()) {
        return false;
    }
    const char* mode = parameters["mode"].as<const char*>();
    if (std::strcmp(mode, "enabled") == 0 ||
        std::strcmp(mode, "paused-indefinitely") == 0) {
        if (!parameters["resumeAtEpoch"].isNull()) return false;
        command.automaticMode = std::strcmp(mode, "enabled") == 0
                                    ? AutomaticWateringMode::Enabled
                                    : AutomaticWateringMode::PausedIndefinitely;
        return true;
    }
    uint32_t resume = 0;
    if (std::strcmp(mode, "paused-until") != 0 ||
        !readUnsigned(parameters["resumeAtEpoch"], 0, UINT32_MAX, resume)) {
        return false;
    }
    command.automaticMode = AutomaticWateringMode::PausedUntil;
    command.resumeAtEpoch = resume;
    return true;
}

bool parseSingle(JsonObjectConst parameters, Command& command) {
    if (!parameters["mode"].is<const char*>()) return false;
    uint32_t zoneId = 0;
    if (!readUnsigned(parameters["zoneId"], 1, BoardPins::kZoneCount, zoneId)) {
        return false;
    }
    command.zoneId = static_cast<uint8_t>(zoneId);
    const char* mode = parameters["mode"].as<const char*>();
    if (std::strcmp(mode, "duration") == 0) {
        return onlyKeys(parameters, {"zoneId", "mode", "durationSeconds"},
                        {"zoneId", "mode", "durationSeconds"}) &&
               readUnsigned(parameters["durationSeconds"], 1, 43200,
                            command.durationSeconds);
    }
    if (std::strcmp(mode, "volume") == 0) {
        return onlyKeys(parameters, {"zoneId", "mode", "targetWaterMl"},
                        {"zoneId", "mode", "targetWaterMl"}) &&
               readUnsigned(parameters["targetWaterMl"], 100, 1000000,
                            command.targetWaterMl);
    }
    return false;
}

void appendUnsigned(char*& cursor, std::size_t& remaining, uint32_t value) {
    if (remaining == 0) return;
    const int written = std::snprintf(cursor, remaining, "%lu|",
                                      static_cast<unsigned long>(value));
    if (written <= 0 || static_cast<std::size_t>(written) >= remaining) {
        remaining = 0;
        return;
    }
    cursor += written;
    remaining -= static_cast<std::size_t>(written);
}

bool writeCanonical(const Command& command, char* output, std::size_t outputSize) {
    if (!output || outputSize == 0) return false;
    output[0] = '\0';
    char* cursor = output;
    std::size_t remaining = outputSize;
    const int header = std::snprintf(cursor, remaining, "%s|%llu|%llu|",
                                     capabilityKey(command.capability),
                                     static_cast<unsigned long long>(command.issuedAtMs),
                                     static_cast<unsigned long long>(command.expiresAtMs));
    if (header <= 0 || static_cast<std::size_t>(header) >= remaining) return false;
    cursor += header;
    remaining -= static_cast<std::size_t>(header);
    appendUnsigned(cursor, remaining, command.revision);
    appendUnsigned(cursor, remaining, command.planCount);
    for (uint8_t index = 0; index < command.planCount && remaining; ++index) {
        const PlanValue& plan = command.plans[index];
        appendUnsigned(cursor, remaining, plan.id);
        const std::size_t nameLength = std::strlen(plan.name.data());
        appendUnsigned(cursor, remaining, static_cast<uint32_t>(nameLength));
        if (remaining <= nameLength) return false;
        std::memcpy(cursor, plan.name.data(), nameLength);
        cursor += nameLength;
        *cursor++ = '|';
        remaining -= nameLength + 1U;
        appendUnsigned(cursor, remaining, plan.automaticEnabled ? 1U : 0U);
        appendUnsigned(cursor, remaining, plan.startCount);
        for (uint8_t item = 0; item < plan.startCount; ++item)
            appendUnsigned(cursor, remaining, plan.startMinutes[item]);
        appendUnsigned(cursor, remaining, plan.zoneCount);
        for (uint8_t item = 0; item < plan.zoneCount; ++item) {
            appendUnsigned(cursor, remaining, plan.zones[item].zoneId);
            appendUnsigned(cursor, remaining, plan.zones[item].durationMinutes);
        }
    }
    appendUnsigned(cursor, remaining, command.zoneCount);
    for (uint8_t index = 0; index < command.zoneCount; ++index) {
        appendUnsigned(cursor, remaining, command.zones[index].zoneId);
        appendUnsigned(cursor, remaining, command.zones[index].durationMinutes);
    }
    appendUnsigned(cursor, remaining, static_cast<uint32_t>(command.automaticMode));
    appendUnsigned(cursor, remaining, command.resumeAtEpoch);
    appendUnsigned(cursor, remaining, command.zoneId);
    appendUnsigned(cursor, remaining, command.durationSeconds);
    appendUnsigned(cursor, remaining, command.targetWaterMl);
    return remaining != 0;
}

}  // namespace

ParseResult parseCommand(const char* payload, std::size_t length, Command& command) {
    command = {};
    if (!payload || length == 0 || length > kMaximumPayloadBytes ||
        length > kMaximumCommandBytes) return ParseResult::PayloadTooLarge;
    if (!isValidUtf8(reinterpret_cast<const uint8_t*>(payload), length))
        return ParseResult::InvalidUtf8;
    JsonDocument document;
    const DeserializationError error = deserializeJson(
        document, payload, length, DeserializationOption::NestingLimit(6));
    if (error) return ParseResult::InvalidJson;
    const JsonObjectConst root = document.as<JsonObjectConst>();
    if (!onlyKeys(root,
                  {"protocol", "commandId", "capabilityKey", "parameters", "issuedAt", "expiresAt"},
                  {"protocol", "commandId", "capabilityKey", "parameters", "issuedAt", "expiresAt"}) ||
        !root["protocol"].is<const char*>() ||
        std::strcmp(root["protocol"].as<const char*>(), kProtocol) != 0 ||
        !root["commandId"].is<const char*>() ||
        !isUuid(root["commandId"].as<const char*>()) ||
        !root["capabilityKey"].is<const char*>() ||
        !root["issuedAt"].is<const char*>() || !root["expiresAt"].is<const char*>()) {
        return ParseResult::InvalidEnvelope;
    }
    std::snprintf(command.commandId.data(), command.commandId.size(), "%s",
                  root["commandId"].as<const char*>());
    const char* capability = root["capabilityKey"].as<const char*>();
    if (std::strcmp(capability, "parameter.plans") == 0) command.capability = Capability::Plans;
    else if (std::strcmp(capability, "parameter.automatic-watering") == 0)
        command.capability = Capability::AutomaticWatering;
    else if (std::strcmp(capability, "operation.start-manual") == 0)
        command.capability = Capability::StartManual;
    else if (std::strcmp(capability, "operation.stop") == 0)
        command.capability = Capability::Stop;
    else if (std::strcmp(capability, "operation.single-output") == 0)
        command.capability = Capability::SingleOutput;
    else return ParseResult::InvalidEnvelope;
    if (!parseTimestamp(root["issuedAt"].as<const char*>(), command.issuedAtMs) ||
        !parseTimestamp(root["expiresAt"].as<const char*>(), command.expiresAtMs) ||
        command.expiresAtMs <= command.issuedAtMs) return ParseResult::InvalidTime;
    if (command.expiresAtMs - command.issuedAtMs > commandTtlMs(command.capability))
        return ParseResult::TtlExceeded;
    const JsonObjectConst parameters = root["parameters"].as<JsonObjectConst>();
    bool valid = false;
    switch (command.capability) {
        case Capability::Plans:
            valid = parsePlans(parameters, command);
            break;
        case Capability::AutomaticWatering:
            valid = parseAutomatic(parameters, command);
            break;
        case Capability::StartManual:
            valid = onlyKeys(parameters, {"zones"}, {"zones"}) &&
                    parseZones(parameters["zones"].as<JsonArrayConst>(), command.zones,
                               command.zoneCount,
                               kMaximumConfigurableZoneDurationMinutes) &&
                    command.zoneCount != 0;
            break;
        case Capability::Stop:
            valid = onlyKeys(parameters, {}, {});
            break;
        case Capability::SingleOutput:
            valid = parseSingle(parameters, command);
            break;
    }
    return valid ? ParseResult::Ok : ParseResult::InvalidSchema;
}

const char* capabilityKey(Capability capability) {
    switch (capability) {
        case Capability::Plans: return "parameter.plans";
        case Capability::AutomaticWatering: return "parameter.automatic-watering";
        case Capability::StartManual: return "operation.start-manual";
        case Capability::Stop: return "operation.stop";
        case Capability::SingleOutput: return "operation.single-output";
    }
    return "";
}

uint32_t commandTtlMs(Capability capability) {
    return capability == Capability::StartManual || capability == Capability::SingleOutput
               ? 30000U
               : 10000U;
}

bool isUuid(const char* value) {
    if (!value || std::strlen(value) != 36) return false;
    for (uint8_t index = 0; index < 36; ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (!((value[index] >= '0' && value[index] <= '9') ||
                     (value[index] >= 'a' && value[index] <= 'f') ||
                     (value[index] >= 'A' && value[index] <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool isValidUtf8(const uint8_t* data, std::size_t length) {
    if (!data) return false;
    for (std::size_t index = 0; index < length;) {
        const uint8_t first = data[index];
        std::size_t count = 0;
        uint32_t codePoint = 0;
        if (first <= 0x7F) { count = 1; codePoint = first; }
        else if (first >= 0xC2 && first <= 0xDF) { count = 2; codePoint = first & 0x1F; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 3; codePoint = first & 0x0F; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 4; codePoint = first & 0x07; }
        else return false;
        if (index + count > length) return false;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const uint8_t next = data[index + offset];
            if ((next & 0xC0) != 0x80) return false;
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if ((count == 2 && codePoint < 0x80) || (count == 3 && codePoint < 0x800) ||
            (count == 4 && codePoint < 0x10000) ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF) {
            return false;
        }
        index += count;
    }
    return true;
}

bool formatUtcIso8601(uint32_t epochSec, char* output, std::size_t outputSize) {
    if (!output || outputSize < 25) return false;
    const uint32_t secondsOfDay = epochSec % 86400U;
    int64_t days = epochSec / 86400U;
    days += 719468;
    const int era = static_cast<int>(days / 146097);
    const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
                                dayOfEra / 146096) / 365;
    int year = static_cast<int>(yearOfEra) + era * 400;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 -
                                           yearOfEra / 100);
    const unsigned mp = (5 * dayOfYear + 2) / 153;
    const unsigned day = dayOfYear - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp < 10 ? mp + 3 : mp - 9;
    year += month <= 2;
    const int written = std::snprintf(
        output, outputSize, "%04d-%02u-%02uT%02lu:%02lu:%02lu.000Z", year, month,
        day, static_cast<unsigned long>(secondsOfDay / 3600U),
        static_cast<unsigned long>((secondsOfDay % 3600U) / 60U),
        static_cast<unsigned long>(secondsOfDay % 60U));
    return written == 24;
}

bool formatTopic(const char* deviceId,
                 const char* channel,
                 char* output,
                 std::size_t outputSize) {
    if (!deviceId || !channel || !output || outputSize == 0) return false;
    const std::size_t length = std::strlen(deviceId);
    if (length == 0 || length > 64) return false;
    for (std::size_t index = 0; index < length; ++index) {
        const char ch = deviceId[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              (ch == '-' && index != 0))) return false;
    }
    static constexpr const char* channels[] = {
        "availability", "state", "event", "command", "receipt", "progress",
    };
    bool known = false;
    for (const char* candidate : channels) {
        if (std::strcmp(channel, candidate) == 0) {
            known = true;
            break;
        }
    }
    if (!known) return false;
    const int written = std::snprintf(output, outputSize, "iot/%s/v1/%s/%s",
                                      kTypeKey, deviceId, channel);
    return written > 0 && static_cast<std::size_t>(written) < outputSize;
}

bool canonicalizeCommand(const Command& command,
                         char* output,
                         std::size_t outputSize) {
    return writeCanonical(command, output, outputSize);
}

}  // namespace IrrigationPlatformProtocol
