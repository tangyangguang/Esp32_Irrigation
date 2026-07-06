#pragma once

#include <Arduino.h>

namespace IrrigationPins {

constexpr uint8_t kValveCount = 6;

constexpr uint8_t kValvePwm[kValveCount] = {
    14, // PWM1 / waterway 1
    27, // PWM2 / waterway 2
    26, // PWM3 / waterway 3
    25, // PWM4 / waterway 4
    33, // PWM5 / waterway 5
    32  // PWM6 / waterway 6
};

constexpr uint8_t kDriverShutdown = 19; // DRV_SD, HIGH disables valve driver.
constexpr uint8_t kFlowPulse = 17;      // PULSE_IN1, mandatory flow meter input.
constexpr uint8_t kLowLevel = 16;       // LOW_IN2, optional dry-contact input.
constexpr uint8_t kPumpDryOut = 18;     // DRY_OUT1, optional dry-contact control signal.
constexpr uint8_t kRtcInterrupt = 4;    // IO4_INT, DS3231 INT/SQW.
constexpr uint8_t kI2cSda = 21;
constexpr uint8_t kI2cScl = 22;

constexpr uint8_t kButton1 = 36;
constexpr uint8_t kButton2 = 39;
constexpr uint8_t kButton3 = 34;
constexpr uint8_t kButton4 = 35;

} // namespace IrrigationPins
