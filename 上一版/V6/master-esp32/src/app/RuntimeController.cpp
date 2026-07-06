#include "app/RuntimeController.h"

#include "BoardPins.h"

#include <stdio.h>

namespace Irrigation {
namespace {
constexpr uint8_t TASK_RESULT_COMPLETED = 1;
constexpr uint8_t TASK_RESULT_STOPPED_BY_USER = 2;
constexpr uint8_t TASK_RESULT_STOPPED_BY_FAULT = 3;
}

RuntimeController::RuntimeController(Rs485Master& master, ConfigStore& config, HistoryStore& history)
    : _master(master), _config(config), _history(history) {
}

void RuntimeController::handle() {
    recordFinishedTasks();
}

bool RuntimeController::startZone(uint8_t sourceId, uint8_t zoneId, uint32_t durationSec) {
    if (!_config.sourceValid(sourceId) || !_config.zoneValid(sourceId, zoneId)) {
        snprintf(_lastMessage, sizeof(_lastMessage), "start rejected: invalid source or zone");
        _history.add(HISTORY_OPERATION, sourceId, zoneId, 1, 0, durationSec, 0, _lastMessage);
        return false;
    }

    const SystemConfig& system = _config.system();
    const SourceConfig& source = _config.source(sourceId);
    const ZoneConfig& zone = _config.zone(sourceId, zoneId);
    const uint32_t maxDurationSec = static_cast<uint32_t>(system.maxRunMinutes) * 60UL;
    if (!source.enabled) {
        snprintf(_lastMessage, sizeof(_lastMessage), "start rejected: source disabled");
        _history.add(HISTORY_OPERATION, sourceId, zoneId, 2, 0, durationSec, 0, _lastMessage);
        return false;
    }
    if (!zone.enabled) {
        snprintf(_lastMessage, sizeof(_lastMessage), "start rejected: zone disabled");
        _history.add(HISTORY_OPERATION, sourceId, zoneId, 3, 0, durationSec, 0, _lastMessage);
        return false;
    }
    if (durationSec == 0 || durationSec > maxDurationSec) {
        snprintf(_lastMessage, sizeof(_lastMessage), "start rejected: duration out of range");
        _history.add(HISTORY_OPERATION, sourceId, zoneId, 4, 0, durationSec, 0, _lastMessage);
        return false;
    }

    ManualTaskRequest request;
    request.sourceId = source.sourceId;
    request.zoneId = zone.zoneId;
    request.stationAddr = source.stationAddr;
    request.valveNo = zone.valveNo;
    request.durationSec = durationSec;
    request.mainValveEnabled = source.mainValveEnabled;
    request.mainValveNo = source.mainValveNo;
    request.pumpEnabled = source.pumpEnabled;
    request.mainValveOpenLeadSec = system.mainValveOpenLeadSec;
    request.mainValveCloseDelaySec = system.mainValveCloseDelaySec;
    request.pumpStartDelaySec = system.pumpStartDelaySec;
    request.pumpStopLeadSec = system.pumpStopLeadSec;
    request.flowMeterEnabled = source.flowMeterEnabled;
    request.flowInputNo = source.flowInputNo;
    request.lowLevelEnabled = source.lowLevelEnabled;
    request.lowLevelInputNo = source.lowLevelInputNo;
    request.lowLevelActiveMode = source.lowLevelActiveMode;
    request.lowLevelDebounceMs = source.lowLevelDebounceMs;
    request.mainValveFullPowerMs = source.mainValveFullPowerMs;
    request.mainValveHoldDutyPermille = source.mainValveHoldDutyPermille;
    request.branchValveFullPowerMs = source.branchValveFullPowerMs;
    request.holdDutyPermille = source.branchValveHoldDutyPermille;

    uint16_t taskId = 0;
    allocateTaskId(taskId);
    if (!requestValid(request) || !_master.startManualTask(request, taskId)) {
        snprintf(_lastMessage, sizeof(_lastMessage), "start rejected: bus busy or invalid request");
        _history.add(HISTORY_OPERATION, sourceId, zoneId, 5, taskId, durationSec, 0, _lastMessage);
        return false;
    }
    snprintf(_lastMessage, sizeof(_lastMessage), "start sent: source=%u zone=%u task=%u",
             sourceId, zoneId, taskId);
    rememberStartedTask(taskId, request);
    _history.add(HISTORY_OPERATION, sourceId, zoneId, 0, taskId, durationSec, 0, _lastMessage);
    return true;
}

bool RuntimeController::startManual(const ManualTaskRequest& request) {
    uint16_t taskId = 0;
    allocateTaskId(taskId);
    if (!requestValid(request) || !_master.startManualTask(request, taskId)) {
        snprintf(_lastMessage, sizeof(_lastMessage), "start rejected: bus busy or invalid request");
        _history.add(HISTORY_OPERATION, 0, 0, 5, taskId, request.durationSec, 0, _lastMessage);
        return false;
    }
    snprintf(_lastMessage, sizeof(_lastMessage), "start sent: station=%u valve=%u task=%u",
             request.stationAddr, request.valveNo, taskId);
    rememberStartedTask(taskId, request);
    _history.add(HISTORY_OPERATION, 0, 0, 0, taskId, request.durationSec, 0, _lastMessage);
    return true;
}

bool RuntimeController::stop(uint8_t stationAddr) {
    if (!_master.stopTask(stationAddr, 0, 1)) {
        snprintf(_lastMessage, sizeof(_lastMessage), "stop rejected: bus busy or invalid station");
        _history.add(HISTORY_OPERATION, 0, 0, 6, 0, 0, 0, _lastMessage);
        return false;
    }
    snprintf(_lastMessage, sizeof(_lastMessage), "stop sent: station=%u", stationAddr);
    _history.add(HISTORY_OPERATION, 0, 0, 0, 0, 0, 0, _lastMessage);
    return true;
}

bool RuntimeController::stopSource(uint8_t sourceId) {
    if (!_config.sourceValid(sourceId)) {
        snprintf(_lastMessage, sizeof(_lastMessage), "stop rejected: invalid source");
        _history.add(HISTORY_OPERATION, sourceId, 0, 1, 0, 0, 0, _lastMessage);
        return false;
    }
    const SourceConfig& source = _config.source(sourceId);
    if (!_master.stopTask(source.stationAddr, 0, 1)) {
        snprintf(_lastMessage, sizeof(_lastMessage), "stop rejected: bus busy or invalid source");
        _history.add(HISTORY_OPERATION, sourceId, 0, 6, 0, 0, 0, _lastMessage);
        return false;
    }
    snprintf(_lastMessage, sizeof(_lastMessage), "stop sent: source=%u station=%u",
             sourceId, source.stationAddr);
    _history.add(HISTORY_OPERATION, sourceId, 0, 0, 0, 0, 0, _lastMessage);
    return true;
}

const char* RuntimeController::lastMessage() const {
    return _lastMessage;
}

uint16_t RuntimeController::nextTaskId() const {
    return _nextTaskId;
}

bool RuntimeController::allocateTaskId(uint16_t& taskId) {
    taskId = _nextTaskId++;
    if (_nextTaskId == 0) {
        _nextTaskId = 1;
    }
    return true;
}

bool RuntimeController::requestValid(const ManualTaskRequest& request) const {
    if (request.stationAddr < IrrigationBoard::STATION_ADDR_MIN ||
        request.stationAddr > IrrigationBoard::STATION_ADDR_MAX ||
        request.valveNo == 0 || request.valveNo > 8 ||
        request.durationSec == 0 ||
        request.holdDutyPermille > 1000 ||
        request.mainValveHoldDutyPermille > 1000 ||
        request.branchValveFullPowerMs == 0 ||
        request.branchValveFullPowerMs > 10000 ||
        request.mainValveFullPowerMs > 10000) {
        return false;
    }
    if (request.mainValveEnabled && (request.mainValveNo == 0 || request.mainValveNo > 8)) {
        return false;
    }
    if (request.flowMeterEnabled && (request.flowInputNo == 0 || request.flowInputNo > 4)) {
        return false;
    }
    if (request.lowLevelEnabled && (request.lowLevelInputNo == 0 || request.lowLevelInputNo > 4)) {
        return false;
    }
    return true;
}

void RuntimeController::rememberStartedTask(uint16_t taskId, const ManualTaskRequest& request) {
    uint8_t slot = 0;
    for (uint8_t i = 0; i < static_cast<uint8_t>(sizeof(_startedTasks) / sizeof(_startedTasks[0])); ++i) {
        if (_startedTasks[i].taskId == 0) {
            slot = i;
            break;
        }
        if (_startedTasks[i].taskId < _startedTasks[slot].taskId) {
            slot = i;
        }
    }
    _startedTasks[slot].taskId = taskId;
    _startedTasks[slot].stationAddr = request.stationAddr;
    _startedTasks[slot].sourceId = request.sourceId;
    _startedTasks[slot].zoneId = request.zoneId;
    _startedTasks[slot].plannedSec = request.durationSec;
}

bool RuntimeController::takeStartedTask(uint8_t stationAddr, uint16_t taskId, StartedTask& out) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(sizeof(_startedTasks) / sizeof(_startedTasks[0])); ++i) {
        if (_startedTasks[i].taskId == taskId && _startedTasks[i].stationAddr == stationAddr) {
            out = _startedTasks[i];
            _startedTasks[i] = StartedTask();
            return true;
        }
    }
    return false;
}

void RuntimeController::recordFinishedTasks() {
    for (uint8_t addr = IrrigationBoard::STATION_ADDR_MIN; addr <= IrrigationBoard::STATION_ADDR_MAX; ++addr) {
        const StationSnapshot& station = _master.station(addr);
        if (station.lastResultSeq == 0 || station.lastResultSeq == _seenResultSeq[addr]) {
            continue;
        }
        _seenResultSeq[addr] = station.lastResultSeq;

        StartedTask task;
        if (!takeStartedTask(addr, station.lastResultTaskId, task)) {
            continue;
        }

        uint8_t result = station.lastResultCode;
        const char* message = "watering result";
        if (result == TASK_RESULT_COMPLETED) {
            message = "watering completed";
        } else if (result == TASK_RESULT_STOPPED_BY_USER) {
            message = "watering stopped by user";
        } else if (result == TASK_RESULT_STOPPED_BY_FAULT) {
            message = "watering stopped by fault";
        }
        _history.add(HISTORY_WATERING,
                     task.sourceId,
                     task.zoneId,
                     result,
                     station.lastResultTaskId,
                     task.plannedSec,
                     station.lastResultActualDurationSec,
                     message);
    }
}

}  // namespace Irrigation
