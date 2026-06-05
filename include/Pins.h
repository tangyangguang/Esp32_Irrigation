#pragma once

#include <stdint.h>

namespace IrrigationPins {
// Fixed board model for the new two-flow, six-zone irrigation controller.
// GPIO35 is input-only and requires an external pull-up when used with
// open-drain flow sensors such as YF-S201. GPIO32 is input-capable and also
// uses the board-provided flow sensor pull-up in this design.
static constexpr uint8_t Valve1 = 16;
static constexpr uint8_t Valve2 = 14;
static constexpr uint8_t Valve3 = 13;
static constexpr uint8_t Valve4 = 27;
static constexpr uint8_t Valve5 = 4;
static constexpr uint8_t Valve6 = 5;

static constexpr uint8_t StatusLed = 17;

static constexpr uint8_t I2cSda = 21;
static constexpr uint8_t I2cScl = 22;

static constexpr uint8_t ButtonPrevZone = 18;
static constexpr uint8_t ButtonNextZone = 19;
static constexpr uint8_t ButtonSelect = 23;
static constexpr uint8_t ButtonStopAll = 26;
static constexpr uint8_t ButtonInfo = 25;
// GPIO0 is also the ESP32 boot strap pin and is often driven by USB serial
// auto-program circuits. Factory reset only reacts to a long press after
// firmware is running; holding GPIO0 during reset still enters download mode.
static constexpr uint8_t FactoryResetButton = 0;

static constexpr uint8_t Flow1 = 32;
static constexpr uint8_t Flow2 = 35;

static constexpr uint8_t MaxFlowMeters = 2;
static constexpr uint8_t MaxZones = 6;
// Bit mask, not zone count: bit0 enables zone 1 ... bit5 enables zone 6.
static constexpr uint8_t DefaultZoneEnabledMask = 0x03;

static constexpr uint32_t ValvePwmFrequency = 1000;
static constexpr uint8_t ValvePullInDutyPercent = 100;
static constexpr uint8_t ValveHoldDutyPercent = 70;
static constexpr uint32_t ValvePullInMs = 5000UL;
}
