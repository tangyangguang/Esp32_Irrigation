#pragma once

#include "domain/ZoneTypes.h"

namespace SystemConfigStore {

void begin();
const Irrigation::SystemConfig& current();
bool set(const Irrigation::SystemConfig& config);
bool clear();
bool validate(const Irrigation::SystemConfig& config);
uint32_t maxWateringDurationSec();
uint16_t scheduleGraceSec();

}
