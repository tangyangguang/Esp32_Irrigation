#include <unity.h>

#include <array>
#include <cstring>

#include "generated/device_discovery_vectors.h"
#include "generated/platform_command_vectors.h"
#include "irrigation/IrrigationMqttCore.h"
#include "irrigation/IrrigationPlatformProtocol.h"

namespace {

using namespace IrrigationPlatformProtocol;
namespace Fixtures = IrrigationPlatformCommandFixtures;
namespace DiscoveryFixtures = IrrigationDeviceDiscoveryFixtures;

const char* manualCommand =
    R"JSON({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.start-manual","parameters":{"zones":[{"zoneId":2,"durationMinutes":5},{"zoneId":1,"durationMinutes":3}]},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:30.000Z"})JSON";

void test_consumes_controlled_platform_command_vectors() {
    TEST_ASSERT_EQUAL_STRING("1", Fixtures::kSchemaVersion);
    TEST_ASSERT_EQUAL_STRING(kTypeKey, Fixtures::kTypeKey);
    TEST_ASSERT_EQUAL_STRING(kModelKey, Fixtures::kModelKey);
    TEST_ASSERT_EQUAL_STRING("1.1.0", kProtocolVersion);
    TEST_ASSERT_EQUAL_STRING(
        "f01520b35eeb1a565d922626a50d20135863fbf1dc365b0afa3de9543b3f47b6",
        kDefinitionChecksum);
    TEST_ASSERT_EQUAL_UINT32(33, Fixtures::kCaseCount);
    TEST_ASSERT_EQUAL_UINT32(64, std::strlen(Fixtures::kSourceFixtureSha256));
    uint8_t acceptedCapabilities = 0;
    for (const auto& vector : Fixtures::kCases) {
        bool accepted = IrrigationMqttCore::matchesCommandTopic(
                            vector.topic, std::strlen(vector.topic),
                            Fixtures::kDeviceId) &&
                        IrrigationMqttCore::acceptsCommandTransport(
                            vector.qos, vector.retain);
        Command command;
        if (accepted) {
            accepted = parseCommand(
                           reinterpret_cast<const char*>(vector.payload),
                           vector.payloadLength, command) == ParseResult::Ok;
        }
        TEST_ASSERT_EQUAL_MESSAGE(vector.accepted, accepted, vector.id);
        if (accepted) {
            acceptedCapabilities |=
                static_cast<uint8_t>(1U << static_cast<uint8_t>(command.capability));
        }
    }
    TEST_ASSERT_EQUAL_HEX8(0x1FU, acceptedCapabilities);
}

void test_consumes_controlled_device_discovery_vectors() {
    TEST_ASSERT_EQUAL_STRING("1", DiscoveryFixtures::kSchemaVersion);
    TEST_ASSERT_EQUAL_UINT32(14, DiscoveryFixtures::kCaseCount);
    TEST_ASSERT_EQUAL_UINT32(5, DiscoveryFixtures::kApplicableCaseCount);
    TEST_ASSERT_EQUAL_UINT32(64, std::strlen(DiscoveryFixtures::kSourceFixtureSha256));
    std::size_t consumed = 0;
    for (const auto& vector : DiscoveryFixtures::kCases) {
        if (!vector.applicableToFirmware) continue;
        ++consumed;
        if (std::strcmp(vector.definitionTypeKey, kTypeKey) == 0) {
            char topic[160]{};
            TEST_ASSERT_TRUE_MESSAGE(
                formatTopic(vector.deviceId, "availability", topic, sizeof(topic)),
                vector.name);
            const auto policy = IrrigationMqttCore::publicationPolicy(
                IrrigationMqttCore::Channel::Availability);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(vector.qos, policy.qos, vector.name);
            TEST_ASSERT_EQUAL_MESSAGE(vector.retain, policy.retain, vector.name);
            char payload[320]{};
            const char* reason = nullptr;
            const char* observedAt = nullptr;
            bool online = false;
            if (std::strstr(vector.name, "online")) {
                online = true;
                observedAt = "2026-08-27T00:00:00.000Z";
            } else if (std::strstr(vector.name, "lwt")) {
                reason = "lwt";
            } else {
                reason = "shutdown";
                observedAt = "2026-08-27T00:01:00.000Z";
            }
            TEST_ASSERT_TRUE_MESSAGE(
                formatAvailability(
                    online, "a2222222-2222-4222-8222-222222222222", reason,
                    observedAt, payload, sizeof(payload)),
                vector.name);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(vector.payload, payload, vector.name);
        } else {
            TEST_ASSERT_EQUAL_STRING("invalid_device_id", vector.error);
            TEST_ASSERT_FALSE_MESSAGE(isCanonicalDeviceId(vector.deviceId), vector.name);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(DiscoveryFixtures::kApplicableCaseCount, consumed);
}

void test_uuid_versions_variants_and_case_match_platform_rule() {
    char value[] = "7d1898cd-adbf-1fc2-88f0-dfa5748774fe";
    constexpr char variants[] = {'8', '9', 'a', 'b', 'A', 'B'};
    for (char version = '1'; version <= '8'; ++version) {
        value[14] = version;
        for (char variant : variants) {
            value[19] = variant;
            TEST_ASSERT_TRUE(isUuid(value));
        }
    }
    TEST_ASSERT_TRUE(isUuid("7D1898CD-ADBF-8FC2-B8F0-DFA5748774FE"));
    value[14] = '0';
    value[19] = '8';
    TEST_ASSERT_FALSE(isUuid(value));
    value[14] = '9';
    TEST_ASSERT_FALSE(isUuid(value));
    value[14] = '4';
    value[19] = '7';
    TEST_ASSERT_FALSE(isUuid(value));
    value[19] = 'c';
    TEST_ASSERT_FALSE(isUuid(value));
}

void test_formats_new_device_identity_as_canonical_uuid_v4() {
    const uint8_t source[16] = {
        0x4b, 0x8a, 0x3f, 0x7e, 0x7d, 0xc4, 0x09, 0xdb,
        0xe8, 0x19, 0x2e, 0x7b, 0x8a, 0xb5, 0xa1, 0xe3,
    };
    char deviceId[37]{};
    TEST_ASSERT_TRUE(formatUuidV4(source, deviceId, sizeof(deviceId)));
    TEST_ASSERT_EQUAL_STRING("4b8a3f7e-7dc4-49db-a819-2e7b8ab5a1e3", deviceId);
    TEST_ASSERT_TRUE(isCanonicalDeviceId(deviceId));
    TEST_ASSERT_FALSE(isCanonicalDeviceId("4B8A3F7E-7DC4-49DB-A819-2E7B8AB5A1E3"));
    TEST_ASSERT_FALSE(isCanonicalDeviceId("irrigation-real-001"));
}

void test_parses_current_manual_command_and_canonicalizes_fields() {
    Command command;
    TEST_ASSERT_EQUAL(static_cast<int>(ParseResult::Ok),
                      static_cast<int>(parseCommand(
                          manualCommand, std::strlen(manualCommand), command)));
    TEST_ASSERT_EQUAL(static_cast<int>(Capability::StartManual),
                      static_cast<int>(command.capability));
    TEST_ASSERT_EQUAL_UINT8(2, command.zoneCount);
    TEST_ASSERT_EQUAL_UINT8(2, command.zones[0].zoneId);
    TEST_ASSERT_EQUAL_UINT16(3, command.zones[1].durationMinutes);
    char canonical[3072]{};
    TEST_ASSERT_TRUE(canonicalizeCommand(command, canonical, sizeof(canonical)));
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(canonical));
}

void test_rejects_unknown_fields_retained_shape_and_ttl_overflow() {
    const char* unknown =
        R"JSON({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.stop","parameters":{},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:10.000Z","extra":true})JSON";
    Command command;
    TEST_ASSERT_EQUAL(static_cast<int>(ParseResult::InvalidEnvelope),
                      static_cast<int>(parseCommand(unknown, std::strlen(unknown), command)));
    const char* ttl =
        R"JSON({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.stop","parameters":{},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:10.001Z"})JSON";
    TEST_ASSERT_EQUAL(static_cast<int>(ParseResult::TtlExceeded),
                      static_cast<int>(parseCommand(ttl, std::strlen(ttl), command)));
}

void test_validates_plan_schema_uuid_and_utf8() {
    const char* plans =
        R"JSON({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"parameter.plans","parameters":{"revision":4,"plans":[{"id":1,"name":"早晨","automaticEnabled":true,"startMinutes":[360],"zones":[{"zoneId":1,"durationMinutes":10}]}]},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:10.000Z"})JSON";
    Command command;
    TEST_ASSERT_EQUAL(static_cast<int>(ParseResult::Ok),
                      static_cast<int>(parseCommand(plans, std::strlen(plans), command)));
    TEST_ASSERT_EQUAL_UINT32(4, command.revision);
    TEST_ASSERT_EQUAL_UINT8(1, command.planCount);
    TEST_ASSERT_TRUE(isUuid(command.commandId.data()));
    const uint8_t invalidUtf8[] = {0xC0, 0xAF};
    TEST_ASSERT_FALSE(isValidUtf8(invalidUtf8, sizeof(invalidUtf8)));
}

