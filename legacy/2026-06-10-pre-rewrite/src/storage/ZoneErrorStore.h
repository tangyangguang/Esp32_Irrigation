#pragma once

#include "domain/ZoneTypes.h"

namespace ZoneErrorStore {

struct ZoneError {
    bool active;
    Irrigation::ZoneErrorCode errorCode;
    uint32_t occurredEpoch;
    uint32_t occurredUptimeMs;
    Irrigation::StopSource source;
    Irrigation::TaskResult result;
    uint8_t reserved[2];
};

void begin();
bool clear();
bool leakAlertActive();
const ZoneError& get(uint8_t zoneId);
bool setError(uint8_t zoneId, Irrigation::ZoneErrorCode code, Irrigation::StopSource source, Irrigation::TaskResult result);
bool clearError(uint8_t zoneId);
bool clearAllErrors();

}
