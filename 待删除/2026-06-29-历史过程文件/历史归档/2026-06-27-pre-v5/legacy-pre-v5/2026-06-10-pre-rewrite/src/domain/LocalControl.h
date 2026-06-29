#pragma once

#include <stdint.h>

namespace LocalControl {

void begin();
void handle();
uint8_t selectedZoneId();
const char* pendingActionName();

}
