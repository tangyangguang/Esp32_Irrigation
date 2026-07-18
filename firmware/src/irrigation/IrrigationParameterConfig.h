#pragma once

#include <Esp32Base.h>

#include "IrrigationTypes.h"

class IrrigationParameterConfig {
public:
    using SavedCallback = void (*)(void* user);
    using SaveAllowedCallback = bool (*)(void* user);

    static bool registerFields(SavedCallback callback,
                               SaveAllowedCallback saveAllowed,
                               void* user);
    static bool applyStored(IrrigationConfig& config);
    static bool saveFlowCalibrationParameters(const FlowMeterConfig& parameters);

private:
    static bool validatePage(char* error, size_t errorLength);
    static void handleSaved(const Esp32BaseAppConfig::SaveSummary& summary);
};
