#include "domain/FlowMeter.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "domain/ZoneTypes.h"
#include "Pins.h"

namespace {

static constexpr uint8_t kFlowPins[] = {
    IrrigationPins::Flow1,
    IrrigationPins::Flow2,
};
static constexpr uint8_t kMaxFlowWindowSec = 30;

volatile uint32_t g_pulses[Irrigation::MaxFlowMeters] = {};
portMUX_TYPE g_pulseMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_lastSampleMs = 0;
uint32_t g_lastSamplePulses[Irrigation::MaxFlowMeters] = {};
uint64_t g_ratePerMinuteX1000[Irrigation::MaxFlowMeters] = {};
uint32_t g_flowMlPerMin[Irrigation::MaxFlowMeters] = {};
bool g_flowReady[Irrigation::MaxFlowMeters] = {};
int32_t g_kUlPerMinPerHz[Irrigation::MaxFlowMeters] = {244897, 244897};
int32_t g_offsetMilliHz[Irrigation::MaxFlowMeters] = {0, 0};
uint16_t g_sampleWindowSec = 5;
uint16_t g_historyIntervalSec = 5;
uint16_t g_historyDepthMin = 10;
uint16_t g_flowHistoryLimit = 120;
uint32_t g_lastHistoryMs = 0;
uint32_t g_deltaHistory[Irrigation::MaxFlowMeters][kMaxFlowWindowSec] = {};
uint16_t g_deltaMsHistory[Irrigation::MaxFlowMeters][kMaxFlowWindowSec] = {};
uint8_t g_deltaHistoryPos[Irrigation::MaxFlowMeters] = {};
uint8_t g_deltaHistoryFilled[Irrigation::MaxFlowMeters] = {};
uint16_t g_flowHistory[Irrigation::MaxFlowMeters][FlowMeter::MaxFlowHistoryPoints] = {};
uint16_t g_flowHistoryHead[Irrigation::MaxFlowMeters] = {};
uint16_t g_flowHistoryCount[Irrigation::MaxFlowMeters] = {};
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

bool flowIndex(uint8_t flowId, uint8_t* index) {
    if (flowId < 1 || flowId > Irrigation::MaxFlowMeters) {
        return false;
    }
    *index = flowId - 1;
    return true;
}

uint16_t clampWindowSec(uint16_t value) {
    if (value < 2) return 2;
    if (value > kMaxFlowWindowSec) return kMaxFlowWindowSec;
    return value;
}

uint16_t clampChartIntervalSec(uint16_t value) {
    if (value < 1) return 1;
    if (value > 30) return 30;
    return value;
}

uint16_t clampChartHistoryMin(uint16_t value) {
    if (value < 1) return 1;
    if (value > 30) return 30;
    return value;
}

uint16_t computeHistoryLimit(uint16_t intervalSec, uint16_t historyMin) {
    uint32_t points = (static_cast<uint32_t>(historyMin) * 60UL) / intervalSec;
    if (points < 1) points = 1;
    if (points > FlowMeter::MaxFlowHistoryPoints) points = FlowMeter::MaxFlowHistoryPoints;
    return static_cast<uint16_t>(points);
}

uint32_t saturatingMlPerMin(uint64_t flowUlPerMin) {
    const uint64_t flowMlPerMin = flowUlPerMin / 1000ULL;
    return flowMlPerMin > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(flowMlPerMin);
}

uint64_t computeFlowUlPerMin(uint64_t correctedMilliHz, uint32_t kUlPerMinPerHz) {
    if (kUlPerMinPerHz == 0) {
        return 0;
    }
    if (correctedMilliHz > UINT64_MAX / static_cast<uint64_t>(kUlPerMinPerHz)) {
        return UINT64_MAX;
    }
    return (correctedMilliHz * static_cast<uint64_t>(kUlPerMinPerHz)) / 1000ULL;
}

void clearFlowHistory(uint8_t index) {
    if (index >= Irrigation::MaxFlowMeters) {
        return;
    }
    memset(g_deltaHistory[index], 0, sizeof(g_deltaHistory[index]));
    memset(g_deltaMsHistory[index], 0, sizeof(g_deltaMsHistory[index]));
    memset(g_flowHistory[index], 0, sizeof(g_flowHistory[index]));
    g_deltaHistoryPos[index] = 0;
    g_deltaHistoryFilled[index] = 0;
    g_flowHistoryHead[index] = 0;
    g_flowHistoryCount[index] = 0;
    g_ratePerMinuteX1000[index] = 0;
    g_flowMlPerMin[index] = 0;
    g_flowReady[index] = false;
}

void clearAllFlowHistory() {
    for (uint8_t i = 0; i < Irrigation::MaxFlowMeters; ++i) {
        clearFlowHistory(i);
    }
}

void pushHistoryPoint(uint8_t index, uint32_t mlPerMin) {
    if (index >= Irrigation::MaxFlowMeters || g_flowHistoryLimit == 0) {
        return;
    }
    if (mlPerMin > 65535UL) {
        mlPerMin = 65535UL;
    }
    g_flowHistory[index][g_flowHistoryHead[index]] = static_cast<uint16_t>(mlPerMin);
    g_flowHistoryHead[index] = static_cast<uint16_t>((g_flowHistoryHead[index] + 1U) % g_flowHistoryLimit);
    if (g_flowHistoryCount[index] < g_flowHistoryLimit) {
        ++g_flowHistoryCount[index];
    }
}

void sampleFlow(uint8_t index, uint32_t delta, uint32_t elapsedMs) {
    g_deltaHistory[index][g_deltaHistoryPos[index]] = delta;
    g_deltaMsHistory[index][g_deltaHistoryPos[index]] = elapsedMs > 65535UL ? 65535U : static_cast<uint16_t>(elapsedMs);
    g_deltaHistoryPos[index] = static_cast<uint8_t>((g_deltaHistoryPos[index] + 1U) % kMaxFlowWindowSec);
    if (g_deltaHistoryFilled[index] < kMaxFlowWindowSec) {
        ++g_deltaHistoryFilled[index];
    }

    const uint32_t targetMs = static_cast<uint32_t>(g_sampleWindowSec) * 1000UL;
    uint32_t sumPulses = 0;
    uint32_t sumMs = 0;
    for (uint8_t n = 0; n < g_deltaHistoryFilled[index] && sumMs < targetMs; ++n) {
        const uint8_t pos = static_cast<uint8_t>((g_deltaHistoryPos[index] + kMaxFlowWindowSec - 1U - n) % kMaxFlowWindowSec);
        sumPulses += g_deltaHistory[index][pos];
        sumMs += g_deltaMsHistory[index][pos];
    }
    if (sumMs == 0) {
        g_ratePerMinuteX1000[index] = 0;
        g_flowMlPerMin[index] = 0;
        g_flowReady[index] = false;
        return;
    }
    g_ratePerMinuteX1000[index] = (static_cast<uint64_t>(sumPulses) * 60000ULL * 1000ULL) / sumMs;
    g_flowReady[index] = sumMs >= targetMs;
    const int64_t measuredMilliHz = (static_cast<int64_t>(sumPulses) * 1000000LL) / sumMs;
    const int64_t correctedMilliHz = measuredMilliHz + g_offsetMilliHz[index];
    if (correctedMilliHz <= 0 || g_kUlPerMinPerHz[index] <= 0) {
        g_flowMlPerMin[index] = 0;
    } else {
        const uint64_t flowUlPerMin = computeFlowUlPerMin(static_cast<uint64_t>(correctedMilliHz),
                                                          static_cast<uint32_t>(g_kUlPerMinPerHz[index]));
        g_flowMlPerMin[index] = saturatingMlPerMin(flowUlPerMin);
    }
}

}

