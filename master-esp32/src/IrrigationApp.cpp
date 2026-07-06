#include "IrrigationApp.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "BoardPins.h"
#include "BoardHardware.h"
#include "CalibrationService.h"
#include "ConfigStore.h"
#include "IrrigationConfig.h"
#include "IrrigationWeb.h"
#include "RunController.h"
#include "Scheduler.h"

namespace {

void appendStartupEvent() {
    Esp32BaseAppEventLog::Event event;
    event.level = Esp32BaseAppEventLog::LEVEL_INFO;
    event.source = "system";
    event.type = "startup";
    event.reason = "boot";
    event.object = "controller";
    event.code = 1;
    event.text = "irrigation firmware started";

    if (!Esp32BaseAppEventLog::append(event)) {
        ESP32BASE_LOG_W("irrigation", "startup_event_failed error=%s", Esp32BaseAppEventLog::lastError());
    }
}

void verifyDefaultConfig() {
    Irrigation::IrrigationConfig config;
    Irrigation::applyDefaultConfig(config);

    const char* error = nullptr;
    if (!Irrigation::validateConfig(config, &error)) {
        ESP32BASE_LOG_E("irrigation", "default_config_invalid error=%s", error != nullptr ? error : "unknown");
        return;
    }

    ESP32BASE_LOG_I("irrigation", "default_config_ok zones_enabled=%u plans_enabled=%u max_plans=%u max_start_times=%u",
                    Irrigation::enabledZoneCount(config),
                    Irrigation::enabledPlanCount(config),
                    Irrigation::kMaxPlans,
                    Irrigation::kMaxPlanStartTimes);
}

} // namespace

namespace IrrigationApp {

void setupBeforeBase() {
    Irrigation::IrrigationConfig config;
    Irrigation::applyDefaultConfig(config);
    Irrigation::BoardHardware::begin(config.valve, config.supply);
    Irrigation::IrrigationWeb::registerRoutes();

    Esp32BaseWeb::setDeviceName("Smart Irrigation");
    Esp32BaseWeb::setHomePath("/esp32base");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
}

void setupAfterBase() {
    verifyDefaultConfig();
    if (!Irrigation::ConfigStore::begin()) {
        ESP32BASE_LOG_W("irrigation", "config_store_defaulted error=%s", Irrigation::ConfigStore::lastError());
    }
    Irrigation::BoardHardware::configure(Irrigation::ConfigStore::config().valve,
                                         Irrigation::ConfigStore::config().supply);
    Irrigation::RunController::begin();
    Irrigation::CalibrationService::begin();
    Irrigation::Scheduler::begin();
    appendStartupEvent();
    ESP32BASE_LOG_I("irrigation", "base_ready valves=%u flow_pin=%u low_level_pin=%u pump_pin=%u",
                    IrrigationPins::kValveCount,
                    IrrigationPins::kFlowPulse,
                    IrrigationPins::kLowLevel,
                    IrrigationPins::kPumpDryOut);
}

void loop() {
    Irrigation::Scheduler::handle();
    Irrigation::RunController::handle(millis());
    Irrigation::CalibrationService::handle();
}

} // namespace IrrigationApp
