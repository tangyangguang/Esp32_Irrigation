#include "WateringExecutor.h"
#include "BoardHardware.h"
#include "IrrigationConfig.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>

std::atomic<uint8_t> duty{0};
std::atomic<uint32_t> frequency{20000};
std::atomic<bool> held{false};
void owner() { assert(xTaskGetCurrentTaskHandle() == reinterpret_cast<void*>(2)); }
BoardHardware& BoardHardware::instance() { static BoardHardware board; return board; }
bool BoardHardware::initialized() const { return true; }
bool BoardHardware::configureValvePwmFrequency(uint32_t f) { owner(); if (f > 25000) return false; frequency = f; return true; }
bool BoardHardware::openValve(uint8_t, uint8_t value) { owner(); duty = value; return true; }
bool BoardHardware::setActiveValveDuty(uint8_t value) { owner(); duty = value; held = true; return true; }
void BoardHardware::closeValves() { owner(); duty = 0; }
void BoardHardware::safeShutdown() { owner(); duty = 0; }
bool BoardHardware::setPumpSignal(bool) { owner(); return true; }
uint32_t BoardHardware::flowPulseCount() const { return 0; }

int main() {
    // Production executor and controller; only RTOS scheduling/GPIO are hosted.
    auto* executor = new WateringExecutor;
    auto config = IrrigationConfigRules::createDefault();
    config.valveDrive.pullInTimeMs = 100;
    config.flowProtection.flowStartTimeoutSec = 1;
    WateringRequest request{};
    request.stepCount = 1;
    request.steps[0].zoneId = 1;
    request.steps[0].targetDurationSec = 5;
    assert(executor->begin(20000));
    assert(executor->start(request, config, 0) == WateringStartResult::Started);
    assert(duty == 100);
    assert(executor->configureValvePwmFrequency(21000));
    assert(executor->configureValvePwmFrequency(22000));
    assert(executor->parametersPending());
    assert(frequency == 20000);
    // The service caller is unavailable for longer than the flow timeout.
    std::this_thread::sleep_for(std::chrono::milliseconds(1150));
    assert(held && duty == 0 && !executor->active());
    assert(frequency == 22000 && !executor->parametersPending());
    const auto* completed = executor->finishedSession();
    assert(completed && completed->result == WateringResult::Failed);
    assert(executor->start(request, config, 0) == WateringStartResult::PreviousResultPending);
    assert(executor->finishedSession() == completed);
    executor->clearFinishedSession();
    assert(executor->start(request, config, 0) == WateringStartResult::Started);
    assert(executor->stop(0));
    assert(!executor->active() && duty == 0);
    assert(executor->stop(0)); // Completion racing with a stop is still success.
    executor->clearFinishedSession();
    assert(executor->start(request, config, 0) == WateringStartResult::Started);
    executor->safeShutdown();
    assert(!executor->active() && duty == 0 && executor->finishedSession());
    assert(!executor->configureValvePwmFrequency(25001));
    assert(!executor->hardwareReady());
    assert(executor->start(request, config, 0) == WateringStartResult::NotReady);
    std::puts("executor: service stall, deferred frequency, result retention, stop, maintenance and hardware failure passed");
    std::fflush(stdout);
    std::_Exit(0); // Firmware-lifetime detached task intentionally lives forever.
}
