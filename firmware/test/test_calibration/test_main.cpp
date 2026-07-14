#include <unity.h>

#include "irrigation/FlowCalibrationService.h"

namespace {

WateringSessionSummary calibrationSummary(uint32_t pulses) {
    WateringSessionSummary summary{};
    summary.purpose = WateringPurpose::FlowCalibration;
    summary.zoneCount = 1;
    summary.anyFlowEstablished = pulses > 0;
    summary.zones[0].zoneId = 1;
    summary.zones[0].pulseCount = pulses;
    return summary;
}

void test_single_sample_uses_exact_fixed_point_rounding() {
    FlowCalibrationService service;
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(250)));
    TEST_ASSERT_TRUE(service.hasPendingMeasurement());
    TEST_ASSERT_TRUE(service.addPendingMeasurement(1000));
    TEST_ASSERT_EQUAL_UINT8(1, service.sampleCount());
    TEST_ASSERT_EQUAL_UINT32(25000, service.combinedPulsesPerLiterX100());
    TEST_ASSERT_EQUAL_UINT16(0, service.sample(0)->deviationPercentX100);
}

void test_multiple_samples_are_weighted_by_measured_volume() {
    FlowCalibrationService service;
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(250)));
    TEST_ASSERT_TRUE(service.addPendingMeasurement(1000));
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(510)));
    TEST_ASSERT_TRUE(service.addPendingMeasurement(2000));
    TEST_ASSERT_EQUAL_UINT32(25333, service.combinedPulsesPerLiterX100());
    TEST_ASSERT_FALSE(service.samplesUnstable());
}

void test_unstable_sample_and_invalid_measurement_are_rejected_or_flagged() {
    FlowCalibrationService service;
    TEST_ASSERT_FALSE(service.captureFinishedSession(calibrationSummary(0)));
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(250)));
    TEST_ASSERT_FALSE(service.addPendingMeasurement(999));
    TEST_ASSERT_TRUE(service.addPendingMeasurement(1000));
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(400)));
    TEST_ASSERT_TRUE(service.addPendingMeasurement(1000));
    TEST_ASSERT_TRUE(service.samplesUnstable());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_sample_uses_exact_fixed_point_rounding);
    RUN_TEST(test_multiple_samples_are_weighted_by_measured_volume);
    RUN_TEST(test_unstable_sample_and_invalid_measurement_are_rejected_or_flagged);
    return UNITY_END();
}
