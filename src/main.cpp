#include <Arduino.h>
#include <Esp32Base.h>

#include "Version.h"
#include "app/IrrigationApp.h"

void setup() {
    IrrigationApp::beginHardwareSafety();

    Esp32Base::setFirmwareInfo(IrrigationVersion::FirmwareName, IrrigationVersion::FirmwareVersion);
    Esp32BaseWeb::setDefaultAuth(IrrigationVersion::WebUser, IrrigationVersion::WebPassword);
    Esp32BaseWeb::setDeviceName("总览");
    Esp32BaseWeb::setHomePath("/irrigation");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "系统");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "WiFi");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_AUTH, "认证");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_OTA, "OTA");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_LOGS, "日志");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_TOOLS, "工具");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_SYSTEM, "系统工具");

    Esp32Base::begin();

    IrrigationApp::begin();
}

void loop() {
    Esp32Base::handle();
    IrrigationApp::handle();
    delay(10);
}
