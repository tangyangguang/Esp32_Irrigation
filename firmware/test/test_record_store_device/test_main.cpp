#include <Arduino.h>
#include <Esp32Base.h>
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "irrigation/IrrigationEvents.h"
#include "irrigation/WateringRecordCodec.h"
#include "irrigation/WateringRecordStore.h"

namespace {

Esp32BaseRecordStore g_mainStore;
Esp32BaseRecordStore g_rotationStore;
Esp32BaseRecordStore g_damageStore;
WateringRecordStore g_productionStore;

struct ReadCapture {
    uint32_t ids[4]{};
    uint8_t markers[4]{};
    uint8_t count = 0;
};

void captureRecord(const Esp32BaseRecordStore::RecordView& record, void* user) {
    ReadCapture* capture = static_cast<ReadCapture*>(user);
    if (!capture || capture->count >= 4 || !record.payload || record.payloadSizeBytes == 0) {
        return;
    }
    capture->ids[capture->count] = record.recordId;
    capture->markers[capture->count] = record.payload[0];
    ++capture->count;
}

void defineStore(Esp32BaseRecordStore::StoreDefinition& definition,
                 const char* name,
                 uint32_t maximumBytes,
                 uint32_t minimumFreeBytes = 0) {
    definition.recordTypeName = name;
    definition.storeVersion = 1;
    definition.payloadSizeBytes = WateringRecordCodec::kPayloadSize;
    definition.maximumStoreBytes = maximumBytes;
    definition.minimumFileSystemFreeBytes = minimumFreeBytes;
}

void fillPayload(uint8_t* payload, uint8_t marker) {
    std::memset(payload, 0, WateringRecordCodec::kPayloadSize);
    payload[0] = marker;
}

void removeTestStore(Esp32BaseRecordStore& store) {
    TEST_ASSERT_TRUE(store.clear());
    char controlPath[Esp32BaseRecordStore::MAX_STORE_PATH_LENGTH];
    std::snprintf(controlPath, sizeof(controlPath), "%s/control.bin", store.path());
    Esp32BaseFs::RemoveFileResult removed = Esp32BaseFs::removeFileWithRecovery(controlPath);
    if (removed == Esp32BaseFs::REMOVE_FILE_CLEARED) {
        removed = Esp32BaseFs::removeFileWithRecovery(controlPath);
    }
    TEST_ASSERT_EQUAL(static_cast<int>(Esp32BaseFs::REMOVE_FILE_DELETED),
                      static_cast<int>(removed));
    TEST_ASSERT_TRUE(Esp32BaseFs::rmdir(store.path()));
}

void test_initialize_base_and_capacity_plan() {
    TEST_ASSERT_TRUE(Esp32Base::begin());
    Esp32BaseRecordStore::StoreDefinition definition;
    defineStore(definition, "watering-it", 512UL * 1024UL, 96UL * 1024UL);
    TEST_ASSERT_TRUE(g_mainStore.begin(definition));
    TEST_ASSERT_TRUE(g_mainStore.clear());

    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(g_mainStore.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(112, status.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(512UL * 1024UL, status.maximumStoreBytes);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4500, status.capacity);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(4700, status.capacity);
    TEST_ASSERT_TRUE(status.writable);
}

void test_production_watering_store_definition_loads() {
    TEST_ASSERT_TRUE(g_productionStore.begin());
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(g_productionStore.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(WateringRecordCodec::kPayloadSize + 24U, status.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(WateringRecordStore::kMaximumStoreBytes,
                             status.maximumStoreBytes);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4500, status.capacity);
    TEST_ASSERT_TRUE(status.writable);
}

void test_completed_append_latest_page_detail_and_reload() {
    Esp32BaseRecordStore::StoreStatus before;
    TEST_ASSERT_TRUE(g_mainStore.readStatus(before));
    const uint32_t firstId = before.nextRecordId;

    uint8_t payload[WateringRecordCodec::kPayloadSize];
    fillPayload(payload, 0x31);
    Esp32BaseRecordStore::RecordStartTime startTime;
    TEST_ASSERT_TRUE(g_mainStore.captureStartTime(startTime));
    delay(1100);
    TEST_ASSERT_TRUE(g_mainStore.appendCompleted(startTime, payload, sizeof(payload)));
    fillPayload(payload, 0x32);
    TEST_ASSERT_TRUE(g_mainStore.appendInstant(payload, sizeof(payload)));

    uint8_t scratch[WateringRecordCodec::kPayloadSize];
    ReadCapture latest;
    TEST_ASSERT_TRUE(g_mainStore.readLatest(0, 2, scratch, sizeof(scratch), captureRecord, &latest));
    TEST_ASSERT_EQUAL_UINT8(2, latest.count);
    TEST_ASSERT_EQUAL_UINT32(firstId + 1U, latest.ids[0]);
    TEST_ASSERT_EQUAL_UINT8(0x32, latest.markers[0]);
    TEST_ASSERT_EQUAL_UINT32(firstId, latest.ids[1]);
    TEST_ASSERT_EQUAL_UINT8(0x31, latest.markers[1]);

    ReadCapture secondPage;
    TEST_ASSERT_TRUE(g_mainStore.readLatest(1, 1, scratch, sizeof(scratch), captureRecord, &secondPage));
    TEST_ASSERT_EQUAL_UINT8(1, secondPage.count);
    TEST_ASSERT_EQUAL_UINT32(firstId, secondPage.ids[0]);

    Esp32BaseRecordStore::RecordMetadata metadata;
    std::memset(payload, 0, sizeof(payload));
    TEST_ASSERT_EQUAL(static_cast<int>(Esp32BaseRecordStore::RecordReadResult::Found),
                      static_cast<int>(g_mainStore.readById(firstId,
                                                           payload,
                                                           sizeof(payload),
                                                           metadata)));
    TEST_ASSERT_EQUAL_UINT8(0x31, payload[0]);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, metadata.timing.durationSec);
    TEST_ASSERT_TRUE(g_mainStore.reload());
    Esp32BaseRecordStore::StoreStatus after;
    TEST_ASSERT_TRUE(g_mainStore.readStatus(after));
    TEST_ASSERT_EQUAL_UINT32(2, after.recordCount);
}

void test_small_store_rotates_oldest_complete_segments() {
    Esp32BaseRecordStore::StoreDefinition definition;
    defineStore(definition, "watering-rotate-it", 1024);
    TEST_ASSERT_TRUE(g_rotationStore.begin(definition));
    TEST_ASSERT_TRUE(g_rotationStore.clear());
    Esp32BaseRecordStore::StoreStatus initial;
    TEST_ASSERT_TRUE(g_rotationStore.readStatus(initial));
    const uint32_t firstCandidateId = initial.nextRecordId;

    uint8_t payload[WateringRecordCodec::kPayloadSize];
    for (uint32_t index = 0; index < initial.capacity + 8U; ++index) {
        fillPayload(payload, static_cast<uint8_t>(index));
        TEST_ASSERT_TRUE(g_rotationStore.appendInstant(payload, sizeof(payload)));
    }
    Esp32BaseRecordStore::StoreStatus rotated;
    TEST_ASSERT_TRUE(g_rotationStore.readStatus(rotated));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(rotated.capacity, rotated.recordCount);
    TEST_ASSERT_GREATER_THAN_UINT32(firstCandidateId, rotated.oldestRecordId);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(rotated.maximumStoreBytes, rotated.currentStoreBytes);
}

void test_crc_damage_is_reported_and_not_returned() {
    Esp32BaseRecordStore::StoreDefinition definition;
    defineStore(definition, "watering-damage-it", 4096);
    TEST_ASSERT_TRUE(g_damageStore.begin(definition));
    TEST_ASSERT_TRUE(g_damageStore.clear());
    Esp32BaseRecordStore::StoreStatus before;
    TEST_ASSERT_TRUE(g_damageStore.readStatus(before));
    const uint32_t damagedId = before.nextRecordId;

    uint8_t payload[WateringRecordCodec::kPayloadSize];
    fillPayload(payload, 0x55);
    TEST_ASSERT_TRUE(g_damageStore.appendInstant(payload, sizeof(payload)));

    char segmentPath[Esp32BaseRecordStore::MAX_STORE_PATH_LENGTH];
    std::snprintf(segmentPath,
                  sizeof(segmentPath),
                  "%s/%08lx.seg",
                  g_damageStore.path(),
                  static_cast<unsigned long>(damagedId));
    uint8_t byte = 0;
    size_t readLength = 0;
    constexpr uint32_t kFirstPayloadOffset = 32U + 20U;
    TEST_ASSERT_TRUE(Esp32BaseFs::readBytesAt(segmentPath,
                                             kFirstPayloadOffset,
                                             &byte,
                                             1,
                                             &readLength));
    TEST_ASSERT_EQUAL_UINT32(1, readLength);
    byte ^= 0x01;
    TEST_ASSERT_TRUE(Esp32BaseFs::writeBytesAt(segmentPath, kFirstPayloadOffset, &byte, 1));
    TEST_ASSERT_TRUE(g_damageStore.reload());

    Esp32BaseRecordStore::StoreStatus damaged;
    TEST_ASSERT_TRUE(g_damageStore.readStatus(damaged));
    TEST_ASSERT_EQUAL(static_cast<int>(Esp32BaseRecordStore::StoreState::Degraded),
                      static_cast<int>(damaged.state));
    TEST_ASSERT_EQUAL_UINT32(1, damaged.damagedRecordCount);
    Esp32BaseRecordStore::RecordMetadata metadata;
    TEST_ASSERT_EQUAL(static_cast<int>(Esp32BaseRecordStore::RecordReadResult::Corrupt),
                      static_cast<int>(g_damageStore.readById(damagedId,
                                                             payload,
                                                             sizeof(payload),
                                                             metadata)));
    TEST_ASSERT_TRUE(g_damageStore.clear());
}

struct EventCapture {
    uint32_t eventCode = 0;
    uint32_t reasonCode = 0;
    uint32_t objectId = 0;
    int32_t value1 = 0;
    int32_t value2 = 0;
    Esp32BaseAppEvents::Level level = Esp32BaseAppEvents::Level::Info;
};

void captureEvent(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    EventCapture* capture = static_cast<EventCapture*>(user);
    if (capture && capture->eventCode == 0) {
        capture->eventCode = event.eventCode;
        capture->reasonCode = event.reasonCode;
        capture->objectId = event.objectId;
        capture->value1 = event.value1;
        capture->value2 = event.value2;
        capture->level = event.level;
    }
}

void test_irrigation_event_mapping_and_latest_read() {
    WateringSessionSummary summary{};
    summary.result = WateringResult::Failed;
    summary.stopReason = WateringStopReason::NoFlowTimeout;
    summary.zoneCount = 1;
    summary.zones[0].zoneId = 2;
    summary.zones[0].result = ZoneWateringResult::Failed;
    summary.zones[0].pulseCount = 37;
    summary.zones[0].actualWateringSec = 18;
    TEST_ASSERT_TRUE(IrrigationEvents::appendAbnormalWateringStop(summary));

    EventCapture capture;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readLatest(0, 1, captureEvent, &capture));
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<uint32_t>(IrrigationEvents::EventCode::WateringStoppedAbnormally),
        capture.eventCode);
    TEST_ASSERT_EQUAL_UINT32(
        static_cast<uint32_t>(IrrigationEvents::ReasonCode::NoFlowTimeout),
        capture.reasonCode);
    TEST_ASSERT_EQUAL_UINT32(2, capture.objectId);
    TEST_ASSERT_EQUAL_INT32(37, capture.value1);
    TEST_ASSERT_EQUAL_INT32(18, capture.value2);
    TEST_ASSERT_EQUAL(static_cast<int>(Esp32BaseAppEvents::Level::Warning),
                      static_cast<int>(capture.level));

    removeTestStore(g_mainStore);
    removeTestStore(g_rotationStore);
    removeTestStore(g_damageStore);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);
    UNITY_BEGIN();
    RUN_TEST(test_initialize_base_and_capacity_plan);
    RUN_TEST(test_production_watering_store_definition_loads);
    RUN_TEST(test_completed_append_latest_page_detail_and_reload);
    RUN_TEST(test_small_store_rotates_oldest_complete_segments);
    RUN_TEST(test_crc_damage_is_reported_and_not_returned);
    RUN_TEST(test_irrigation_event_mapping_and_latest_read);
    UNITY_END();
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
