#pragma once

#include "IrrigationTypes.h"

class IrrigationConfigDefaults {
public:
    static IrrigationConfig create();
    static bool validate(const IrrigationConfig& config);
};
