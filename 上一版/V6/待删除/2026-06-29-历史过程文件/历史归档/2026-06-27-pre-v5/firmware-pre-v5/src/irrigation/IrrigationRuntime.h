#pragma once

#include <stdint.h>

namespace irrigation {

class EventService;
class FlowMeterService;
class IrrigationConfig;
class IrrigationHardware;
class RecordsStore;
struct ZoneConfig;

enum class IrrigationRunState : uint8_t {
    Idle = 0,
    Starting,
    WaitingForFirstPulse,
    FlowStabilizing,
    Running,
    Stopping,
    FaultStopping,
};

enum class IrrigationStartResult : uint8_t {
    Started = 0,
    NotReady,
    Busy,
    InvalidZone,
    ZoneDisabled,
    InvalidDuration,
    FlowNotCalibrated,
    LowLevelActive,
    HardwareError,
};

enum class IrrigationStopReason : uint8_t {
    Completed = 0,
    User,
    Fault,
};

struct IrrigationRuntimeStatus {
    IrrigationRunState state;
    uint8_t activeZoneId;
    uint16_t durationMin;
    uint32_t startedAtMs;
    uint32_t deadlineMs;
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
    IrrigationRuntimeStatus status() const;
    bool ready() const;

    bool startManualZone(uint8_t zoneId, uint16_t durationMin, IrrigationStartResult* result = nullptr);
    bool stopCurrent(IrrigationStopReason reason);

    static const char* runStateKey(IrrigationRunState state);
    static const char* startResultReason(IrrigationStartResult result);

private:
    bool canStartZone(uint8_t zoneId, uint16_t durationMin, IrrigationStartResult& result) const;
    bool validZoneId(uint8_t zoneId) const;
    const ZoneConfig* zoneConfig(uint8_t zoneId) const;
    void clearActiveRun();

    IrrigationConfig* _config = nullptr;
    IrrigationHardware* _hardware = nullptr;
    FlowMeterService* _flow = nullptr;
    RecordsStore* _records = nullptr;
    EventService* _events = nullptr;
    IrrigationRunState _state = IrrigationRunState::Idle;
    IrrigationRuntimeStatus _status = {};
    bool _ready = false;
};

}  // namespace irrigation
