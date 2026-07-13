#include <unity.h>

#include <cstdio>

#include "irrigation/BoardPins.h"
#include "irrigation/IrrigationConfig.h"

namespace {

void test_board_pin_mapping_matches_finalized_pcb() {
    const std::array<uint8_t, BoardPins::kZoneCount> expected = {33, 32, 26, 25, 14, 27};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), BoardPins::kValvePins.data(), expected.size());
    TEST_ASSERT_EQUAL_UINT8(19, BoardPins::kValveDriverShutdownPin);
    TEST_ASSERT_EQUAL_UINT8(18, BoardPins::kPumpSignalPin);
    TEST_ASSERT_EQUAL_UINT8(17, BoardPins::kFlowMeterPin);
    TEST_ASSERT_EQUAL_UINT8(21, BoardPins::kI2cSdaPin);
    TEST_ASSERT_EQUAL_UINT8(22, BoardPins::kI2cSclPin);
}

void test_default_config_matches_confirmed_product_defaults() {
    const IrrigationConfig config = IrrigationConfigRules::createDefault();

    TEST_ASSERT_TRUE(IrrigationConfigRules::validate(config));
    TEST_ASSERT_EQUAL_UINT16(3000, config.valveDrive.pullInTimeMs);
    TEST_ASSERT_EQUAL_UINT32(20000, config.valveDrive.pwmFrequencyHz);
    TEST_ASSERT_EQUAL_UINT8(75, config.valveDrive.holdDutyPercent);
    TEST_ASSERT_FALSE(config.pump.enabled);
    TEST_ASSERT_EQUAL_UINT32(25000, config.flowMeter.pulsesPerLiterX100);
    TEST_ASSERT_EQUAL_UINT8(5, config.timeSafety.rtcRollbackThresholdMinutes);
    TEST_ASSERT_EQUAL_UINT8(12, config.timeSafety.aliveCheckpointHours);
    TEST_ASSERT_TRUE(config.zones[0].enabled);
    TEST_ASSERT_TRUE(config.zones[1].enabled);
    for (std::size_t index = 2; index < config.zones.size(); ++index) {
        TEST_ASSERT_FALSE(config.zones[index].enabled);
    }
    for (const WateringPlan& plan : config.plans) {
        TEST_ASSERT_FALSE(plan.configured);
        TEST_ASSERT_FALSE(plan.scheduleEnabled);
    }
}

void test_names_and_cross_field_rules_are_validated() {
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.plans[0].configured = true;
    config.plans[0].scheduleEnabled = true;
    std::snprintf(config.plans[0].name.data(), config.plans[0].name.size(), "%s", "早晨浇水");
    config.plans[0].startMinutes[0] = 7U * 60U;
    config.plans[0].zoneDurationMinutes[0] = 10;
    TEST_ASSERT_TRUE(IrrigationConfigRules::validate(config));

    config.plans[1].configured = true;
    config.plans[1].scheduleEnabled = true;
    std::snprintf(config.plans[1].name.data(), config.plans[1].name.size(), "%s", "重复时间");
    config.plans[1].startMinutes[0] = 7U * 60U;
    config.plans[1].zoneDurationMinutes[1] = 10;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    std::snprintf(config.zones[0].name.data(), config.zones[0].name.size(), "%s", " 区域");
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
}

void test_confirmed_parameter_ranges_are_validated() {
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.valveDrive.pullInTimeMs = 99;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    config.flowMeter.pulsesPerLiterX100 = 10000001;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    config.flowProtection.highFlowPercent = 100;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    config.flowProtection.lowFlowAction = static_cast<FlowAlertAction>(2);
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
}

void test_checkpoint_zero_is_valid_and_invalid_ranges_are_rejected() {
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.timeSafety.aliveCheckpointHours = 0;
    TEST_ASSERT_TRUE(IrrigationConfigRules::validate(config));

    config.timeSafety.aliveCheckpointHours = 169;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    config.timeSafety.rtcRollbackThresholdMinutes = 0;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_board_pin_mapping_matches_finalized_pcb);
    RUN_TEST(test_default_config_matches_confirmed_product_defaults);
    RUN_TEST(test_checkpoint_zero_is_valid_and_invalid_ranges_are_rejected);
    RUN_TEST(test_names_and_cross_field_rules_are_validated);
    RUN_TEST(test_confirmed_parameter_ranges_are_validated);
    return UNITY_END();
}
