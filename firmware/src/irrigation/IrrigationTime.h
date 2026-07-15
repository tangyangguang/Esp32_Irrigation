#pragma once

#include <cstdint>

namespace IrrigationTime {

bool parseLocalDateTimeUtc8(const char* text, uint32_t& epochSec);
bool resumeAfterHours(uint32_t currentEpoch, uint32_t hours, uint32_t& resumeEpoch);

}  // namespace IrrigationTime
