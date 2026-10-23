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

void test_start_rejected_watering_validates() {
    uint8_t fact[kWateringData]{};
    WateringRecordPayload payload{};
    payload.taskId = 1;
    payload.startedEpoch = 1700000000U;
    payload.source = WateringSource::AutomaticPlan;
    payload.targetMode = WateringTargetMode::Duration;
    payload.planId = 2;
    payload.result = WateringResult::StartFailed;
    payload.stopReason = WateringStopReason::BusyManualWatering;
    // No zone ever started: all zones remain zero/not-started.
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(
        payload, fact + 4, WateringRecordCodec::kPayloadSize));

    const RecordCodec* codec = findCodec(IrrigationPlatform::FactWateringFailed);
    TEST_ASSERT_NOT_NULL(codec);
    DynamicJsonDocument doc(4096);
    TEST_ASSERT_TRUE(codec->decode(fact, sizeof(fact), doc));
    TEST_ASSERT_EQUAL_STRING("failed", doc["result"]);
    TEST_ASSERT_EQUAL_STRING("busy_manual_watering", doc["reason"]);
    TEST_ASSERT_EQUAL_STRING("device_schedule", doc["sourceKey"]);
    TEST_ASSERT_EQUAL_INT(2, doc["planId"].as<int>());
    TEST_ASSERT_EQUAL_INT(0, doc["durationSeconds"].as<int>());
    TEST_ASSERT_EQUAL_INT(0, doc["zones"].size());
    const RecordContract* rc =
        model_irrigation_controller_6_zone::contract.record(codec->recordKey);
    TEST_ASSERT_NOT_NULL(rc);
    TEST_ASSERT_TRUE(rc->validate(doc.as<JsonVariantConst>()));

    // A plan whose configuration resolves to no executable zones also stores
    // a zero-zone start rejection (invalid_request).
    payload.stopReason = WateringStopReason::InvalidRequest;
    std::memset(fact, 0, sizeof(fact));
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(
        payload, fact + 4, WateringRecordCodec::kPayloadSize));
    assertValidates(IrrigationPlatform::FactWateringFailed, fact, sizeof(fact));
}

