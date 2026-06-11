#include <stdint.h>
#include <unity.h>

#include "irrigation/IrrigationRecords.h"

using namespace irrigation;

void test_record_layout_is_fixed_for_binary_store() {
    TEST_ASSERT_EQUAL_UINT16(1, kIrrigationRecordVersion);
    TEST_ASSERT_EQUAL_UINT16(sizeof(IrrigationRunRecord), kIrrigationRunRecordSize);
    TEST_ASSERT_EQUAL_UINT16(128, kIrrigationRunRecordSize);
}

void test_record_defaults_are_empty_and_versioned() {
    const IrrigationRunRecord record = makeEmptyRunRecord();

    TEST_ASSERT_EQUAL_UINT16(kIrrigationRecordVersion, record.version);
    TEST_ASSERT_EQUAL_UINT8(0, record.zoneId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(IrrigationRecordStopReason::Unknown), record.stopReason);
    TEST_ASSERT_EQUAL_UINT32(0, record.startedAt);
    TEST_ASSERT_EQUAL_UINT32(0, record.actualDurationSec);
    TEST_ASSERT_EQUAL_UINT32(0, record.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(0, record.estimatedMl);
}

void test_record_enum_keys_are_stable_for_api_and_csv() {
    TEST_ASSERT_EQUAL_STRING("manual", irrigationRecordSourceKey(IrrigationRecordSource::Manual));
    TEST_ASSERT_EQUAL_STRING("schedule", irrigationRecordSourceKey(IrrigationRecordSource::Schedule));
    TEST_ASSERT_EQUAL_STRING("completed", irrigationRecordStopReasonKey(IrrigationRecordStopReason::Completed));
    TEST_ASSERT_EQUAL_STRING("user_stop", irrigationRecordStopReasonKey(IrrigationRecordStopReason::UserStop));
    TEST_ASSERT_EQUAL_STRING("no_water", irrigationRecordStopReasonKey(IrrigationRecordStopReason::NoWater));
}

void test_record_store_meta_advances_as_ring_buffer() {
    IrrigationRecordStoreMeta meta = makeEmptyRecordStoreMeta(3);

    TEST_ASSERT_EQUAL_UINT16(kIrrigationRecordVersion, meta.version);
    TEST_ASSERT_EQUAL_UINT16(kIrrigationRunRecordSize, meta.recordSize);
    TEST_ASSERT_EQUAL_UINT16(3, meta.recordCapacity);
    TEST_ASSERT_EQUAL_UINT16(0, meta.recordHead);
    TEST_ASSERT_EQUAL_UINT16(0, meta.recordCount);
    TEST_ASSERT_EQUAL_UINT32(1, meta.recordNextId);

    TEST_ASSERT_EQUAL_UINT16(0, nextRecordAppendSlot(meta));
    advanceRecordStoreMetaAfterAppend(meta);
    TEST_ASSERT_EQUAL_UINT16(1, nextRecordAppendSlot(meta));
    advanceRecordStoreMetaAfterAppend(meta);
    TEST_ASSERT_EQUAL_UINT16(2, nextRecordAppendSlot(meta));
    advanceRecordStoreMetaAfterAppend(meta);
    TEST_ASSERT_EQUAL_UINT16(0, meta.recordHead);
    TEST_ASSERT_EQUAL_UINT16(3, meta.recordCount);
    TEST_ASSERT_EQUAL_UINT32(4, meta.recordNextId);

    TEST_ASSERT_EQUAL_UINT16(0, nextRecordAppendSlot(meta));
    advanceRecordStoreMetaAfterAppend(meta);
    TEST_ASSERT_EQUAL_UINT16(1, meta.recordHead);
    TEST_ASSERT_EQUAL_UINT16(3, meta.recordCount);
    TEST_ASSERT_EQUAL_UINT32(5, meta.recordNextId);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_record_layout_is_fixed_for_binary_store);
    RUN_TEST(test_record_defaults_are_empty_and_versioned);
    RUN_TEST(test_record_enum_keys_are_stable_for_api_and_csv);
    RUN_TEST(test_record_store_meta_advances_as_ring_buffer);
    return UNITY_END();
}
