#include <Arduino.h>
#include <Esp32Base.h>

#include "Version.h"
#include "app/IrrigationApp.h"

void setup() {
    IrrigationApp::beginHardwareSafety();

    Esp32Base::setFirmwareInfo(IrrigationVersion::FirmwareName, IrrigationVersion::FirmwareVersion);
    Esp32BaseWeb::setDefaultAuth(IrrigationVersion::WebUser, IrrigationVersion::WebPassword);
    Esp32BaseWeb::setDeviceName("首页");
    Esp32BaseWeb::setHomePath("/irrigation");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);

    Esp32Base::begin();

    IrrigationApp::begin();
}

void loop() {
    Esp32Base::handle();
    IrrigationApp::handle();
    delay(10);
}
