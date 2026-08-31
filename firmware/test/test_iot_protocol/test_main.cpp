#include <unity.h>

#include <cstring>

#include "irrigation/IrrigationIotProtocol.h"

namespace {

constexpr const char* kCommandTopic =
    "iot/irrigation-controller/v1/33333333-3333-4333-8333-333333333333/command";
constexpr const char* kAckTopic =
    "iot/irrigation-controller/v1/33333333-3333-4333-8333-333333333333/record-ack";

IrrigationIotProtocol::ParseError parse(const char* json,
                                        IrrigationIotProtocol::Command& command,
                                        uint8_t qos = 1,
                                        bool retain = false,
                                        const char* topic = kCommandTopic) {
    IrrigationIotProtocol::CommandPacket packet;
    packet.topic = topic;
    packet.payload = reinterpret_cast<const uint8_t*>(json);
    packet.payloadLength = std::strlen(json);
    packet.qos = qos;
    packet.retain = retain;
    return IrrigationIotProtocol::parseCommand(packet, kCommandTopic, command);
}

void test_timestamp_round_trip_and_rejects_noncanonical_values() {
    uint64_t epochMs = 0;
    TEST_ASSERT_TRUE(IrrigationIotProtocol::parseCanonicalTimestamp(
        "2026-08-25T02:00:10.001Z", epochMs));
    TEST_ASSERT_EQUAL_UINT64(1787623210001ULL, epochMs);
    char timestamp[25]{};
    TEST_ASSERT_TRUE(IrrigationIotProtocol::formatTimestamp(
        static_cast<uint32_t>(epochMs / 1000ULL),
        static_cast<uint16_t>(epochMs % 1000ULL),
        timestamp,
        sizeof(timestamp)));
    TEST_ASSERT_EQUAL_STRING("2026-08-25T02:00:10.001Z", timestamp);
    TEST_ASSERT_FALSE(IrrigationIotProtocol::parseCanonicalTimestamp(
        "2026-08-25T02:00:10Z", epochMs));
    TEST_ASSERT_FALSE(IrrigationIotProtocol::parseCanonicalTimestamp(
        "2026-02-29T02:00:10.000Z", epochMs));
}

void test_valid_manual_command_is_parsed_without_losing_order() {
    constexpr const char* json = R"json({
      "protocol":"irrigation-controller/v1",
      "commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe",
      "capabilityKey":"operation.start-manual",
      "parameters":{"zones":[{"zoneId":6,"durationMinutes":720},{"zoneId":1,"durationMinutes":1}]},
      "issuedAt":"2026-08-25T02:00:00.000Z",
      "expiresAt":"2026-08-25T02:00:30.000Z"
    })json";
    IrrigationIotProtocol::Command command;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::None,
                      parse(json, command));
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::CommandKind::StartManual,
                      command.kind);
    TEST_ASSERT_EQUAL_UINT8(2, command.zoneCount);
    TEST_ASSERT_EQUAL_UINT8(6, command.zones[0].zoneId);
    TEST_ASSERT_EQUAL_UINT16(720, command.zones[0].durationMinutes);
    TEST_ASSERT_EQUAL_UINT8(1, command.zones[1].zoneId);
    TEST_ASSERT_NOT_EQUAL_UINT64(0, command.signature);
}

void test_plans_accept_utf8_and_reject_duplicate_or_unknown_fields() {
    constexpr const char* valid = R"json({
      "protocol":"irrigation-controller/v1",
      "commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe",
      "capabilityKey":"parameter.plans",
      "parameters":{"revision":4,"plans":[{"id":1,"name":"清晨菜园","automaticEnabled":true,"startMinutes":[0,1439],"zones":[{"zoneId":1,"durationMinutes":1},{"zoneId":6,"durationMinutes":720}]}]},
      "issuedAt":"2026-08-25T02:00:00.000Z",
      "expiresAt":"2026-08-25T02:00:10.000Z"
    })json";
    IrrigationIotProtocol::Command command;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::None,
                      parse(valid, command));
    TEST_ASSERT_EQUAL_UINT8(1, command.planCount);
    TEST_ASSERT_EQUAL_STRING("清晨菜园", command.plans[0].name);

    constexpr const char* duplicate = R"json({
      "protocol":"irrigation-controller/v1",
      "commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe",
      "capabilityKey":"parameter.plans",
      "parameters":{"revision":4,"plans":[{"id":1,"name":"A","automaticEnabled":false,"startMinutes":[1,1],"zones":[]}]},
      "issuedAt":"2026-08-25T02:00:00.000Z",
      "expiresAt":"2026-08-25T02:00:10.000Z"
    })json";
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::SchemaMismatch,
                      parse(duplicate, command));
}

