#include "domain/SafetyManager.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"
#include "io/ButtonInput.h"

namespace {

ButtonInput g_factoryReset(IrrigationPins::FactoryResetButton, true, 30, 3000, true);
bool g_factoryResetRequested = false;

}

namespace SafetyManager {

void begin() {
    g_factoryResetRequested = false;
    g_factoryReset.begin();
    ESP32BASE_LOG_I("safety", "ready");
}

void handle() {
    const uint32_t now = millis();
    g_factoryReset.handle(now);
    if (g_factoryReset.wasLongPressed()) {
        g_factoryResetRequested = true;
        ESP32BASE_LOG_W("safety", "factory reset requested by gpio0 long press");
    }
}

bool factoryResetRequested() {
    return g_factoryResetRequested;
}

void clearFactoryResetRequest() {
    g_factoryResetRequested = false;
}

}
