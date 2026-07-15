#include <unity.h>

#include "irrigation/FlowCalibrationService.h"

namespace {

WateringSessionSummary calibrationSummary(
    uint32_t pulses,
    uint8_t zoneId = 1,
    WateringStopReason reason = WateringStopReason::UserStopped,
    uint32_t elapsedSec = 30) {
    WateringSessionSummary summary{};
    summary.purpose = WateringPurpose::FlowCalibration;
    summary.zoneCount = 1;
    summary.anyFlowEstablished = pulses > 0;
    summary.elapsedSec = elapsedSec;
    summary.stopReason = reason;
    summary.zones[0].zoneId = zoneId;
    summary.zones[0].result = reason == WateringStopReason::UserStopped
                                  ? ZoneWateringResult::Stopped
                                  : (reason == WateringStopReason::Completed
                                         ? ZoneWateringResult::Completed
                                         : ZoneWateringResult::Failed);
    summary.zones[0].pulseCount = pulses;
    return summary;
}

void addSample(FlowCalibrationService& service,
               uint32_t pulses,
               uint32_t waterMl,
               uint8_t zoneId = 1,
               uint32_t epoch = 1000) {
    TEST_ASSERT_TRUE(service.captureFinishedSession(
        calibrationSummary(pulses, zoneId), epoch));
    TEST_ASSERT_TRUE(service.addPendingMeasurement(waterMl, epoch));
}

void test_two_samples_remove_shared_startup_effect() {
    FlowCalibrationService service;
    addSample(service, 520, 2000);
    TEST_ASSERT_FALSE(service.resultReady());
    addSample(service, 1270, 5000, 1, 1100);

    TEST_ASSERT_TRUE(service.resultReady());
    TEST_ASSERT_EQUAL_UINT8(2, service.validSampleCount());
    TEST_ASSERT_EQUAL_UINT32(25000, service.combinedPulsesPerLiterX100());
    TEST_ASSERT_EQUAL_UINT32(3000, service.volumeSpanMl());
    TEST_ASSERT_EQUAL_UINT32(1100, service.resultUpdatedEpoch());
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

void test_zero_and_abnormal_sessions_become_invalid_without_measurement() {
    FlowCalibrationService service;
    TEST_ASSERT_TRUE(service.captureFinishedSession(
        calibrationSummary(0, 1, WateringStopReason::FlowStartTimeout, 20), 1000));
    TEST_ASSERT_FALSE(service.hasPendingMeasurement());
    TEST_ASSERT_EQUAL_UINT8(1, service.sampleCount());
    TEST_ASSERT_EQUAL_UINT8(0, service.validSampleCount());
    const FlowCalibrationService::Sample* zero = service.sample(0);
    TEST_ASSERT_NOT_NULL(zero);
    TEST_ASSERT_FALSE(zero->valid);
    TEST_ASSERT_EQUAL_UINT32(0, zero->pulseCount);
    TEST_ASSERT_EQUAL_UINT32(20, zero->elapsedSec);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::FlowStartTimeout),
                      static_cast<int>(zero->stopReason));

    TEST_ASSERT_TRUE(service.captureFinishedSession(
        calibrationSummary(25, 1, WateringStopReason::NoFlowTimeout, 45), 1010));
    TEST_ASSERT_FALSE(service.hasPendingMeasurement());
    TEST_ASSERT_EQUAL_UINT8(2, service.sampleCount());
    TEST_ASSERT_FALSE(service.sample(1)->valid);
}

