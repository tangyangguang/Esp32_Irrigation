#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

constexpr uint16_t kConfigVersion = 2;

void applyDefaultConfig(IrrigationConfig& config);

bool isValidZoneId(uint8_t zoneId);
bool isValidMinuteOfDay(uint16_t minuteOfDay);
bool isValidHoldPercent(uint8_t holdPercent);

uint8_t zoneIndexFromId(uint8_t zoneId);
uint8_t enabledZoneCount(const IrrigationConfig& config);
uint8_t usedPlanCount(const IrrigationConfig& config);
uint8_t enabledPlanCount(const IrrigationConfig& config);
uint8_t enabledStartTimeCount(const WateringPlan& plan);

bool planHasEffectiveStep(const IrrigationConfig& config, const WateringPlan& plan);
bool validateConfig(const IrrigationConfig& config, const char** error);

const char* runReasonToString(RunReason reason);

} // namespace Irrigation
