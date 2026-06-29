#pragma once

#include <stdint.h>

namespace ValveController {

void begin();
void handle();

bool setZone(uint8_t zoneId, bool open, const char* reason);
bool off(uint8_t zoneId, const char* reason);
void allOff(const char* reason);
bool isOpen(uint8_t zoneId);

}
