#pragma once

#include <stdint.h>

namespace irrigation {

class EventService;
class FlowMeterService;
class IrrigationConfig;
class IrrigationHardware;
class RecordsStore;

enum class IrrigationRunState : uint8_t {
    Idle = 0,
    Starting,
    WaitingForFirstPulse,
    FlowStabilizing,
    Running,
    Stopping,
    FaultStopping,
};

class IrrigationRuntime {
public:
    // Sole business owner of valve and pump output control. Web, API, schedule
    // and local UI must submit requests here instead of touching hardware.
    bool begin(IrrigationConfig& config,
               IrrigationHardware& hardware,
               FlowMeterService& flow,
               RecordsStore& records,
               EventService& events);
    void handle();

    IrrigationRunState state() const;
    bool ready() const;

private:
    IrrigationConfig* _config = nullptr;
    IrrigationHardware* _hardware = nullptr;
    FlowMeterService* _flow = nullptr;
    RecordsStore* _records = nullptr;
    EventService* _events = nullptr;
    IrrigationRunState _state = IrrigationRunState::Idle;
    bool _ready = false;
};

}  // namespace irrigation
