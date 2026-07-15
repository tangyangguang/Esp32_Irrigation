#include <unity.h>

#include <climits>

#include "irrigation/IrrigationConfig.h"
#include "irrigation/WateringController.h"

namespace {

class FakeWateringHardware final : public WateringHardware {
public:
    bool openValve(uint8_t zoneId, uint8_t dutyPercent) override {
        if (failOpen) {
            return false;
        }
        activeZone = zoneId;
        duty = dutyPercent;
        ++openCalls;
        return true;
    }

    bool setActiveValveDuty(uint8_t dutyPercent) override {
        if (failDuty) {
            return false;
        }
        duty = dutyPercent;
        ++dutyCalls;
        return true;
    }

    void closeValves() override {
        activeZone = 0;
        duty = 0;
        ++closeCalls;
    }

    bool setPumpSignal(bool active) override {
        if (active && failPump) {
            return false;
        }
        pump = active;
        return true;
    }

    void safeShutdown() override {
        activeZone = 0;
        duty = 0;
        pump = false;
        ++safeShutdownCalls;
    }

    uint32_t flowPulseCount() const override {
        return pulses;
    }

    uint32_t pulses = 0;
    uint8_t activeZone = 0;
    uint8_t duty = 0;
    uint8_t openCalls = 0;
    uint8_t dutyCalls = 0;
    uint8_t closeCalls = 0;
    uint8_t safeShutdownCalls = 0;
    bool pump = false;
    bool failOpen = false;
    bool failDuty = false;
    bool failPump = false;
};

WateringRequest requestFor(uint8_t firstZone, uint32_t firstDurationSec) {
    WateringRequest request{};
    request.source = WateringSource::ManualZones;
    request.planId = 0;
    request.stepCount = 1;
    request.steps[0] = {firstZone, firstDurationSec};
    return request;
}

void establishFlow(WateringController& controller, FakeWateringHardware& hardware, uint32_t nowMs) {
    controller.handle(nowMs);
    ++hardware.pulses;
    controller.handle(nowMs + 1U);
}

void test_gravity_watering_completes_and_applies_hold_duty() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();

    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(requestFor(1, 5), config, 0)));
    establishFlow(controller, hardware, 0);
    controller.handle(3000);
    TEST_ASSERT_EQUAL_UINT8(75, hardware.duty);
    TEST_ASSERT_EQUAL_UINT8(1, hardware.dutyCalls);

    ++hardware.pulses;
    controller.handle(5001);
    const WateringStatus status = controller.status();
    TEST_ASSERT_FALSE(status.active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringResult::Completed), static_cast<int>(status.lastResult));
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::Completed), static_cast<int>(status.lastStopReason));
    TEST_ASSERT_EQUAL_UINT8(0, hardware.activeZone);

    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringResult::Completed), static_cast<int>(summary->result));
    TEST_ASSERT_TRUE(summary->anyFlowEstablished);
    TEST_ASSERT_EQUAL_UINT8(1, summary->zoneCount);
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Completed),
                      static_cast<int>(summary->zones[0].result));
    TEST_ASSERT_EQUAL_UINT32(5, summary->zones[0].actualWateringSec);
    TEST_ASSERT_EQUAL_UINT32(2, summary->zones[0].pulseCount);
    TEST_ASSERT_EQUAL_UINT32(8, summary->zones[0].estimatedWaterMl);
    TEST_ASSERT_FALSE(summary->zones[0].waterEstimateCapped);

    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::PreviousResultPending),
                      static_cast<int>(controller.start(requestFor(1, 1), config, 6000)));
    controller.clearFinishedSession();
    TEST_ASSERT_NULL(controller.finishedSession());
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(requestFor(1, 1), config, 6000)));
}

