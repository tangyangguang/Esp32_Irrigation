#pragma once

#include "WateringController.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// One service-task caller, one control-task owner. No network, filesystem or NVS
// work belongs in the control task. Queries hold the mutex only while copying
// or calculating an in-memory snapshot, never while rendering or publishing it.
class WateringExecutor {
public:
    WateringExecutor();
    bool begin(uint32_t appliedFrequency);
    WateringStartResult start(const WateringRequest&, const IrrigationConfig&, uint32_t);
    bool stop(uint32_t);
    bool abortForMaintenance(uint32_t);
    void safeShutdown();
    bool configureValvePwmFrequency(uint32_t);
    bool parametersPending() const;
    bool hardwareReady() const;
    bool active() const;
    WateringStatus status() const;
    FlowHistorySnapshot flowHistory() const;
    // Immutable once published; only the service task can clear or start again.
    const WateringSessionSummary* finishedSession() const;
    void clearFinishedSession();

private:
    enum class Operation { Start, Stop, Shutdown, Frequency, ClearFinished };
    struct Command {
        Operation operation;
        const WateringRequest* request = nullptr;
        const IrrigationConfig* config = nullptr;
        uint32_t frequency = 0;
        WateringStartResult startResult = WateringStartResult::NotReady;
        bool result = false;
    };
    bool submit(Command&);
    static void run(void*);
    void execute(Command&);
    void applyFrequency();
    void lock() const;
    void unlock() const;
    WateringController controller_;
    TaskHandle_t task_ = nullptr;
    TaskHandle_t caller_ = nullptr;
    StaticQueue_t queueStorage_{};
    uint8_t queueBytes_[sizeof(Command*)]{};
    QueueHandle_t queue_ = nullptr;
    StaticSemaphore_t mutexStorage_{};
    StaticSemaphore_t doneStorage_{};
    SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t done_ = nullptr;
    uint32_t targetFrequency_ = 0;
    uint32_t appliedFrequency_ = 0;
    bool hardwareReady_ = false;
};