void test_transport_envelope_and_expiry_rules_match_platform_contract() {
    constexpr const char* stop = R"json({
      "protocol":"irrigation-controller/v1",
      "commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe",
      "capabilityKey":"operation.stop",
      "parameters":{},
      "issuedAt":"2026-08-25T02:00:00.000Z",
      "expiresAt":"2026-08-25T02:00:10.001Z"
    })json";
    IrrigationIotProtocol::Command command;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::InvalidExpiry,
                      parse(stop, command));
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::InvalidQos,
                      parse(stop, command, 0));
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::RetainedCommand,
                      parse(stop, command, 1, true));
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::UnexpectedTopic,
                      parse(stop, command, 1, false,
                            "iot/irrigation-controller/v1/other/state"));
}

void test_same_semantic_object_has_stable_signature_and_changed_array_does_not() {
    constexpr const char* first = R"json({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.start-manual","parameters":{"zones":[{"zoneId":1,"durationMinutes":1},{"zoneId":2,"durationMinutes":2}]},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:30.000Z"})json";
    constexpr const char* reorderedFields = R"json({"expiresAt":"2026-08-25T02:00:30.000Z","parameters":{"zones":[{"durationMinutes":1,"zoneId":1},{"durationMinutes":2,"zoneId":2}]},"capabilityKey":"operation.start-manual","issuedAt":"2026-08-25T02:00:00.000Z","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","protocol":"irrigation-controller/v1"})json";
    constexpr const char* changedOrder = R"json({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.start-manual","parameters":{"zones":[{"zoneId":2,"durationMinutes":2},{"zoneId":1,"durationMinutes":1}]},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:30.000Z"})json";
    IrrigationIotProtocol::Command a;
    IrrigationIotProtocol::Command b;
    IrrigationIotProtocol::Command c;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::None, parse(first, a));
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::None,
                      parse(reorderedFields, b));
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::None,
                      parse(changedOrder, c));
    TEST_ASSERT_EQUAL_UINT64(a.signature, b.signature);
    TEST_ASSERT_NOT_EQUAL_UINT64(a.signature, c.signature);
}

void test_business_evaluation_uses_runtime_limits_and_maintenance_boundary() {
    constexpr const char* json = R"json({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.start-manual","parameters":{"zones":[{"zoneId":1,"durationMinutes":120}]},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:30.000Z"})json";
    IrrigationIotProtocol::Command command;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::None, parse(json, command));
    IrrigationIotProtocol::BusinessContext context;
    context.nowMs = command.issuedAtMs;
    context.ready = true;
    context.enabledZones[0] = true;
    context.maximumZoneDurationMinutes = 60;
    context.maximumSingleOutputLiters = 100;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::Rejection::DurationLimit,
                      IrrigationIotProtocol::evaluateCommand(command, context));
    context.maximumZoneDurationMinutes = 120;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::Rejection::None,
                      IrrigationIotProtocol::evaluateCommand(command, context));
    context.activeKind = IrrigationIotProtocol::ActiveKind::Calibration;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::Rejection::Busy,
                      IrrigationIotProtocol::evaluateCommand(command, context));
    context.activeKind = IrrigationIotProtocol::ActiveKind::Idle;
    context.recordWritable = false;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::Rejection::NotReady,
                      IrrigationIotProtocol::evaluateCommand(command, context));
    command.kind = IrrigationIotProtocol::CommandKind::Stop;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::Rejection::None,
                      IrrigationIotProtocol::evaluateCommand(command, context));
}