void test_flow_start_timeout_stops_immediately() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(requestFor(1, 60), config, 0)));
    controller.handle(0);
    controller.handle(20000);

    const WateringStatus status = controller.status();
    TEST_ASSERT_FALSE(status.active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::FlowStartTimeout),
                      static_cast<int>(status.lastStopReason));
    TEST_ASSERT_EQUAL_UINT8(1, hardware.safeShutdownCalls);
    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_FALSE(summary->anyFlowEstablished);
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Failed),
                      static_cast<int>(summary->zones[0].result));
    TEST_ASSERT_EQUAL_UINT32(0, summary->zones[0].actualWateringSec);
}

void test_running_no_flow_timeout_stops_immediately() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    controller.start(requestFor(1, 60), config, 0);
    establishFlow(controller, hardware, 0);
    controller.handle(10001);

    const WateringStatus status = controller.status();
    TEST_ASSERT_FALSE(status.active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::NoFlowTimeout),
                      static_cast<int>(status.lastStopReason));
    TEST_ASSERT_FALSE(hardware.pump);
    TEST_ASSERT_EQUAL_UINT8(0, hardware.activeZone);
}

void test_user_stop_with_pump_waits_before_closing_valve() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.pump.enabled = true;
    config.pump.startDelayMs = 500;
    config.pump.stopToValveCloseDelayMs = 1000;

    controller.start(requestFor(1, 60), config, 0);
    TEST_ASSERT_TRUE(hardware.pump);
    TEST_ASSERT_TRUE(controller.stop(100));
    TEST_ASSERT_FALSE(hardware.pump);
    TEST_ASSERT_EQUAL_UINT8(1, hardware.activeZone);
    controller.handle(1099);
    TEST_ASSERT_EQUAL_UINT8(1, hardware.activeZone);
    controller.handle(1100);
    TEST_ASSERT_EQUAL_UINT8(0, hardware.activeZone);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::UserStopped),
                      static_cast<int>(controller.status().lastStopReason));
}

void test_stop_delay_still_transitions_valve_to_hold_duty() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.pump.enabled = true;
    config.pump.stopToValveCloseDelayMs = 5000;

    controller.start(requestFor(1, 60), config, 0);
    TEST_ASSERT_TRUE(controller.stop(100));
    controller.handle(2999);
    TEST_ASSERT_EQUAL_UINT8(100, hardware.duty);
    controller.handle(3000);
    TEST_ASSERT_EQUAL_UINT8(75, hardware.duty);
    TEST_ASSERT_EQUAL_UINT8(1, hardware.activeZone);
    controller.handle(5100);
    TEST_ASSERT_EQUAL_UINT8(0, hardware.activeZone);
}

void test_pump_watering_completes_with_start_and_stop_delays() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.pump.enabled = true;
    config.pump.startDelayMs = 500;
    config.pump.stopToValveCloseDelayMs = 1000;

    controller.start(requestFor(1, 1), config, 0);
    controller.handle(499);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringState::StartingZone),
                      static_cast<int>(controller.status().state));
    controller.handle(500);
    ++hardware.pulses;
    controller.handle(501);
    ++hardware.pulses;
    controller.handle(1501);
    TEST_ASSERT_FALSE(hardware.pump);
    TEST_ASSERT_EQUAL_UINT8(1, hardware.activeZone);
    controller.handle(2501);
    TEST_ASSERT_EQUAL_UINT8(0, hardware.activeZone);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringResult::Completed),
                      static_cast<int>(controller.status().lastResult));
}

void test_multiple_zones_run_in_order() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    WateringRequest request = requestFor(1, 1);
    request.stepCount = 2;
    request.steps[1] = {2, 1};

    controller.start(request, config, 0);
    establishFlow(controller, hardware, 0);
    ++hardware.pulses;
    controller.handle(1001);
    TEST_ASSERT_EQUAL_UINT8(2, hardware.activeZone);

    controller.handle(1001);
    ++hardware.pulses;
    controller.handle(1002);
    ++hardware.pulses;
    controller.handle(2002);
    TEST_ASSERT_FALSE(controller.status().active);
    TEST_ASSERT_EQUAL_UINT8(2, hardware.openCalls);

    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_EQUAL_UINT8(2, summary->zoneCount);
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Completed),
                      static_cast<int>(summary->zones[0].result));
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Completed),
                      static_cast<int>(summary->zones[1].result));
}

