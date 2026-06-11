#include "IrrigationRuntime.h"

#include <Arduino.h>

#include "EventService.h"
#include "FlowMeterService.h"
#include "IrrigationConfig.h"
#include "IrrigationHardware.h"
#include "RecordsStore.h"

namespace irrigation {

namespace {

constexpr uint32_t kMsPerMinute = 60UL * 1000UL;

void setStartResult(IrrigationStartResult* out, IrrigationStartResult value) {
    if (out) {
        *out = value;
    }
}

const char* stopReasonText(IrrigationStopReason reason) {
    switch (reason) {
        case IrrigationStopReason::Completed:
            return "completed";
        case IrrigationStopReason::User:
            return "user_stop";
        case IrrigationStopReason::Fault:
            return "fault_stop";
        default:
            return "unknown";
    }
}

}  // namespace

bool IrrigationRuntime::begin(IrrigationConfig& config,
                              IrrigationHardware& hardware,
                              FlowMeterService& flow,
                              RecordsStore& records,
                              EventService& events) {
    _config = &config;
    _hardware = &hardware;
    _flow = &flow;
    _records = &records;
    _events = &events;
    clearActiveRun();
    _hardware->closeAllOutputs();
    _ready = true;
    return _ready;
}

void IrrigationRuntime::handle() {
    if (!_ready || _state == IrrigationRunState::Idle) {
        return;
    }

    const uint32_t now = millis();
    if (static_cast<int32_t>(now - _status.deadlineMs) >= 0) {
        stopCurrent(IrrigationStopReason::Completed);
    }
}

IrrigationRunState IrrigationRuntime::state() const {
    return _state;
}

IrrigationRuntimeStatus IrrigationRuntime::status() const {
    return _status;
}

bool IrrigationRuntime::ready() const {
    return _ready;
}

bool IrrigationRuntime::startManualZone(uint8_t zoneId, uint16_t durationMin, IrrigationStartResult* result) {
    const ZoneConfig* zone = zoneConfig(zoneId);
    if (zone && durationMin == 0) {
        durationMin = zone->defaultManualDurationMin;
    }

    IrrigationStartResult localResult = IrrigationStartResult::Started;
    if (!canStartZone(zoneId, durationMin, localResult)) {
        setStartResult(result, localResult);
        return false;
    }

    _hardware->closeAllOutputs();
    _flow->reset();

    _state = IrrigationRunState::Starting;
    if (!_hardware->setValveOutput(zoneId, true)) {
        _hardware->closeAllOutputs();
        clearActiveRun();
        setStartResult(result, IrrigationStartResult::HardwareError);
        return false;
    }

    const IrrigationConfigSnapshot& snapshot = _config->snapshot();
    if (snapshot.system.pumpStartEnabled) {
        _hardware->setPumpStartOutput(true);
    }

    const uint32_t now = millis();
    _status.state = IrrigationRunState::Running;
    _status.activeZoneId = zoneId;
    _status.durationMin = durationMin;
    _status.startedAtMs = now;
    _status.deadlineMs = now + static_cast<uint32_t>(durationMin) * kMsPerMinute;
    _state = IrrigationRunState::Running;

    if (_events) {
        _events->append(IrrigationEventLevel::Info, "runtime", "manual_start", "zone", nullptr, 0, zoneId, durationMin, 0, 0x03);
    }

    setStartResult(result, IrrigationStartResult::Started);
    return true;
}

bool IrrigationRuntime::stopCurrent(IrrigationStopReason reason) {
    if (!_ready || _state == IrrigationRunState::Idle) {
        return false;
    }

    _state = (reason == IrrigationStopReason::Fault) ? IrrigationRunState::FaultStopping : IrrigationRunState::Stopping;
    const uint8_t stoppedZoneId = _status.activeZoneId;
    _hardware->closeAllOutputs();

    if (_events) {
        _events->append(IrrigationEventLevel::Info, "runtime", stopReasonText(reason), "zone", nullptr, 0, stoppedZoneId, 0, 0, 0x01);
    }

    clearActiveRun();
    return true;
}

const char* IrrigationRuntime::runStateKey(IrrigationRunState state) {
    switch (state) {
        case IrrigationRunState::Idle:
            return "idle";
        case IrrigationRunState::Starting:
            return "starting";
        case IrrigationRunState::WaitingForFirstPulse:
            return "waiting_for_first_pulse";
        case IrrigationRunState::FlowStabilizing:
            return "flow_stabilizing";
        case IrrigationRunState::Running:
            return "running";
        case IrrigationRunState::Stopping:
            return "stopping";
        case IrrigationRunState::FaultStopping:
            return "fault_stopping";
        default:
            return "unknown";
    }
}

const char* IrrigationRuntime::startResultReason(IrrigationStartResult result) {
    switch (result) {
        case IrrigationStartResult::Started:
            return "started";
        case IrrigationStartResult::NotReady:
            return "not_ready";
        case IrrigationStartResult::Busy:
            return "busy";
        case IrrigationStartResult::InvalidZone:
            return "invalid_zone";
        case IrrigationStartResult::ZoneDisabled:
            return "zone_disabled";
        case IrrigationStartResult::InvalidDuration:
            return "invalid_duration";
        case IrrigationStartResult::FlowNotCalibrated:
            return "flow_not_calibrated";
        case IrrigationStartResult::LowLevelActive:
            return "low_level_active";
        case IrrigationStartResult::HardwareError:
            return "hardware_error";
        default:
            return "unknown";
    }
}

bool IrrigationRuntime::canStartZone(uint8_t zoneId, uint16_t durationMin, IrrigationStartResult& result) const {
    if (!_ready || !_config || !_hardware || !_flow) {
        result = IrrigationStartResult::NotReady;
        return false;
    }
    if (_state != IrrigationRunState::Idle) {
        result = IrrigationStartResult::Busy;
        return false;
    }
    const ZoneConfig* zone = zoneConfig(zoneId);
    if (!zone) {
        result = IrrigationStartResult::InvalidZone;
        return false;
    }
    if (!zone->enabled) {
        result = IrrigationStartResult::ZoneDisabled;
        return false;
    }
    if (durationMin < kMinRunDurationMin || durationMin > kMaxRunDurationMin) {
        result = IrrigationStartResult::InvalidDuration;
        return false;
    }

    const IrrigationConfigSnapshot& snapshot = _config->snapshot();
    if (snapshot.flowValve.pulsesPerLiter == 0) {
        result = IrrigationStartResult::FlowNotCalibrated;
        return false;
    }
    if (snapshot.system.lowLevelEnabled && _hardware->lowLevelActive()) {
        result = IrrigationStartResult::LowLevelActive;
        return false;
    }

    result = IrrigationStartResult::Started;
    return true;
}

bool IrrigationRuntime::validZoneId(uint8_t zoneId) const {
    return zoneId >= 1 && zoneId <= kMaxZones;
}

const ZoneConfig* IrrigationRuntime::zoneConfig(uint8_t zoneId) const {
    if (!_config || !validZoneId(zoneId)) {
        return nullptr;
    }
    return &_config->snapshot().zones[zoneId - 1];
}

void IrrigationRuntime::clearActiveRun() {
    _status = {};
    _status.state = IrrigationRunState::Idle;
    _state = IrrigationRunState::Idle;
}

}  // namespace irrigation
