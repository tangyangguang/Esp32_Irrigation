#include "WateringExecutor.h"
#include "BoardHardware.h"
#include <Arduino.h>
#include <esp_task_wdt.h>

WateringExecutor::WateringExecutor() : controller_(BoardHardware::instance()) {}

bool WateringExecutor::begin(uint32_t appliedFrequency) {
    if (task_) return true;
    caller_ = xTaskGetCurrentTaskHandle();
    mutex_ = xSemaphoreCreateMutexStatic(&mutexStorage_);
    done_ = xSemaphoreCreateBinaryStatic(&doneStorage_);
    queue_ = xQueueCreateStatic(1, sizeof(Command*), queueBytes_, &queueStorage_);
    targetFrequency_ = appliedFrequency_ = appliedFrequency;
    hardwareReady_ = BoardHardware::instance().initialized();
    // ESP-IDF task stack sizes are bytes. This task uses only controller/GPIO
    // operations. It must never acquire locks owned by the service task.
    if (!hardwareReady_ || xTaskCreatePinnedToCore(
        run, "watering", 4096, this, 3, &task_, 1) != pdPASS) return false;
    xSemaphoreTake(done_, portMAX_DELAY);
    return hardwareReady();
}

void WateringExecutor::lock() const { if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY); }
void WateringExecutor::unlock() const { if (mutex_) xSemaphoreGive(mutex_); }

bool WateringExecutor::submit(Command& command) {
    if (!task_ || xTaskGetCurrentTaskHandle() != caller_) return false;
    Command* pointer = &command;
    if (xQueueSend(queue_, &pointer, 0) != pdTRUE) return false;
    // Keep borrowed command inputs alive until the owner replies. Timing out
    // here could report failure while a queued start executes later. The owner
    // never waits for service work and is independently watched by the TWDT.
    return xSemaphoreTake(done_, portMAX_DELAY) == pdTRUE;
}

void WateringExecutor::run(void* context) {
    auto& self = *static_cast<WateringExecutor*>(context);
    const bool watched = esp_task_wdt_add(nullptr) == ESP_OK;
    if (!watched) {
        self.lock();
        self.hardwareReady_ = false;
        BoardHardware::instance().safeShutdown();
        self.unlock();
    }
    xSemaphoreGive(self.done_);
    for (;;) {
        Command* command = nullptr;
        // A single service caller waits for each reply, so a stop cannot be
        // displaced by a backlog of ordinary commands.
        const bool received = xQueueReceive(self.queue_, &command,
            pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1) == pdTRUE;
        self.lock();
        if (received) self.execute(*command);
        if (watched && self.hardwareReady_) self.controller_.handle(millis());
        self.applyFrequency();
        self.unlock();
        if (received) xSemaphoreGive(self.done_);
        if (watched) esp_task_wdt_reset();
    }
}

void WateringExecutor::execute(Command& command) {
    const uint32_t now = millis();
    switch (command.operation) {
        case Operation::Start:
            if (hardwareReady_) command.startResult = controller_.start(*command.request, *command.config, now);
            break;
        case Operation::Stop:
            // Execution may finish between the service's observation and this
            // command. An already stopped controller satisfies a stop request.
            command.result = !controller_.active() || controller_.stop(now);
            break;
        case Operation::Shutdown:
            BoardHardware::instance().safeShutdown();
            controller_.abortForMaintenance(now);
            command.result = true;
            break;
        case Operation::Frequency:
            targetFrequency_ = command.frequency;
            applyFrequency();
            command.result = hardwareReady_;
            break;
        case Operation::ClearFinished:
            controller_.clearFinishedSession();
            command.result = true;
            break;
    }
}

void WateringExecutor::applyFrequency() {
    if (!hardwareReady_ || controller_.active() || targetFrequency_ == appliedFrequency_) return;
    hardwareReady_ = BoardHardware::instance().configureValvePwmFrequency(targetFrequency_);
    if (hardwareReady_) appliedFrequency_ = targetFrequency_;
}

WateringStartResult WateringExecutor::start(const WateringRequest& request,
    const IrrigationConfig& config, uint32_t) {
    Command command{Operation::Start}; command.request = &request; command.config = &config;
    return submit(command) ? command.startResult : WateringStartResult::NotReady;
}
bool WateringExecutor::stop(uint32_t) {
    Command command{Operation::Stop}; return submit(command) && command.result;
}
bool WateringExecutor::abortForMaintenance(uint32_t) {
    Command command{Operation::Shutdown}; return submit(command) && command.result;
}
void WateringExecutor::safeShutdown() {
    if (task_) abortForMaintenance(0);
    else BoardHardware::instance().safeShutdown();
}
bool WateringExecutor::configureValvePwmFrequency(uint32_t frequency) {
    Command command{Operation::Frequency}; command.frequency = frequency;
    return submit(command) && command.result;
}
void WateringExecutor::clearFinishedSession() { Command command{Operation::ClearFinished}; submit(command); }
bool WateringExecutor::parametersPending() const {
    lock(); const bool value = targetFrequency_ != appliedFrequency_; unlock(); return value;
}
bool WateringExecutor::hardwareReady() const {
    lock(); const bool value = hardwareReady_; unlock(); return value;
}
bool WateringExecutor::active() const {
    lock(); const bool value = controller_.active(); unlock(); return value;
}
WateringStatus WateringExecutor::status() const {
    lock(); const auto value = controller_.status(); unlock(); return value;
}
FlowHistorySnapshot WateringExecutor::flowHistory() const {
    lock(); const auto value = controller_.flowHistory(); unlock(); return value;
}
const WateringSessionSummary* WateringExecutor::finishedSession() const {
    lock(); const auto* value = controller_.finishedSession(); unlock(); return value;
}