void test_stopped_session_keeps_unstarted_zones_explicit() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    WateringRequest request = requestFor(1, 60);
    request.stepCount = 2;
    request.steps[1] = {2, 60};

    controller.start(request, config, 0);
    establishFlow(controller, hardware, 0);
    ++hardware.pulses;
    TEST_ASSERT_TRUE(controller.stop(2001));

    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringResult::Stopped), static_cast<int>(summary->result));
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Stopped),
                      static_cast<int>(summary->zones[0].result));
    TEST_ASSERT_EQUAL_UINT32(2, summary->zones[0].actualWateringSec);
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::NotStarted),
                      static_cast<int>(summary->zones[1].result));
}

void test_water_estimate_uses_exact_fixed_point_arithmetic() {
    uint32_t waterMl = 0;
    TEST_ASSERT_TRUE(FlowMonitor::estimateWaterMilliliters(250, 25000, waterMl));
    TEST_ASSERT_EQUAL_UINT32(1000, waterMl);
    TEST_ASSERT_TRUE(FlowMonitor::estimateWaterMilliliters(1, 25000, waterMl));
    TEST_ASSERT_EQUAL_UINT32(4, waterMl);
    TEST_ASSERT_FALSE(FlowMonitor::estimateWaterMilliliters(1, 0, waterMl));
    TEST_ASSERT_EQUAL_UINT32(0, waterMl);
    TEST_ASSERT_FALSE(FlowMonitor::estimateWaterMilliliters(UINT32_MAX, 1, waterMl));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, waterMl);
}

void test_rate_window_uses_fixed_point_arithmetic() {
    FlowMonitor monitor;
    monitor.beginRateWindow(1000, 10);
    FlowMonitor::RateSample sample{};
    TEST_ASSERT_FALSE(monitor.takeRateSample(5999, 35, 25000, sample));
    TEST_ASSERT_TRUE(monitor.takeRateSample(6000, 35, 25000, sample));
    TEST_ASSERT_EQUAL_UINT32(1200, sample.flowMlPerMinute);
    TEST_ASSERT_EQUAL_UINT32(25, sample.pulseCount);
    TEST_ASSERT_EQUAL_UINT32(5000, sample.windowMs);
}

void test_live_status_reports_plan_progress_remaining_time_and_water() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    WateringRequest request = requestFor(1, 60);
    request.source = WateringSource::AutomaticPlan;
    request.planId = 2;
    request.stepCount = 2;
    request.steps[1] = {2, 120};
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(request, config, 0)));
    establishFlow(controller, hardware, 0);
    hardware.pulses += 25;
    controller.handle(5001);

    const WateringStatus status = controller.status();
    TEST_ASSERT_TRUE(status.active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringSource::AutomaticPlan),
                      static_cast<int>(status.source));
    TEST_ASSERT_EQUAL_UINT8(2, status.planId);
    TEST_ASSERT_EQUAL_UINT8(2, status.stepCount);
    TEST_ASSERT_EQUAL_UINT8(0, status.currentStepIndex);
    TEST_ASSERT_EQUAL_UINT32(5, status.currentZoneElapsedSec);
    TEST_ASSERT_EQUAL_UINT32(55, status.currentZoneRemainingSec);
    TEST_ASSERT_EQUAL_UINT32(175, status.plannedRemainingSec);
    TEST_ASSERT_EQUAL_UINT32(60, status.zones[0].plannedDurationSec);
    TEST_ASSERT_EQUAL_UINT32(120, status.zones[1].plannedDurationSec);
    TEST_ASSERT_GREATER_THAN_UINT32(0, status.currentFlowMlPerMinute);
    TEST_ASSERT_GREATER_THAN_UINT64(0, status.totalEstimatedWaterMl);

    const FlowHistorySnapshot history = controller.flowHistory();
    TEST_ASSERT_EQUAL_UINT8(1, history.zoneId);
    TEST_ASSERT_EQUAL_UINT16(1, history.sampleCount);
    TEST_ASSERT_EQUAL_UINT32(status.currentFlowMlPerMinute, history.samples[0]);
}

