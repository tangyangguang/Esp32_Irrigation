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
    summary.zones[1] = {3, ZoneWateringResult::Stopped, 120, 40, 10000, UINT32_MAX, true};
    summary.zones[0].lowFlowDetected = true;
    return summary;
}

void test_fixed_payload_round_trip_preserves_business_fields() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(exampleSummary(), payload));
    TEST_ASSERT_EQUAL_UINT8(3, payload.planId);
    TEST_ASSERT_EQUAL_UINT16(60, payload.zones[0].plannedDurationSec);
    TEST_ASSERT_EQUAL_UINT16(0, payload.zones[1].plannedDurationSec);
    TEST_ASSERT_EQUAL_UINT16(120, payload.zones[2].plannedDurationSec);

    uint8_t encoded[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, encoded, sizeof(encoded)));
    TEST_ASSERT_EQUAL_UINT32(88, sizeof(encoded));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WateringSource::AutomaticPlan), encoded[0]);
    TEST_ASSERT_EQUAL_UINT8(3, encoded[1]);
    TEST_ASSERT_EQUAL_UINT8(0, encoded[18]);  // Zone 2 fixed slot is empty.
    TEST_ASSERT_EQUAL_UINT8(0, encoded[19]);

    WateringRecordPayload decoded{};
    TEST_ASSERT_TRUE(WateringRecordCodec::decode(encoded, sizeof(encoded), decoded));
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Completed),
                      static_cast<int>(decoded.zones[0].result));
    TEST_ASSERT_EQUAL_UINT8(WateringRecordCodec::kZoneFlagLowFlow,
                            decoded.zones[0].flags);
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::NotStarted),
                      static_cast<int>(decoded.zones[1].result));
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Stopped),
                      static_cast<int>(decoded.zones[2].result));
    TEST_ASSERT_EQUAL_UINT32(10000, decoded.zones[2].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, decoded.zones[2].estimatedWaterMl);
    TEST_ASSERT_EQUAL_UINT8(WateringRecordCodec::kZoneFlagWaterEstimateCapped,
                            decoded.zones[2].flags);
}

void test_totals_are_derived_without_overflow() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(exampleSummary(), payload));
    WateringRecordTotals totals = WateringRecordCodec::calculateTotals(payload);
    TEST_ASSERT_EQUAL_UINT32(180, totals.plannedDurationSec);
    TEST_ASSERT_EQUAL_UINT32(100, totals.actualWateringSec);
    TEST_ASSERT_EQUAL_UINT64(25000, totals.pulseCount);
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(UINT32_MAX) + 60000ULL,
                             totals.estimatedWaterMl);

    payload = {};
    payload.source = WateringSource::ManualZones;
    payload.result = WateringResult::Completed;
    payload.stopReason = WateringStopReason::Completed;
    for (ZoneWateringRecord& zone : payload.zones) {
        zone.result = ZoneWateringResult::Completed;
        zone.flags = WateringRecordCodec::kZoneFlagWaterEstimateCapped;
        zone.plannedDurationSec = 7200;
        zone.actualWateringSec = 7200;
        zone.pulseCount = UINT32_MAX;
        zone.estimatedWaterMl = UINT32_MAX;
    }
    uint8_t encoded[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, encoded, sizeof(encoded)));
    totals = WateringRecordCodec::calculateTotals(payload);
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(UINT32_MAX) * 6ULL, totals.pulseCount);
    TEST_ASSERT_EQUAL_UINT64(static_cast<uint64_t>(UINT32_MAX) * 6ULL,
                             totals.estimatedWaterMl);
}

void test_decoder_rejects_wrong_size_unknown_flags_and_invalid_empty_zone() {
    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(exampleSummary(), payload));
    uint8_t encoded[WateringRecordCodec::kPayloadSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, encoded, sizeof(encoded)));

    WateringRecordPayload decoded{};
    TEST_ASSERT_FALSE(WateringRecordCodec::decode(encoded, sizeof(encoded) - 1U, decoded));
    encoded[5] = 0x80;
    TEST_ASSERT_FALSE(WateringRecordCodec::decode(encoded, sizeof(encoded), decoded));

    TEST_ASSERT_TRUE(WateringRecordCodec::encode(payload, encoded, sizeof(encoded)));
    encoded[18] = static_cast<uint8_t>(ZoneWateringResult::Completed);
    TEST_ASSERT_FALSE(WateringRecordCodec::decode(encoded, sizeof(encoded), decoded));
}

void test_only_normal_sessions_with_actual_flow_become_records() {
    WateringSessionSummary summary = exampleSummary();
    WateringRecordPayload payload{};

    summary.anyFlowEstablished = false;
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(summary, payload));
    summary.anyFlowEstablished = true;
    summary.purpose = WateringPurpose::FlowCalibration;
    TEST_ASSERT_FALSE(WateringRecordCodec::fromSession(summary, payload));
}

void test_included_but_unstarted_zone_keeps_plan_and_zero_actuals() {
    WateringSessionSummary summary = exampleSummary();
    summary.zones[1] = {3, ZoneWateringResult::NotStarted, 120, 0, 0, 0, false};
    summary.result = WateringResult::Failed;
    summary.stopReason = WateringStopReason::HardwareFailure;

    WateringRecordPayload payload{};
    TEST_ASSERT_TRUE(WateringRecordCodec::fromSession(summary, payload));
    TEST_ASSERT_EQUAL_UINT16(120, payload.zones[2].plannedDurationSec);
    TEST_ASSERT_EQUAL_UINT16(0, payload.zones[2].actualWateringSec);
    TEST_ASSERT_EQUAL_UINT32(0, payload.zones[2].pulseCount);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fixed_payload_round_trip_preserves_business_fields);
    RUN_TEST(test_totals_are_derived_without_overflow);
    RUN_TEST(test_decoder_rejects_wrong_size_unknown_flags_and_invalid_empty_zone);
    RUN_TEST(test_only_normal_sessions_with_actual_flow_become_records);
    RUN_TEST(test_included_but_unstarted_zone_keeps_plan_and_zero_actuals);
    return UNITY_END();
}
