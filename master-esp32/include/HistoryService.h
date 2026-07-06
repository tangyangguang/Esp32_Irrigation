#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

class HistoryService {
public:
    static bool appendRun(const WateringRun& run);
    static const char* lastError();
    static const char* path();

private:
    static void setLastError(const char* error);
};

} // namespace Irrigation

