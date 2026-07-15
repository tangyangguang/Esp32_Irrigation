#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IrrigationTypes.h"

class FlowCalibrationService {
public:
    static constexpr std::size_t kMaximumSamples = 10;
    static constexpr uint32_t kMinimumMeasuredWaterMl = 1000;
    static constexpr uint32_t kMaximumMeasuredWaterMl = 1000000;
    static constexpr uint8_t kQualityOnlyTwoSamples = 1U << 0U;
    static constexpr uint8_t kQualitySmallVolumeSpan = 1U << 1U;
    static constexpr uint8_t kQualityNonMonotonic = 1U << 2U;
    static constexpr uint8_t kQualityResidualHigh = 1U << 3U;

    struct Sample {
        uint32_t pulseCount;
        uint32_t measuredWaterMl;
        uint32_t elapsedSec;
        int64_t predictedPulseX100;
        int64_t residualPulseX100;
        int64_t residualPercentX100;
        WateringStopReason stopReason;
        uint8_t zoneId;
        bool valid;
    };

    void clear();
    bool captureFinishedSession(const WateringSessionSummary& summary, uint32_t resultEpoch);
    bool addPendingMeasurement(uint32_t measuredWaterMl, uint32_t resultEpoch);
    bool markPendingInvalid();
    bool updateMeasurement(uint8_t index, uint32_t measuredWaterMl, uint32_t resultEpoch);
    bool deleteSample(uint8_t index, uint32_t resultEpoch);
    void markResultApplied(uint32_t appliedEpoch, uint32_t coefficientX100);

    bool hasPendingMeasurement() const;
    uint32_t pendingPulseCount() const;
    uint32_t pendingElapsedSec() const;
    uint8_t pendingZoneId() const;
    WateringStopReason pendingStopReason() const;
    uint8_t sampleCount() const;
    uint8_t validSampleCount() const;
    const Sample* sample(uint8_t index) const;
    bool resultReady() const;
    uint32_t combinedPulsesPerLiterX100() const;
    int64_t nonSteadyPulseX100() const;
    int64_t equivalentWaterMlX100() const;
    uint32_t volumeSpanMl() const;
    uint8_t validZoneCount() const;
    uint16_t maximumResidualPercentX100() const;
    uint8_t qualityFlags() const;
    bool samplesUnstable() const;
    uint32_t resultUpdatedEpoch() const;
    uint32_t appliedEpoch() const;
    uint32_t appliedCoefficientX100() const;

private:
    bool appendPending(bool valid, uint32_t measuredWaterMl, uint32_t resultEpoch);
    void recalculate(uint32_t resultEpoch);

    std::array<Sample, kMaximumSamples> samples_{};
    uint32_t combinedPulsesPerLiterX100_ = 0;
    uint32_t volumeSpanMl_ = 0;
    uint32_t pendingPulseCount_ = 0;
    uint32_t pendingElapsedSec_ = 0;
    int64_t nonSteadyPulseX100_ = 0;
    int64_t equivalentWaterMlX100_ = 0;
    uint32_t resultUpdatedEpoch_ = 0;
    uint32_t appliedEpoch_ = 0;
    uint32_t appliedCoefficientX100_ = 0;
    uint16_t maximumResidualPercentX100_ = 0;
    uint8_t sampleCount_ = 0;
    uint8_t validSampleCount_ = 0;
    uint8_t validZoneCount_ = 0;
    uint8_t pendingZoneId_ = 0;
    uint8_t qualityFlags_ = 0;
    WateringStopReason pendingStopReason_ = WateringStopReason::None;
    bool pendingMeasurement_ = false;
    bool resultReady_ = false;
};
