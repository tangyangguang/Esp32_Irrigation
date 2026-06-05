#pragma once

#include "domain/ZoneTypes.h"

namespace ZoneConfigStore {

void begin();
bool clear();
const Irrigation::ZoneConfig& get(uint8_t zoneId);
bool set(uint8_t zoneId, const Irrigation::ZoneConfig& config);
bool validate(const Irrigation::ZoneConfig& config);
bool validateName(const char* name);
bool validateBaseline(const Irrigation::ZoneFlowBaselineProfile& profile);
Irrigation::ZoneFlowBaselineProfile defaultBaseline();
bool schemaResetDetected();

}
