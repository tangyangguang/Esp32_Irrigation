#include <unity.h>

#include "irrigation/IrrigationConfig.h"
#include "irrigation/WateringScheduler.h"

namespace {

constexpr uint32_t kLocalMidnight = 1767196800UL;  // 2026-01-01 00:00 UTC+8.

class FakeStorage : public WateringSchedulerStorage {
public:
    SchedulerStorageLoadResult load(WateringSchedulerPersistentState& output) override {
        if (loadResult == SchedulerStorageLoadResult::Loaded) {
            output = state;
        }
        return loadResult;
    }

    bool save(const WateringSchedulerPersistentState& input) override {
        ++saveCount;
        if (failSave) {
            return false;
        }
        state = input;
        loadResult = SchedulerStorageLoadResult::Loaded;
        return true;
    }

    bool clear() override {
        state = {};
        loadResult = SchedulerStorageLoadResult::Missing;
        return true;
    }

    WateringSchedulerPersistentState state{};
    SchedulerStorageLoadResult loadResult = SchedulerStorageLoadResult::Missing;
    uint32_t saveCount = 0;
    bool failSave = false;
};

struct CallbackState {
    WateringStartResult nextStartResult = WateringStartResult::Started;
    WateringRequest lastRequest{};
    WateringScheduler::Event lastEvent = WateringScheduler::Event::StorageFault;
    uint32_t startCount = 0;
    uint32_t eventCount = 0;
    uint8_t eventPlanId = 0;
};

WateringStartResult startWatering(const WateringRequest& request, void* user) {
    CallbackState& state = *static_cast<CallbackState*>(user);
    state.lastRequest = request;
    ++state.startCount;
    return state.nextStartResult;
}

void captureEvent(WateringScheduler::Event event, uint8_t planId, int32_t, void* user) {
    CallbackState& state = *static_cast<CallbackState*>(user);
    state.lastEvent = event;
    state.eventPlanId = planId;
    ++state.eventCount;
}

IrrigationConfig scheduledConfig(uint16_t startMinute) {
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    WateringPlan& plan = config.plans[0];
    plan.configured = true;
    plan.scheduleEnabled = true;
    plan.name[0] = 'A';
    plan.name[1] = '\0';
    plan.startMinutes[0] = startMinute;
    plan.zoneDurationMinutes[0] = 2;
    plan.zoneDurationMinutes[1] = 3;
    return config;
}

void initialize(WateringScheduler& scheduler,
                FakeStorage& storage,
                CallbackState& callbacks) {
    scheduler.setCallbacks(startWatering, captureEvent, &callbacks);
    TEST_ASSERT_TRUE(scheduler.begin(storage));
}

void test_startup_minute_is_skipped_and_next_minute_runs_once() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    const IrrigationConfig config = scheduledConfig(1);

    scheduler.handle(config, true, false, kLocalMidnight);
    TEST_ASSERT_EQUAL_UINT32(0, callbacks.startCount);
    scheduler.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(1, callbacks.startCount);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringSource::AutomaticPlan),
                      static_cast<int>(callbacks.lastRequest.source));
    TEST_ASSERT_EQUAL_UINT8(1, callbacks.lastRequest.planId);
    TEST_ASSERT_EQUAL_UINT8(2, callbacks.lastRequest.stepCount);
    TEST_ASSERT_EQUAL_UINT32(120, callbacks.lastRequest.steps[0].targetDurationSec);
    TEST_ASSERT_EQUAL_UINT32(180, callbacks.lastRequest.steps[1].targetDurationSec);
    scheduler.handle(config, true, false, kLocalMidnight + 61U);
    TEST_ASSERT_EQUAL_UINT32(1, callbacks.startCount);
}

void test_reboot_uses_persisted_processed_mask() {
    FakeStorage storage;
    CallbackState firstCallbacks;
    WateringScheduler first;
    initialize(first, storage, firstCallbacks);
    const IrrigationConfig config = scheduledConfig(1);
    first.handle(config, true, false, kLocalMidnight);
    first.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(1, firstCallbacks.startCount);

    CallbackState rebootCallbacks;
    WateringScheduler rebooted;
    initialize(rebooted, storage, rebootCallbacks);
    rebooted.handle(config, true, false, kLocalMidnight);
    rebooted.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(0, rebootCallbacks.startCount);
}

