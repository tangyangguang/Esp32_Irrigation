#ifndef IRRIGATION_RUNTIME_CONTROLLER_H
#define IRRIGATION_RUNTIME_CONTROLLER_H

#include "IrrigationTypes.h"
#include "comm/Rs485Master.h"
#include "storage/ConfigStore.h"
#include "storage/HistoryStore.h"

namespace Irrigation {

class RuntimeController {
public:
    RuntimeController(Rs485Master& master, ConfigStore& config, HistoryStore& history);

    void handle();
    bool startZone(uint8_t sourceId, uint8_t zoneId, uint32_t durationSec);
    bool startManual(const ManualTaskRequest& request);
    bool stop(uint8_t stationAddr);
    bool stopSource(uint8_t sourceId);
    const char* lastMessage() const;
    uint16_t nextTaskId() const;

private:
    struct StartedTask {
        uint16_t taskId = 0;
        uint8_t stationAddr = 0;
        uint8_t sourceId = 0;
        uint8_t zoneId = 0;
        uint32_t plannedSec = 0;
    };

    bool allocateTaskId(uint16_t& taskId);
    bool requestValid(const ManualTaskRequest& request) const;
    void rememberStartedTask(uint16_t taskId, const ManualTaskRequest& request);
    bool takeStartedTask(uint8_t stationAddr, uint16_t taskId, StartedTask& out);
    void recordFinishedTasks();

    Rs485Master& _master;
    ConfigStore& _config;
    HistoryStore& _history;
    StartedTask _startedTasks[8];
    uint16_t _seenResultSeq[16] = {};
    uint16_t _nextTaskId = 1;
    char _lastMessage[96] = "idle";
};

}  // namespace Irrigation

#endif
