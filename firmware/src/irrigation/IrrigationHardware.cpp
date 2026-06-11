#include "IrrigationHardware.h"

#include <Arduino.h>

#include "IrrigationConfig.h"

namespace irrigation {

namespace {

constexpr uint8_t kFirstZoneId = 1;
constexpr uint8_t kOutputOffLevel = LOW;
constexpr uint8_t kOutputOnLevel = HIGH;
constexpr uint8_t kLowLevelActiveLevel = HIGH;

bool validPin(int8_t pin) {
    return pin >= 0;
}

void configureOutputOff(int8_t pin) {
    if (!validPin(pin)) {
        return;
    }
    digitalWrite(pin, kOutputOffLevel);
    pinMode(pin, OUTPUT);
}

void configureInputOnly(int8_t pin) {
    if (!validPin(pin)) {
        return;
    }
    pinMode(pin, INPUT);
}

bool validZoneId(uint8_t zoneId) {
    return zoneId >= kFirstZoneId && zoneId <= kMaxZones;
}

}  // namespace

bool IrrigationHardware::begin(const IrrigationConfig& config) {
    (void)config;
    configureInputOnly(_pinMap.flowInput);
    configureInputOnly(_pinMap.lowLevelInput);

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        configureOutputOff(_pinMap.valveOutputs[i]);
    }
    configureOutputOff(_pinMap.pumpStartOutput);

    _ready = true;
    return _ready;
}

void IrrigationHardware::handle() {
}

void IrrigationHardware::closeAllOutputs() {
    for (uint8_t zoneId = kFirstZoneId; zoneId <= kMaxZones; ++zoneId) {
        setValveOutput(zoneId, false);
    }
    setPumpStartOutput(false);
}

bool IrrigationHardware::ready() const {
    return _ready;
}

const IrrigationPinMap& IrrigationHardware::pinMap() const {
    return _pinMap;
}

int8_t IrrigationHardware::flowInputPin() const {
    return _pinMap.flowInput;
}

int8_t IrrigationHardware::lowLevelInputPin() const {
    return _pinMap.lowLevelInput;
}

int8_t IrrigationHardware::valveOutputPin(uint8_t zoneId) const {
    if (!validZoneId(zoneId)) {
        return -1;
    }
    return _pinMap.valveOutputs[zoneId - kFirstZoneId];
}

int8_t IrrigationHardware::pumpStartOutputPin() const {
    return _pinMap.pumpStartOutput;
}

bool IrrigationHardware::setValveOutput(uint8_t zoneId, bool enabled) {
    const int8_t pin = valveOutputPin(zoneId);
    if (!validPin(pin)) {
        return false;
    }
    digitalWrite(pin, enabled ? kOutputOnLevel : kOutputOffLevel);
    return true;
}

void IrrigationHardware::setPumpStartOutput(bool enabled) {
    if (!validPin(_pinMap.pumpStartOutput)) {
        return;
    }
    digitalWrite(_pinMap.pumpStartOutput, enabled ? kOutputOnLevel : kOutputOffLevel);
}

bool IrrigationHardware::lowLevelActive() const {
    if (!validPin(_pinMap.lowLevelInput)) {
        return false;
    }
    return digitalRead(_pinMap.lowLevelInput) == kLowLevelActiveLevel;
}

}  // namespace irrigation
