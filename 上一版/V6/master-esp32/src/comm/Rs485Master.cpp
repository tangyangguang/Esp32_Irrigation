#include "comm/Rs485Master.h"

#include "BoardPins.h"

#include <Arduino.h>
#include <Esp32Base.h>

namespace Irrigation {
namespace {
constexpr uint32_t REQUEST_TIMEOUT_MS = 200;
constexpr uint32_t REQUEST_GAP_MS = 50;
constexpr uint8_t COMMAND_SEQUENCE[] = {CMD_PING, CMD_GET_INFO, CMD_GET_STATUS, CMD_GET_INPUTS, CMD_GET_LAST_RESULT};
constexpr uint8_t START_TASK_PAYLOAD_LEN = 37;
}

Rs485Master::Rs485Master(Esp32BaseRs485Port& port) : _port(port) {
}

void Rs485Master::begin() {
    _ready = _port.isBegun();
}

void Rs485Master::handle() {
    _ready = _port.isBegun();
    if (!_ready) {
        return;
    }
    pollRx();
    const uint32_t now = millis();
    handleTimeout(now);
    sendNextRequest(now);
}

const StationSnapshot& Rs485Master::station(uint8_t addr) const {
    static const StationSnapshot empty;
    if (addr > 15) {
        return empty;
    }
    return _stations[addr];
}

bool Rs485Master::rs485Ready() const {
    return _ready;
}

void Rs485Master::setOfflineFailureThreshold(uint8_t threshold) {
    if (threshold == 0) {
        threshold = 1;
    }
    _offlineFailureThreshold = threshold;
}

bool Rs485Master::idle() const {
    return _phase == RequestPhase::Idle;
}

bool Rs485Master::startManualTask(const ManualTaskRequest& request, uint16_t taskId) {
    if (!_ready || _phase != RequestPhase::Idle ||
        request.stationAddr < IrrigationBoard::STATION_ADDR_MIN ||
        request.stationAddr > IrrigationBoard::STATION_ADDR_MAX ||
        request.valveNo == 0 || request.valveNo > 8 ||
        request.durationSec == 0) {
        return false;
    }

    uint8_t payload[START_TASK_PAYLOAD_LEN] = {};
    writeLe16(&payload[0], taskId);
    payload[2] = request.sourceId;
    payload[3] = request.zoneId;
    payload[4] = request.valveNo;
    writeLe32(&payload[5], request.durationSec);
    payload[9] = request.mainValveEnabled;
    payload[10] = request.mainValveNo;
    payload[11] = request.pumpEnabled;
    payload[12] = request.mainValveOpenLeadSec;
    payload[13] = request.mainValveCloseDelaySec;
    payload[14] = request.pumpStartDelaySec;
    payload[15] = request.pumpStopLeadSec;
    payload[16] = request.flowMeterEnabled;
    payload[17] = request.flowInputNo;
    payload[18] = request.lowLevelEnabled;
    payload[19] = request.lowLevelInputNo;
    payload[20] = request.lowLevelActiveMode;
    writeLe16(&payload[21], request.lowLevelDebounceMs);
    writeLe16(&payload[23], request.noFlowGraceSec);
    writeLe16(&payload[25], request.noFlowConfirmSec);
    payload[27] = request.lowFlowAction;
    payload[28] = request.overFlowAction;
    writeLe16(&payload[29], request.mainValveFullPowerMs);
    writeLe16(&payload[31], request.mainValveHoldDutyPermille);
    writeLe16(&payload[33], request.branchValveFullPowerMs);
    writeLe16(&payload[35], request.holdDutyPermille);
    sendRequest(request.stationAddr, CMD_START_TASK, payload, START_TASK_PAYLOAD_LEN);
    return true;
}

bool Rs485Master::stopTask(uint8_t addr, uint16_t taskId, uint8_t reason) {
    if (!_ready || _phase != RequestPhase::Idle ||
        addr < IrrigationBoard::STATION_ADDR_MIN ||
        addr > IrrigationBoard::STATION_ADDR_MAX) {
        return false;
    }
    uint8_t payload[3] = {};
    writeLe16(&payload[0], taskId);
    payload[2] = reason;
    sendRequest(addr, CMD_STOP_TASK, payload, sizeof(payload));
    return true;
}

void Rs485Master::pollRx() {
    while (_port.readable()) {
        const int value = _port.readByte();
        if (value < 0) {
            continue;
        }
        Frame frame;
        if (_parser.feed(static_cast<uint8_t>(value), frame)) {
            handleFrame(frame);
        }
    }
}

void Rs485Master::handleFrame(const Frame& frame) {
    if (_phase != RequestPhase::Waiting ||
        frame.addr != _pendingAddr ||
        frame.seq != _pendingSeq ||
        frame.cmd != _pendingCmd ||
        (frame.flags & FLAG_RESPONSE) == 0) {
        return;
    }

    if ((frame.flags & FLAG_ERROR) != 0) {
        markOnline(frame.addr);
        if (frame.addr <= 15 && frame.len > 0) {
            _stations[frame.addr].lastErrorCode = frame.payload[0];
        }
        _phase = RequestPhase::Idle;
        return;
    }

    markOnline(frame.addr);
    if (frame.cmd == CMD_PING) {
        parsePing(frame);
    } else if (frame.cmd == CMD_GET_INFO) {
        parseInfo(frame);
    } else if (frame.cmd == CMD_GET_STATUS) {
        parseStatus(frame);
    } else if (frame.cmd == CMD_GET_INPUTS) {
        parseInputs(frame);
    } else if (frame.cmd == CMD_GET_LAST_RESULT) {
        parseLastResult(frame);
    } else if (frame.cmd == CMD_START_TASK) {
        parseStartAck(frame);
    } else if (frame.cmd == CMD_STOP_TASK) {
        parseStopAck(frame);
    }
    _phase = RequestPhase::Idle;
}

void Rs485Master::sendNextRequest(uint32_t nowMs) {
    if (_phase == RequestPhase::Waiting) {
        return;
    }
    if (_pendingSinceMs != 0 && nowMs - _pendingSinceMs < REQUEST_GAP_MS) {
        return;
    }
    sendRequest(_nextAddr, COMMAND_SEQUENCE[_nextCmdIndex]);
    ++_nextCmdIndex;
    if (_nextCmdIndex >= sizeof(COMMAND_SEQUENCE)) {
        _nextCmdIndex = 0;
        ++_nextAddr;
        if (_nextAddr > IrrigationBoard::STATION_ADDR_MAX) {
            _nextAddr = IrrigationBoard::STATION_ADDR_MIN;
        }
    }
}

void Rs485Master::sendRequest(uint8_t addr, uint8_t cmd, const uint8_t* payload, uint8_t payloadLen) {
    uint8_t frame[FRAME_MAX_SIZE];
    const uint8_t seq = ++_seq;
    const size_t len = encodeFrame(addr, seq, cmd, 0, payload, payloadLen, frame, sizeof(frame));
    if (len == 0) {
        return;
    }
    _port.writeBytes(frame, len);
    _pendingAddr = addr;
    _pendingCmd = cmd;
    _pendingSeq = seq;
    _pendingSinceMs = millis();
    _stations[addr].lastAttemptMs = _pendingSinceMs;
    _phase = RequestPhase::Waiting;
}

void Rs485Master::handleTimeout(uint32_t nowMs) {
    if (_phase != RequestPhase::Waiting) {
        return;
    }
    if (nowMs - _pendingSinceMs < REQUEST_TIMEOUT_MS) {
        return;
    }
    markFailure(_pendingAddr);
    _phase = RequestPhase::Idle;
}

void Rs485Master::markOnline(uint8_t addr) {
    if (addr > 15) {
        return;
    }
    _stations[addr].online = StationOnlineState::Online;
    _stations[addr].lastSeenMs = millis();
    _stations[addr].failureCount = 0;
}

void Rs485Master::markFailure(uint8_t addr) {
    if (addr > 15) {
        return;
    }
    StationSnapshot& s = _stations[addr];
    if (s.failureCount < 255) {
        ++s.failureCount;
    }
    if (s.failureCount >= _offlineFailureThreshold) {
        s.online = StationOnlineState::Offline;
    }
}

void Rs485Master::parsePing(const Frame& frame) {
    if (frame.len >= 4) {
        _stations[frame.addr].uptimeSec = readLe32(frame.payload);
    }
}

void Rs485Master::parseInfo(const Frame& frame) {
    if (frame.len < 9) {
        return;
    }
    StationSnapshot& s = _stations[frame.addr];
    s.protocolVersion = frame.payload[1];
    s.valveCount = frame.payload[2];
    s.inputCount = frame.payload[3];
}

void Rs485Master::parseStatus(const Frame& frame) {
    if (frame.len < 26) {
        return;
    }
    StationSnapshot& s = _stations[frame.addr];
    s.stationState = frame.payload[0];
    s.taskState = frame.payload[1];
    s.faultCode = frame.payload[2];
    s.activeTaskId = readLe16(&frame.payload[3]);
    s.activeZoneId = frame.payload[5];
    s.activeValveNo = frame.payload[6];
    s.elapsedSec = readLe32(&frame.payload[7]);
    s.remainingSec = readLe32(&frame.payload[11]);
    s.flowPulseCount = readLe32(&frame.payload[15]);
    s.flowPulseRate = readLe16(&frame.payload[19]);
    s.inputBits = frame.payload[21];
    s.voltageMv = readLe16(&frame.payload[22]);
    s.lastResultSeq = readLe16(&frame.payload[24]);
}

void Rs485Master::parseInputs(const Frame& frame) {
    if (frame.len < 9) {
        return;
    }
    StationSnapshot& s = _stations[frame.addr];
    s.inputBits = frame.payload[0];
    s.flowPulseCount = readLe32(&frame.payload[1]);
    s.flowPulseRate = readLe16(&frame.payload[5]);
    s.voltageMv = readLe16(&frame.payload[7]);
}

void Rs485Master::parseLastResult(const Frame& frame) {
    if (frame.len < 24) {
        return;
    }
    StationSnapshot& s = _stations[frame.addr];
    s.lastResultSeq = readLe16(&frame.payload[0]);
    s.lastResultTaskId = readLe16(&frame.payload[2]);
    s.lastResultZoneId = frame.payload[4];
    s.lastResultCode = frame.payload[5];
    s.lastResultFaultCode = frame.payload[6];
    s.lastResultPlannedDurationSec = readLe32(&frame.payload[7]);
    s.lastResultActualDurationSec = readLe32(&frame.payload[11]);
    s.lastResultFlowPulseCount = readLe32(&frame.payload[15]);
    s.lastResultStablePulseRate = readLe16(&frame.payload[19]);
}

void Rs485Master::parseStartAck(const Frame& frame) {
    if (frame.len < 2) {
        return;
    }
    StationSnapshot& s = _stations[frame.addr];
    s.activeTaskId = readLe16(frame.payload);
    s.lastErrorCode = 0;
}

void Rs485Master::parseStopAck(const Frame& frame) {
    if (frame.len < 3) {
        return;
    }
    StationSnapshot& s = _stations[frame.addr];
    s.activeTaskId = readLe16(frame.payload);
    s.taskState = frame.payload[2];
    s.lastErrorCode = 0;
}

}  // namespace Irrigation
