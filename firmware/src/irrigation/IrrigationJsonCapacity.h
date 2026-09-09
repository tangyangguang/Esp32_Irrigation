#pragma once

#include <ArduinoJson.h>
#include "IrrigationTypes.h"

// Pools are allocated only during parsing/serialization. Keep whole documents off
// the loop stack. Size by the current full model; input strings have a separate
// upper bound so long valid names do not consume the structural budget.
namespace IrrigationJsonCapacity {
constexpr size_t plans = JSON_ARRAY_SIZE(kWateringPlanCount) +
    kWateringPlanCount * (JSON_OBJECT_SIZE(8) + JSON_ARRAY_SIZE(kPlanStartTimeCount) +
        JSON_ARRAY_SIZE(BoardPins::kZoneCount) + BoardPins::kZoneCount * JSON_OBJECT_SIZE(2));
constexpr size_t command = JSON_OBJECT_SIZE(6) + JSON_OBJECT_SIZE(2) + plans + 4096;
constexpr size_t state = JSON_OBJECT_SIZE(16) + plans + 2048;
constexpr size_t record = JSON_OBJECT_SIZE(8) + JSON_OBJECT_SIZE(32) +
    JSON_ARRAY_SIZE(BoardPins::kZoneCount) + BoardPins::kZoneCount * JSON_OBJECT_SIZE(12) + 1024;
constexpr size_t config = JSON_OBJECT_SIZE(9 + 4 + 3 + 1 + 10 + 2) +
    JSON_ARRAY_SIZE(BoardPins::kZoneCount) + BoardPins::kZoneCount * JSON_OBJECT_SIZE(4) +
    JSON_ARRAY_SIZE(kWateringPlanCount) + kWateringPlanCount *
        (JSON_OBJECT_SIZE(6) + JSON_ARRAY_SIZE(kPlanStartTimeCount) + JSON_ARRAY_SIZE(BoardPins::kZoneCount)) +
    2048; // all unique field names and 14 names of up to 63 UTF-8 bytes
constexpr size_t ack = JSON_OBJECT_SIZE(4) + 512;
}
