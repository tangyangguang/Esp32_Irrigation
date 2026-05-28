#include "domain/FlowMeter.h"

#include <Arduino.h>

#include "Pins.h"

namespace {

static constexpr uint8_t kFlowPins[] = {
    IrrigationPins::Flow1,
    IrrigationPins::Flow2,
    IrrigationPins::Flow3,
    IrrigationPins::Flow4,
};

volatile uint32_t g_pulses[IrrigationPins::MaxRoads] = {};
portMUX_TYPE g_pulseMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastSamplePulses[IrrigationPins::MaxRoads] = {};
uint32_t g_ratePerMinuteX1000[IrrigationPins::MaxRoads] = {};

void IRAM_ATTR onFlow1Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    ++g_pulses[0];
    portEXIT_CRITICAL_ISR(&g_pulseMux);
}

void IRAM_ATTR onFlow2Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    ++g_pulses[1];
    portEXIT_CRITICAL_ISR(&g_pulseMux);
}

void IRAM_ATTR onFlow3Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    ++g_pulses[2];
    portEXIT_CRITICAL_ISR(&g_pulseMux);
}

void IRAM_ATTR onFlow4Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    ++g_pulses[3];
    portEXIT_CRITICAL_ISR(&g_pulseMux);
}

bool roadIndex(uint8_t road, uint8_t* index) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    *index = road - 1;
    return true;
}

}

namespace FlowMeter {

void begin() {
    using Handler = void (*)();
    static constexpr Handler handlers[] = {onFlow1Pulse, onFlow2Pulse, onFlow3Pulse, onFlow4Pulse};
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        pinMode(kFlowPins[i], INPUT);
        attachInterrupt(digitalPinToInterrupt(kFlowPins[i]), handlers[i], RISING);
    }
    g_lastSampleMs = millis();
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        g_lastSamplePulses[i] = pulseCount(i + 1);
    }
}

void handle() {
    const uint32_t now = millis();
    const uint32_t elapsedMs = now - g_lastSampleMs;
    if (elapsedMs < 1000UL) {
        return;
    }
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        const uint32_t pulses = pulseCount(i + 1);
        const uint32_t delta = pulses >= g_lastSamplePulses[i] ? pulses - g_lastSamplePulses[i] : 0;
        g_ratePerMinuteX1000[i] = elapsedMs > 0 ? static_cast<uint32_t>((static_cast<uint64_t>(delta) * 60000ULL * 1000ULL) / elapsedMs) : 0;
        g_lastSamplePulses[i] = pulses;
    }
    g_lastSampleMs = now;
}

uint32_t pulseCount(uint8_t road) {
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        return 0;
    }
    portENTER_CRITICAL(&g_pulseMux);
    const uint32_t count = g_pulses[index];
    portEXIT_CRITICAL(&g_pulseMux);
    return count;
}

uint32_t pulseRatePerMinuteX1000(uint8_t road) {
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        return 0;
    }
    return g_ratePerMinuteX1000[index];
}

}
