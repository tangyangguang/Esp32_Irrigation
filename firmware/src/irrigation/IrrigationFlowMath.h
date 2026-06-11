#pragma once

#include <stdint.h>

namespace irrigation {

inline uint32_t estimateVolumeMl(uint32_t pulseCount, uint32_t pulsesPerLiter) {
    if (pulsesPerLiter == 0) {
        return 0;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(pulseCount) * 1000ULL) / pulsesPerLiter);
}

inline uint32_t estimateFlowMlPerMin(uint32_t windowPulseCount, uint32_t pulsesPerLiter, uint32_t windowMs) {
    if (pulsesPerLiter == 0 || windowMs == 0) {
        return 0;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(windowPulseCount) * 60000ULL * 1000ULL) /
                                 (static_cast<uint64_t>(pulsesPerLiter) * windowMs));
}

inline uint32_t lowFlowThresholdMlPerMin(uint32_t normalFlowMlPerMin, uint8_t lowFlowPercent) {
    return static_cast<uint32_t>((static_cast<uint64_t>(normalFlowMlPerMin) * lowFlowPercent) / 100ULL);
}

inline uint32_t highFlowThresholdMlPerMin(uint32_t normalFlowMlPerMin, uint8_t highFlowPercent) {
    return static_cast<uint32_t>((static_cast<uint64_t>(normalFlowMlPerMin) * highFlowPercent) / 100ULL);
}

}  // namespace irrigation
