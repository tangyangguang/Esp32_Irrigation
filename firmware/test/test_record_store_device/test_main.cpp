#include <Arduino.h>
#include <Esp32Base.h>
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "irrigation/DeviceAliveCheckpoint.h"
#include "irrigation/IrrigationEvents.h"
#include "irrigation/WateringRecordCodec.h"
#include "irrigation/WateringRecordStore.h"
#include "irrigation/WateringSchedulerStore.h"

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
    TEST_ASSERT_EQUAL_UINT32(136, status.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(512UL * 1024UL, status.maximumStoreBytes);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3800, status.capacity);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(3900, status.capacity);
    TEST_ASSERT_TRUE(status.writable);
}

void test_production_watering_store_definition_loads() {
    TEST_ASSERT_TRUE(g_productionStore.begin());
    Esp32BaseRecordStore::StoreStatus status;
    TEST_ASSERT_TRUE(g_productionStore.readStatus(status));
    TEST_ASSERT_EQUAL_UINT32(WateringRecordCodec::kPayloadSize + 24U, status.slotSizeBytes);
    TEST_ASSERT_EQUAL_UINT32(WateringRecordStore::kMaximumStoreBytes,
                             status.maximumStoreBytes);
    TEST_ASSERT_EQUAL_UINT32(32UL * 1024UL,
                             WateringRecordStore::kMinimumFileSystemFreeBytes);
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
    uint8_t flags = 0;
    Esp32BaseAppEvents::Level level = Esp32BaseAppEvents::Level::Info;
    Esp32BaseAppEvents::EventKind eventKind = Esp32BaseAppEvents::EventKind::Discrete;
    uint8_t conditionId = 0;
};

void captureEvent(const Esp32BaseAppEvents::EventRecord& event, void* user) {
    EventCapture* capture = static_cast<EventCapture*>(user);
    if (capture && capture->eventCode == 0) {
        capture->eventCode = event.eventCode;
        capture->reasonCode = event.reasonCode;
        capture->objectId = event.objectId;
        capture->value1 = event.value1;
        capture->value2 = event.value2;
        capture->flags = event.flags;
        capture->level = event.level;
        capture->eventKind = event.eventKind;
        capture->conditionId = event.conditionId;
    }
}

