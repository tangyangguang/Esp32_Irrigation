#pragma once

#include <stdint.h>

namespace IrrigationPins {
// Current hardware target is the new 1-2 road controller, not the older
// 4-road EC11 design in old-docs/02. GPIO34/35 are input-only pins and require
// external pull-ups when used with open-drain flow sensors such as YF-S201.
static constexpr uint8_t Valve1 = 13;
static constexpr uint8_t Valve2 = 14;

static constexpr uint8_t StatusLed = 17;

static constexpr uint8_t I2cSda = 21;
static constexpr uint8_t I2cScl = 22;

static constexpr uint8_t StartOkButton = 23;
static constexpr uint8_t StopAllButton = 26;
static constexpr uint8_t MenuBackButton = 25;
static constexpr uint8_t LockButton = 33;
static constexpr uint8_t Road1UpButton = 18;
static constexpr uint8_t Road2DownButton = 19;
// GPIO0 is also the ESP32 boot strap pin. It must not be held during reset or
// power-on; long press is only valid after the firmware is already running.
static constexpr uint8_t FactoryResetButton = 0;

static constexpr uint8_t Flow1 = 34;
static constexpr uint8_t Flow2 = 35;

static constexpr uint8_t MaxRoads = 2;
static constexpr uint8_t DefaultRoadEnabledMask = 0x01;
}