void test_busy_is_reported_and_still_marked_processed() {
    FakeStorage storage;
    CallbackState callbacks;
    callbacks.nextStartResult = WateringStartResult::Busy;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    const IrrigationConfig config = scheduledConfig(1);
    scheduler.handle(config, true, false, kLocalMidnight);
    scheduler.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(1, callbacks.startCount);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringScheduler::Event::PlanSkippedBusy),
                      static_cast<int>(callbacks.lastEvent));
    TEST_ASSERT_EQUAL_UINT8(1, callbacks.eventPlanId);

    CallbackState rebootCallbacks;
    WateringScheduler rebooted;
    initialize(rebooted, storage, rebootCallbacks);
    rebooted.handle(config, true, false, kLocalMidnight);
    rebooted.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(0, rebootCallbacks.startCount);
}

void test_pause_modes_skip_or_resume_without_immediate_manual_run() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(1);
    TEST_ASSERT_TRUE(scheduler.pauseIndefinitely());
    scheduler.handle(config, true, false, kLocalMidnight);
    scheduler.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(0, callbacks.startCount);
    TEST_ASSERT_TRUE(scheduler.resumeManually());
    scheduler.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(0, callbacks.startCount);

    FakeStorage timedStorage;
    CallbackState timedCallbacks;
    WateringScheduler timedScheduler;
    initialize(timedScheduler, timedStorage, timedCallbacks);
    config.plans[0].startMinutes[0] = 3;
    TEST_ASSERT_FALSE(timedScheduler.pauseUntil(kLocalMidnight + 180U,
                                               false,
                                               kLocalMidnight + 60U));
    TEST_ASSERT_FALSE(timedScheduler.pauseUntil(kLocalMidnight + 60U,
                                               true,
                                               kLocalMidnight + 60U));
    TEST_ASSERT_TRUE(timedScheduler.pauseUntil(kLocalMidnight + 180U,
                                              true,
                                              kLocalMidnight + 60U));
    timedScheduler.handle(config, true, false, kLocalMidnight + 120U);
    timedScheduler.handle(config, true, false, kLocalMidnight + 180U);
    TEST_ASSERT_EQUAL_UINT32(1, timedCallbacks.startCount);
    TEST_ASSERT_EQUAL(static_cast<int>(AutomaticWateringMode::Enabled),
                      static_cast<int>(timedScheduler.automaticState().mode));
}

void test_time_jump_and_rtc_rollback_rebase_without_running() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(10);
    scheduler.handle(config, true, false, kLocalMidnight);
    scheduler.handle(config, true, false, kLocalMidnight + 600U);
    TEST_ASSERT_EQUAL_UINT32(0, callbacks.startCount);

    scheduler.handle(config, true, false, kLocalMidnight + 1200U);
    scheduler.handle(config, true, false, kLocalMidnight + 600U);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringScheduler::TimeState::RtcRollback),
                      static_cast<int>(scheduler.timeState()));
    scheduler.handle(config, true, true, kLocalMidnight + 660U);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringScheduler::TimeState::Ready),
                      static_cast<int>(scheduler.timeState()));
    TEST_ASSERT_EQUAL_UINT32(0, callbacks.startCount);
}

void test_persisted_trusted_epoch_detects_rollback_after_restart() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(10);

    scheduler.setTrustedEpochBaseline(kLocalMidnight + 1200U);
    scheduler.handle(config, true, false, kLocalMidnight + 600U);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringScheduler::TimeState::RtcRollback),
                      static_cast<int>(scheduler.timeState()));

    scheduler.handle(config, true, true, kLocalMidnight + 660U);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringScheduler::TimeState::Ready),
                      static_cast<int>(scheduler.timeState()));
}

void test_persist_failure_prevents_automatic_start() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    const IrrigationConfig config = scheduledConfig(1);
    scheduler.handle(config, true, false, kLocalMidnight);
    storage.failSave = true;
    scheduler.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(0, callbacks.startCount);
    TEST_ASSERT_FALSE(scheduler.storageReady());
    TEST_ASSERT_EQUAL(static_cast<int>(WateringScheduler::Event::StorageFault),
                      static_cast<int>(callbacks.lastEvent));
}

void test_next_automatic_watering_finds_same_day_and_next_day() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(8U * 60U);
    scheduler.handle(config, true, false, kLocalMidnight + 6U * 3600U);

    NextAutomaticWatering next = scheduler.nextAutomaticWatering(
        config, kLocalMidnight + 6U * 3600U);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::Available),
                      static_cast<int>(next.status));
    TEST_ASSERT_EQUAL_UINT8(1, next.planId);
    TEST_ASSERT_EQUAL_UINT32(kLocalMidnight + 8U * 3600U, next.scheduledEpoch);

    next = scheduler.nextAutomaticWatering(config, kLocalMidnight + 8U * 3600U);
    TEST_ASSERT_EQUAL_UINT32(kLocalMidnight + 32U * 3600U, next.scheduledEpoch);
}

