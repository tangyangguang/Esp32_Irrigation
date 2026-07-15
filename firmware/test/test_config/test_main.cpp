#include <unity.h>

#include <cstdio>
#include <string>

#include "irrigation/BoardPins.h"
#include "irrigation/IrrigationConfig.h"
#include "irrigation/IrrigationConfigJson.h"

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
    TEST_ASSERT_EQUAL_UINT32(0, config.flowMeter.calibrationStartupPulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, config.flowMeter.calibrationStartupWaterMl);
    TEST_ASSERT_EQUAL_UINT32(0, config.flowMeter.calibrationSteadyFlowMlPerMinute);
    TEST_ASSERT_EQUAL_UINT8(3, config.calibrationStability.windowSec);
    TEST_ASSERT_EQUAL_UINT8(3, config.calibrationStability.requiredWindows);
    TEST_ASSERT_EQUAL_UINT8(10, config.calibrationStability.allowedVariationPercent);
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

    config = IrrigationConfigRules::createDefault();
    std::snprintf(config.zones[0].name.data(), config.zones[0].name.size(), "%s", "区域\n1");
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
    config.flowMeter.calibrationStartupPulseCount = 10000001;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
    config = IrrigationConfigRules::createDefault();
    config.flowMeter.calibrationStartupWaterMl = 1000001;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
    config = IrrigationConfigRules::createDefault();
    config.flowMeter.calibrationSteadyFlowMlPerMinute = 100001;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    config.calibrationStability.windowSec = 0;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
    config.calibrationStability.windowSec = 10;
    config.calibrationStability.requiredWindows = 10;
    config.calibrationStability.allowedVariationPercent = 30;
    TEST_ASSERT_TRUE(IrrigationConfigRules::validate(config));
    config.calibrationStability.allowedVariationPercent = 31;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    config.flowProtection.highFlowPercent = 100;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));

    config = IrrigationConfigRules::createDefault();
    config.flowProtection.lowFlowAction = static_cast<FlowAlertAction>(2);
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
}

void test_flow_coefficient_decimal_conversion_is_exact() {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(IrrigationConfigRules::parsePulsesPerLiter("250.37", value));
    TEST_ASSERT_EQUAL_UINT32(25037, value);
    TEST_ASSERT_TRUE(IrrigationConfigRules::parsePulsesPerLiter("250.3", value));
    TEST_ASSERT_EQUAL_UINT32(25030, value);
    TEST_ASSERT_TRUE(IrrigationConfigRules::parsePulsesPerLiter("0.01", value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_TRUE(IrrigationConfigRules::parsePulsesPerLiter("100000.00", value));
    TEST_ASSERT_EQUAL_UINT32(10000000, value);

    TEST_ASSERT_FALSE(IrrigationConfigRules::parsePulsesPerLiter("0", value));
    TEST_ASSERT_FALSE(IrrigationConfigRules::parsePulsesPerLiter("250.375", value));
    TEST_ASSERT_FALSE(IrrigationConfigRules::parsePulsesPerLiter("100000.01", value));
    TEST_ASSERT_FALSE(IrrigationConfigRules::parsePulsesPerLiter(" 250.00", value));

    char text[16];
    TEST_ASSERT_TRUE(IrrigationConfigRules::formatPulsesPerLiter(25037, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("250.37", text);
}

void test_baseline_flow_liters_conversion_is_exact() {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(IrrigationConfigRules::parseLitersPerMinute("0", value));
    TEST_ASSERT_EQUAL_UINT32(0, value);
    TEST_ASSERT_TRUE(IrrigationConfigRules::parseLitersPerMinute("1.234", value));
    TEST_ASSERT_EQUAL_UINT32(1234, value);
    TEST_ASSERT_TRUE(IrrigationConfigRules::parseLitersPerMinute("100.000", value));
    TEST_ASSERT_EQUAL_UINT32(100000, value);

    TEST_ASSERT_FALSE(IrrigationConfigRules::parseLitersPerMinute("1.2345", value));
    TEST_ASSERT_FALSE(IrrigationConfigRules::parseLitersPerMinute("100.001", value));
    TEST_ASSERT_FALSE(IrrigationConfigRules::parseLitersPerMinute(" 1.000", value));

    char text[16];
    TEST_ASSERT_TRUE(IrrigationConfigRules::formatLitersPerMinute(1234, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("1.234", text);

    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.zones[0].learnedFlowMlPerMinute = 100001;
    TEST_ASSERT_FALSE(IrrigationConfigRules::validate(config));
}

void test_config_json_round_trip_is_exact_and_strict() {
    IrrigationConfig original = IrrigationConfigRules::createDefault();
    original.flowMeter.pulsesPerLiterX100 = 25037;
    original.plans[0].configured = true;
    std::snprintf(original.plans[0].name.data(), original.plans[0].name.size(), "%s", "日常浇水");
    original.plans[0].zoneDurationMinutes[0] = 15;

    std::string json;
    TEST_ASSERT_TRUE(IrrigationConfigJson::encode(original, json));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"pulses_per_liter_x100\":25037"));

    IrrigationConfig decoded{};
    TEST_ASSERT_TRUE(IrrigationConfigJson::decode(json.data(), json.size(), decoded));
    std::string encodedAgain;
    TEST_ASSERT_TRUE(IrrigationConfigJson::encode(decoded, encodedAgain));
    TEST_ASSERT_EQUAL_STRING(json.c_str(), encodedAgain.c_str());

    const std::size_t field = json.find("\"revision\":1,");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, field);
    json.erase(field, std::string("\"revision\":1,").size());
    TEST_ASSERT_FALSE(IrrigationConfigJson::decode(json.data(), json.size(), decoded));
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
    RUN_TEST(test_flow_coefficient_decimal_conversion_is_exact);
    RUN_TEST(test_baseline_flow_liters_conversion_is_exact);
    RUN_TEST(test_config_json_round_trip_is_exact_and_strict);
    return UNITY_END();
}