void test_audit_records_validate() {
    uint8_t fact[IrrigationAuditCodec::kPayloadSize]{};

    buildAuditFact(fact, IrrigationAuditPayload::Kind::AutomaticStateChanged,
                   2 /*paused-until*/, 0, 1700003600U, 0);
    assertValidates(IrrigationPlatform::FactAutomaticPaused, fact,
                    sizeof(fact));

    buildAuditFact(fact, IrrigationAuditPayload::Kind::AutomaticStateChanged,
                   3 /*resumed manually*/, 0, 0, 0);
    assertValidates(IrrigationPlatform::FactAutomaticResumed, fact,
                    sizeof(fact));

    // Expired (timed-pause auto-resume): the oneOf "expired" branch forbids
    // resumeAtEpoch. The extra null used to fail schema validation and, via
    // RecordStream strict ordering, permanently poisoned the audit stream.
    buildAuditFact(fact, IrrigationAuditPayload::Kind::AutomaticStateChanged,
                   4 /*resumed automatically (expired)*/, 0, 0, 0);
    {
        const RecordCodec* rc = findCodec(
            IrrigationPlatform::FactAutomaticResumed);
        TEST_ASSERT_NOT_NULL(rc);
        DynamicJsonDocument d(1024);
        TEST_ASSERT_TRUE(rc->decode(fact, sizeof(fact), d));
        TEST_ASSERT_EQUAL_STRING("expired", d["mode"] | "");
        TEST_ASSERT_TRUE(d["resumeAtEpoch"].isNull());
        TEST_ASSERT_FALSE(d.as<JsonVariantConst>().containsKey(
            "resumeAtEpoch"));
        const RecordContract* contract =
            model_irrigation_controller_6_zone::contract.record(
                rc->recordKey);
        TEST_ASSERT_NOT_NULL(contract);
        TEST_ASSERT_TRUE(contract->validate(d.as<JsonVariantConst>()));
    }

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

// Regression: a 6-zone fact must decode into the production-sized document
// without overflow; an overflowed decode made ModelPublisher drop the record
// forever and RecordStream's strict ordering blocked every later fact.
void test_six_zone_fact_fits_record_document() {
    uint8_t fact[kWateringData]{};
    WateringRecordPayload payload{};
    payload.taskId = 1;
    payload.startedEpoch = 1700000000U;
    payload.source = WateringSource::WechatMiniprogram;
    payload.targetMode = WateringTargetMode::Duration;
    payload.result = WateringResult::Completed;
    payload.stopReason = WateringStopReason::Completed;
    std::memcpy(payload.commandId.data(),
                "01234567-89ab-4def-8123-456789abcdef", 36);
    for (uint8_t i = 0; i < BoardPins::kZoneCount; ++i) {
        auto& zone = payload.zones[i];
        zone.plannedDurationSec = 720;
        zone.actualWateringSec = 720;
        zone.result = ZoneWateringResult::Completed;
        zone.pulseCount = 999999;
        zone.estimatedWaterMl = 9999999;
        zone.averageFlowMlPerMinute = 999999;
        zone.flags = WateringRecordCodec::kZoneFlagFlowBaselineAvailable |
                     WateringRecordCodec::kZoneFlagLowFlow |
                     WateringRecordCodec::kZoneFlagHighFlow;
        zone.baselinePulseRateX10000 = UINT32_MAX;
        zone.baselineFlowMlPerMinute = UINT32_MAX;
        zone.suggestedBaselinePulseRateX10000 = UINT32_MAX;
    }
    TEST_ASSERT_TRUE(WateringRecordCodec::encode(
        payload, fact + 4, WateringRecordCodec::kPayloadSize));
    fact[0] = 60;

    StaticJsonDocument<IrrigationPlatformRecords::kRecordDocBytes> doc;
    const RecordCodec* codec =
        findCodec(IrrigationPlatform::FactWateringCompleted);
    TEST_ASSERT_NOT_NULL(codec);
    TEST_ASSERT_TRUE(codec->decode(fact, sizeof(fact), doc));
    TEST_ASSERT_FALSE(doc.overflowed());
    const size_t body = measureJson(doc.as<JsonVariantConst>());
    // Leave room for the event envelope inside ESP32BASE_MQTT_MAX_PAYLOAD_BYTES.
    TEST_ASSERT_TRUE(body < 4096 - 300);
    const RecordContract* rc =
        model_irrigation_controller_6_zone::contract.record(codec->recordKey);
    TEST_ASSERT_TRUE(rc->validate(doc.as<JsonVariantConst>()));
}

// Fill the 20 diagnostics fields produced by the shared Esp32Diagnostics core
// plus the device-appended fields, so only the two block fields under test
// remain to be projected.
void buildDiagnosticsBase(DynamicJsonDocument& doc) {
    doc["fwName"] = "esp32-irrigation";
    doc["fwVer"] = "0.9.1";
    doc["host"] = "irrigation-a1b2c3";
    doc["bootId"] = "20261024T000000Z-123";
    doc["bootNo"] = 123U;
    doc["reset"] = "1";
    doc["ssid"] = "home";
    doc["rssi"] = -55;
    doc["ip"] = "192.168.1.20";
    doc["uptime"] = 3600U;
    doc["heap"] = 200000U;
    doc["heapLow"] = 150000U;
    doc["ntp"] = true;
    doc["wifiAtt"] = 1U;
    doc["mqttAtt"] = 2U;
    doc["mqttErr"] = nullptr;
    doc["wdt"] = 0U;
    doc["mac"] = "AA:BB:CC:DD:EE:FF";
    doc["rssiWindowMin"] = -70;
    doc["heapWindowMin"] = 180000U;
}

const CapabilityContract* diagnosticsCapability() {
    const CapabilityContract* capability =
        model_irrigation_controller_6_zone::contract.capability(
            "state.diagnostics");
    TEST_ASSERT_NOT_NULL(capability);
    return capability;
}

// Healthy frame: both block fields are required and validate as 0.
void test_diagnostics_healthy_record_blocks_validate() {
    DynamicJsonDocument doc(2048);
    buildDiagnosticsBase(doc);
    IrrigationPlatformRecords::projectRecordBlocks(doc, 0, 0);
    TEST_ASSERT_FALSE(doc.overflowed());
    TEST_ASSERT_EQUAL_UINT64(0, doc["wateringBlockedSeq"].as<uint64_t>());
    TEST_ASSERT_EQUAL_UINT64(0, doc["auditBlockedSeq"].as<uint64_t>());
    TEST_ASSERT_TRUE(
        diagnosticsCapability()->validate(doc.as<JsonVariantConst>()));
}

// Blocked frame: latched sequences for both fixed streams validate, and the
// complete diagnostics body still fits ModelPublisher's 1024 byte frame cap.
void test_diagnostics_blocked_record_blocks_validate() {
    DynamicJsonDocument doc(2048);
    buildDiagnosticsBase(doc);
    constexpr uint64_t kWateringSeq = 17;
    constexpr uint64_t kAuditSeq = 13;
    IrrigationPlatformRecords::projectRecordBlocks(doc, kWateringSeq,
                                                   kAuditSeq);
    TEST_ASSERT_FALSE(doc.overflowed());
    TEST_ASSERT_EQUAL_UINT64(
        kWateringSeq, doc["wateringBlockedSeq"].as<uint64_t>());
    TEST_ASSERT_EQUAL_UINT64(kAuditSeq,
                             doc["auditBlockedSeq"].as<uint64_t>());
    TEST_ASSERT_TRUE(
        diagnosticsCapability()->validate(doc.as<JsonVariantConst>()));
    const size_t body = measureJson(doc.as<JsonVariantConst>());
    TEST_ASSERT_TRUE(body < 1024);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_watering_completed_validates);
    RUN_TEST(test_suggested_baseline_is_projected_and_null_when_absent);
    RUN_TEST(test_watering_stopped_and_failed_validate);
    RUN_TEST(test_start_rejected_watering_validates);
    RUN_TEST(test_audit_records_validate);
    RUN_TEST(test_six_zone_fact_fits_record_document);
    RUN_TEST(test_diagnostics_healthy_record_blocks_validate);
    RUN_TEST(test_diagnostics_blocked_record_blocks_validate);
    return UNITY_END();
}
