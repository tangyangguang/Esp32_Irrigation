#pragma once

#include <stddef.h>

#include "IrrigationTypes.h"

namespace Irrigation {

class HistoryService {
public:
    static bool appendRun(const WateringRun& run);
    static bool readRecent(char* out, size_t len);
    static const char* lastError();
    static const char* path();

private:
    static void setLastError(const char* error);
};

} // namespace Irrigation
