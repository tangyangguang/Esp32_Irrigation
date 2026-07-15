#include <unity.h>

#include "irrigation/FlowCalibrationService.h"

namespace {

WateringSessionSummary calibrationSummary(uint32_t pulses, uint8_t zoneId = 1) {
    WateringSessionSummary summary{};
    summary.purpose = WateringPurpose::FlowCalibration;
    summary.zoneCount = 1;
    summary.anyFlowEstablished = pulses > 0;
    summary.zones[0].zoneId = zoneId;
    summary.zones[0].pulseCount = pulses;
    return summary;
}

void addSample(FlowCalibrationService& service,
               uint32_t pulses,
               uint32_t waterMl,
               uint8_t zoneId = 1) {
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(pulses, zoneId)));
    TEST_ASSERT_TRUE(service.addPendingMeasurement(waterMl));
}

void test_two_samples_remove_shared_startup_effect() {
    FlowCalibrationService service;
    addSample(service, 520, 2000);
    TEST_ASSERT_FALSE(service.resultReady());
    addSample(service, 1270, 5000);

    TEST_ASSERT_TRUE(service.resultReady());
    TEST_ASSERT_EQUAL_UINT32(25000, service.combinedPulsesPerLiterX100());
    TEST_ASSERT_EQUAL_UINT32(3000, service.volumeSpanMl());
    TEST_ASSERT_BITS_HIGH(FlowCalibrationService::kQualityOnlyTwoSamples,
                          service.qualityFlags());
}

void test_three_samples_fit_is_order_independent_and_rounded() {
    FlowCalibrationService ordered;
    addSample(ordered, 473, 1500);
    addSample(ordered, 848, 3000);
    addSample(ordered, 1348, 5000);

    FlowCalibrationService shuffled;
    addSample(shuffled, 1348, 5000);
    addSample(shuffled, 473, 1500);
    addSample(shuffled, 848, 3000);

    TEST_ASSERT_TRUE(ordered.resultReady());
    TEST_ASSERT_TRUE(shuffled.resultReady());
    TEST_ASSERT_EQUAL_UINT32(25000, ordered.combinedPulsesPerLiterX100());
    TEST_ASSERT_EQUAL_UINT32(ordered.combinedPulsesPerLiterX100(),
                             shuffled.combinedPulsesPerLiterX100());

    FlowCalibrationService rounded;
    addSample(rounded, 300, 1000);
    addSample(rounded, 2303, 9000);
    TEST_ASSERT_EQUAL_UINT32(25038, rounded.combinedPulsesPerLiterX100());
}

void test_small_volume_span_warns_but_result_is_available() {
    FlowCalibrationService service;
    addSample(service, 270, 1000);
    addSample(service, 345, 1300);

    TEST_ASSERT_TRUE(service.resultReady());
    TEST_ASSERT_EQUAL_UINT32(25000, service.combinedPulsesPerLiterX100());
    TEST_ASSERT_EQUAL_UINT32(300, service.volumeSpanMl());
    TEST_ASSERT_BITS_HIGH(FlowCalibrationService::kQualitySmallVolumeSpan,
                          service.qualityFlags());
}

void test_invalid_fit_inputs_do_not_produce_result() {
    FlowCalibrationService sameVolume;
    addSample(sameVolume, 270, 1000);
    addSample(sameVolume, 520, 1000);
    TEST_ASSERT_FALSE(sameVolume.resultReady());

    FlowCalibrationService negativeSlope;
    addSample(negativeSlope, 500, 1000);
    addSample(negativeSlope, 400, 2000);
    TEST_ASSERT_FALSE(negativeSlope.resultReady());

    FlowCalibrationService outOfRange;
    addSample(outOfRange, 1, 1000);
    addSample(outOfRange, 200002, 2000);
    TEST_ASSERT_FALSE(outOfRange.resultReady());
}

void test_measurement_rules_zone_lock_and_sample_limit() {
    FlowCalibrationService service;
    TEST_ASSERT_FALSE(service.captureFinishedSession(calibrationSummary(0)));
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(270)));
    TEST_ASSERT_FALSE(service.addPendingMeasurement(999));
    TEST_ASSERT_FALSE(service.addPendingMeasurement(1000001));
    TEST_ASSERT_TRUE(service.hasPendingMeasurement());
    TEST_ASSERT_TRUE(service.addPendingMeasurement(1000));
    TEST_ASSERT_FALSE(service.captureFinishedSession(calibrationSummary(520, 2)));

    addSample(service, 520, 2000);
    addSample(service, 770, 3000);
    addSample(service, 1020, 4000);
    addSample(service, 1270, 5000);
    TEST_ASSERT_EQUAL_UINT8(FlowCalibrationService::kMaximumSamples,
                            service.sampleCount());
    TEST_ASSERT_FALSE(service.captureFinishedSession(calibrationSummary(1520)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_two_samples_remove_shared_startup_effect);
    RUN_TEST(test_three_samples_fit_is_order_independent_and_rounded);
    RUN_TEST(test_small_volume_span_warns_but_result_is_available);
    RUN_TEST(test_invalid_fit_inputs_do_not_produce_result);
    RUN_TEST(test_measurement_rules_zone_lock_and_sample_limit);
    return UNITY_END();
}
