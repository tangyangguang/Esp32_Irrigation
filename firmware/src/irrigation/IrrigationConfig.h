#pragma once

#include "IrrigationTypes.h"

class IrrigationConfigRules {
public:
    static IrrigationConfig createDefault();
    static bool validate(const IrrigationConfig& config);
    static bool parsePulsesPerLiter(const char* text, uint32_t& valueX100);
    static bool formatPulsesPerLiter(uint32_t valueX100, char* out, std::size_t outSize);
    static bool parseLitersPerMinute(const char* text, uint32_t& valueMlPerMinute);
    static bool parseWaterVolumeLiters(const char* text, uint32_t& valueMl);
    static bool formatLitersPerMinute(uint32_t valueMlPerMinute,
                                      char* out,
                                      std::size_t outSize);
};
