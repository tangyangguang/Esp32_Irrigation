#ifndef IRRIGATION_TYPES_H
#define IRRIGATION_TYPES_H

#include <stdint.h>

namespace Irrigation {

constexpr uint8_t MAX_SOURCES = 4;
constexpr uint8_t MAX_ZONES_PER_SOURCE = 8;
constexpr uint8_t MAX_HISTORY_RECORDS = 16;

enum class StationOnlineState : uint8_t {
    Unknown = 0,
    Online = 1,
    Offline = 2,
};

struct StationSnapshot {
    StationOnlineState online = StationOnlineState::Unknown;
    uint32_t lastSeenMs = 0;
    uint32_t lastAttemptMs = 0;
    uint32_t uptimeSec = 0;
    uint8_t protocolVersion = 0;
    uint8_t valveCount = 0;
    uint8_t inputCount = 0;
    uint8_t stationState = 0;
    uint8_t taskState = 0;
    uint8_t faultCode = 0;
    uint16_t activeTaskId = 0;
    uint8_t activeZoneId = 0;
    uint8_t activeValveNo = 0;
    uint32_t elapsedSec = 0;
    uint32_t remainingSec = 0;
    uint32_t flowPulseCount = 0;
    uint16_t flowPulseRate = 0;
    uint8_t inputBits = 0;
    uint16_t voltageMv = 0;
    uint16_t lastResultSeq = 0;
    uint16_t lastResultTaskId = 0;
    uint8_t lastResultZoneId = 0;
    uint8_t lastResultCode = 0;
    uint8_t lastResultFaultCode = 0;
    uint32_t lastResultPlannedDurationSec = 0;
    uint32_t lastResultActualDurationSec = 0;
    uint32_t lastResultFlowPulseCount = 0;
    uint16_t lastResultStablePulseRate = 0;
    uint8_t lastErrorCode = 0;
    uint8_t failureCount = 0;
};

struct ManualTaskRequest {
    uint8_t sourceId = 0;
    uint8_t zoneId = 0;
    uint8_t stationAddr = 1;
    uint8_t valveNo = 1;
    uint32_t durationSec = 60;
    uint8_t mainValveEnabled = 0;
    uint8_t mainValveNo = 8;
    uint8_t pumpEnabled = 0;
    uint8_t mainValveOpenLeadSec = 1;
    uint8_t mainValveCloseDelaySec = 1;
    uint8_t pumpStartDelaySec = 1;
    uint8_t pumpStopLeadSec = 1;
    uint8_t flowMeterEnabled = 0;
    uint8_t flowInputNo = 1;
    uint8_t lowLevelEnabled = 0;
    uint8_t lowLevelInputNo = 2;
    uint8_t lowLevelActiveMode = 1;
    uint16_t lowLevelDebounceMs = 1000;
    uint16_t noFlowGraceSec = 0;
    uint16_t noFlowConfirmSec = 0;
    uint8_t lowFlowAction = 0;
    uint8_t overFlowAction = 0;
    uint16_t mainValveFullPowerMs = 300;
    uint16_t mainValveHoldDutyPermille = 700;
    uint16_t branchValveFullPowerMs = 300;
    uint16_t holdDutyPermille = 1000;
};

struct SystemConfig {
    uint32_t magic = 0x49525253UL;
    uint16_t version = 1;
    uint16_t maxRunMinutes = 120;
    uint8_t mainValveOpenLeadSec = 1;
    uint8_t mainValveCloseDelaySec = 1;
    uint8_t pumpStartDelaySec = 1;
    uint8_t pumpStopLeadSec = 1;
    uint32_t rs485Baud = 115200;
    uint8_t offlineFailureThreshold = 3;
};

struct SourceConfig {
    uint32_t magic = 0x49525241UL;
    uint16_t version = 1;
    uint8_t sourceId = 1;
    uint8_t enabled = 1;
    uint8_t stationAddr = 1;
    uint8_t mainValveEnabled = 0;
    uint8_t mainValveNo = 8;
    uint8_t pumpEnabled = 0;
    uint8_t flowMeterEnabled = 1;
    uint8_t flowInputNo = 1;
    uint8_t lowLevelEnabled = 0;
    uint8_t lowLevelInputNo = 2;
    uint8_t lowLevelActiveMode = 1;
    uint16_t lowLevelDebounceMs = 1000;
    uint16_t pulsesPerLiter = 0;
    uint16_t mainValveFullPowerMs = 300;
    uint16_t mainValveHoldDutyPermille = 700;
    uint16_t branchValveFullPowerMs = 300;
    uint16_t branchValveHoldDutyPermille = 700;
    char name[24] = "Source 1";
};

struct ZoneConfig {
    uint32_t magic = 0x4952525AUL;
    uint16_t version = 1;
    uint8_t sourceId = 1;
    uint8_t zoneId = 1;
    uint8_t enabled = 1;
    uint8_t valveNo = 1;
    uint16_t standardFlowPulseRate = 0;
    uint16_t lastManualDurationSec = 60;
    char name[24] = "Zone 1";
};

struct HistoryRecord {
    uint32_t seq = 0;
    uint32_t uptimeMs = 0;
    uint8_t type = 0;
    uint8_t sourceId = 0;
    uint8_t zoneId = 0;
    uint8_t resultCode = 0;
    uint16_t taskId = 0;
    uint32_t plannedSec = 0;
    uint32_t actualSec = 0;
    char message[48] = "";
};

}  // namespace Irrigation

#endif