void test_flow_history_keeps_latest_ten_minutes_and_resets_for_next_zone() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(requestFor(1, 7200), config, 0)));
    establishFlow(controller, hardware, 0);
    for (uint32_t sample = 1; sample <= kFlowHistorySampleCount + 5U; ++sample) {
        hardware.pulses += sample;
        controller.handle(sample * 5000U + 1U);
    }
    const FlowHistorySnapshot full = controller.flowHistory();
    TEST_ASSERT_EQUAL_UINT16(kFlowHistorySampleCount, full.sampleCount);
    TEST_ASSERT_EQUAL_UINT32(kFlowHistorySampleCount + 5U, full.latestSerial);
    TEST_ASSERT_LESS_THAN_UINT32(full.samples[kFlowHistorySampleCount - 1U], full.samples[0]);

    TEST_ASSERT_TRUE(controller.stop((kFlowHistorySampleCount + 6U) * 5000U));
    controller.clearFinishedSession();
    WateringRequest next = requestFor(1, 5);
    next.stepCount = 2;
    next.steps[1] = {2, 60};
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(next, config, 0)));
    establishFlow(controller, hardware, 0);
    const uint32_t firstGeneration = controller.flowHistory().generation;
    ++hardware.pulses;
    controller.handle(5001);
    const FlowHistorySnapshot secondZone = controller.flowHistory();
    TEST_ASSERT_EQUAL_UINT8(2, secondZone.zoneId);
    TEST_ASSERT_EQUAL_UINT16(0, secondZone.sampleCount);
    TEST_ASSERT_GREATER_THAN_UINT32(firstGeneration, secondZone.generation);
}

void test_persistent_low_flow_alert_can_stop_watering() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.zones[0].learnedFlowMlPerMinute = 2000;
    config.flowProtection.flowDeviationConfirmSec = 20;
    config.flowProtection.lowFlowAction = FlowAlertAction::StopWatering;
    controller.start(requestFor(1, 60), config, 0);
    establishFlow(controller, hardware, 0);

    for (uint32_t second = 5; second <= 25; second += 5) {
        hardware.pulses += 10;
        controller.handle(second * 1000U + 1U);
    }
    TEST_ASSERT_FALSE(controller.status().active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::LowFlow),
                      static_cast<int>(controller.status().lastStopReason));
    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_TRUE(summary->zones[0].lowFlowDetected);
    TEST_ASSERT_FALSE(summary->zones[0].highFlowDetected);
}

void test_unlearned_zone_does_not_enable_flow_deviation_protection() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.flowProtection.flowDeviationConfirmSec = 1;
    config.flowProtection.lowFlowAction = FlowAlertAction::StopWatering;
    controller.start(requestFor(1, 60), config, 0);
    establishFlow(controller, hardware, 0);
    for (uint32_t second = 5; second <= 25; second += 5) {
        ++hardware.pulses;
        controller.handle(second * 1000U + 1U);
    }
    TEST_ASSERT_TRUE(controller.status().active);
    TEST_ASSERT_TRUE(controller.stop(26001));
    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_FALSE(summary->zones[0].lowFlowDetected);
}

