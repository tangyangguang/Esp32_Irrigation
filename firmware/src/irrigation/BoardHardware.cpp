#include "BoardHardware.h"

#include <Arduino.h>

#include "BoardPins.h"

namespace {

constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint8_t kPwmChannelCount = BoardPins::kZoneCount;

}  // namespace

volatile uint32_t BoardHardware::flowPulseCount_ = 0;

BoardHardware& BoardHardware::instance() {
    static BoardHardware hardware;
    return hardware;
}

bool BoardHardware::begin(uint32_t pwmFrequencyHz) {
    // Preload safe output levels before switching the pins to OUTPUT.
    digitalWrite(BoardPins::kValveDriverShutdownPin, HIGH);
    pinMode(BoardPins::kValveDriverShutdownPin, OUTPUT);

    // The pump PhotoMOS input is active-low on the finalized PCB.
    digitalWrite(BoardPins::kPumpSignalPin, HIGH);
    pinMode(BoardPins::kPumpSignalPin, OUTPUT);

    for (const uint8_t pin : BoardPins::kValvePins) {
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
    }

    initialized_ = false;
    activeZoneId_ = 0;
    pumpSignalActive_ = false;
    if (pwmFrequencyHz < 1000 || pwmFrequencyHz > 25000) {
        safeShutdown();
        return false;
    }

    bool pwmReady = true;
    for (uint8_t channel = 0; channel < kPwmChannelCount; ++channel) {
        if (ledcSetup(channel, pwmFrequencyHz, kPwmResolutionBits) == 0) {
            pwmReady = false;
        }
        ledcWrite(channel, 0);
        ledcAttachPin(BoardPins::kValvePins[channel], channel);
    }

    pinMode(BoardPins::kFlowMeterPin, INPUT);
    flowPulseCount_ = 0;
    attachInterrupt(digitalPinToInterrupt(BoardPins::kFlowMeterPin), onFlowPulse, RISING);

    safeShutdown();
    if (!pwmReady) {
        return false;
    }
    initialized_ = true;
    return true;
}

void BoardHardware::safeShutdown() {
    // Disable the valve drivers first; later writes cannot briefly energize a valve.
    digitalWrite(BoardPins::kValveDriverShutdownPin, HIGH);
    digitalWrite(BoardPins::kPumpSignalPin, HIGH);
    pumpSignalActive_ = false;
    writeAllValveDuties(0);
    activeZoneId_ = 0;
}

bool BoardHardware::configureValvePwmFrequency(uint32_t frequencyHz) {
    if (!initialized_ || activeZoneId_ != 0 || frequencyHz < 1000 || frequencyHz > 25000) {
        return false;
    }

    safeShutdown();
    bool success = true;
    for (uint8_t channel = 0; channel < kPwmChannelCount; ++channel) {
        if (ledcChangeFrequency(channel, frequencyHz, kPwmResolutionBits) == 0) {
            success = false;
        }
        ledcWrite(channel, 0);
    }
    if (!success) {
        initialized_ = false;
    }
    return success;
}

bool BoardHardware::openValve(uint8_t zoneId, uint8_t dutyPercent) {
    if (!initialized_ || !BoardPins::isValidZoneId(zoneId) || dutyPercent == 0 || dutyPercent > 100) {
        return false;
    }

    closeValves();
    activeZoneId_ = zoneId;
    ledcWrite(BoardPins::zoneIndex(zoneId), dutyToRaw(dutyPercent));
    digitalWrite(BoardPins::kValveDriverShutdownPin, LOW);
    return true;
}

bool BoardHardware::setActiveValveDuty(uint8_t dutyPercent) {
    if (!initialized_ || activeZoneId_ == 0 || dutyPercent == 0 || dutyPercent > 100) {
        return false;
    }
    ledcWrite(BoardPins::zoneIndex(activeZoneId_), dutyToRaw(dutyPercent));
    return true;
}

void BoardHardware::closeValves() {
    digitalWrite(BoardPins::kValveDriverShutdownPin, HIGH);
    writeAllValveDuties(0);
    activeZoneId_ = 0;
}

bool BoardHardware::setPumpSignal(bool active) {
    if (active && (!initialized_ || activeZoneId_ == 0)) {
        return false;
    }
    digitalWrite(BoardPins::kPumpSignalPin, active ? LOW : HIGH);
    pumpSignalActive_ = active;
    return true;
}

bool BoardHardware::initialized() const {
    return initialized_;
}

uint8_t BoardHardware::activeZoneId() const {
    return activeZoneId_;
}

bool BoardHardware::pumpSignalActive() const {
    return pumpSignalActive_;
}

uint32_t BoardHardware::flowPulseCount() const {
    return flowPulseCount_;
}

void IRAM_ATTR BoardHardware::onFlowPulse() {
    ++flowPulseCount_;
}

uint8_t BoardHardware::dutyToRaw(uint8_t dutyPercent) {
    return static_cast<uint8_t>((static_cast<uint16_t>(dutyPercent) * 255U + 50U) / 100U);
}

void BoardHardware::writeAllValveDuties(uint8_t rawDuty) {
    for (uint8_t channel = 0; channel < kPwmChannelCount; ++channel) {
        ledcWrite(channel, rawDuty);
    }
}
