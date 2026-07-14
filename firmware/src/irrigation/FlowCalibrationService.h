#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

class FlowCalibrationService {
public:
    static constexpr std::size_t kMaximumSamples = 5;

    struct Sample {
        uint32_t pulseCount;
        uint32_t measuredWaterMl;
        uint32_t pulsesPerLiterX100;
        uint16_t deviationPercentX100;
    };

    void clear();
    bool captureFinishedSession(const WateringSessionSummary& summary);
    bool addPendingMeasurement(uint32_t measuredWaterMl);

    bool hasPendingMeasurement() const;
    uint8_t sampleCount() const;
    const Sample* sample(uint8_t index) const;
    uint32_t combinedPulsesPerLiterX100() const;
    bool samplesUnstable() const;

private:
    void recalculate();

    std::array<Sample, kMaximumSamples> samples_{};
    uint64_t totalPulses_ = 0;
    uint64_t totalMeasuredWaterMl_ = 0;
    uint32_t combinedPulsesPerLiterX100_ = 0;
    uint32_t pendingPulseCount_ = 0;
    uint8_t sampleCount_ = 0;
    bool pendingMeasurement_ = false;
};
