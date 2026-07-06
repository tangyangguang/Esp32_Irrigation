#pragma once

#include <stdint.h>

namespace Irrigation {

class Scheduler {
public:
    static void begin();
    static void handle();

    static void markTriggered(uint8_t planIndex, uint8_t startIndex, uint32_t dayKey);
    static bool alreadyTriggered(uint8_t planIndex, uint8_t startIndex, uint32_t dayKey);
};

} // namespace Irrigation
