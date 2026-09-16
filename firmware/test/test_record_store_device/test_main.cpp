#include <Arduino.h>
#include <Esp32Base.h>
#include <unity.h>

#include <cstring>

#include "irrigation/IrrigationAuditStore.h"
#include "irrigation/IrrigationRecords.h"
#include "irrigation/WateringRecordCodec.h"
#include "irrigation/WateringRecordStore.h"

namespace {
WateringRecordStore g_watering;
IrrigationAuditStore g_audit;

WateringSessionSummary makeWateringSummary() {
    WateringSessionSummary summary{};
    summary.source = WateringSource::LocalWeb;
    summary.purpose = WateringPurpose::Normal;
    summary.zoneCount = 1U;
    summary.elapsedSec = 12U;
    summary.result = WateringResult::Completed;
    summary.stopReason = WateringStopReason::Completed;
    ZoneWateringSummary& zone = summary.zones[0];
    zone.zoneId = 1U;
    zone.result = ZoneWateringResult::Completed;
    zone.plannedDurationSec = 12U;
    zone.actualWateringSec = 12U;
    zone.pulseCount = 120U;
    zone.estimatedWaterMl = 500U;
    zone.averageFlowMlPerMinute = 2500U;
    zone.flowBaselineAvailable = true;
    zone.baselinePulseRateX10000 = 10000U;
    zone.baselineFlowMlPerMinute = 2500U;
    return summary;
}

void test_initialize_two_bounded_local_stores() {
    TEST_ASSERT_TRUE(Esp32Base::begin());
    TEST_ASSERT_TRUE(g_watering.begin());
    TEST_ASSERT_TRUE(g_audit.begin());

    TEST_ASSERT_TRUE(IrrigationRecords::instance().begin(g_watering, g_audit));
    Esp32BaseStorage::FormatResult formatted;
    TEST_ASSERT_TRUE(Esp32BaseStorage::formatAndReload(formatted));
    TEST_ASSERT_TRUE(IrrigationRecords::instance().reloadAfterFormat());
    IrrigationRecords::instance().handle(millis());
    Esp32BaseRecordStore::StoreStatus wateringStatus{};
    Esp32BaseRecordStore::StoreStatus auditStatus{};
    TEST_ASSERT_TRUE(g_watering.readStatus(wateringStatus));
    TEST_ASSERT_TRUE(g_audit.readStatus(auditStatus));
    TEST_ASSERT_EQUAL_UINT32(WateringRecordStore::kStoredBytes + 24U,
                             wateringStatus.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(IrrigationAuditStore::kStoredBytes + 24U,
                             auditStatus.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(160UL * 1024UL,
                             wateringStatus.maximumStoreBytes);
    TEST_ASSERT_EQUAL_UINT32(48UL * 1024UL,
                             auditStatus.maximumStoreBytes);
}

void test_compact_watering_and_audit_records_are_independent() {
    WateringRequest request{}; request.stepCount = 1; request.steps[0] = {1, 12, 0};
    TEST_ASSERT_TRUE(g_watering.prepareTask(request));
    Esp32BaseRecordStore::RecordStartTime startTime{};
    TEST_ASSERT_TRUE(g_watering.captureStartTime(startTime));
    TEST_ASSERT_TRUE(g_watering.appendCompleted(startTime, makeWateringSummary()));
    TEST_ASSERT_TRUE(g_watering.cancelPreparedTask());
    IrrigationAuditPayload fact{}; fact.kind = IrrigationAuditPayload::Kind::PlansChanged;
    TEST_ASSERT_TRUE(IrrigationRecords::instance().appendAudit(fact));
    StoredWateringRecord stored{};
    TEST_ASSERT_EQUAL(Esp32BaseRecordStore::RecordReadResult::Found, g_watering.readById(1, stored));
    TEST_ASSERT_EQUAL_UINT32(12, stored.payload.zones[0].actualWateringSec);
    TEST_ASSERT_EQUAL_UINT32(1, stored.payload.taskId);
}

void test_conditions_keep_only_current_state() {
    TEST_ASSERT_TRUE(Esp32BaseConditions::forgetAll());
    Esp32BaseConditions::ConditionTracker tracker(3U, 0U, 0U);
    TEST_ASSERT_EQUAL(
        static_cast<int>(Esp32BaseConditions::ObservationResult::Activated),
        static_cast<int>(Esp32BaseConditions::observe(
            tracker, Esp32BaseConditions::ObservedState::Active)));
    bool active = false;
    TEST_ASSERT_TRUE(Esp32BaseConditions::isActive(3U, active));
    TEST_ASSERT_TRUE(active);
    TEST_ASSERT_EQUAL(
        static_cast<int>(Esp32BaseConditions::ObservationResult::Recovered),
        static_cast<int>(Esp32BaseConditions::observe(
            tracker, Esp32BaseConditions::ObservedState::Inactive)));
    TEST_ASSERT_TRUE(Esp32BaseConditions::isActive(3U, active));
    TEST_ASSERT_FALSE(active);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);
    UNITY_BEGIN();
    RUN_TEST(test_initialize_two_bounded_local_stores);
    RUN_TEST(test_compact_watering_and_audit_records_are_independent);
    RUN_TEST(test_conditions_keep_only_current_state);
    UNITY_END();
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
