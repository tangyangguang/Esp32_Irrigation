#include "IrrigationApp.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>

#include "BoardHardware.h"
#include "BoardPins.h"

namespace {

constexpr const char* kFirmwareName = "esp32-irrigation";
constexpr const char* kFirmwareVersion = "0.1.0";

}  // namespace

IrrigationApp::IrrigationApp() : wateringController_(BoardHardware::instance()) {}

IrrigationApp& IrrigationApp::instance() {
    static IrrigationApp app;
    return app;
}

bool IrrigationApp::begin() {
    if (started_) {
        return baseReady_;
    }
    started_ = true;

    // This must remain the first hardware operation of application startup.
    BoardHardware& hardware = BoardHardware::instance();
    const bool hardwareReady = hardware.begin(20000);

    Serial.begin(115200);
    if (!hardwareReady) {
        hardware.safeShutdown();
        return false;
    }

    Wire.begin(BoardPins::kI2cSdaPin, BoardPins::kI2cSclPin);
    Esp32Base::setFirmwareInfo(kFirmwareName, kFirmwareVersion);
    Esp32BaseRtc::configure(Wire);

    baseReady_ = Esp32Base::begin();
    if (!baseReady_) {
        hardware.safeShutdown();
        return false;
    }

    if (!configStore_.begin()) {
        hardware.safeShutdown();
        return false;
    }

    const IrrigationConfig* config = configStore_.current();
    if (!config ||
        (config->valveDrive.pwmFrequencyHz != 20000U &&
         !hardware.configureValvePwmFrequency(config->valveDrive.pwmFrequencyHz))) {
        hardware.safeShutdown();
        return false;
    }

    // Watering commands become ready after the controller layer is implemented.
    businessReady_ = false;
    return true;
}

void IrrigationApp::handle() {
    if (!started_ || !baseReady_) {
        BoardHardware::instance().safeShutdown();
        return;
    }

    advanceBusiness();
    Esp32Base::handle();
}

bool IrrigationApp::baseReady() const {
    return baseReady_;
}

bool IrrigationApp::businessReady() const {
    return businessReady_;
}

void IrrigationApp::advanceBusiness() {
    wateringController_.handle(millis());
}
