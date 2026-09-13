#pragma once

#include <cstdint>

namespace IrrigationWebAssets {

enum class Asset : uint8_t {
    HomeStyle,
    PlansStyle,
    HomeScript,
    ActiveTaskStyle,
    ActiveTaskScript,
    PauseScript,
    RecordsStyle,
    EventsStyle,
    LearningStyle,
    LearningScript,
};

bool registerAssets();
bool send(Asset asset);

}  // namespace IrrigationWebAssets
