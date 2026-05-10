#include "domain/FlowMeter.h"

#include <Arduino.h>

#include "Pins.h"

namespace {

static constexpr uint8_t kFlowPins[] = {
    IrrigationPins::Flow1,
    IrrigationPins::Flow2,
};

volatile uint32_t g_pulses[2] = {0, 0};
portMUX_TYPE g_pulseMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastSamplePulses[2] = {0, 0};
uint32_t g_ratePerMinuteX1000[2] = {0, 0};

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
    pinMode(kFlowPins[0], INPUT);
    pinMode(kFlowPins[1], INPUT);
    attachInterrupt(digitalPinToInterrupt(kFlowPins[0]), onFlow1Pulse, RISING);
    attachInterrupt(digitalPinToInterrupt(kFlowPins[1]), onFlow2Pulse, RISING);
    g_lastSampleMs = millis();
    g_lastSamplePulses[0] = pulseCount(1);
    g_lastSamplePulses[1] = pulseCount(2);
}

void handle() {
    const uint32_t now = millis();
    const uint32_t elapsedMs = now - g_lastSampleMs;
    if (elapsedMs < 1000UL) {
        return;
    }
    for (uint8_t i = 0; i < 2; ++i) {
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
