#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

namespace IrrigationPlatformProtocol {

constexpr const char* kProtocol = "irrigation-controller/v1";
constexpr const char* kTypeKey = "irrigation-controller";
constexpr const char* kModelKey = "irrigation-controller-6-zone";
constexpr const char* kDefinitionSha256 =
    "e09e0e9e649d6b7d618c3ab849fd0c3695e557c9e2e920fedf91b55e91cd92f8";
constexpr uint32_t kStateFreshnessSeconds = 30;
constexpr std::size_t kMaximumPayloadBytes = 16U * 1024U;
constexpr std::size_t kMaximumCommandBytes = 4096U;

enum class Capability : uint8_t {
    Plans,
    AutomaticWatering,
    StartManual,
    Stop,
    SingleOutput,
};

enum class ParseResult : uint8_t {
    Ok,
    PayloadTooLarge,
    InvalidUtf8,
    InvalidJson,
    InvalidEnvelope,
    InvalidSchema,
    InvalidTime,
    TtlExceeded,
};

struct ZoneDuration {
    uint8_t zoneId = 0;
    uint16_t durationMinutes = 0;
};

struct PlanValue {
    uint8_t id = 0;
    bool automaticEnabled = false;
    std::array<char, kObjectNameCapacity> name{};
    uint8_t startCount = 0;
    std::array<uint16_t, kPlanStartTimeCount> startMinutes{};
    uint8_t zoneCount = 0;
    std::array<ZoneDuration, BoardPins::kZoneCount> zones{};
};

struct Command {
    Capability capability = Capability::Stop;
    std::array<char, 37> commandId{};
    uint64_t issuedAtMs = 0;
    uint64_t expiresAtMs = 0;
    uint32_t revision = 0;
    uint8_t planCount = 0;
    std::array<PlanValue, kWateringPlanCount> plans{};
    uint8_t zoneCount = 0;
    std::array<ZoneDuration, BoardPins::kZoneCount> zones{};
    AutomaticWateringMode automaticMode = AutomaticWateringMode::Enabled;
    uint32_t resumeAtEpoch = 0;
    uint8_t zoneId = 0;
    uint32_t durationSeconds = 0;
    uint32_t targetWaterMl = 0;
};

ParseResult parseCommand(const char* payload,
                         std::size_t length,
                         Command& command);
const char* capabilityKey(Capability capability);
uint32_t commandTtlMs(Capability capability);
bool isUuid(const char* value);
bool isValidUtf8(const uint8_t* data, std::size_t length);
bool formatUtcIso8601(uint32_t epochSec, char* output, std::size_t outputSize);
bool formatTopic(const char* deviceId,
                 const char* channel,
                 char* output,
                 std::size_t outputSize);
bool canonicalizeCommand(const Command& command,
                         char* output,
                         std::size_t outputSize);

}  // namespace IrrigationPlatformProtocol
