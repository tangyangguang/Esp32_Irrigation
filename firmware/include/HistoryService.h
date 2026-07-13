#pragma once

#include <stddef.h>

#include "IrrigationTypes.h"

namespace Irrigation {

class HistoryService {
public:
    static bool appendRun(const WateringRun& run);
    static bool readRecent(char* out, size_t len);
    static bool readPage(uint32_t page, uint32_t perPage, char* out, size_t len, uint32_t& totalOut);
    static const char* lastError();
    static const char* path();

private:
    static void setLastError(const char* error);
};

} // namespace Irrigation
