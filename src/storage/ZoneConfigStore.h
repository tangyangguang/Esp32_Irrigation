#pragma once

#include "domain/ZoneTypes.h"

namespace ZoneConfigStore {

void begin();
bool clear();
const Irrigation::ZoneConfig& get(uint8_t zoneId);
bool set(uint8_t zoneId, const Irrigation::ZoneConfig& config);
bool validate(const Irrigation::ZoneConfig& config);
bool validateName(const char* name);
uint32_t estimateMilliliters(const Irrigation::ZoneConfigSnapshot& snapshot, uint32_t pulses);

}
