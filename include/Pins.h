#pragma once

#include <stdint.h>

namespace IrrigationPins {
// Fixed 4-road board configuration. GPIO35/36/39 are input-only pins and
// require external pull-ups when used with open-drain flow sensors such as
// YF-S201. GPIO32 is input-capable and also uses the board-provided flow
// sensor pull-up in this design.
static constexpr uint8_t Valve1 = 16;
static constexpr uint8_t Valve2 = 14;
static constexpr uint8_t Valve3 = 13;
static constexpr uint8_t Valve4 = 27;

static constexpr uint8_t StatusLed = 17;

static constexpr uint8_t I2cSda = 21;
static constexpr uint8_t I2cScl = 22;

static constexpr uint8_t StartOkButton = 23;
static constexpr uint8_t StopAllButton = 26;
static constexpr uint8_t MenuBackButton = 25;
static constexpr uint8_t LockButton = 33;
static constexpr uint8_t Road1UpButton = 18;
static constexpr uint8_t Road2DownButton = 19;
// GPIO0 is also the ESP32 boot strap pin and is often driven by USB serial
// auto-program circuits. Factory reset only reacts to a long press after
// firmware is running; holding GPIO0 during reset still enters download mode.
static constexpr uint8_t FactoryResetButton = 0;

static constexpr uint8_t Flow1 = 32;
static constexpr uint8_t Flow2 = 35;
static constexpr uint8_t Flow3 = 36;
static constexpr uint8_t Flow4 = 39;

static constexpr uint8_t MaxRoads = 4;
// Bit mask, not road count: bit0 enables road 1 ... bit3 enables road 4.
static constexpr uint8_t DefaultRoadEnabledMask = 0x03;

static constexpr uint32_t ValvePwmFrequency = 1000;
static constexpr uint8_t ValvePullInDutyPercent = 100;
static constexpr uint8_t ValveHoldDutyPercent = 70;
static constexpr uint32_t ValvePullInMs = 5000UL;
}