void test_invalid_utf8_is_distinguished_from_invalid_json() {
    const uint8_t invalidUtf8[] = {0xff};
    IrrigationIotProtocol::CommandPacket packet;
    packet.topic = kCommandTopic;
    packet.payload = invalidUtf8;
    packet.payloadLength = sizeof(invalidUtf8);
    packet.qos = 1;
    IrrigationIotProtocol::Command command;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::InvalidUtf8,
                      IrrigationIotProtocol::parseCommand(packet, kCommandTopic, command));
    const uint8_t invalidJson[] = {'{'};
    packet.payload = invalidJson;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::InvalidJson,
                      IrrigationIotProtocol::parseCommand(packet, kCommandTopic, command));
}

void test_plan_replacement_preserves_only_disabled_zone_projection() {
    IrrigationConfig current = IrrigationConfigRules::createDefault();
    current.zones[1].enabled = false;
    current.plans[0].configured = true;
    std::snprintf(current.plans[0].name.data(),
                  current.plans[0].name.size(), "旧计划");
    current.plans[0].zoneDurationMinutes[0] = 5;
    current.plans[0].zoneDurationMinutes[1] = 7;
    current.plans[1].configured = true;
    current.plans[1].zoneDurationMinutes[0] = 8;

    IrrigationIotProtocol::Command command;
    command.kind = IrrigationIotProtocol::CommandKind::Plans;
    command.planCount = 1;
    command.plans[0].id = 1;
    std::snprintf(command.plans[0].name, sizeof(command.plans[0].name),
                  "新计划");
    command.plans[0].zoneCount = 1;
    command.plans[0].zones[0] = {1, 9};

    IrrigationConfig replacement;
    TEST_ASSERT_TRUE(IrrigationIotProtocol::buildPlanReplacement(
        command, current, replacement));
    TEST_ASSERT_TRUE(replacement.plans[0].configured);
    TEST_ASSERT_EQUAL_STRING("新计划", replacement.plans[0].name.data());
    TEST_ASSERT_EQUAL_UINT16(9,
                             replacement.plans[0].zoneDurationMinutes[0]);
    TEST_ASSERT_EQUAL_UINT16(7,
                             replacement.plans[0].zoneDurationMinutes[1]);
    TEST_ASSERT_FALSE(replacement.plans[1].configured);
    TEST_ASSERT_EQUAL_UINT16(0,
                             replacement.plans[1].zoneDurationMinutes[0]);
}

void test_record_ack_requires_current_fixed_shape() {
    constexpr const char* json = R"json({"protocol":"irrigation-controller/v1","recordStreamId":"9cbaf1cf-e1a9-4f9d-9a39-fd7db5a93446","acknowledgedThroughSequence":42,"acknowledgedAt":"2026-08-25T02:00:32.000Z"})json";
    IrrigationIotProtocol::CommandPacket packet;
    packet.topic = kAckTopic;
    packet.payload = reinterpret_cast<const uint8_t*>(json);
    packet.payloadLength = std::strlen(json);
    packet.qos = 1;
    IrrigationIotProtocol::RecordAck ack;
    TEST_ASSERT_EQUAL(IrrigationIotProtocol::ParseError::None,
                      IrrigationIotProtocol::parseRecordAck(packet, kAckTopic, ack));
    TEST_ASSERT_EQUAL_STRING("9cbaf1cf-e1a9-4f9d-9a39-fd7db5a93446",
                             ack.recordStreamId);
    TEST_ASSERT_EQUAL_UINT32(42, ack.acknowledgedThroughSequence);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_timestamp_round_trip_and_rejects_noncanonical_values);
    RUN_TEST(test_valid_manual_command_is_parsed_without_losing_order);
    RUN_TEST(test_plans_accept_utf8_and_reject_duplicate_or_unknown_fields);
    RUN_TEST(test_transport_envelope_and_expiry_rules_match_platform_contract);
    RUN_TEST(test_same_semantic_object_has_stable_signature_and_changed_array_does_not);
    RUN_TEST(test_business_evaluation_uses_runtime_limits_and_maintenance_boundary);
    RUN_TEST(test_invalid_utf8_is_distinguished_from_invalid_json);
    RUN_TEST(test_plan_replacement_preserves_only_disabled_zone_projection);
    RUN_TEST(test_record_ack_requires_current_fixed_shape);
    return UNITY_END();
}
