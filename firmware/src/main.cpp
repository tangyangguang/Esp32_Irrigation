#include <Arduino.h>
#include <Esp32Base.h>

void setup() {
    Serial.begin(115200);

    Esp32Base::setFirmwareInfo("esp32-irrigation", "0.1.0");
#if ESP32BASE_ENABLE_WEB
    Esp32BaseWeb::setDefaultAuth("admin", "irrigation");
    Esp32BaseWeb::setDeviceName("ESP32 Irrigation");
    Esp32BaseWeb::setHomePath("/");
#endif

    Esp32Base::begin();
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