void test_irrigation_event_mapping_and_latest_read() {
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::clearEventHistory());
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::forgetAllConditionStates());
    IrrigationEvents events;
    WateringSessionSummary summary{};
    summary.purpose = WateringPurpose::Normal;
    summary.source = WateringSource::AutomaticPlan;
    summary.planId = 3;
    summary.result = WateringResult::Failed;
    summary.stopReason = WateringStopReason::NoFlowTimeout;
    summary.zoneCount = 1;
    summary.zones[0].zoneId = 2;
    summary.zones[0].result = ZoneWateringResult::Failed;
    summary.zones[0].pulseCount = 37;
    summary.zones[0].actualWateringSec = 18;
    events.syncStorageStatus();
    events.recordAbnormalWateringStop(summary);

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
    TEST_ASSERT_EQUAL_UINT8(
        (1U << 7U) | (1U << 2U) | (3U << 3U), capture.flags);
    TEST_ASSERT_EQUAL(static_cast<int>(Esp32BaseAppEvents::Level::Error),
                      static_cast<int>(capture.level));
    Esp32BaseAppEvents::EventRecord wateringPresentation{};
    wateringPresentation.eventCode = capture.eventCode;
    wateringPresentation.reasonCode = capture.reasonCode;
    wateringPresentation.objectId = capture.objectId;
    wateringPresentation.flags = capture.flags;
    char wateringTitle[96]{};
    IrrigationEvents::formatTitle(wateringPresentation,
                                  wateringTitle,
                                  sizeof(wateringTitle),
                                  "晚间花园",
                                  "菜地");
    TEST_ASSERT_EQUAL_STRING(
        "自动计划“晚间花园”失败：菜地浇水时水流中断",
        wateringTitle);
    TEST_ASSERT_EQUAL(
        static_cast<int>(WateringSource::AutomaticPlan),
        static_cast<int>(IrrigationEvents::wateringSource(
            wateringPresentation)));
    TEST_ASSERT_TRUE(
        IrrigationEvents::hasWateringContext(wateringPresentation));
    TEST_ASSERT_EQUAL_UINT8(
        3, IrrigationEvents::wateringPlanId(wateringPresentation));

    const uint32_t afterWateringEvent = Esp32BaseAppEvents::eventCount();
    summary.purpose = WateringPurpose::FlowCalibration;
    events.recordAbnormalWateringStop(summary);
    TEST_ASSERT_EQUAL_UINT32(afterWateringEvent, Esp32BaseAppEvents::eventCount());

    events.observeRtcRollback(Esp32BaseAppEvents::ObservedConditionState::Active);
    const uint32_t afterActivation = Esp32BaseAppEvents::eventCount();
    TEST_ASSERT_EQUAL_UINT32(afterWateringEvent + 1U, afterActivation);
    events.observeRtcRollback(Esp32BaseAppEvents::ObservedConditionState::Active);
    TEST_ASSERT_EQUAL_UINT32(afterActivation, Esp32BaseAppEvents::eventCount());
    events.observeRtcRollback(Esp32BaseAppEvents::ObservedConditionState::Inactive);
    TEST_ASSERT_EQUAL_UINT32(afterActivation + 1U, Esp32BaseAppEvents::eventCount());

    TEST_ASSERT_TRUE(Esp32BaseAppEvents::clearEventHistory());
    events.observeRtcRollback(Esp32BaseAppEvents::ObservedConditionState::Active);
    TEST_ASSERT_EQUAL_UINT32(1U, Esp32BaseAppEvents::eventCount());
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::clearEventHistory());
    TEST_ASSERT_TRUE(events.resetConditionHistory());
    TEST_ASSERT_EQUAL(
        static_cast<int>(IrrigationEvents::ConditionDisplayState::Unknown),
        static_cast<int>(events.conditionState(3)));
    events.observeRtcRollback(Esp32BaseAppEvents::ObservedConditionState::Active);
    TEST_ASSERT_EQUAL_UINT32(1U, Esp32BaseAppEvents::eventCount());

    EventCapture recoveredEvent;
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::readLatest(0, 1, captureEvent, &recoveredEvent));
    TEST_ASSERT_EQUAL(static_cast<int>(Esp32BaseAppEvents::EventKind::ConditionActivated),
                      static_cast<int>(recoveredEvent.eventKind));
    TEST_ASSERT_EQUAL_UINT8(3, recoveredEvent.conditionId);
    char title[64]{};
    Esp32BaseAppEvents::EventRecord presentation{};
    presentation.eventCode = recoveredEvent.eventCode;
    presentation.eventKind = recoveredEvent.eventKind;
    IrrigationEvents::formatTitle(presentation, title, sizeof(title));
    TEST_ASSERT_EQUAL_STRING("检测到设备时间倒退", title);

    constexpr uint32_t eventCodes[] = {1001, 1002, 1003, 1004, 1006, 1009, 1101,
                                       1102, 1103, 1104, 1201, 1202, 1203};
    for (const uint32_t eventCode : eventCodes) {
        Esp32BaseAppEvents::EventRecord sample{};
        sample.eventCode = eventCode;
        sample.objectId = 1;
        sample.reasonCode = eventCode == 1001 ? 1U :
                            eventCode == 1002 ? 201U :
                            eventCode == 1003 ? 211U :
                            eventCode == 1201 ? 5U :
                            eventCode == 1009 ? 405U : 0U;
        char sampleTitle[96]{};
        char sampleSummary[160]{};
        IrrigationEvents::formatTitle(sample, sampleTitle, sizeof(sampleTitle));
        IrrigationEvents::formatSummary(sample, sampleSummary, sizeof(sampleSummary));
        TEST_ASSERT_NOT_EQUAL(0, sampleTitle[0]);
        TEST_ASSERT_NOT_EQUAL(0, sampleSummary[0]);
        TEST_ASSERT_NOT_EQUAL(0, std::strcmp("未知事件", sampleTitle));
        TEST_ASSERT_NOT_EQUAL(0, std::strcmp("没有可用的业务说明。", sampleSummary));
    }

    TEST_ASSERT_TRUE(Esp32BaseAppEvents::clearEventHistory());
    TEST_ASSERT_TRUE(Esp32BaseAppEvents::forgetAllConditionStates());

    removeTestStore(g_mainStore);
    removeTestStore(g_rotationStore);
    removeTestStore(g_damageStore);
}

