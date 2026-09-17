#pragma once

#include <Esp32Base.h>

#include "IrrigationTypes.h"

class IrrigationParameterConfig {
public:
    static constexpr const char* kNamespace = "irr_params";

    using SavedCallback = void (*)(void* user);
    using ValidateCallback = bool (*)(const IrrigationParameters& proposed,
                                      char* error,
                                      size_t errorLength,
                                      void* user);

    static bool registerFields(SavedCallback callback,
                               ValidateCallback validateCallback,
                               void* user);
    static bool applyStored(IrrigationParameters& config);
    // Remote single-field maintenance. field is the contract name (e.g.
    // meter.pulsesPerLiterX100). The value is range-checked against the
    // registered descriptor before the single NVS field is written.
    // Build and range-validate the parameters after applying one remote
    // field, without touching NVS. The app adds the plan cross-check.
    static bool buildRemoteFieldCandidate(const char* field,
                                          bool valueIsInteger,
                                          int32_t integerValue,
                                          bool valueIsBoolean,
                                          bool booleanValue,
                                          const char* textValue,
                                          IrrigationParameters& candidate);
    static bool applyRemoteField(const char* field,
                                 bool valueIsInteger,
                                 int32_t integerValue,
                                 bool valueIsBoolean,
                                 bool booleanValue,
                                 const char* textValue);
    // Write back one field from the supplied parameters (rollback support).
    static bool writeStoredField(const char* field,
                                 const IrrigationParameters& parameters);
    // 1-based index in the 22-field directory; 0 when unknown.
    static uint8_t fieldIndex(const char* field);

private:
    static bool validatePage(char* error, size_t errorLength);
    static void handleSaved(const Esp32BaseAppConfig::SaveSummary& summary);
};
