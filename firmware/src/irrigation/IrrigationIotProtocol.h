#pragma once

#include <cstddef>
#include <cstdint>

#include "IrrigationConfig.h"
#include "IrrigationTypes.h"

namespace IrrigationIotProtocol {

constexpr const char* kProtocol = "irrigation-controller/v1";
constexpr const char* kTypeKey = "irrigation-controller";
constexpr const char* kModelKey = "irrigation-controller-6-zone";
constexpr const char* kDefinitionChecksum =
    "c6313c760a303f73dc7e0c61321376fafb35546fb2ca7567f0d2a10fbdeeacf7";
constexpr uint32_t kStateFreshnessMs = 30000U;
constexpr uint32_t kStateRepublishMs = 24000U;
constexpr uint32_t kRecordAckRetryMs = 5000U;
constexpr std::size_t kUuidTextLength = 36;
constexpr std::size_t kUuidBufferSize = kUuidTextLength + 1U;

using Uuid = char[kUuidBufferSize];

enum class CommandKind : uint8_t {
    Plans,
    AutomaticWatering,
    StartManual,
    Stop,
    SingleOutput,
};

enum class AutomaticMode : uint8_t {
    Enabled,
    PausedIndefinitely,
    PausedUntil,
};

enum class SingleOutputMode : uint8_t {
    Duration,
    Volume,
};

struct ZoneDuration {
    uint8_t zoneId = 0;
    uint16_t durationMinutes = 0;
};

struct PlanValue {
    uint8_t id = 0;
    char name[kObjectNameCapacity]{};
    bool automaticEnabled = false;
    uint8_t startMinuteCount = 0;
    uint16_t startMinutes[kPlanStartTimeCount]{};
    uint8_t zoneCount = 0;
    ZoneDuration zones[BoardPins::kZoneCount]{};
};

struct Command {
    Uuid commandId{};
    CommandKind kind = CommandKind::Stop;
    uint64_t issuedAtMs = 0;
    uint64_t expiresAtMs = 0;
    uint64_t signature = 0;

    uint32_t expectedRevision = 0;
    uint8_t planCount = 0;
    PlanValue plans[kWateringPlanCount]{};

    AutomaticMode automaticMode = AutomaticMode::Enabled;
    uint32_t resumeAtEpoch = 0;

    uint8_t zoneCount = 0;
    ZoneDuration zones[BoardPins::kZoneCount]{};

    uint8_t zoneId = 0;
    SingleOutputMode singleOutputMode = SingleOutputMode::Duration;
    uint32_t durationSeconds = 0;
    uint32_t targetWaterMl = 0;
};

enum class ParseError : uint8_t {
    None,
    UnexpectedTopic,
    InvalidQos,
    RetainedCommand,
    InvalidUtf8,
    InvalidJson,
    InvalidFields,
    InvalidProtocol,
    InvalidCommandId,
    UnknownCapability,
    InvalidTimestamp,
    InvalidExpiry,
    SchemaMismatch,
};

struct CommandPacket {
    const char* topic = nullptr;
    const uint8_t* payload = nullptr;
    std::size_t payloadLength = 0;
    uint8_t qos = 0;
    bool retain = false;
};

ParseError parseCommand(const CommandPacket& packet,
                        const char* expectedTopic,
                        Command& command);
const char* parseErrorName(ParseError error);
const char* capabilityKey(CommandKind kind);
uint32_t commandTtlMs(CommandKind kind);

enum class ActiveKind : uint8_t {
    Idle,
    Manual,
    Automatic,
    SingleOutput,
    Calibration,
    Learning,
};

enum class Rejection : uint8_t {
    None,
    Expired,
    NotReady,
    Busy,
    MaintenanceActivity,
    ZoneUnavailable,
    DurationLimit,
    VolumeLimit,
    RevisionConflict,
    PlanConflict,
    TimeUntrusted,
    InvalidResumeTime,
};

struct BusinessContext {
    uint64_t nowMs = 0;
    bool ready = false;
    bool recordWritable = true;
    ActiveKind activeKind = ActiveKind::Idle;
    bool enabledZones[BoardPins::kZoneCount]{};
    uint32_t plansRevision = 0;
    uint16_t maximumZoneDurationMinutes = 0;
    uint16_t maximumSingleOutputLiters = 0;
    bool timeTrusted = false;
};

Rejection evaluateCommand(const Command& command,
                          const BusinessContext& context);
bool buildPlanReplacement(const Command& command,
                          const IrrigationConfig& current,
                          IrrigationConfig& replacement);
const char* rejectionName(Rejection rejection);
bool parseCanonicalTimestamp(const char* value, uint64_t& epochMs);
bool formatTimestamp(uint32_t epochSec,
                     uint16_t milliseconds,
                     char* output,
                     std::size_t outputLength);
bool isValidUuid(const char* value);

struct RecordAck {
    Uuid recordStreamId{};
    uint32_t acknowledgedThroughSequence = 0;
    uint64_t acknowledgedAtMs = 0;
};

ParseError parseRecordAck(const CommandPacket& packet,
                          const char* expectedTopic,
                          RecordAck& ack);

}  // namespace IrrigationIotProtocol
