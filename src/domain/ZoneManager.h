#pragma once

#include <stdint.h>

#include "domain/Zone.h"
#include "domain/ZoneScheduler.h"
#include "domain/ZoneTypes.h"

namespace ZoneManager {

void begin();
void handle();
bool reloadZone(uint8_t zoneId);
bool startManual(uint8_t zoneId, uint32_t durationSec, Irrigation::StartSource source);
bool startPlan(uint8_t zoneId,
               uint32_t planId,
               uint8_t planSlot,
               const char* planName,
               uint32_t durationSec,
               uint32_t trustedEpoch,
               uint32_t nowMs);
bool stopZone(uint8_t zoneId, Irrigation::StopSource source, Irrigation::TaskResult result = Irrigation::TaskResult::USER_STOPPED);
uint8_t stopAll(Irrigation::StopSource source, Irrigation::TaskResult result = Irrigation::TaskResult::USER_STOPPED);
bool clearError(uint8_t zoneId);
bool clearAllErrors();
bool isBusy();
bool isZoneBusy(uint8_t zoneId);
bool isFlowBusy(uint8_t flowId);
bool canStartZoneNow(uint8_t zoneId);
const char* blockedReason(uint8_t zoneId);
bool leakAlertActive();
Irrigation::ZoneStatus status(uint8_t zoneId);
const Irrigation::ZoneConfig& config(uint8_t zoneId);
uint32_t trustedEpoch();
Zone& zone(uint8_t zoneId);

}
