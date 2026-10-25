#pragma once

#include <ArduinoJson.h>
#include "IrrigationTypes.h"

// Pool is allocated only during config parsing/serialization. Keep documents off
// the loop stack. Size by the current full model; input strings have a separate
// upper bound so long valid names do not consume the structural budget.
namespace IrrigationJsonCapacity {
constexpr size_t config = JSON_OBJECT_SIZE(4) +
    JSON_ARRAY_SIZE(BoardPins::kZoneCount) + BoardPins::kZoneCount * JSON_OBJECT_SIZE(4) +
    JSON_ARRAY_SIZE(kWateringPlanCount) + kWateringPlanCount *
        (JSON_OBJECT_SIZE(6) + JSON_ARRAY_SIZE(kPlanStartTimeCount) + JSON_ARRAY_SIZE(BoardPins::kZoneCount)) +
    2048; // all unique field names and 14 names of up to 63 UTF-8 bytes
}
