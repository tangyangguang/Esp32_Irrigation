#pragma once

#include "domain/ZoneTypes.h"

namespace FlowConfigStore {

void begin();
bool clear();
const Irrigation::FlowMeterConfig& get(uint8_t flowId);
bool set(uint8_t flowId, const Irrigation::FlowMeterConfig& config);
bool validate(const Irrigation::FlowMeterConfig& config);
bool validateCalibration(const Irrigation::FlowMeterCalibrationProfile& profile);
Irrigation::FlowMeterCalibrationProfile defaultCalibration();
uint32_t estimateMilliliters(const Irrigation::FlowMeterCalibrationProfile& profile,
                             uint32_t pulses,
                             uint32_t durationMs);
bool schemaResetDetected();

}
