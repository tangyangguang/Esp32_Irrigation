#pragma once

#include <cstddef>
#include <string>

#include "IrrigationTypes.h"

class IrrigationConfigJson {
public:
    static bool encode(const IrrigationConfig& config, std::string& json);
    static bool decode(const char* json, std::size_t length, IrrigationConfig& config);
};
