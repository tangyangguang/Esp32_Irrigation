#pragma once

#include <Esp32Base.h>

#include "IrrigationTypes.h"

class IrrigationParameterConfig {
public:
    using SavedCallback = void (*)(void* user);
    using ValidateCallback = bool (*)(const IrrigationParameters& proposed,
                                      char* error,
                                      size_t errorLength,
                                      void* user);

    static bool registerFields(SavedCallback callback,
                               ValidateCallback validateCallback,
                               void* user);
    static bool applyStored(IrrigationParameters& config);

private:
    static bool validatePage(char* error, size_t errorLength);
    static void handleSaved(const Esp32BaseAppConfig::SaveSummary& summary);
};
