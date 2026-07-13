#pragma once

#include <cstdint>

class FlowMonitor {
public:
    void begin(uint32_t nowMs, uint32_t pulseCount);
    void observe(uint32_t nowMs, uint32_t pulseCount);

    bool flowEstablished() const;
    bool flowStartTimedOut(uint32_t nowMs, uint16_t timeoutSec) const;
    bool noFlowTimedOut(uint32_t nowMs, uint16_t timeoutSec) const;

    static bool estimateWaterMilliliters(uint32_t pulseCount,
                                         uint32_t pulsesPerLiterX100,
                                         uint32_t& waterMl);

private:
    uint32_t waitStartedMs_ = 0;
    uint32_t lastPulseObservedMs_ = 0;
    uint32_t lastPulseCount_ = 0;
    bool flowEstablished_ = false;
};