void test_zone_flow_learning_completes_after_five_stable_windows() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    WateringRequest request = requestFor(1, 600);
    request.purpose = WateringPurpose::ZoneFlowLearning;
    controller.start(request, config, 0);
    establishFlow(controller, hardware, 0);
    for (uint32_t second = 5; second <= 25; second += 5) {
        hardware.pulses += 21;
        controller.handle(second * 1000U + 1U);
        if (second == 20) {
            const WateringStatus live = controller.status();
            TEST_ASSERT_TRUE(live.active);
            TEST_ASSERT_EQUAL_UINT32(1008, live.currentFlowMlPerMinute);
            TEST_ASSERT_EQUAL_UINT32(1008, live.learningAverageMlPerMinute);
            TEST_ASSERT_EQUAL_UINT32(1008, live.learningMinimumMlPerMinute);
            TEST_ASSERT_EQUAL_UINT32(1008, live.learningMaximumMlPerMinute);
            TEST_ASSERT_EQUAL_UINT8(4, live.learningSampleCount);
        }
    }
    TEST_ASSERT_FALSE(controller.status().active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::Completed),
                      static_cast<int>(controller.status().lastStopReason));
    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_EQUAL_UINT32(1008, summary->zones[0].suggestedFlowMlPerMinute);
    TEST_ASSERT_EQUAL_UINT8(5, controller.status().learningSampleCount);
}

void test_zone_flow_learning_can_be_stopped_manually() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    WateringRequest request = requestFor(1, 600);
    request.purpose = WateringPurpose::ZoneFlowLearning;
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(request, config, 0)));
    establishFlow(controller, hardware, 0);
    hardware.pulses += 21;
    controller.handle(5001);
    TEST_ASSERT_TRUE(controller.stop(6000));
    TEST_ASSERT_FALSE(controller.status().active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::UserStopped),
                      static_cast<int>(controller.status().lastStopReason));
    TEST_ASSERT_EQUAL_UINT32(0,
                             controller.finishedSession()->zones[0]
                                 .suggestedFlowMlPerMinute);
}

void test_zone_flow_learning_stops_at_ten_minute_limit_when_unstable() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    WateringRequest request = requestFor(1, 600);
    request.purpose = WateringPurpose::ZoneFlowLearning;
    controller.start(request, config, 0);
    establishFlow(controller, hardware, 0);
    for (uint32_t second = 5; second <= 600; second += 5) {
        hardware.pulses += (second / 5U) % 2U == 0 ? 40U : 10U;
        controller.handle(second * 1000U + 1U);
    }
    TEST_ASSERT_FALSE(controller.status().active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::LearningTimeout),
                      static_cast<int>(controller.status().lastStopReason));
    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_EQUAL_UINT32(0, summary->zones[0].suggestedFlowMlPerMinute);
}

void test_invalid_request_and_hardware_failure_are_rejected_safely() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::InvalidRequest),
                      static_cast<int>(controller.start(requestFor(3, 60), config, 0)));

    WateringRequest invalidPurpose = requestFor(1, 60);
    invalidPurpose.purpose = static_cast<WateringPurpose>(99);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::InvalidRequest),
                      static_cast<int>(controller.start(invalidPurpose, config, 0)));

    hardware.failOpen = true;
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::HardwareFailure),
                      static_cast<int>(controller.start(requestFor(1, 60), config, 0)));
    TEST_ASSERT_EQUAL_UINT8(1, hardware.safeShutdownCalls);
}

void test_timers_work_across_millis_wraparound() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();
    const uint32_t started = UINT32_MAX - 2000U;

    controller.start(requestFor(1, 5), config, started);
    controller.handle(started);
    ++hardware.pulses;
    controller.handle(started + 1U);
    controller.handle(started + 3000U);
    TEST_ASSERT_EQUAL_UINT8(75, hardware.duty);
    ++hardware.pulses;
    controller.handle(started + 5001U);
    TEST_ASSERT_FALSE(controller.status().active);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringResult::Completed),
                      static_cast<int>(controller.status().lastResult));
}

