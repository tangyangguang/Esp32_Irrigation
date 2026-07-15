#include <unity.h>

#include "irrigation/IrrigationTime.h"

namespace {

void test_parse_local_datetime_uses_fixed_utc8() {
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(IrrigationTime::parseLocalDateTimeUtc8("2026-01-01T00:00", epoch));
    TEST_ASSERT_EQUAL_UINT32(1767196800UL, epoch);
}

void test_parse_local_datetime_validates_calendar_and_range() {
    uint32_t epoch = 0;
    TEST_ASSERT_TRUE(IrrigationTime::parseLocalDateTimeUtc8("2028-02-29T23:59", epoch));
    TEST_ASSERT_TRUE(IrrigationTime::parseLocalDateTimeUtc8("2099-12-31T23:59", epoch));
    TEST_ASSERT_FALSE(IrrigationTime::parseLocalDateTimeUtc8("2027-02-29T08:00", epoch));
    TEST_ASSERT_FALSE(IrrigationTime::parseLocalDateTimeUtc8("2026-04-31T08:00", epoch));
    TEST_ASSERT_FALSE(IrrigationTime::parseLocalDateTimeUtc8("2026-01-01T24:00", epoch));
    TEST_ASSERT_FALSE(IrrigationTime::parseLocalDateTimeUtc8("2025-12-31T23:59", epoch));
    TEST_ASSERT_FALSE(IrrigationTime::parseLocalDateTimeUtc8("2026/01/01 08:00", epoch));
}

void test_parse_local_datetime_crosses_month_and_year() {
    uint32_t before = 0;
    uint32_t after = 0;
    TEST_ASSERT_TRUE(IrrigationTime::parseLocalDateTimeUtc8("2026-01-31T23:59", before));
    TEST_ASSERT_TRUE(IrrigationTime::parseLocalDateTimeUtc8("2026-02-01T00:00", after));
    TEST_ASSERT_EQUAL_UINT32(60U, after - before);
    TEST_ASSERT_TRUE(IrrigationTime::parseLocalDateTimeUtc8("2026-12-31T23:00", before));
    TEST_ASSERT_TRUE(IrrigationTime::parseLocalDateTimeUtc8("2027-01-01T00:00", after));
    TEST_ASSERT_EQUAL_UINT32(3600U, after - before);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_local_datetime_uses_fixed_utc8);
    RUN_TEST(test_parse_local_datetime_validates_calendar_and_range);
    RUN_TEST(test_parse_local_datetime_crosses_month_and_year);
    return UNITY_END();
}
