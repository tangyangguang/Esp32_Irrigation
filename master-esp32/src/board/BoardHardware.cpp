#include "BoardHardware.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "BoardPins.h"
#include "IrrigationConfig.h"

namespace Irrigation {

namespace {

constexpr uint8_t kValvePwmResolutionBits = 8;
constexpr uint32_t kValvePwmFrequencyHz = 1000;
constexpr uint8_t kValvePwmMaxDuty = (1U << kValvePwmResolutionBits) - 1U;

struct ValveRuntime {
    bool open = false;
    bool holding = false;
    uint32_t openedAtMs = 0;
};

ValveConfig g_valveConfig;
SupplyConfig g_supplyConfig;
ValveRuntime g_valves[kMaxZones];
bool g_started = false;
bool g_pumpSignal = false;
bool g_lowLevelStable = false;
bool g_lowLevelLastRaw = false;
uint32_t g_lowLevelLastChangeMs = 0;
uint32_t g_lastSnapshotPulses = 0;
volatile uint32_t g_flowPulseCount = 0;

uint8_t dutyFromHoldPercent(uint8_t percent) {
    if (percent >= 100) {
        return kValvePwmMaxDuty;
    }
    if (percent == 0) {
        return 0;
    }
    return static_cast<uint8_t>((static_cast<uint16_t>(kValvePwmMaxDuty) * percent + 50U) / 100U);
}

void writeValveDuty(uint8_t valveIndex, uint8_t duty) {
    if (valveIndex >= kMaxZones) {
        return;
    }
    ledcWrite(valveIndex, duty);
}

void disableDriverIfIdle() {
    if (BoardHardware::activeValveCount() == 0) {
        digitalWrite(IrrigationPins::kDriverShutdown, HIGH);
    }
}

bool rawLowLevelByContact(bool pinHigh) {
    if (g_supplyConfig.lowLevelContactType == ContactType::NormallyClosed) {
        return pinHigh;
    }
    return !pinHigh;
}

} // namespace

void IRAM_ATTR BoardHardware::onFlowPulse() {
    ++g_flowPulseCount;
}

void BoardHardware::begin(const ValveConfig& valveConfig, const SupplyConfig& supplyConfig) {
    configure(valveConfig, supplyConfig);

    pinMode(IrrigationPins::kDriverShutdown, OUTPUT);
    pinMode(IrrigationPins::kPumpDryOut, OUTPUT);

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        pinMode(IrrigationPins::kValvePwm[i], OUTPUT);
        ledcSetup(i, kValvePwmFrequencyHz, kValvePwmResolutionBits);
        ledcAttachPin(IrrigationPins::kValvePwm[i], i);
    }

    pinMode(IrrigationPins::kFlowPulse, INPUT);
    pinMode(IrrigationPins::kLowLevel, INPUT_PULLUP);
    pinMode(IrrigationPins::kRtcInterrupt, INPUT_PULLUP);
    pinMode(IrrigationPins::kButton1, INPUT);
    pinMode(IrrigationPins::kButton2, INPUT);
    pinMode(IrrigationPins::kButton3, INPUT);
    pinMode(IrrigationPins::kButton4, INPUT);

    safeOff();
    resetFlowCounter();
    attachInterrupt(digitalPinToInterrupt(IrrigationPins::kFlowPulse), BoardHardware::onFlowPulse, RISING);

    const bool raw = lowLevelRawActive();
    g_lowLevelStable = raw;
    g_lowLevelLastRaw = raw;
    g_lowLevelLastChangeMs = millis();
    g_started = true;

    ESP32BASE_LOG_I("irrigation_hw", "board_ready valves=%u flow_pin=%u low_level_pin=%u pump_pin=%u",
                    kMaxZones,
                    IrrigationPins::kFlowPulse,
                    IrrigationPins::kLowLevel,
                    IrrigationPins::kPumpDryOut);
}

void BoardHardware::configure(const ValveConfig& valveConfig, const SupplyConfig& supplyConfig) {
    g_valveConfig = valveConfig;
    g_supplyConfig = supplyConfig;

    if (!isValidHoldPercent(g_valveConfig.holdPercent)) {
        g_valveConfig.holdPercent = 60;
    }
}