void test_maintenance_abort_immediately_closes_outputs_and_keeps_result() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    const IrrigationConfig config = IrrigationConfigRules::createDefault();

    controller.start(requestFor(1, 60), config, 0);
    establishFlow(controller, hardware, 0);
    ++hardware.pulses;
    TEST_ASSERT_TRUE(controller.abortForMaintenance(2001));
    TEST_ASSERT_FALSE(controller.status().active);
    TEST_ASSERT_EQUAL_UINT8(0, hardware.activeZone);
    TEST_ASSERT_FALSE(hardware.pump);

    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringResult::Failed), static_cast<int>(summary->result));
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::MaintenanceInterrupted),
                      static_cast<int>(summary->stopReason));
    TEST_ASSERT_EQUAL(static_cast<int>(ZoneWateringResult::Failed),
                      static_cast<int>(summary->zones[0].result));
    TEST_ASSERT_EQUAL_UINT32(2, summary->zones[0].actualWateringSec);
}

void test_flow_calibration_limit_counts_from_valve_open() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.flowProtection.flowStartTimeoutSec = 120;
    config.flowProtection.noFlowTimeoutSec = 120;
    WateringRequest request = requestFor(1, 600);
    request.purpose = WateringPurpose::FlowCalibration;

    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(request, config, 0)));
    controller.handle(100000);
    ++hardware.pulses;
    controller.handle(100001);
    ++hardware.pulses;
    controller.handle(500000);
    controller.handle(599999);
    TEST_ASSERT_TRUE(controller.status().active);
    TEST_ASSERT_EQUAL_UINT32(1, controller.status().currentZoneRemainingSec);

    controller.handle(600000);
    TEST_ASSERT_FALSE(controller.status().active);
    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT_EQUAL(static_cast<int>(WateringStopReason::Completed),
                      static_cast<int>(summary->stopReason));
    TEST_ASSERT_EQUAL_UINT32(600, summary->elapsedSec);
}

void test_flow_calibration_detects_and_records_steady_phases() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.calibrationStability = {1, 3, 10};
    config.flowProtection.noFlowTimeoutSec = 20;
    WateringRequest request = requestFor(1, 60);
    request.purpose = WateringPurpose::FlowCalibration;

    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(request, config, 0)));
    controller.handle(0);
    hardware.pulses = 1;
    controller.handle(1);
    for (uint32_t window = 1; window <= 3; ++window) {
        hardware.pulses += 8;
        controller.handle(window * 1000U + 1U);
    }

    WateringStatus status = controller.status();
    TEST_ASSERT_TRUE(status.zones[0].calibrationSteadyDetected);
    TEST_ASSERT_EQUAL_UINT32(1, status.zones[0].calibrationSteadyStartedMs);
    TEST_ASSERT_EQUAL_UINT32(1, status.zones[0].calibrationStartupPulses);
    TEST_ASSERT_EQUAL_UINT32(24, status.zones[0].calibrationSteadyPulses);
    TEST_ASSERT_EQUAL_UINT32(800, status.zones[0].calibrationPulseRateX100);
    TEST_ASSERT_EQUAL_UINT8(3, status.zones[0].calibrationCollectedWindows);

    hardware.pulses += 16;
    controller.handle(4001);
    TEST_ASSERT_TRUE(controller.status().zones[0].calibrationSteadyLaterUnstable);
    TEST_ASSERT_TRUE(controller.stop(4501));
    const WateringSessionSummary* summary = controller.finishedSession();
    TEST_ASSERT_NOT_NULL(summary);
    const ZoneWateringSummary& zone = summary->zones[0];
    TEST_ASSERT_TRUE(zone.calibrationSteadyDetected);
    TEST_ASSERT_TRUE(zone.calibrationSteadyLaterUnstable);
    TEST_ASSERT_EQUAL_UINT32(1, zone.calibrationFlowEstablishedMs);
    TEST_ASSERT_EQUAL_UINT32(1, zone.calibrationSteadyStartedMs);
    TEST_ASSERT_EQUAL_UINT32(1, zone.calibrationStartupPulses);
    TEST_ASSERT_EQUAL_UINT32(4500, zone.calibrationSteadyDurationMs);
    TEST_ASSERT_EQUAL_UINT32(40, zone.calibrationSteadyPulses);
    TEST_ASSERT_EQUAL_UINT32(0, zone.calibrationStopPulses);
    TEST_ASSERT_EQUAL_UINT32(
        zone.pulseCount,
        zone.calibrationStartupPulses + zone.calibrationSteadyPulses +
            zone.calibrationStopPulses);
}

