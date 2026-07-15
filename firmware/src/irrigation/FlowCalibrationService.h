#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

class FlowCalibrationService {
public:
    static constexpr std::size_t kMaximumSamples = 5;
    static constexpr uint32_t kMinimumMeasuredWaterMl = 1000;
    static constexpr uint32_t kMaximumMeasuredWaterMl = 1000000;
    static constexpr uint8_t kQualityOnlyTwoSamples = 1U << 0U;
    static constexpr uint8_t kQualitySmallVolumeSpan = 1U << 1U;
    static constexpr uint8_t kQualityNonMonotonic = 1U << 2U;
    static constexpr uint8_t kQualityResidualHigh = 1U << 3U;

    struct Sample {
        uint32_t pulseCount;
        uint32_t measuredWaterMl;
    };

    void clear();
    bool captureFinishedSession(const WateringSessionSummary& summary);
    bool addPendingMeasurement(uint32_t measuredWaterMl);

    bool hasPendingMeasurement() const;
    uint8_t sampleCount() const;
    const Sample* sample(uint8_t index) const;
    uint8_t zoneId() const;
    bool resultReady() const;
    uint32_t combinedPulsesPerLiterX100() const;
    uint32_t volumeSpanMl() const;
    uint16_t maximumResidualPercentX100() const;
    uint8_t qualityFlags() const;
    bool samplesUnstable() const;

private:
    void recalculate();

    std::array<Sample, kMaximumSamples> samples_{};
    uint32_t combinedPulsesPerLiterX100_ = 0;
    uint32_t volumeSpanMl_ = 0;
    uint32_t pendingPulseCount_ = 0;
    uint16_t maximumResidualPercentX100_ = 0;
    uint8_t sampleCount_ = 0;
    uint8_t zoneId_ = 0;
    uint8_t qualityFlags_ = 0;
    bool pendingMeasurement_ = false;
    bool resultReady_ = false;
};