void test_parses_automatic_and_single_output_variants() {
    const char* pause =
        R"JSON({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"parameter.automatic-watering","parameters":{"mode":"paused-until","resumeAtEpoch":1787626800},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:10.000Z"})JSON";
    Command command;
    TEST_ASSERT_EQUAL(static_cast<int>(ParseResult::Ok),
                      static_cast<int>(parseCommand(pause, std::strlen(pause), command)));
    TEST_ASSERT_EQUAL(static_cast<int>(AutomaticWateringMode::PausedUntil),
                      static_cast<int>(command.automaticMode));
    const char* volume =
        R"JSON({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.single-output","parameters":{"zoneId":1,"mode":"volume","targetWaterMl":1000},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:30.000Z"})JSON";
    TEST_ASSERT_EQUAL(static_cast<int>(ParseResult::Ok),
                      static_cast<int>(parseCommand(volume, std::strlen(volume), command)));
    TEST_ASSERT_EQUAL_UINT32(1000, command.targetWaterMl);
    TEST_ASSERT_EQUAL_UINT32(0, command.durationSeconds);
}

void test_formats_precise_utc_timestamp() {
    char output[25]{};
    TEST_ASSERT_TRUE(formatUtcIso8601(1767196800U, output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("2025-12-31T16:00:00.000Z", output);
}

void test_formats_only_fixed_platform_topics() {
    char topic[128]{};
    TEST_ASSERT_TRUE(formatTopic("4b8a3f7e-7dc4-49db-a819-2e7b8ab5a1e3", "command",
                                 topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(
        "iot/irrigation-controller/v1/4b8a3f7e-7dc4-49db-a819-2e7b8ab5a1e3/command", topic);
    TEST_ASSERT_FALSE(formatTopic("irrigation-real-001", "command", topic, sizeof(topic)));
    TEST_ASSERT_FALSE(formatTopic("4b8a3f7e-7dc4-49db-a819-2e7b8ab5a1e3", "debug", topic,
                                  sizeof(topic)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_consumes_controlled_platform_command_vectors);
    RUN_TEST(test_consumes_controlled_device_discovery_vectors);
    RUN_TEST(test_uuid_versions_variants_and_case_match_platform_rule);
    RUN_TEST(test_formats_new_device_identity_as_canonical_uuid_v4);
    RUN_TEST(test_parses_current_manual_command_and_canonicalizes_fields);
    RUN_TEST(test_rejects_unknown_fields_retained_shape_and_ttl_overflow);
    RUN_TEST(test_validates_plan_schema_uuid_and_utf8);
    RUN_TEST(test_parses_automatic_and_single_output_variants);
    RUN_TEST(test_formats_precise_utc_timestamp);
    RUN_TEST(test_formats_only_fixed_platform_topics);
    return UNITY_END();
}
