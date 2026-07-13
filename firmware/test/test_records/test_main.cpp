#include <unity.h>

#include "irrigation/WateringRecordCodec.h"

namespace {

WateringSessionSummary exampleSummary() {
    WateringSessionSummary summary{};
    summary.source = WateringSource::AutomaticPlan;
    summary.purpose = WateringPurpose::Normal;
    summary.planId = 3;
    summary.zoneCount = 2;
    summary.elapsedSec = 125;
    summary.result = WateringResult::Stopped;
    summary.stopReason = WateringStopReason::UserStopped;
    summary.anyFlowEstablished = true;
    summary.zones[0] = {1, ZoneWateringResult::Completed, 60, 60, 15000, 60000, false};
    summary.zones[1] = {2, ZoneWateringResult::Stopped, 120, 40, 10000, UINT32_MAX, true};
    return summary;
}

void test_record_round_trip_preserves_all_business_fields() {
    WateringRecord record{};
    const WateringRecordTime time{true, 1783987200U, 17, 42};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(exampleSummary(), 9, time, record));

    uint8_t encoded[WateringRecordCodec::kMaximumEncodedSize]{};
    std::size_t size = 0;
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(record, encoded, sizeof(encoded), size));
    TEST_ASSERT_EQUAL_UINT32(81, size);

    WateringRecord decoded{};
    TEST_ASSERT_TRUE(WateringRecordCodec::decode(encoded, size, decoded));
    TEST_ASSERT_EQUAL_UINT32(9, decoded.id);
    TEST_ASSERT_TRUE(decoded.startTime.synced);
    TEST_ASSERT_EQUAL_UINT32(1783987200U, decoded.startTime.epochSec);
    TEST_ASSERT_EQUAL_UINT32(17, decoded.startTime.bootId);
    TEST_ASSERT_EQUAL_UINT32(125, decoded.elapsedSec);
    TEST_ASSERT_EQUAL_UINT8(3, decoded.planId);
    TEST_ASSERT_EQUAL_UINT8(2, decoded.zoneCount);
    TEST_ASSERT_EQUAL_UINT32(15000, decoded.zones[0].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, decoded.zones[1].estimatedWaterMl);
    TEST_ASSERT_EQUAL_UINT16(WateringRecordCodec::kZoneFlagWaterEstimateCapped,
                             decoded.zones[1].flags);
}

void test_record_rejects_corruption_and_truncation() {
    WateringRecord record{};
    const WateringRecordTime time{false, 0, 18, 100};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(exampleSummary(), 10, time, record));
    uint8_t encoded[WateringRecordCodec::kMaximumEncodedSize]{};
    std::size_t size = 0;
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(record, encoded, sizeof(encoded), size));

    WateringRecord decoded{};
    TEST_ASSERT_FALSE(WateringRecordCodec::decode(encoded, size - 1, decoded));
    encoded[40] ^= 0x01;
    TEST_ASSERT_FALSE(WateringRecordCodec::decode(encoded, size, decoded));
}

void test_record_rejects_invalid_time_and_identifier() {
    WateringRecord record{};
    WateringRecordTime time{false, 123, 1, 2};
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(exampleSummary(), 1, time, record));
    time = {true, 123, 1, 2};
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(exampleSummary(), 0, time, record));
}

void test_only_normal_sessions_with_actual_flow_become_records() {
    WateringSessionSummary summary = exampleSummary();
    const WateringRecordTime time{true, 123, 1, 2};
    WateringRecord record{};

    summary.anyFlowEstablished = false;
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(summary, 1, time, record));
    summary.anyFlowEstablished = true;
    summary.purpose = WateringPurpose::FlowCalibration;
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(summary, 1, time, record));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_record_round_trip_preserves_all_business_fields);
    RUN_TEST(test_record_rejects_corruption_and_truncation);
    RUN_TEST(test_record_rejects_invalid_time_and_identifier);
    RUN_TEST(test_only_normal_sessions_with_actual_flow_become_records);
    return UNITY_END();
}
