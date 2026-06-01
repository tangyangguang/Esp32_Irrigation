#pragma once

#include "domain/ZoneTypes.h"

namespace ZoneConfigStore {

void begin();
bool clear();
const Irrigation::ZoneConfig& get(uint8_t zoneId);
bool set(uint8_t zoneId, const Irrigation::ZoneConfig& config);
bool validate(const Irrigation::ZoneConfig& config);
bool validateName(const char* name);
bool validateFlowParameters(const Irrigation::FlowParameters& params);
Irrigation::FlowParameters normalizeFlowParameters(Irrigation::FlowParameters params);
bool flowParametersEqual(const Irrigation::FlowParameters& a, const Irrigation::FlowParameters& b);
bool saveManualCandidate(uint8_t zoneId, Irrigation::FlowParameters params);
bool saveCalibrationCandidate(uint8_t zoneId, Irrigation::FlowParameters params);
bool copyCurrentToCandidate(uint8_t sourceZoneId, uint8_t targetZoneId);
bool applyCandidate(uint8_t zoneId, Irrigation::FlowParameters* oldParams, Irrigation::FlowParameters* newParams);
bool restorePrevious(uint8_t zoneId, Irrigation::FlowParameters* oldParams, Irrigation::FlowParameters* newParams);
uint32_t estimateMilliliters(const Irrigation::ZoneConfigSnapshot& snapshot, uint32_t pulses);
uint32_t estimateMilliliters(const Irrigation::FlowParameters& params, uint32_t pulses);

}
