#include <Arduino.h>
#include <Esp32Base.h>

#include "IrrigationApp.h"

namespace {
bool g_baseReady = false;
}

void setup() {
    Serial.begin(115200);
    delay(100);

    Esp32Base::setFirmwareInfo("esp32-irrigation", "0.1.0");
    IrrigationApp::setupBeforeBase();

    if (!Esp32Base::begin()) {
        ESP32BASE_LOG_E("irrigation", "esp32base_begin_failed error=%s", Esp32Base::lastError());
        return;
    }

    g_baseReady = true;
    IrrigationApp::setupAfterBase();
}

void loop() {
    if (!g_baseReady) {
        delay(100);
        return;
    }

    Esp32Base::handle();
    IrrigationApp::loop();
    delay(10);
}
