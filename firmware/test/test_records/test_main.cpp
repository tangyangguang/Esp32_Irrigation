#include <unity.h>

#include <cstring>

#include "irrigation/WateringRecordCodec.h"

namespace {
WateringSessionSummary summary() {
    WateringSessionSummary value{};
    value.source = WateringSource::AutomaticPlan;
    value.purpose = WateringPurpose::Normal;
    value.planId = 2U;
    value.zoneCount = 2U;
    value.elapsedSec = 45U;
    value.result = WateringResult::Completed;
    value.stopReason = WateringStopReason::Completed;
    value.anyFlowEstablished = true;
    value.zones[0].zoneId = 1U;
    value.zones[0].result = ZoneWateringResult::Completed;
    value.zones[0].plannedDurationSec = 20U;
    value.zones[0].actualWateringSec = 20U;
    value.zones[0].pulseCount = 100U;
    value.zones[0].estimatedWaterMl = 400U;
    value.zones[0].averageFlowMlPerMinute = 1200U;
    value.zones[0].flowBaselineAvailable = true;
    value.zones[0].baselinePulseRateX10000 = 5000U;
    value.zones[0].baselineFlowMlPerMinute = 1200U;
    value.zones[1].zoneId = 3U;
    value.zones[1].result = ZoneWateringResult::Completed;
    value.zones[1].plannedDurationSec = 25U;
    value.zones[1].actualWateringSec = 25U;
    value.zones[1].pulseCount = 150U;
    value.zones[1].estimatedWaterMl = 600U;
    value.zones[1].averageFlowMlPerMinute = 1440U;
    return value;
}

void test_layout_is_fixed_210_bytes() {
    TEST_ASSERT_EQUAL_UINT32(210U, WateringRecordCodec::kPayloadSize);
}

void test_round_trip_keeps_only_core_evidence() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(
        summary(), payload));
    uint8_t bytes[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, bytes, sizeof(bytes)));
    WateringRecordPayload decoded{};
    TEST_ASSERT_TRUE(WateringRecordCodec::decode(bytes, sizeof(bytes), decoded));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WateringSource::AutomaticPlan),
                            static_cast<uint8_t>(decoded.source));
    TEST_ASSERT_EQUAL_UINT8(2U, decoded.planId);
    TEST_ASSERT_EQUAL_UINT32(20U, decoded.zones[0].actualWateringSec);
    TEST_ASSERT_EQUAL_UINT32(150U, decoded.zones[2].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(5000U,
                             decoded.zones[0].baselinePulseRateX10000);
}

void test_task_identity_and_start_offsets_round_trip() {
    auto session = summary(); session.zones[1].startedOffsetSec = 28;
    WateringRecordPayload payload{}, decoded{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(session, payload));
    payload.taskId = 42; payload.startedEpoch = 1800000000;
    uint8_t bytes[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(WateringRecordCodec::decode(bytes, sizeof(bytes), decoded));
    TEST_ASSERT_EQUAL_UINT32(42, decoded.taskId);
    TEST_ASSERT_EQUAL_UINT32(1800000000, decoded.startedEpoch);
    TEST_ASSERT_EQUAL_UINT32(28, decoded.zones[2].startedOffsetSec);
}
void test_unknown_recovery_does_not_claim_zero_or_measured_progress() {
    WateringRecordPayload p{}; p.taskId = 9;
    p.result = WateringResult::Incomplete; p.stopReason = WateringStopReason::RebootInterrupted;
    p.zones[0].plannedDurationSec = 60;
    p.zones[0].flags = WateringRecordCodec::kZoneFlagUnknown;
    uint8_t bytes[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(p, bytes, sizeof(bytes)));
    p.zones[0].actualWateringSec = 1;
    TEST_ASSERT_FALSE(WateringRecordCodec::encode(p, bytes, sizeof(bytes)));
}
void test_volume_is_manual_single_zone_and_zone_order_is_validated() {
    WateringRecordPayload p{}; auto session = summary();
    session.zones[1].zoneId = 1;
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(session, p));
    session = summary(); session.source = WateringSource::Manual; session.planId = 0;
    session.targetMode = WateringTargetMode::Volume; session.zoneCount = 1;
    session.zones[0].targetWaterMl = 500;
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(session, p));
    session.source = WateringSource::AutomaticPlan; session.planId = 1;
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(session, p));
}

void test_mixed_mode_round_trips_duration_and_volume_steps() {
    auto session = summary();
    session.source = WateringSource::Manual;
    session.planId = 0;
    session.targetMode = WateringTargetMode::Mixed;
    session.zones[1].targetWaterMl = 500U;  // zones 1 and 3: one duration, one volume
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(session, payload));
    uint8_t bytes[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, bytes, sizeof(bytes)));
    WateringRecordPayload decoded{};
    TEST_ASSERT_TRUE(WateringRecordCodec::decode(bytes, sizeof(bytes), decoded));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WateringTargetMode::Mixed),
                            static_cast<uint8_t>(decoded.targetMode));
    TEST_ASSERT_EQUAL_UINT32(0U, decoded.zones[0].targetWaterMl);
    TEST_ASSERT_EQUAL_UINT32(500U, decoded.zones[2].targetWaterMl);

    // A Mixed payload must actually contain both kinds of steps.
    WateringRecordPayload volumeOnly = payload;
    volumeOnly.zones[0].targetWaterMl = 400U;
    TEST_ASSERT_FALSE(WateringRecordCodec::encode(volumeOnly, bytes, sizeof(bytes)));
}

void test_corrupted_header_and_invalid_result_pair_are_rejected() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(summary(), payload));
    uint8_t bytes[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, bytes, sizeof(bytes)));
    bytes[0] ^= 0x01U;
    WateringRecordPayload decoded{};
    TEST_ASSERT_FALSE(WateringRecordCodec::decode(bytes, sizeof(bytes), decoded));
    payload.result = WateringResult::Stopped;
    payload.stopReason = WateringStopReason::Completed;
    TEST_ASSERT_FALSE(WateringRecordCodec::encode(payload, bytes, sizeof(bytes)));
}
}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_is_fixed_210_bytes);
    RUN_TEST(test_round_trip_keeps_only_core_evidence);
    RUN_TEST(test_task_identity_and_start_offsets_round_trip);
    RUN_TEST(test_unknown_recovery_does_not_claim_zero_or_measured_progress);
    RUN_TEST(test_volume_is_manual_single_zone_and_zone_order_is_validated);
    RUN_TEST(test_mixed_mode_round_trips_duration_and_volume_steps);
    RUN_TEST(test_corrupted_header_and_invalid_result_pair_are_rejected);
    return UNITY_END();
}
