#include <Arduino.h>
#include <Esp32Base.h>

#include "Version.h"
#include "app/IrrigationApp.h"

void setup() {
    IrrigationApp::beginHardwareSafety();

    Esp32Base::setFirmwareInfo(IrrigationVersion::FirmwareName, IrrigationVersion::FirmwareVersion);
    Esp32Base::setHostname(IrrigationVersion::Hostname);
    Esp32BaseWeb::setDefaultAuth(IrrigationVersion::WebUser, IrrigationVersion::WebPassword);
    Esp32BaseWeb::setDeviceName("灌溉");
    Esp32BaseWeb::setHomePath("/irrigation");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "系统");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "WiFi");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_AUTH, "认证");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_OTA, "OTA");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_LOGS, "日志");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_REBOOT, "重启");
    Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_SYSTEM, "系统工具");

    Esp32Base::begin();

#if ESP32BASE_ENABLE_FILELOG
    Esp32BaseFileLog::enable("/logs/irrigation.log", 32UL * 1024UL, Esp32BaseLog::WARN, 4);
#endif

    IrrigationApp::begin();
}

void loop() {
    Esp32Base::handle();
    IrrigationApp::handle();
    delay(10);
}
