#pragma once

#include <cstdint>

namespace IrrigationWebAssets {

enum class Asset : uint8_t {
    HomeStyle,
    HomeScript,
    ActiveTaskStyle,
    ActiveTaskScript,
    PauseScript,
    SingleOutputForm,
    LearningStyle,
    LearningScript,
    RecordsStyle,
    EventsStyle,
};

bool send(Asset asset);

}  // namespace IrrigationWebAssets
