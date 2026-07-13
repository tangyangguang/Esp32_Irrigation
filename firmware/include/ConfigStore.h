#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

class ConfigStore {
public:
    static bool begin();
    static bool load();
    static bool save(const IrrigationConfig& config);

    static const IrrigationConfig& config();
    static IrrigationConfig& mutableConfig();
    static bool loadedFromDefaults();
    static bool configValid();
    static const char* lastError();
    static const char* path();

private:
    static bool loadFromFile(IrrigationConfig& out);
};

} // namespace Irrigation