void test_scheduler_state_nvs_round_trip_and_corruption_detection() {
    WateringSchedulerStore store;
    TEST_ASSERT_TRUE(store.clear());
    WateringSchedulerPersistentState state{};
    TEST_ASSERT_EQUAL(static_cast<int>(SchedulerStorageLoadResult::Missing),
                      static_cast<int>(store.load(state)));

    state.mode = AutomaticWateringMode::PausedUntil;
    state.resumeAtEpoch = 1767200400UL;
    state.currentLocalDay = 20454;
    state.currentProcessedMask = 0x80000001UL;
    state.previousLocalDay = 20453;
    state.previousProcessedMask = 0x00000002UL;

    uint8_t encodedWithoutMarker[WateringSchedulerStateCodec::kEncodedSize]{};
    TEST_ASSERT_TRUE(WateringSchedulerStateCodec::encode(
        state, encodedWithoutMarker, sizeof(encodedWithoutMarker)));
    TEST_ASSERT_TRUE(Esp32BaseConfig::setBlob("irrigation",
                                             "sched_state",
                                             encodedWithoutMarker,
                                             sizeof(encodedWithoutMarker)));
    WateringSchedulerPersistentState loadedWithoutMarker{};
    TEST_ASSERT_EQUAL(static_cast<int>(SchedulerStorageLoadResult::Loaded),
                      static_cast<int>(store.load(loadedWithoutMarker)));
    TEST_ASSERT_EQUAL_UINT32(state.resumeAtEpoch,
                             loadedWithoutMarker.resumeAtEpoch);
    TEST_ASSERT_TRUE(store.clear());

    TEST_ASSERT_TRUE(store.save(state));
    TEST_ASSERT_TRUE(Esp32BaseConfig::getBool("irrigation", "sched_init", false));

    WateringSchedulerPersistentState loaded{};
    TEST_ASSERT_EQUAL(static_cast<int>(SchedulerStorageLoadResult::Loaded),
                      static_cast<int>(store.load(loaded)));
    TEST_ASSERT_EQUAL(static_cast<int>(state.mode), static_cast<int>(loaded.mode));
    TEST_ASSERT_EQUAL_UINT32(state.resumeAtEpoch, loaded.resumeAtEpoch);
    TEST_ASSERT_EQUAL_UINT32(state.currentLocalDay, loaded.currentLocalDay);
    TEST_ASSERT_EQUAL_UINT32(state.currentProcessedMask, loaded.currentProcessedMask);
    TEST_ASSERT_EQUAL_UINT32(state.previousLocalDay, loaded.previousLocalDay);
    TEST_ASSERT_EQUAL_UINT32(state.previousProcessedMask, loaded.previousProcessedMask);

    uint8_t damaged[WateringSchedulerStateCodec::kEncodedSize]{};
    TEST_ASSERT_TRUE(WateringSchedulerStateCodec::encode(state, damaged, sizeof(damaged)));
    damaged[12] ^= 0x01;
    TEST_ASSERT_TRUE(Esp32BaseConfig::setBlob("irrigation",
                                             "sched_state",
                                             damaged,
                                             sizeof(damaged)));
    TEST_ASSERT_EQUAL(static_cast<int>(SchedulerStorageLoadResult::Invalid),
                      static_cast<int>(store.load(loaded)));
    TEST_ASSERT_TRUE(store.clear());
}

void test_alive_checkpoint_waits_for_quiet_interval_and_persists() {
    TEST_ASSERT_TRUE(Esp32BaseConfig::clearNamespace("irrigation"));
    DeviceAliveCheckpoint checkpoint;
    TEST_ASSERT_TRUE(checkpoint.begin());
    Esp32BaseTime::Snapshot now{};
    now.synced = true;
    now.source = Esp32BaseTime::SOURCE_NTP;
    now.epochSec = 1767200000UL;
    checkpoint.handle(now, 1, false, 10);
    TEST_ASSERT_EQUAL_UINT32(0, checkpoint.lastKnownAliveEpoch());
    now.epochSec += 3500U;
    checkpoint.handle(now, 1, false, 11);
    now.epochSec += 3599U;
    checkpoint.handle(now, 1, false, 11);
    TEST_ASSERT_EQUAL_UINT32(0, checkpoint.lastKnownAliveEpoch());
    ++now.epochSec;
    checkpoint.handle(now, 1, false, 11);
    TEST_ASSERT_EQUAL_UINT32(now.epochSec, checkpoint.lastKnownAliveEpoch());

    DeviceAliveCheckpoint reloaded;
    TEST_ASSERT_TRUE(reloaded.begin());
    TEST_ASSERT_EQUAL_UINT32(now.epochSec, reloaded.lastKnownAliveEpoch());
    TEST_ASSERT_TRUE(Esp32BaseConfig::clearNamespace("irrigation"));
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
    RUN_TEST(test_scheduler_state_nvs_round_trip_and_corruption_detection);
    RUN_TEST(test_alive_checkpoint_waits_for_quiet_interval_and_persists);
    UNITY_END();
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
