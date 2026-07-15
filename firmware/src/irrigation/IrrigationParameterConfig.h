#pragma once

#include <Esp32Base.h>

#include "IrrigationTypes.h"

class IrrigationParameterConfig {
public:
    using SavedCallback = void (*)(void* user);

    static bool registerFields(SavedCallback callback, void* user);
    static bool applyStored(IrrigationConfig& config);
    static bool saveFlowCoefficient(uint32_t pulsesPerLiterX100);

private:
    static bool validatePage(char* error, size_t errorLength);
    static void handleSaved(const Esp32BaseAppConfig::SaveSummary& summary);
};
