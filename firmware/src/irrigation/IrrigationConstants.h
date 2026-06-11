#pragma once

#include <stdint.h>

namespace irrigation {

constexpr uint8_t kMaxZones = 6;
constexpr uint8_t kMaxPlanGroups = 4;
constexpr uint8_t kMaxStartTimesPerPlan = 4;

constexpr uint16_t kMinRunDurationMin = 1;
constexpr uint16_t kMaxRunDurationMin = 360;
constexpr uint16_t kDefaultManualDurationMin = 5;
constexpr uint16_t kDefaultQueuedPlanMaxDelayMin = 60;

constexpr uint16_t kDefaultFlowSampleWindowSec = 5;
constexpr uint16_t kDefaultFlowUpdateIntervalMs = 1000;
constexpr uint16_t kDefaultFirstPulseTimeoutSec = 15;
constexpr uint16_t kDefaultRunningNoPulseTimeoutSec = 15;
constexpr uint16_t kDefaultFlowStabilizeSec = 10;
constexpr uint16_t kDefaultMaintenanceMaxDurationSec = 600;
constexpr uint16_t kDefaultIdleLeakConfirmSec = 30;

constexpr uint16_t kDefaultValvePullInMs = 3000;
constexpr uint8_t kDefaultValveHoldDutyPercent = 60;
constexpr uint32_t kDefaultValvePwmFrequencyHz = 20000;

constexpr uint8_t kDefaultLowFlowPercent = 60;
constexpr uint8_t kDefaultHighFlowPercent = 160;
constexpr uint16_t kDefaultFlowFaultConfirmSec = 10;

constexpr const char* kNamespaceSystem = "irr_sys";
constexpr const char* kNamespaceZone = "irr_zone";
constexpr const char* kNamespacePlan = "irr_plan";
constexpr const char* kNamespaceFlow = "irr_flow";
constexpr const char* kNamespaceFault = "irr_fault";
constexpr const char* kNamespaceStore = "irr_store";

constexpr const char* kRecordsPath = "/irrigation/records.bin";

}  // namespace irrigation
