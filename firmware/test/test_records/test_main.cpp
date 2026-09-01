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

void test_layout_is_fixed_193_bytes() {
    TEST_ASSERT_EQUAL_UINT32(193U, WateringRecordCodec::kPayloadSize);
}

void test_round_trip_keeps_only_core_evidence() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(
        summary(), "550e8400-e29b-41d4-a716-446655440000", payload));
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
    char uuid[37]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::formatRelatedCommandId(
        decoded, uuid, sizeof(uuid)));
    TEST_ASSERT_EQUAL_STRING("550e8400-e29b-41d4-a716-446655440000", uuid);
}

void test_empty_command_id_round_trips_as_empty() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(summary(), nullptr, payload));
    char uuid[37]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::formatRelatedCommandId(
        payload, uuid, sizeof(uuid)));
    TEST_ASSERT_EQUAL_STRING("", uuid);
}

void test_invalid_uuid_and_zone_order_are_rejected() {
    WateringRecordPayload payload{};
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(
        summary(), "not-a-uuid", payload));
    WateringSessionSummary invalid = summary();
    invalid.zones[1].zoneId = 1U;
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(invalid, nullptr, payload));
}

void test_corrupted_header_and_invalid_result_pair_are_rejected() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(summary(), nullptr, payload));
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
    RUN_TEST(test_layout_is_fixed_193_bytes);
    RUN_TEST(test_round_trip_keeps_only_core_evidence);
    RUN_TEST(test_empty_command_id_round_trips_as_empty);
    RUN_TEST(test_invalid_uuid_and_zone_order_are_rejected);
    RUN_TEST(test_corrupted_header_and_invalid_result_pair_are_rejected);
    return UNITY_END();
}
