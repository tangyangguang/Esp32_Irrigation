#ifndef IRRIGATION_RS485_MASTER_H
#define IRRIGATION_RS485_MASTER_H

#include "IrrigationTypes.h"
#include "comm/Rs485Protocol.h"

class Esp32BaseRs485Port;

namespace Irrigation {

class Rs485Master {
public:
    explicit Rs485Master(Esp32BaseRs485Port& port);

    void begin();
    void handle();
    const StationSnapshot& station(uint8_t addr) const;
    bool rs485Ready() const;
    void setOfflineFailureThreshold(uint8_t threshold);
    bool startManualTask(const ManualTaskRequest& request, uint16_t taskId);
    bool stopTask(uint8_t addr, uint16_t taskId, uint8_t reason);
    bool idle() const;

private:
    enum class RequestPhase : uint8_t {
        Idle,
        Waiting,
    };

    void pollRx();
    void handleFrame(const Frame& frame);
    void sendNextRequest(uint32_t nowMs);
    void sendRequest(uint8_t addr, uint8_t cmd, const uint8_t* payload = nullptr, uint8_t payloadLen = 0);
    void handleTimeout(uint32_t nowMs);
    void markOnline(uint8_t addr);
    void markFailure(uint8_t addr);
    void parsePing(const Frame& frame);
    void parseInfo(const Frame& frame);
    void parseStatus(const Frame& frame);
    void parseInputs(const Frame& frame);
    void parseLastResult(const Frame& frame);
    void parseStartAck(const Frame& frame);
    void parseStopAck(const Frame& frame);

    Esp32BaseRs485Port& _port;
    FrameParser _parser;
    StationSnapshot _stations[16] = {};
    RequestPhase _phase = RequestPhase::Idle;
    uint8_t _nextAddr = 1;
    uint8_t _nextCmdIndex = 0;
    uint8_t _seq = 0;
    uint8_t _pendingAddr = 0;
    uint8_t _pendingCmd = 0;
    uint8_t _pendingSeq = 0;
    uint32_t _pendingSinceMs = 0;
    uint8_t _offlineFailureThreshold = 3;
    bool _ready = false;
};

}  // namespace Irrigation

#endif
