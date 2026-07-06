#include <stdint.h>
#include <unity.h>

#include "irrigation/IrrigationFlowMath.h"

using namespace irrigation;

void test_estimated_volume_uses_global_pulses_per_liter() {
    TEST_ASSERT_EQUAL_UINT32(5000, estimateVolumeMl(2250, 450));
    TEST_ASSERT_EQUAL_UINT32(0, estimateVolumeMl(2250, 0));
}

void test_flow_rate_uses_window_pulses_and_elapsed_ms() {
    TEST_ASSERT_EQUAL_UINT32(600, estimateFlowMlPerMin(45, 450, 10000));
    TEST_ASSERT_EQUAL_UINT32(0, estimateFlowMlPerMin(45, 0, 10000));
    TEST_ASSERT_EQUAL_UINT32(0, estimateFlowMlPerMin(45, 450, 0));
}

void test_threshold_helpers_use_zone_percentages() {
    TEST_ASSERT_EQUAL_UINT32(300, lowFlowThresholdMlPerMin(500, 60));
    TEST_ASSERT_EQUAL_UINT32(800, highFlowThresholdMlPerMin(500, 160));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_estimated_volume_uses_global_pulses_per_liter);
    RUN_TEST(test_flow_rate_uses_window_pulses_and_elapsed_ms);
    RUN_TEST(test_threshold_helpers_use_zone_percentages);
    return UNITY_END();
}
