#pragma once

#include <stdint.h>

namespace FlowAlertStore {

struct FlowAlert {
    bool idleLeakActive;
    uint32_t observedPulses;
    uint16_t pulseThreshold;
    uint16_t windowSec;
    uint32_t occurredEpoch;
    uint32_t occurredUptimeMs;
};

void begin();
bool clear();
bool anyIdleLeakActive();
bool idleLeakActive(uint8_t flowId);
const FlowAlert& get(uint8_t flowId);
bool setIdleLeak(uint8_t flowId, uint32_t observedPulses, uint16_t pulseThreshold, uint16_t windowSec);
bool clearFlow(uint8_t flowId);
bool clearAll();

}
