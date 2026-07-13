#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

class ZoneService {
public:
    static const ZoneConfig* find(uint8_t zoneId);
    static bool snapshot(uint8_t zoneId, ZoneSnapshot& out);
    static bool setEnabled(uint8_t zoneId, bool enabled);
    static bool saveZone(const ZoneConfig& zone);
    static const char* lastError();

private:
    static void setLastError(const char* error);
};

} // namespace Irrigation

