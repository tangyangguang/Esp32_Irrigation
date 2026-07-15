#pragma once

#include <cstdint>

namespace IrrigationTime {

bool parseLocalDateTimeUtc8(const char* text, uint32_t& epochSec);

}  // namespace IrrigationTime
