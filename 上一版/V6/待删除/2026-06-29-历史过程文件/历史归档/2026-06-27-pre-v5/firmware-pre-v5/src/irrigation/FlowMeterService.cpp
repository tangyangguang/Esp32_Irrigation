#include "FlowMeterService.h"

#include <Arduino.h>

namespace irrigation {

bool FlowMeterService::begin(int8_t inputPin) {
    _inputPin = inputPin;
    _snapshot = {};
    _snapshot.inputPin = _inputPin;
    _pulseCount = 0;
    _lastPulseMs = 0;
    _lastObservedPulseCount = 0;

    if (_inputPin >= 0) {
        attachInterruptArg(digitalPinToInterrupt(_inputPin), FlowMeterService::onPulse, this, RISING);
    }

    _ready = true;
    return _ready;
}

void FlowMeterService::handle() {
    const uint32_t currentPulseCount = snapshot().pulseCount;
    if (currentPulseCount != _lastObservedPulseCount) {
        _lastObservedPulseCount = currentPulseCount;
        _lastPulseMs = millis();
    }
    _snapshot = snapshot();
}

void FlowMeterService::reset() {
    noInterrupts();
    _pulseCount = 0;
    interrupts();

    _lastPulseMs = 0;
    _lastObservedPulseCount = 0;
    _snapshot = {};
    _snapshot.inputPin = _inputPin;
}

FlowMeterSnapshot FlowMeterService::snapshot() const {
    FlowMeterSnapshot value = {};
    noInterrupts();
    value.pulseCount = _pulseCount;
    value.lastPulseMs = _lastPulseMs;
    interrupts();

    value.inputPin = _inputPin;
    value.hasPulse = value.pulseCount > 0;
    return value;
}

void FlowMeterService::onPulse(void* arg) {
    if (!arg) {
        return;
    }
    static_cast<FlowMeterService*>(arg)->recordPulse();
}

void FlowMeterService::recordPulse() {
    ++_pulseCount;
}

}  // namespace irrigation
