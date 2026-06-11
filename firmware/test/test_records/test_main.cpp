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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_record_layout_is_fixed_for_binary_store);
    RUN_TEST(test_record_defaults_are_empty_and_versioned);
    return UNITY_END();
}