void test_next_automatic_watering_uses_earliest_enabled_plan() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(9U * 60U);
    WateringPlan& second = config.plans[1];
    second.configured = true;
    second.scheduleEnabled = true;
    second.name[0] = 'B';
    second.startMinutes[0] = 7U * 60U;
    second.zoneDurationMinutes[0] = 1;
    scheduler.handle(config, true, false, kLocalMidnight + 6U * 3600U);

    const NextAutomaticWatering next = scheduler.nextAutomaticWatering(
        config, kLocalMidnight + 6U * 3600U);
    TEST_ASSERT_EQUAL_UINT8(2, next.planId);
    TEST_ASSERT_EQUAL_UINT32(kLocalMidnight + 7U * 3600U, next.scheduledEpoch);
}

void test_next_automatic_watering_starts_after_timed_resume_minute() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(8U * 60U);
    config.plans[0].startMinutes[1] = 9U * 60U;
    scheduler.handle(config, true, false, kLocalMidnight + 6U * 3600U);
    TEST_ASSERT_TRUE(scheduler.pauseUntil(kLocalMidnight + 8U * 3600U,
                                         true,
                                         kLocalMidnight + 6U * 3600U));

    const NextAutomaticWatering next = scheduler.nextAutomaticWatering(
        config, kLocalMidnight + 6U * 3600U);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::Available),
                      static_cast<int>(next.status));
    TEST_ASSERT_EQUAL_UINT32(kLocalMidnight + 9U * 3600U, next.scheduledEpoch);
}

void test_next_automatic_watering_reports_unavailable_states() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(8U * 60U);

    NextAutomaticWatering next = scheduler.nextAutomaticWatering(config, kLocalMidnight);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::TimeUnavailable),
                      static_cast<int>(next.status));

    scheduler.handle(config, true, false, kLocalMidnight + 10U * 60U);
    scheduler.handle(config, true, false, kLocalMidnight);
    next = scheduler.nextAutomaticWatering(config, kLocalMidnight);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::RtcRollback),
                      static_cast<int>(next.status));

    scheduler.handle(config, true, true, kLocalMidnight + 60U);
    TEST_ASSERT_TRUE(scheduler.pauseIndefinitely());
    next = scheduler.nextAutomaticWatering(config, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::PausedIndefinitely),
                      static_cast<int>(next.status));

    TEST_ASSERT_TRUE(scheduler.resumeManually());
    config.plans[0].scheduleEnabled = false;
    next = scheduler.nextAutomaticWatering(config, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::NoEnabledPlans),
                      static_cast<int>(next.status));

    WateringScheduler notInitialized;
    next = notInitialized.nextAutomaticWatering(config, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::NoEnabledPlans),
                      static_cast<int>(next.status));
}

void test_next_automatic_watering_skips_start_already_processed_after_small_rollback() {
    FakeStorage storage;
    CallbackState callbacks;
    WateringScheduler scheduler;
    initialize(scheduler, storage, callbacks);
    IrrigationConfig config = scheduledConfig(1U);
    config.plans[0].startMinutes[1] = 3U;
    scheduler.handle(config, true, false, kLocalMidnight);
    scheduler.handle(config, true, false, kLocalMidnight + 60U);
    TEST_ASSERT_EQUAL_UINT32(1U, callbacks.startCount);

    scheduler.handle(config, true, false, kLocalMidnight);
    const NextAutomaticWatering next = scheduler.nextAutomaticWatering(config, kLocalMidnight);
    TEST_ASSERT_EQUAL(static_cast<int>(NextAutomaticWateringStatus::Available),
                      static_cast<int>(next.status));
    TEST_ASSERT_EQUAL_UINT32(kLocalMidnight + 180U, next.scheduledEpoch);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_startup_minute_is_skipped_and_next_minute_runs_once);
    RUN_TEST(test_reboot_uses_persisted_processed_mask);
    RUN_TEST(test_busy_is_reported_and_still_marked_processed);
    RUN_TEST(test_pause_modes_skip_or_resume_without_immediate_manual_run);
    RUN_TEST(test_time_jump_and_rtc_rollback_rebase_without_running);
    RUN_TEST(test_persisted_trusted_epoch_detects_rollback_after_restart);
    RUN_TEST(test_persist_failure_prevents_automatic_start);
    RUN_TEST(test_next_automatic_watering_finds_same_day_and_next_day);
    RUN_TEST(test_next_automatic_watering_uses_earliest_enabled_plan);
    RUN_TEST(test_next_automatic_watering_starts_after_timed_resume_minute);
    RUN_TEST(test_next_automatic_watering_reports_unavailable_states);
    RUN_TEST(test_next_automatic_watering_skips_start_already_processed_after_small_rollback);
    return UNITY_END();
}
