#include <Arduino.h>
#include <Esp32Base.h>
#include <Preferences.h>
#include <unity.h>

#include <cstring>

#include "irrigation/IrrigationAuditStore.h"
#include "irrigation/IrrigationRecordSync.h"
#include "irrigation/WateringRecordCodec.h"
#include "irrigation/WateringRecordStore.h"

namespace {
WateringRecordStore g_watering;
IrrigationAuditStore g_audit;

void clearNamespace(const char* name) {
    Preferences preferences;
    TEST_ASSERT_TRUE(preferences.begin(name, false));
    TEST_ASSERT_TRUE(preferences.clear());
    preferences.end();
}

WateringSessionSummary makeWateringSummary() {
    WateringSessionSummary summary{};
    summary.source = WateringSource::ManualZones;
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

void test_initialize_two_bounded_stores_and_sync() {
    TEST_ASSERT_TRUE(Esp32Base::begin());
    TEST_ASSERT_TRUE(g_watering.begin());
    TEST_ASSERT_TRUE(g_audit.begin());
    TEST_ASSERT_TRUE(g_watering.baseStore().clear());
    TEST_ASSERT_TRUE(g_audit.baseStore().clear());
    clearNamespace("irr_wtr_sync");
    clearNamespace("irr_aud_sync");

    TEST_ASSERT_TRUE(IrrigationRecordSync::instance().begin(g_watering, g_audit));
    Esp32BaseRecordStore::StoreStatus wateringStatus{};
    Esp32BaseRecordStore::StoreStatus auditStatus{};
    TEST_ASSERT_TRUE(g_watering.readStatus(wateringStatus));
    TEST_ASSERT_TRUE(g_audit.readStatus(auditStatus));
    TEST_ASSERT_EQUAL_UINT32(WateringRecordCodec::kPayloadSize + 24U,
                             wateringStatus.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(IrrigationAuditCodec::kPayloadSize + 24U,
                             auditStatus.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(384UL * 1024UL,
                             wateringStatus.maximumStoreBytes);
    TEST_ASSERT_EQUAL_UINT32(128UL * 1024UL,
                             auditStatus.maximumStoreBytes);
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp(
        IrrigationRecordSync::instance().streamId(
            IrrigationRecordSync::StreamKind::Watering),
        IrrigationRecordSync::instance().streamId(
            IrrigationRecordSync::StreamKind::Audit)));
}

void test_compact_watering_and_audit_records_are_independent() {
    Esp32BaseRecordStore::RecordStartTime startTime{};
    TEST_ASSERT_TRUE(g_watering.baseStore().captureStartTime(startTime));
    const WateringSessionSummary summary = makeWateringSummary();
    TEST_ASSERT_TRUE(IrrigationRecordSync::instance().appendWatering(
        startTime, summary, "550e8400-e29b-41d4-a716-446655440000"));

    IrrigationAuditPayload audit{};
    audit.kind = IrrigationAuditPayload::Kind::AutomaticStateChanged;
    audit.reason = 1U;
    TEST_ASSERT_TRUE(IrrigationRecordSync::instance().appendAudit(audit));
    TEST_ASSERT_EQUAL_UINT32(1U, IrrigationRecordSync::instance().pendingCount(
                                           IrrigationRecordSync::StreamKind::Watering));
    TEST_ASSERT_EQUAL_UINT32(1U, IrrigationRecordSync::instance().pendingCount(
                                           IrrigationRecordSync::StreamKind::Audit));

    IrrigationRecordSync::PendingRecord watering{};
    IrrigationRecordSync::PendingRecord storedAudit{};
    TEST_ASSERT_TRUE(IrrigationRecordSync::instance().readOldestPending(
        IrrigationRecordSync::StreamKind::Watering, watering));
    TEST_ASSERT_TRUE(IrrigationRecordSync::instance().readOldestPending(
        IrrigationRecordSync::StreamKind::Audit, storedAudit));
    TEST_ASSERT_EQUAL_UINT32(12U,
                             watering.watering.zones[0].plannedDurationSec);
    char relatedCommandId[IrrigationIotProtocol::kUuidBufferSize]{};
    TEST_ASSERT_TRUE(WateringRecordCodec::formatRelatedCommandId(
        watering.watering, relatedCommandId, sizeof(relatedCommandId)));
    TEST_ASSERT_EQUAL_STRING("550e8400-e29b-41d4-a716-446655440000",
                             relatedCommandId);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(IrrigationAuditPayload::Kind::AutomaticStateChanged),
        static_cast<uint8_t>(storedAudit.audit.kind));

    IrrigationIotProtocol::RecordAck ack{};
    std::strncpy(ack.recordStreamId,
                 IrrigationRecordSync::instance().streamId(
                     IrrigationRecordSync::StreamKind::Audit),
                 sizeof(ack.recordStreamId) - 1U);
    ack.acknowledgedThroughSequence = storedAudit.sequence;
    IrrigationRecordSync::StreamKind acknowledgedStream{};
    TEST_ASSERT_TRUE(IrrigationRecordSync::instance().acknowledge(
        ack, millis(), &acknowledgedStream));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(IrrigationRecordSync::StreamKind::Audit),
        static_cast<uint8_t>(acknowledgedStream));
    TEST_ASSERT_EQUAL_UINT32(1U, IrrigationRecordSync::instance().pendingCount(
                                           IrrigationRecordSync::StreamKind::Watering));
    TEST_ASSERT_EQUAL_UINT32(0U, IrrigationRecordSync::instance().pendingCount(
                                           IrrigationRecordSync::StreamKind::Audit));
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
    RUN_TEST(test_initialize_two_bounded_stores_and_sync);
    RUN_TEST(test_compact_watering_and_audit_records_are_independent);
    RUN_TEST(test_conditions_keep_only_current_state);
    UNITY_END();
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
