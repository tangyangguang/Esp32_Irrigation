#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace BoardPins {

constexpr std::size_t kZoneCount = 6;
constexpr std::array<uint8_t, kZoneCount> kValvePins = {33, 32, 26, 25, 14, 27};
constexpr uint8_t kValveDriverShutdownPin = 19;
constexpr uint8_t kPumpSignalPin = 18;
constexpr uint8_t kFlowMeterPin = 17;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;

constexpr bool isValidZoneId(uint8_t zoneId) {
    return zoneId >= 1 && zoneId <= kZoneCount;
}

constexpr std::size_t zoneIndex(uint8_t zoneId) {
    return static_cast<std::size_t>(zoneId - 1);
}

}  // namespace BoardPins
