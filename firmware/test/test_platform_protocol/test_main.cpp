#include <unity.h>

#include <cstring>

#include "irrigation/IrrigationPlatformProtocol.h"

namespace {

using namespace IrrigationPlatformProtocol;

const char* manualCommand =
    R"JSON({"protocol":"irrigation-controller/v1","commandId":"7d1898cd-adbf-4fc2-a8f0-dfa5748774fe","capabilityKey":"operation.start-manual","parameters":{"zones":[{"zoneId":2,"durationMinutes":5},{"zoneId":1,"durationMinutes":3}]},"issuedAt":"2026-08-25T02:00:00.000Z","expiresAt":"2026-08-25T02:00:30.000Z"})JSON";

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
    TEST_ASSERT_TRUE(formatTopic("irrigation-real-001", "command",
                                 topic, sizeof(topic)));
    TEST_ASSERT_EQUAL_STRING(
        "iot/irrigation-controller/v1/irrigation-real-001/command", topic);
    TEST_ASSERT_FALSE(formatTopic("-invalid", "command", topic, sizeof(topic)));
    TEST_ASSERT_FALSE(formatTopic("irrigation-real-001", "debug", topic,
                                  sizeof(topic)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_current_manual_command_and_canonicalizes_fields);
    RUN_TEST(test_rejects_unknown_fields_retained_shape_and_ttl_overflow);
    RUN_TEST(test_validates_plan_schema_uuid_and_utf8);
    RUN_TEST(test_parses_automatic_and_single_output_variants);
    RUN_TEST(test_formats_precise_utc_timestamp);
    RUN_TEST(test_formats_only_fixed_platform_topics);
    return UNITY_END();
}
