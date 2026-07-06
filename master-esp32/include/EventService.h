#pragma once

#include <stdint.h>

#include <Esp32Base.h>

namespace Irrigation {

class EventService {
public:
    static bool append(Esp32BaseAppEventLog::Level level,
                       const char* source,
                       const char* type,
                       const char* reason,
                       const char* object,
                       uint16_t code = 0,
                       int32_t value1 = 0,
                       int32_t value2 = 0,
                       int32_t value3 = 0,
                       uint8_t valueMask = 0);

    static bool planTriggered(uint8_t planId, uint8_t startIndex);
    static bool planSkipped(uint8_t planId, uint8_t startIndex, const char* reason);
};

} // namespace Irrigation

