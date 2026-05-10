#pragma once

#include <stdint.h>

namespace FlowMeter {

void begin();
void handle();
uint32_t pulseCount(uint8_t road);
uint32_t pulseRatePerMinuteX1000(uint8_t road);

}