namespace FlowMeter {

void begin() {
    using Handler = void (*)();
    static constexpr Handler handlers[] = {onFlow1Pulse, onFlow2Pulse};
    for (uint8_t i = 0; i < Irrigation::MaxFlowMeters; ++i) {
        pinMode(kFlowPins[i], INPUT);
        attachInterrupt(digitalPinToInterrupt(kFlowPins[i]), handlers[i], RISING);
    }
    g_lastSampleMs = millis();
    for (uint8_t i = 0; i < Irrigation::MaxFlowMeters; ++i) {
        g_lastSamplePulses[i] = pulseCount(i + 1);
    }
    g_lastHistoryMs = g_lastSampleMs;
}

void handle() {
    const uint32_t now = millis();
    const uint32_t elapsedMs = now - g_lastSampleMs;
    if (elapsedMs < 1000UL) {
        return;
    }
    for (uint8_t i = 0; i < Irrigation::MaxFlowMeters; ++i) {
        const uint32_t pulses = pulseCount(i + 1);
        const uint32_t delta = pulses >= g_lastSamplePulses[i] ? pulses - g_lastSamplePulses[i] : 0;
        sampleFlow(i, delta, elapsedMs);
        g_lastSamplePulses[i] = pulses;
    }
    g_lastSampleMs = now;
    if (now - g_lastHistoryMs >= static_cast<uint32_t>(g_historyIntervalSec) * 1000UL) {
        for (uint8_t i = 0; i < Irrigation::MaxFlowMeters; ++i) {
            pushHistoryPoint(i, g_flowMlPerMin[i]);
        }
        g_lastHistoryMs = now;
    }
}

void configureFlowRate(uint16_t windowSec, uint16_t chartIntervalSec, uint16_t chartHistoryMin) {
    windowSec = clampWindowSec(windowSec);
    chartIntervalSec = clampChartIntervalSec(chartIntervalSec);
    chartHistoryMin = clampChartHistoryMin(chartHistoryMin);
    const uint16_t limit = computeHistoryLimit(chartIntervalSec, chartHistoryMin);
    if (windowSec == g_sampleWindowSec &&
        chartIntervalSec == g_historyIntervalSec &&
        chartHistoryMin == g_historyDepthMin &&
        limit == g_flowHistoryLimit) {
        return;
    }
    g_sampleWindowSec = windowSec;
    g_historyIntervalSec = chartIntervalSec;
    g_historyDepthMin = chartHistoryMin;
    g_flowHistoryLimit = limit;
    clearAllFlowHistory();
    g_lastHistoryMs = millis();
}

void configureCalibration(uint8_t flowId, int32_t kUlPerMinPerHz, int32_t offsetMilliHz) {
    uint8_t index = 0;
    if (!flowIndex(flowId, &index) || kUlPerMinPerHz <= 0) {
        return;
    }
    if (g_kUlPerMinPerHz[index] == kUlPerMinPerHz && g_offsetMilliHz[index] == offsetMilliHz) {
        return;
    }
    g_kUlPerMinPerHz[index] = kUlPerMinPerHz;
    g_offsetMilliHz[index] = offsetMilliHz;
    clearFlowHistory(index);
}

uint32_t pulseCount(uint8_t flowId) {
    uint8_t index = 0;
    if (!flowIndex(flowId, &index)) {
        return 0;
    }
    portENTER_CRITICAL(&g_pulseMux);
    const uint32_t count = g_pulses[index];
    portEXIT_CRITICAL(&g_pulseMux);
    return count;
}

uint64_t pulseRatePerMinuteX1000(uint8_t flowId) {
    uint8_t index = 0;
    if (!flowIndex(flowId, &index)) {
        return 0;
    }
    return g_ratePerMinuteX1000[index];
}

uint32_t flowMillilitersPerMinute(uint8_t flowId) {
    uint8_t index = 0;
    if (!flowIndex(flowId, &index)) {
        return 0;
    }
    return g_flowMlPerMin[index];
}

bool flowRateReady(uint8_t flowId) {
    uint8_t index = 0;
    if (!flowIndex(flowId, &index)) {
        return false;
    }
    return g_flowReady[index];
}

uint16_t sampleWindowSec() {
    return g_sampleWindowSec;
}

uint16_t historyIntervalSec() {
    return g_historyIntervalSec;
}

uint16_t historyDepthMin() {
    return g_historyDepthMin;
}

uint16_t readFlowHistory(uint8_t flowId, uint16_t* out, uint16_t capacity) {
    uint8_t index = 0;
    if (!out || capacity == 0 || !flowIndex(flowId, &index)) {
        return 0;
    }
    uint16_t count = g_flowHistoryCount[index];
    if (count > capacity) {
        count = capacity;
    }
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t oldest = g_flowHistoryCount[index] < g_flowHistoryLimit
            ? 0
            : g_flowHistoryHead[index];
        const uint16_t source = static_cast<uint16_t>((oldest + g_flowHistoryCount[index] - count + i) % g_flowHistoryLimit);
        out[i] = g_flowHistory[index][source];
    }
    return count;
}

bool beginCapture(uint8_t flowId, uint32_t detailCaptureMs, uint16_t detailPulseLimit) {
    uint8_t index = 0;
    if (!flowIndex(flowId, &index) || detailCaptureMs == 0 || detailPulseLimit == 0) {
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
