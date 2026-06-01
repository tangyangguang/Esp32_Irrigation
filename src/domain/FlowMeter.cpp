#include "domain/FlowMeter.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

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
volatile bool g_captureActive = false;
volatile uint8_t g_captureIndex = 0;
volatile uint32_t g_captureStartedMs = 0;
volatile uint32_t g_captureLastMs = 0;
volatile uint32_t g_captureDetailMs = 0;
volatile uint16_t g_captureLimit = 0;
volatile uint16_t g_captureCount = 0;
volatile uint16_t g_captureDeltas[5000] = {};
char g_captureEndedReason[16] = "none";

uint32_t IRAM_ATTR isrMillis() {
    return static_cast<uint32_t>(xTaskGetTickCountFromISR()) * portTICK_PERIOD_MS;
}

void IRAM_ATTR recordPulse(uint8_t index) {
    const uint32_t nowMs = isrMillis();
    ++g_pulses[index];
    if (!g_captureActive || index != g_captureIndex) {
        return;
    }
    const uint32_t elapsedMs = nowMs - g_captureStartedMs;
    if (elapsedMs > g_captureDetailMs) {
        return;
    }
    const uint16_t count = g_captureCount;
    if (count >= g_captureLimit || count >= 5000) {
        return;
    }
    uint32_t delta = count == 0 ? elapsedMs : nowMs - g_captureLastMs;
    if (delta > 65535UL) {
        delta = 65535UL;
    }
    g_captureDeltas[count] = static_cast<uint16_t>(delta);
    g_captureCount = count + 1;
    g_captureLastMs = nowMs;
}

void IRAM_ATTR onFlow1Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    recordPulse(0);
    portEXIT_CRITICAL_ISR(&g_pulseMux);
}

void IRAM_ATTR onFlow2Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    recordPulse(1);
    portEXIT_CRITICAL_ISR(&g_pulseMux);
}

void IRAM_ATTR onFlow3Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    recordPulse(2);
    portEXIT_CRITICAL_ISR(&g_pulseMux);
}

void IRAM_ATTR onFlow4Pulse() {
    portENTER_CRITICAL_ISR(&g_pulseMux);
    recordPulse(3);
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

bool beginCapture(uint8_t road, uint32_t detailCaptureMs, uint16_t detailPulseLimit) {
    uint8_t index = 0;
    if (!roadIndex(road, &index) || detailCaptureMs == 0 || detailPulseLimit == 0) {
        return false;
    }
    if (detailPulseLimit > 5000) {
        detailPulseLimit = 5000;
    }
    portENTER_CRITICAL(&g_pulseMux);
    g_captureActive = true;
    g_captureIndex = index;
    g_captureStartedMs = millis();
    g_captureLastMs = g_captureStartedMs;
    g_captureDetailMs = detailCaptureMs;
    g_captureLimit = detailPulseLimit;
    g_captureCount = 0;
    portEXIT_CRITICAL(&g_pulseMux);
    strlcpy(g_captureEndedReason, "active", sizeof(g_captureEndedReason));
    return true;
}

bool endCapture(uint16_t* deltas, uint16_t capacity, CaptureResult* result) {
    if (!deltas || !result) {
        return false;
    }
    uint16_t count = 0;
    uint32_t startedMs = 0;
    uint32_t endedMs = millis();
    uint32_t detailMs = 0;
    uint16_t limit = 0;
    portENTER_CRITICAL(&g_pulseMux);
    const bool wasActive = g_captureActive;
    g_captureActive = false;
    count = g_captureCount;
    startedMs = g_captureStartedMs;
    detailMs = g_captureDetailMs;
    limit = g_captureLimit;
    if (count > capacity) {
        count = capacity;
    }
    portEXIT_CRITICAL(&g_pulseMux);
    if (!wasActive) {
        return false;
    }
    for (uint16_t i = 0; i < count; ++i) {
        deltas[i] = g_captureDeltas[i];
    }
    const uint32_t durationMs = endedMs - startedMs;
    const char* reason = "sample_end";
    if (g_captureCount >= limit) {
        reason = "pulse_limit";
    } else if (durationMs >= detailMs) {
        reason = "time_limit";
    }
    strlcpy(g_captureEndedReason, reason, sizeof(g_captureEndedReason));
    result->startedMs = startedMs;
    result->endedMs = endedMs;
    result->capturedPulses = count;
    result->endedReason = g_captureEndedReason;
    return true;
}

}
