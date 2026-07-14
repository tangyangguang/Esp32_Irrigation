#include <unity.h>

#include "irrigation/UnexpectedFlowMonitor.h"

namespace {

void test_delay_ignores_pulses_then_rolling_window_raises_and_clears() {
    UnexpectedFlowMonitor monitor;
    monitor.begin(0, 100, 30, 30, 3);
    TEST_ASSERT_EQUAL(static_cast<int>(UnexpectedFlowMonitor::Update::None),
                      static_cast<int>(monitor.observe(29999, 110)));
    TEST_ASSERT_EQUAL(static_cast<int>(UnexpectedFlowMonitor::Update::None),
                      static_cast<int>(monitor.observe(30000, 110)));
    TEST_ASSERT_EQUAL(static_cast<int>(UnexpectedFlowMonitor::Update::None),
                      static_cast<int>(monitor.observe(31000, 112)));
    TEST_ASSERT_EQUAL(static_cast<int>(UnexpectedFlowMonitor::Update::AlarmRaised),
                      static_cast<int>(monitor.observe(59000, 113)));
    TEST_ASSERT_TRUE(monitor.alarmActive());
    TEST_ASSERT_EQUAL(static_cast<int>(UnexpectedFlowMonitor::Update::AlarmCleared),
                      static_cast<int>(monitor.observe(62000, 113)));
    TEST_ASSERT_FALSE(monitor.alarmActive());
}

void test_pulses_across_fixed_window_boundary_are_not_missed() {
    UnexpectedFlowMonitor monitor;
    monitor.begin(0, 0, 0, 30, 3);
    monitor.observe(29000, 2);
    TEST_ASSERT_EQUAL(static_cast<int>(UnexpectedFlowMonitor::Update::AlarmRaised),
                      static_cast<int>(monitor.observe(30000, 3)));
}

void test_millis_and_pulse_counter_wrap_are_safe() {
    UnexpectedFlowMonitor monitor;
    const uint32_t started = UINT32_MAX - 1000U;
    monitor.begin(started, UINT32_MAX - 1U, 1, 5, 2);
    monitor.observe(0, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(UnexpectedFlowMonitor::Update::AlarmRaised),
                      static_cast<int>(monitor.observe(1000, 2)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_delay_ignores_pulses_then_rolling_window_raises_and_clears);
    RUN_TEST(test_pulses_across_fixed_window_boundary_are_not_missed);
    RUN_TEST(test_millis_and_pulse_counter_wrap_are_safe);
    return UNITY_END();
}
