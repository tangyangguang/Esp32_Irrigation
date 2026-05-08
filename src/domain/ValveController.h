#pragma once

#include <stdint.h>

namespace ValveController {

static constexpr uint8_t Road1 = 1;
static constexpr uint8_t Road2 = 2;

void begin();
void handle();

bool setRoad(uint8_t road, bool open, const char* reason);
bool off(uint8_t road, const char* reason);
void allOff(const char* reason);
bool isOpen(uint8_t road);

}