void test_flow_calibration_allows_one_pulse_window_quantization() {
    FakeWateringHardware hardware;
    WateringController controller(hardware);
    IrrigationConfig config = IrrigationConfigRules::createDefault();
    config.calibrationStability = {3, 3, 10};
    WateringRequest request = requestFor(1, 60);
    request.purpose = WateringPurpose::FlowCalibration;

    TEST_ASSERT_EQUAL(static_cast<int>(WateringStartResult::Started),
                      static_cast<int>(controller.start(request, config, 0)));
    controller.handle(0);
    hardware.pulses = 1;
    controller.handle(1);
    const uint32_t windowPulses[] = {22, 23, 22};
    for (uint32_t window = 0; window < 3; ++window) {
        hardware.pulses += windowPulses[window];
        controller.handle((window + 1U) * 3000U + 1U);
    }

    const WateringStatus status = controller.status();
    const ZoneWateringSummary& zone = status.zones[0];
    TEST_ASSERT_TRUE(zone.calibrationSteadyDetected);
    TEST_ASSERT_EQUAL_UINT32(1, zone.calibrationSteadyStartedMs);
    TEST_ASSERT_EQUAL_UINT32(1, zone.calibrationStartupPulses);
    TEST_ASSERT_EQUAL_UINT32(67, zone.calibrationSteadyPulses);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gravity_watering_completes_and_applies_hold_duty);
    RUN_TEST(test_flow_start_timeout_stops_immediately);
    RUN_TEST(test_running_no_flow_timeout_stops_immediately);
    RUN_TEST(test_user_stop_with_pump_waits_before_closing_valve);
    RUN_TEST(test_stop_delay_still_transitions_valve_to_hold_duty);
    RUN_TEST(test_pump_watering_completes_with_start_and_stop_delays);
    RUN_TEST(test_multiple_zones_run_in_order);
    RUN_TEST(test_stopped_session_keeps_unstarted_zones_explicit);
    RUN_TEST(test_water_estimate_uses_exact_fixed_point_arithmetic);
    RUN_TEST(test_rate_window_uses_fixed_point_arithmetic);
    RUN_TEST(test_live_status_reports_plan_progress_remaining_time_and_water);
    RUN_TEST(test_flow_history_keeps_latest_ten_minutes_and_resets_for_next_zone);
    RUN_TEST(test_persistent_low_flow_alert_can_stop_watering);
    RUN_TEST(test_unlearned_zone_does_not_enable_flow_deviation_protection);
    RUN_TEST(test_zone_flow_learning_completes_after_five_stable_windows);
    RUN_TEST(test_zone_flow_learning_can_be_stopped_manually);
    RUN_TEST(test_zone_flow_learning_stops_at_ten_minute_limit_when_unstable);
    RUN_TEST(test_invalid_request_and_hardware_failure_are_rejected_safely);
    RUN_TEST(test_timers_work_across_millis_wraparound);
    RUN_TEST(test_maintenance_abort_immediately_closes_outputs_and_keeps_result);
    RUN_TEST(test_flow_calibration_limit_counts_from_valve_open);
    RUN_TEST(test_flow_calibration_detects_and_records_steady_phases);
    RUN_TEST(test_flow_calibration_allows_one_pulse_window_quantization);
    return UNITY_END();
}
