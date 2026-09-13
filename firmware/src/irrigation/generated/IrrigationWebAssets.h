#pragma once

#include <cstdint>

namespace IrrigationWebAssets {

enum class Asset : uint8_t {
    HomeStyle,
    HomeScript,
    ActiveTaskStyle,
    ActiveTaskScript,
    LearningStyle,
    LearningScript,
};

bool registerAssets();
bool send(Asset asset);

}  // namespace IrrigationWebAssets