void BoardHardware::handle(uint32_t nowMs) {
    if (!g_started) {
        return;
    }

    const uint8_t holdDuty = dutyFromHoldPercent(g_valveConfig.holdPercent);
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        ValveRuntime& valve = g_valves[i];
        if (!valve.open || valve.holding) {
            continue;
        }
        if (nowMs - valve.openedAtMs >= g_valveConfig.pullInMs) {
            writeValveDuty(i, holdDuty);
            valve.holding = true;
        }
    }

    lowLevelActive(nowMs);
}

void BoardHardware::safeOff() {
    digitalWrite(IrrigationPins::kDriverShutdown, HIGH);

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        g_valves[i] = {};
        writeValveDuty(i, 0);
        digitalWrite(IrrigationPins::kValvePwm[i], LOW);
    }

    g_pumpSignal = false;
    digitalWrite(IrrigationPins::kPumpDryOut, LOW);
}

bool BoardHardware::openValve(uint8_t zoneId, uint32_t nowMs) {
    const uint8_t index = zoneIndexFromId(zoneId);
    if (index >= kMaxZones) {
        return false;
    }

    g_valves[index].open = true;
    g_valves[index].holding = false;
    g_valves[index].openedAtMs = nowMs;
    digitalWrite(IrrigationPins::kDriverShutdown, LOW);
    writeValveDuty(index, kValvePwmMaxDuty);
    return true;
}

void BoardHardware::closeValve(uint8_t zoneId) {
    const uint8_t index = zoneIndexFromId(zoneId);
    if (index >= kMaxZones) {
        return;
    }

    g_valves[index] = {};
    writeValveDuty(index, 0);
    disableDriverIfIdle();
}

void BoardHardware::closeAllValves() {
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        g_valves[i] = {};
        writeValveDuty(i, 0);
    }
    digitalWrite(IrrigationPins::kDriverShutdown, HIGH);
}

bool BoardHardware::isValveOpen(uint8_t zoneId) {
    const uint8_t index = zoneIndexFromId(zoneId);
    return index < kMaxZones && g_valves[index].open;
}

uint8_t BoardHardware::activeValveCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        if (g_valves[i].open) {
            ++count;
        }
    }
    return count;
}

void BoardHardware::setPumpSignal(bool enabled) {
    g_pumpSignal = enabled;
    digitalWrite(IrrigationPins::kPumpDryOut, enabled ? HIGH : LOW);
}

bool BoardHardware::pumpSignalActive() {
    return g_pumpSignal;
}

bool BoardHardware::lowLevelRawActive() {
    const bool pinHigh = digitalRead(IrrigationPins::kLowLevel) == HIGH;
    return rawLowLevelByContact(pinHigh);
}

bool BoardHardware::lowLevelActive(uint32_t nowMs) {
    if (!g_supplyConfig.pumpEnabled || !g_supplyConfig.lowLevelEnabled) {
        return false;
    }

    const bool raw = lowLevelRawActive();
    if (raw != g_lowLevelLastRaw) {
        g_lowLevelLastRaw = raw;
        g_lowLevelLastChangeMs = nowMs;
    }

    if (nowMs - g_lowLevelLastChangeMs >= g_supplyConfig.lowLevelDebounceMs) {
        g_lowLevelStable = raw;
    }

    return g_lowLevelStable;
}

BoardHardware::FlowSnapshot BoardHardware::flowSnapshot(uint32_t nowMs) {
    const uint32_t pulses = flowPulseCount();
    FlowSnapshot snapshot = {};
    snapshot.pulseCount = pulses;
    snapshot.deltaPulses = pulses - g_lastSnapshotPulses;
    snapshot.nowMs = nowMs;
    g_lastSnapshotPulses = pulses;
    return snapshot;
}

uint32_t BoardHardware::flowPulseCount() {
    noInterrupts();
    const uint32_t pulses = g_flowPulseCount;
    interrupts();
    return pulses;
}

void BoardHardware::resetFlowCounter() {
    noInterrupts();
    g_flowPulseCount = 0;
    interrupts();
    g_lastSnapshotPulses = 0;
}

} // namespace Irrigation
