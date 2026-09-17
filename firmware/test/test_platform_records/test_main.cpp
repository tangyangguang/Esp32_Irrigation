#include <unity.h>

#include <ArduinoJson.h>
#include <cstring>

#include "irrigation/IrrigationAuditPayload.h"
#include "irrigation/IrrigationPlatform.h"
#include "irrigation/IrrigationPlatformRecords.h"
#include "irrigation/WateringRecordCodec.h"
#include "IrrigationSdkModel.generated.h"

using namespace iot_device;

namespace {

constexpr size_t kWateringData = 4 + WateringRecordCodec::kPayloadSize;

// Build the on-disk business fact: 4 LE duration bytes + fixed codec payload.
void buildWateringFact(uint8_t* out, WateringResult result,
                       WateringStopReason stopReason,
                       WateringSource source, uint32_t duration,
                       const char* commandId = nullptr,
                       uint8_t planId = 0,
                       uint32_t startedEpoch = 1700000000U) {
    WateringRecordPayload payload{};
    payload.taskId = 1;
    payload.startedEpoch = startedEpoch;
    payload.source = source;
    payload.targetMode = WateringTargetMode::Duration;
    payload.planId = planId;
    payload.result = result;
    payload.stopReason = stopReason;
    if (commandId)
        std::memcpy(payload.commandId.data(), commandId,
                    payload.commandId.size());
    payload.zones[0].plannedDurationSec = 20;
    payload.zones[0].actualWateringSec = 20;
    payload.zones[0].result =
        result == WateringResult::Completed
            ? ZoneWateringResult::Completed
            : ZoneWateringResult::Stopped;
    payload.zones[0].pulseCount = 100;
    payload.zones[0].estimatedWaterMl = 400;
    payload.zones[0].averageFlowMlPerMinute = 1200;
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(
        payload, out + 4, WateringRecordCodec::kPayloadSize));
    for (int n = 0; n < 4; ++n)
        out[n] = uint8_t(duration >> (8 * n));
}

void buildAuditFact(uint8_t* out, IrrigationAuditPayload::Kind kind,
                    uint8_t reason, uint8_t objectId, uint32_t v1,
                    uint32_t v2) {
    IrrigationAuditPayload p{};
    p.kind = kind;
    p.reason = reason;
    p.objectId = objectId;
    p.value1 = v1;
    p.value2 = v2;
    TEST_ASSERT_TRUE(
        IrrigationAuditCodec::encode(p, out, IrrigationAuditCodec::kPayloadSize));
}

const RecordCodec* findCodec(uint16_t typeCode) {
    const RecordCodec* codecs = IrrigationPlatformRecords::codecs();
    for (size_t i = 0; i < IrrigationPlatformRecords::codecCount(); ++i)
        if (codecs[i].typeCode == typeCode) return &codecs[i];
    return nullptr;
}

void assertValidates(uint16_t typeCode, const uint8_t* fact, size_t length) {
    const RecordCodec* codec = findCodec(typeCode);
    TEST_ASSERT_NOT_NULL(codec);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)length, (uint16_t)codec->dataBytes);
    DynamicJsonDocument doc(4096);
    TEST_ASSERT_TRUE(codec->decode(fact, length, doc));
    const RecordContract* rc =
        model_irrigation_controller_6_zone::contract.record(codec->recordKey);
    TEST_ASSERT_NOT_NULL(rc);
    TEST_ASSERT_TRUE(rc->validate(doc.as<JsonVariantConst>()));
}

}  // namespace

void test_watering_completed_validates() {
    uint8_t fact[kWateringData]{};
    buildWateringFact(fact, WateringResult::Completed,
                      WateringStopReason::Completed,
                      WateringSource::WechatMiniprogram, 20,
                      "01234567-89ab-4def-8123-456789abcdef");
    assertValidates(IrrigationPlatform::FactWateringCompleted, fact,
                    sizeof(fact));
}

