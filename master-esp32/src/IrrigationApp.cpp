#include "IrrigationApp.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "BoardPins.h"

namespace {

const Esp32BaseAppConfig::EnumOption LOW_LEVEL_CONTACT_OPTIONS[] = {
    {"normally_open", "Normally open"},
    {"normally_closed", "Normally closed"},
};

void setOutputsSafe() {
    pinMode(IrrigationPins::kDriverShutdown, OUTPUT);
    digitalWrite(IrrigationPins::kDriverShutdown, HIGH);

    for (uint8_t i = 0; i < IrrigationPins::kValveCount; ++i) {
        pinMode(IrrigationPins::kValvePwm[i], OUTPUT);
        digitalWrite(IrrigationPins::kValvePwm[i], LOW);
    }

    pinMode(IrrigationPins::kPumpDryOut, OUTPUT);
    digitalWrite(IrrigationPins::kPumpDryOut, LOW);
}

void configureInputs() {
    pinMode(IrrigationPins::kFlowPulse, INPUT);
    pinMode(IrrigationPins::kLowLevel, INPUT_PULLUP);
    pinMode(IrrigationPins::kRtcInterrupt, INPUT_PULLUP);
    pinMode(IrrigationPins::kButton1, INPUT);
    pinMode(IrrigationPins::kButton2, INPUT);
    pinMode(IrrigationPins::kButton3, INPUT);
    pinMode(IrrigationPins::kButton4, INPUT);
}

void registerAppConfig() {
    Esp32BaseAppConfig::setTitle("Irrigation Config");

    Esp32BaseAppConfig::addGroup({"water", "Water Source"});
    Esp32BaseAppConfig::addGroup({"flow", "Flow Meter"});
    Esp32BaseAppConfig::addGroup({"valves", "Valves"});
    Esp32BaseAppConfig::addGroup({"schedule", "Schedule"});
    Esp32BaseAppConfig::addGroup({"safety", "Safety"});

    Esp32BaseAppConfig::addBool({"water", "irrigation", "pump_enabled", "Self-priming pump enabled", false,
                                 "Enables the dry-contact pump control signal.", true, nullptr});
    Esp32BaseAppConfig::addBool({"safety", "irrigation", "low_level_enabled", "Low-level sensor enabled", false,
                                 "Only protects the pump when the pump is enabled.", true, nullptr});
    Esp32BaseAppConfig::addEnum({"safety", "irrigation", "low_level_contact", "Low-level contact type",
                                 "normally_closed", LOW_LEVEL_CONTACT_OPTIONS, 2,
                                 "Dry-contact input polarity.", true, nullptr});
    Esp32BaseAppConfig::addInt({"flow", "irrigation", "flow_pulses_per_liter", "Flow pulses per liter",
                                450, 1, 100000, 1, "p/L",
                                "Calibration value for the mandatory flow meter.", false, nullptr});
    Esp32BaseAppConfig::addInt({"valves", "irrigation", "valve_pull_ms", "Valve pull-in time",
                                300, 50, 3000, 50, "ms",
                                "Full-power valve pull-in duration before PWM hold.", true, nullptr});
    Esp32BaseAppConfig::addInt({"valves", "irrigation", "valve_hold_percent", "Valve hold PWM",
                                60, 10, 100, 1, "%",
                                "PWM duty after valve pull-in.", true, nullptr});
    Esp32BaseAppConfig::addInt({"water", "irrigation", "pump_start_delay_ms", "Pump start delay",
                                500, 0, 10000, 100, "ms",
                                "Delay after pump control signal before watering logic continues.", false, nullptr});
    Esp32BaseAppConfig::addInt({"safety", "irrigation", "no_flow_timeout_sec", "No-flow timeout",
                                30, 5, 600, 5, "s",
                                "Stops the current batch when no flow is detected while a valve is open.", false, nullptr});
    Esp32BaseAppConfig::addInt({"schedule", "irrigation", "max_zone_duration_min", "Max waterway duration",
                                120, 1, 360, 1, "min",
                                "Upper bound used by UI and schedule validation.", false, nullptr});
}

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

} // namespace

namespace IrrigationApp {

void setupBeforeBase() {
    setOutputsSafe();
    configureInputs();
    registerAppConfig();

    Esp32BaseWeb::setDeviceName("Smart Irrigation");
    Esp32BaseWeb::setHomePath("/esp32base");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
}

void setupAfterBase() {
    appendStartupEvent();
    ESP32BASE_LOG_I("irrigation", "base_ready valves=%u flow_pin=%u low_level_pin=%u pump_pin=%u",
                    IrrigationPins::kValveCount,
                    IrrigationPins::kFlowPulse,
                    IrrigationPins::kLowLevel,
                    IrrigationPins::kPumpDryOut);
}

void loop() {
    // Business state machines will be called here. Keep this loop non-blocking.
}

} // namespace IrrigationApp
