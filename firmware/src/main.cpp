#include <Arduino.h>
#include <Esp32Base.h>

#include "irrigation/IrrigationApp.h"

namespace {

irrigation::IrrigationApp g_irrigationApp;

void logBringupStatus() {
    static uint32_t lastLogMs = 0;
    static uint8_t logCount = 0;

    const uint32_t now = millis();
    if (logCount >= 40 || (lastLogMs != 0 && now - lastLogMs < 3000UL)) {
        return;
    }

    lastLogMs = now;
    ++logCount;

#if ESP32BASE_ENABLE_WIFI
    char ip[24] = "-";
    Esp32BaseWiFi::ip(ip, sizeof(ip));
    ESP32BASE_LOG_I("irrigation", "bringup hostname=%s wifi=%s ip=%s web=%s ota=%s",
                    Esp32Base::hostname(),
                    Esp32BaseWiFi::stateName(),
                    ip,
#if ESP32BASE_ENABLE_WEB
                    Esp32BaseWeb::isReady() ? "ready" : "not_ready",
#else
                    "disabled",
#endif
#if ESP32BASE_ENABLE_OTA
                    Esp32BaseOta::isReady() ? "ready" : "not_ready");
#else
                    "disabled");
#endif
#else
    ESP32BASE_LOG_I("irrigation", "bringup hostname=%s wifi=disabled web=disabled ota=disabled",
                    Esp32Base::hostname());
#endif
}

}  // namespace

void setup() {
    Serial.begin(115200);

    Esp32Base::setFirmwareInfo("esp32-irrigation", "0.1.0");
#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::setDeviceName("ESP32 Irrigation");
    Esp32BaseWeb::setHomePath("/");
#endif

    Esp32Base::begin();
    g_irrigationApp.begin();
}

void loop() {
    g_irrigationApp.handle();
    Esp32Base::handle();
    logBringupStatus();
    delay(10);
}
