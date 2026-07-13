#pragma once

#include "IrrigationTypes.h"

class IrrigationConfigRules {
public:
    static IrrigationConfig createDefault();
    static bool validate(const IrrigationConfig& config);
};
