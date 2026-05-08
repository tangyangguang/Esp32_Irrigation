#pragma once

#include <stdint.h>

namespace IrrigationPins {
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
static constexpr uint8_t FactoryResetButton = 0;

static constexpr uint8_t Flow1 = 34;
static constexpr uint8_t Flow2 = 35;

static constexpr uint8_t MaxRoads = 2;
static constexpr uint8_t DefaultEnabledRoads = 1;
}