void test_suggested_baseline_is_projected_and_null_when_absent() {
    uint8_t fact[kWateringData]{};
    buildWateringFact(fact, WateringResult::Completed,
                      WateringStopReason::Completed,
                      WateringSource::AutomaticPlan, 20, nullptr, 2);
    // No suggested baseline: decoder projects null.
    {
        const RecordCodec* codec =
            findCodec(IrrigationPlatform::FactWateringCompleted);
        DynamicJsonDocument doc(4096);
        TEST_ASSERT_TRUE(codec->decode(fact, sizeof(fact), doc));
        TEST_ASSERT_TRUE(doc["zones"][0]["suggestedBaselinePulseRateX10000"].isNull());
    }
    // Inject a stable-terminal suggestion and verify it reaches the record.
    WateringRecordPayload payload{};
    WateringRecordCodec::decode(fact + 4, WateringRecordCodec::kPayloadSize,
                                payload);
    payload.zones[0].suggestedBaselinePulseRateX10000 = 4166667U;
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(
        payload, fact + 4, WateringRecordCodec::kPayloadSize));
    {
        const RecordCodec* codec =
            findCodec(IrrigationPlatform::FactWateringCompleted);
        DynamicJsonDocument doc(4096);
        TEST_ASSERT_TRUE(codec->decode(fact, sizeof(fact), doc));
        TEST_ASSERT_EQUAL_UINT32(
            4166667U,
            doc["zones"][0]["suggestedBaselinePulseRateX10000"].as<uint32_t>());
        const RecordContract* rc =
            model_irrigation_controller_6_zone::contract.record(
                codec->recordKey);
        TEST_ASSERT_TRUE(rc->validate(doc.as<JsonVariantConst>()));
    }
}

void test_watering_stopped_and_failed_validate() {
    uint8_t fact[kWateringData]{};
    buildWateringFact(fact, WateringResult::Stopped,
                      WateringStopReason::UserStopped,
                      WateringSource::LocalWeb, 10);
    assertValidates(IrrigationPlatform::FactWateringStopped, fact,
                    sizeof(fact));

    std::memset(fact, 0, sizeof(fact));
    buildWateringFact(fact, WateringResult::Failed,
                      WateringStopReason::NoFlowTimeout,
                      WateringSource::AutomaticPlan, 5, nullptr, 3);
    assertValidates(IrrigationPlatform::FactWateringFailed, fact,
                    sizeof(fact));
}

void test_audit_records_validate() {
    uint8_t fact[IrrigationAuditCodec::kPayloadSize]{};

    buildAuditFact(fact, IrrigationAuditPayload::Kind::PlanSkipped,
                   7 /*busy_manual_watering*/, 2, 0, 0);
    assertValidates(IrrigationPlatform::FactAutomaticRunCompleted, fact,
                    sizeof(fact));

    buildAuditFact(fact, IrrigationAuditPayload::Kind::AutomaticStateChanged,
                   2 /*paused-until*/, 0, 1700003600U, 0);
    assertValidates(IrrigationPlatform::FactAutomaticPaused, fact,
                    sizeof(fact));

    buildAuditFact(fact, IrrigationAuditPayload::Kind::AutomaticStateChanged,
                   3 /*resumed manually*/, 0, 0, 0);
    assertValidates(IrrigationPlatform::FactAutomaticResumed, fact,
                    sizeof(fact));

    buildAuditFact(fact, IrrigationAuditPayload::Kind::PlansChanged, 0, 0,
                   42 /*revision*/, 0b101 /*plan ids 1,3*/);
    assertValidates(IrrigationPlatform::FactPlansChanged, fact,
                    sizeof(fact));

    buildAuditFact(fact, IrrigationAuditPayload::Kind::ZoneBaselineSaved, 0,
                   4 /*zoneId*/, 5000, 1200);
    assertValidates(IrrigationPlatform::FactZoneBaselineSaved, fact,
                    sizeof(fact));

    // Zone changed: revision in value1, enabled carried by flag bit 0.
    IrrigationAuditPayload zoneChanged{};
    zoneChanged.kind = IrrigationAuditPayload::Kind::ZoneChanged;
    zoneChanged.flags = 1U;  // enabled
    zoneChanged.objectId = 3U;
    zoneChanged.value1 = 7U;  // revision
    TEST_ASSERT_TRUE(IrrigationAuditCodec::encode(
        zoneChanged, fact, IrrigationAuditCodec::kPayloadSize));
    assertValidates(IrrigationPlatform::FactZoneChanged, fact, sizeof(fact));

    // System field changed: objectId is the 1-based 22-field index.
    buildAuditFact(fact, IrrigationAuditPayload::Kind::SystemFieldChanged, 0,
                   9 /*fieldIndex*/, 0, 0);
    assertValidates(IrrigationPlatform::FactSystemFieldChanged, fact,
                    sizeof(fact));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_watering_completed_validates);
    RUN_TEST(test_suggested_baseline_is_projected_and_null_when_absent);
    RUN_TEST(test_watering_stopped_and_failed_validate);
    RUN_TEST(test_audit_records_validate);
    return UNITY_END();
}
