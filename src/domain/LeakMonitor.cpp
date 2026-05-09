#include "domain/LeakMonitor.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"
#include "domain/FlowMeter.h"
#include "domain/ValveController.h"
#include "domain/WateringSession.h"
#include "storage/EventStore.h"
#include "storage/SettingsStore.h"

namespace {

static constexpr uint32_t kIdleSettleMs = 3000UL;

struct RoadLeakState {
    uint32_t windowStartedMs;
    uint32_t windowStartPulses;
    bool alert;
};

RoadLeakState g_roads[2] = {};
bool g_wasMonitoringAllowed = false;
uint32_t g_monitoringAllowedSinceMs = 0;

bool roadIndex(uint8_t road, uint8_t* index) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    *index = road - 1;
    return true;
}

void resetWindow(uint8_t index, uint32_t now) {
    g_roads[index].windowStartedMs = now;
    g_roads[index].windowStartPulses = FlowMeter::pulseCount(index + 1);
}

bool monitoringAllowed() {
    return !WateringSession::isActive() &&
           !ValveController::isOpen(ValveController::Road1) &&
           !ValveController::isOpen(ValveController::Road2);
}

}

namespace LeakMonitor {

void begin() {
    const uint32_t now = millis();
    g_wasMonitoringAllowed = monitoringAllowed();
    g_monitoringAllowedSinceMs = g_wasMonitoringAllowed ? now : 0;
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        g_roads[i].alert = false;
        resetWindow(i, now);
    }
}

void handle() {
    const uint32_t now = millis();
    const bool allowed = monitoringAllowed();
    if (allowed != g_wasMonitoringAllowed) {
        g_wasMonitoringAllowed = allowed;
        g_monitoringAllowedSinceMs = allowed ? now : 0;
    }
    if (!allowed || now - g_monitoringAllowedSinceMs < kIdleSettleMs) {
        for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
            resetWindow(i, now);
        }
        return;
    }

    const SettingsStore::Settings& settings = SettingsStore::current();
    const uint32_t windowMs = static_cast<uint32_t>(settings.idleLeakWindowSec) * 1000UL;
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (!SettingsStore::isRoadEnabled(i + 1)) {
            resetWindow(i, now);
            continue;
        }
        if (now - g_roads[i].windowStartedMs < windowMs) {
            continue;
        }
        const uint32_t pulses = FlowMeter::pulseCount(i + 1);
        const uint32_t delta = pulses - g_roads[i].windowStartPulses;
        if (delta >= settings.idleLeakPulseThreshold) {
            const bool firstAlert = !g_roads[i].alert;
            g_roads[i].alert = true;
            ValveController::allOff("idle leak alert");
            if (firstAlert) {
                (void)EventStore::append(EventStore::TYPE_LEAK_ALERT,
                                         EventStore::SOURCE_SYSTEM,
                                         i + 1,
                                         0,
                                         static_cast<int32_t>(delta),
                                         settings.idleLeakPulseThreshold,
                                         "idle leak");
                ESP32BASE_LOG_W("leak", "idle leak alert road=%u pulses=%lu window=%u threshold=%u",
                                static_cast<unsigned>(i + 1),
                                static_cast<unsigned long>(delta),
                                static_cast<unsigned>(settings.idleLeakWindowSec),
                                static_cast<unsigned>(settings.idleLeakPulseThreshold));
            }
        }
        resetWindow(i, now);
    }
}

bool hasAlert() {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_roads[i].alert) {
            return true;
        }
    }
    return false;
}

bool roadAlert(uint8_t road) {
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        return false;
    }
    return g_roads[index].alert;
}

void clearAlerts(EventStore::Source source) {
    const uint32_t now = millis();
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        g_roads[i].alert = false;
        resetWindow(i, now);
    }
    (void)EventStore::append(EventStore::TYPE_ALERT_CLEAR, source, 0, 0, 0, 0, "leak alerts");
    ESP32BASE_LOG_I("leak", "alerts cleared");
}

}