void test_session_rejected_before_output_does_not_create_sample() {
    FlowCalibrationService service;
    WateringSessionSummary summary = calibrationSummary(
        0, 1, WateringStopReason::HardwareFailure, 0);
    summary.zones[0].result = ZoneWateringResult::NotStarted;
    TEST_ASSERT_FALSE(service.captureFinishedSession(summary, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, service.sampleCount());
}

void test_pending_sample_can_be_saved_or_marked_invalid() {
    FlowCalibrationService service;
    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(270), 1000));
    TEST_ASSERT_TRUE(service.hasPendingMeasurement());
    TEST_ASSERT_EQUAL_UINT32(270, service.pendingPulseCount());
    TEST_ASSERT_EQUAL_UINT32(30, service.pendingElapsedSec());
    TEST_ASSERT_FALSE(service.addPendingMeasurement(999, 1000));
    TEST_ASSERT_TRUE(service.markPendingInvalid());
    TEST_ASSERT_FALSE(service.sample(0)->valid);

    TEST_ASSERT_TRUE(service.captureFinishedSession(calibrationSummary(520), 1010));
    TEST_ASSERT_TRUE(service.addPendingMeasurement(2000, 1010));
    TEST_ASSERT_TRUE(service.sample(1)->valid);
    TEST_ASSERT_EQUAL_UINT8(1, service.validSampleCount());
}

void test_edit_delete_recalculate_and_unlock_zone() {
    FlowCalibrationService service;
    addSample(service, 520, 2000, 1, 1000);
    addSample(service, 1270, 5000, 1, 1100);
    TEST_ASSERT_EQUAL_UINT32(25000, service.combinedPulsesPerLiterX100());

    TEST_ASSERT_TRUE(service.updateMeasurement(1, 6000, 1200));
    TEST_ASSERT_EQUAL_UINT32(18750, service.combinedPulsesPerLiterX100());
    TEST_ASSERT_EQUAL_UINT32(1200, service.resultUpdatedEpoch());
    TEST_ASSERT_FALSE(service.updateMeasurement(2, 3000, 1200));

    TEST_ASSERT_TRUE(service.deleteSample(0, 1300));
    TEST_ASSERT_EQUAL_UINT8(1, service.validSampleCount());
    TEST_ASSERT_FALSE(service.resultReady());
    TEST_ASSERT_TRUE(service.deleteSample(0, 1400));
    TEST_ASSERT_EQUAL_UINT8(0, service.sampleCount());
    TEST_ASSERT_EQUAL_UINT8(0, service.zoneId());
}

void test_zone_lock_ten_sample_limit_and_application_metadata() {
    FlowCalibrationService service;
    TEST_ASSERT_TRUE(service.captureFinishedSession(
        calibrationSummary(0, 1, WateringStopReason::FlowStartTimeout, 20), 900));
    TEST_ASSERT_FALSE(service.captureFinishedSession(calibrationSummary(520, 2), 1000));
    for (uint8_t index = 1; index < FlowCalibrationService::kMaximumSamples; ++index) {
        const uint32_t pulses = 20U + static_cast<uint32_t>(index) * 250U;
        const uint32_t volume = static_cast<uint32_t>(index) * 1000U;
        addSample(service, pulses, volume);
    }
    TEST_ASSERT_EQUAL_UINT8(FlowCalibrationService::kMaximumSamples,
                            service.sampleCount());
    TEST_ASSERT_EQUAL_UINT8(FlowCalibrationService::kMaximumSamples - 1U,
                            service.validSampleCount());
    TEST_ASSERT_FALSE(service.captureFinishedSession(calibrationSummary(3000), 1000));

    service.markResultApplied(2000, service.combinedPulsesPerLiterX100());
    TEST_ASSERT_EQUAL_UINT32(2000, service.appliedEpoch());
    TEST_ASSERT_EQUAL_UINT32(service.combinedPulsesPerLiterX100(),
                             service.appliedCoefficientX100());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_two_samples_remove_shared_startup_effect);
    RUN_TEST(test_three_samples_fit_is_order_independent_and_rounded);
    RUN_TEST(test_small_volume_span_warns_but_result_is_available);
    RUN_TEST(test_invalid_fit_inputs_do_not_produce_result);
    RUN_TEST(test_zero_and_abnormal_sessions_become_invalid_without_measurement);
    RUN_TEST(test_session_rejected_before_output_does_not_create_sample);
    RUN_TEST(test_pending_sample_can_be_saved_or_marked_invalid);
    RUN_TEST(test_edit_delete_recalculate_and_unlock_zone);
    RUN_TEST(test_zone_lock_ten_sample_limit_and_application_metadata);
    return UNITY_END();
}
