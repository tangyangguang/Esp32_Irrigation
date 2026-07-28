#include <unity.h>

#include <cstring>

#include "irrigation/IrrigationMqttProtocol.h"

namespace {

bool parse(const char* json,
           IrrigationMqttProtocol::Command& command,
           IrrigationMqttProtocol::ParseError& error) {
    return IrrigationMqttProtocol::parse(
        reinterpret_cast<const uint8_t*>(json),
        std::strlen(json),
        command,
        error);
}

void test_set_plan_parses_complete_atomic_plan() {
    IrrigationMqttProtocol::Command command;
    IrrigationMqttProtocol::ParseError error;
    TEST_ASSERT_TRUE(parse(
        R"JSON({"v":1,"id":"plan-12","action":"set_plan","revision":7,"args":{"id":2,"name":"早晨浇水","enabled":true,"starts":[360,1080],"durations":[10,8,0,5,0,0]}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::Action::SetPlan,
                      command.action);
    TEST_ASSERT_EQUAL_STRING("plan-12", command.id.data());
    TEST_ASSERT_EQUAL_UINT32(7, command.revision);
    TEST_ASSERT_EQUAL_UINT8(2, command.planId);
    TEST_ASSERT_TRUE(command.planEnabled);
    TEST_ASSERT_EQUAL_STRING("早晨浇水", command.planName.data());
    TEST_ASSERT_EQUAL_UINT16(360, command.startMinutes[0]);
    TEST_ASSERT_EQUAL_UINT16(1080, command.startMinutes[1]);
    TEST_ASSERT_EQUAL_UINT16(kUnusedStartMinute,
                             command.startMinutes[2]);
    TEST_ASSERT_EQUAL_UINT16(10, command.zoneDurationMinutes[0]);
    TEST_ASSERT_EQUAL_UINT16(5, command.zoneDurationMinutes[3]);
}

void test_daily_commands_parse_without_extra_state_models() {
    IrrigationMqttProtocol::Command command;
    IrrigationMqttProtocol::ParseError error;

    TEST_ASSERT_TRUE(parse(
        R"JSON({"v":1,"id":"delete","action":"delete_plan","revision":8,"args":{"id":3}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::Action::DeletePlan,
                      command.action);
    TEST_ASSERT_EQUAL_UINT8(3, command.planId);

    TEST_ASSERT_TRUE(parse(
        R"JSON({"v":1,"id":"pause","action":"pause_automatic","args":{"resume_at":1785200030}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::Action::PauseAutomatic,
                      command.action);
    TEST_ASSERT_EQUAL_UINT32(1785200030, command.resumeAtEpoch);

    TEST_ASSERT_TRUE(parse(
        R"JSON({"v":1,"id":"resume","action":"resume_automatic"})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::Action::ResumeAutomatic,
                      command.action);

    TEST_ASSERT_TRUE(parse(
        R"JSON({"v":1,"id":"start-plan","action":"start_plan","args":{"id":4}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::Action::StartPlan,
                      command.action);
    TEST_ASSERT_EQUAL_UINT8(4, command.planId);

    TEST_ASSERT_TRUE(parse(
        R"JSON({"v":1,"id":"stop","action":"stop"})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::Action::Stop,
                      command.action);
}

void test_manual_start_requires_exactly_six_durations() {
    IrrigationMqttProtocol::Command command;
    IrrigationMqttProtocol::ParseError error;
    TEST_ASSERT_TRUE(parse(
        R"JSON({"v":1,"id":"manual","action":"start_manual","args":{"durations":[1,2,3,4,5,6]}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::Action::StartManual,
                      command.action);
    TEST_ASSERT_EQUAL_UINT16(6, command.zoneDurationMinutes[5]);

    TEST_ASSERT_FALSE(parse(
        R"JSON({"v":1,"id":"manual","action":"start_manual","args":{"durations":[1,2]}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::ParseError::InvalidArguments,
                      error);
}

void test_set_plan_rejects_partial_or_out_of_range_data() {
    IrrigationMqttProtocol::Command command;
    IrrigationMqttProtocol::ParseError error;
    TEST_ASSERT_FALSE(parse(
        R"JSON({"v":1,"id":"bad","action":"set_plan","revision":7,"args":{"id":1,"name":"计划","enabled":true,"starts":[1440],"durations":[1,2,3,4,5,6]}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::ParseError::InvalidArguments,
                      error);

    TEST_ASSERT_FALSE(parse(
        R"JSON({"v":1,"id":"bad","action":"set_plan","revision":7,"args":{"id":9,"name":"计划","enabled":true,"starts":[60],"durations":[1,2,3,4,5,6]}})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::ParseError::InvalidArguments,
                      error);
}

void test_invalid_envelope_and_version_are_distinguished() {
    IrrigationMqttProtocol::Command command;
    IrrigationMqttProtocol::ParseError error;
    TEST_ASSERT_FALSE(parse("{", command, error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::ParseError::MalformedJson,
                      error);

    TEST_ASSERT_FALSE(parse(
        R"JSON({"v":2,"id":"x","action":"stop"})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::ParseError::UnsupportedVersion,
                      error);

    TEST_ASSERT_FALSE(parse(
        R"JSON({"v":1,"action":"stop"})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::ParseError::InvalidEnvelope,
                      error);
}

void test_unknown_action_is_rejected() {
    IrrigationMqttProtocol::Command command;
    IrrigationMqttProtocol::ParseError error;
    TEST_ASSERT_FALSE(parse(
        R"JSON({"v":1,"id":"x","action":"open_valve_directly"})JSON",
        command,
        error));
    TEST_ASSERT_EQUAL(IrrigationMqttProtocol::ParseError::UnsupportedAction,
                      error);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_set_plan_parses_complete_atomic_plan);
    RUN_TEST(test_daily_commands_parse_without_extra_state_models);
    RUN_TEST(test_manual_start_requires_exactly_six_durations);
    RUN_TEST(test_set_plan_rejects_partial_or_out_of_range_data);
    RUN_TEST(test_invalid_envelope_and_version_are_distinguished);
    RUN_TEST(test_unknown_action_is_rejected);
    return UNITY_END();
}
