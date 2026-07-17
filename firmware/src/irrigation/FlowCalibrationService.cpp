#include "FlowCalibrationService.h"

#include <climits>
#include <cmath>

namespace {

constexpr uint32_t kMinimumCoefficientX100 = 1;
constexpr uint32_t kMaximumCoefficientX100 = 10000000;
constexpr uint32_t kRecommendedVolumeSpanMl = 500;
constexpr uint16_t kUnstableResidualPercentX100 = 800;

int64_t roundSaturated(double value) {
    if (!std::isfinite(value) || value >= static_cast<double>(INT64_MAX)) {
        return value < 0.0 ? INT64_MIN : INT64_MAX;
    }
    if (value <= static_cast<double>(INT64_MIN)) return INT64_MIN;
    return static_cast<int64_t>(std::llround(value));
}

}  // namespace

void FlowCalibrationService::clear() {
    samples_ = {};
    pendingSample_ = {};
    combinedPulsesPerLiterX100_ = 0;
    combinedStartupPulseCount_ = 0;
    combinedStartupWaterMl_ = 0;
    volumeSpanMl_ = 0;
    combinedStartupWaterMlX100_ = 0;
    resultUpdatedEpoch_ = 0;
    appliedEpoch_ = 0;
    appliedCoefficientX100_ = 0;
    maximumResidualPercentX100_ = 0;
    sampleCount_ = 0;
    validSampleCount_ = 0;
    validZoneCount_ = 0;
    qualityFlags_ = 0;
    pendingStopReason_ = WateringStopReason::None;
    pendingMeasurement_ = false;
    resultReady_ = false;
}

bool FlowCalibrationService::captureFinishedSession(
    const WateringSessionSummary& summary,
    uint32_t resultEpoch) {
    if (summary.purpose != WateringPurpose::FlowCalibration ||
        summary.zoneCount != 1 || summary.zones[0].zoneId == 0 ||
        summary.zones[0].result == ZoneWateringResult::NotStarted ||
        pendingMeasurement_ || sampleCount_ >= samples_.size()) {
        return false;
    }
    const ZoneWateringSummary& zone = summary.zones[0];
    pendingSample_ = {};
    pendingSample_.zoneId = zone.zoneId;
    pendingSample_.pulseCount = zone.pulseCount;
    pendingSample_.elapsedSec = summary.elapsedSec;
    pendingSample_.flowEstablishedMs = zone.calibrationFlowEstablishedMs;
    pendingSample_.steadyStartedMs = zone.calibrationSteadyStartedMs;
    pendingSample_.startupPulseCount = zone.calibrationStartupPulses;
    pendingSample_.steadyDurationMs = zone.calibrationSteadyDurationMs;
    pendingSample_.steadyPulseCount = zone.calibrationSteadyPulses;
    pendingSample_.stopDurationMs = zone.calibrationStopDurationMs;
    pendingSample_.stopPulseCount = zone.calibrationStopPulses;
    pendingSample_.pulseRateX100 = zone.calibrationPulseRateX100;
    pendingSample_.latestPulseRateX100 = zone.calibrationLatestPulseRateX100;
    pendingSample_.stabilityWindowSec = zone.calibrationWindowSec;
    pendingSample_.requiredStableWindows = zone.calibrationRequiredWindows;
    pendingSample_.allowedVariationPercent =
        zone.calibrationAllowedVariationPercent;
    pendingSample_.steadyDetected = zone.calibrationSteadyDetected;
    pendingSample_.steadyLaterUnstable = zone.calibrationSteadyLaterUnstable;
    pendingSample_.stopReason = summary.stopReason;
    pendingStopReason_ = summary.stopReason;
    const bool normalFinish = summary.stopReason == WateringStopReason::UserStopped ||
                              summary.stopReason == WateringStopReason::Completed;
    if (pendingSample_.pulseCount == 0 || !pendingSample_.steadyDetected ||
        pendingSample_.steadyPulseCount == 0 || !normalFinish) {
        return appendPending(false, 0, resultEpoch);
    }
    pendingMeasurement_ = true;
    return true;
}

bool FlowCalibrationService::addPendingMeasurement(uint32_t measuredWaterMl,
                                                   uint32_t resultEpoch) {
    if (!pendingMeasurement_ || sampleCount_ >= samples_.size() ||
        measuredWaterMl < kMinimumMeasuredWaterMl ||
        measuredWaterMl > kMaximumMeasuredWaterMl) {
        return false;
    }
    return appendPending(true, measuredWaterMl, resultEpoch);
}

bool FlowCalibrationService::markPendingInvalid() {
    return pendingMeasurement_ && appendPending(false, 0, 0);
}

bool FlowCalibrationService::updateMeasurement(uint8_t index,
                                               uint32_t measuredWaterMl,
                                               uint32_t resultEpoch) {
    if (pendingMeasurement_ || index >= sampleCount_ || !samples_[index].valid ||
        measuredWaterMl < kMinimumMeasuredWaterMl ||
        measuredWaterMl > kMaximumMeasuredWaterMl) {
        return false;
    }
    if (samples_[index].measuredWaterMl == measuredWaterMl) return true;
    samples_[index].measuredWaterMl = measuredWaterMl;
    recalculate(resultEpoch);
    return true;
}

bool FlowCalibrationService::deleteSample(uint8_t index, uint32_t resultEpoch) {
    if (pendingMeasurement_ || index >= sampleCount_) return false;
    const bool affectedResult = samples_[index].valid;
    for (uint8_t move = index + 1U; move < sampleCount_; ++move) {
        samples_[move - 1U] = samples_[move];
    }
    samples_[--sampleCount_] = {};
    if (sampleCount_ == 0) {
        clear();
    } else if (affectedResult) {
        recalculate(resultEpoch);
    }
    return true;
}

void FlowCalibrationService::markResultApplied(uint32_t appliedEpoch,
                                               uint32_t coefficientX100) {
    appliedEpoch_ = appliedEpoch;
    appliedCoefficientX100_ = coefficientX100;
}

bool FlowCalibrationService::appendPending(bool valid,
                                           uint32_t measuredWaterMl,
                                           uint32_t resultEpoch) {
    if (sampleCount_ >= samples_.size()) return false;
    Sample& sample = samples_[sampleCount_++];
    sample = pendingSample_;
    sample.measuredWaterMl = measuredWaterMl;
    sample.valid = valid;
    pendingSample_ = {};
    pendingStopReason_ = WateringStopReason::None;
    pendingMeasurement_ = false;
    if (valid) recalculate(resultEpoch);
    return true;
}

bool FlowCalibrationService::hasPendingMeasurement() const {
    return pendingMeasurement_;
}

uint32_t FlowCalibrationService::pendingPulseCount() const {
    return pendingSample_.pulseCount;
}
uint32_t FlowCalibrationService::pendingElapsedSec() const {
    return pendingSample_.elapsedSec;
}
uint8_t FlowCalibrationService::pendingZoneId() const { return pendingSample_.zoneId; }
const FlowCalibrationService::Sample* FlowCalibrationService::pendingSample() const {
    return pendingMeasurement_ ? &pendingSample_ : nullptr;
}
WateringStopReason FlowCalibrationService::pendingStopReason() const {
    return pendingStopReason_;
}

uint8_t FlowCalibrationService::sampleCount() const {
    return sampleCount_;
}

uint8_t FlowCalibrationService::validSampleCount() const { return validSampleCount_; }

const FlowCalibrationService::Sample* FlowCalibrationService::sample(uint8_t index) const {
    return index < sampleCount_ ? &samples_[index] : nullptr;
}

bool FlowCalibrationService::resultReady() const {
    return resultReady_;
}

uint32_t FlowCalibrationService::combinedPulsesPerLiterX100() const {
    return combinedPulsesPerLiterX100_;
}

uint32_t FlowCalibrationService::combinedStartupPulseCount() const {
    return combinedStartupPulseCount_;
}

uint32_t FlowCalibrationService::combinedStartupWaterMl() const {
    return combinedStartupWaterMl_;
}

int64_t FlowCalibrationService::combinedStartupWaterMlX100() const {
    return combinedStartupWaterMlX100_;
}

uint32_t FlowCalibrationService::volumeSpanMl() const {
    return volumeSpanMl_;
}

uint8_t FlowCalibrationService::validZoneCount() const { return validZoneCount_; }

uint16_t FlowCalibrationService::maximumResidualPercentX100() const {
    return maximumResidualPercentX100_;
}

uint8_t FlowCalibrationService::qualityFlags() const {
    return qualityFlags_;
}

uint32_t FlowCalibrationService::resultUpdatedEpoch() const { return resultUpdatedEpoch_; }
uint32_t FlowCalibrationService::appliedEpoch() const { return appliedEpoch_; }
uint32_t FlowCalibrationService::appliedCoefficientX100() const {
    return appliedCoefficientX100_;
}

void FlowCalibrationService::recalculate(uint32_t resultEpoch) {
    combinedPulsesPerLiterX100_ = 0;
    combinedStartupPulseCount_ = 0;
    combinedStartupWaterMl_ = 0;
    combinedStartupWaterMlX100_ = 0;
    volumeSpanMl_ = 0;
    maximumResidualPercentX100_ = 0;
    validSampleCount_ = 0;
    validZoneCount_ = 0;
    bool zonesSeen[UINT8_MAX + 1U]{};
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        samples_[index].predictedPulseX100 = 0;
        samples_[index].residualPulseX100 = 0;
        samples_[index].residualPercentX100 = 0;
        samples_[index].estimatedSteadyWaterMlX100 = 0;
        samples_[index].estimatedStartupWaterMlX100 = 0;
        if (!samples_[index].valid) continue;
        ++validSampleCount_;
        if (!zonesSeen[samples_[index].zoneId]) {
            zonesSeen[samples_[index].zoneId] = true;
            ++validZoneCount_;
        }
    }
    qualityFlags_ = validSampleCount_ == 2 ? kQualityOnlyTwoSamples : 0;
    resultReady_ = false;
    resultUpdatedEpoch_ = resultEpoch;
    if (validSampleCount_ < 2) return;

    uint64_t sumVolume = 0;
    uint64_t sumPulses = 0;
    uint64_t sumStartupPulses = 0;
    uint64_t sumVolumeSquared = 0;
    uint64_t sumVolumePulses = 0;
    uint32_t minimumVolume = UINT32_MAX;
    uint32_t maximumVolume = 0;
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        const Sample& sample = samples_[index];
        if (!sample.valid) continue;
        sumVolume += sample.measuredWaterMl;
        sumPulses += sample.steadyPulseCount;
        sumStartupPulses += sample.startupPulseCount;
        sumVolumeSquared += static_cast<uint64_t>(sample.measuredWaterMl) *
                            sample.measuredWaterMl;
        sumVolumePulses += static_cast<uint64_t>(sample.measuredWaterMl) *
                           sample.steadyPulseCount;
        if (sample.measuredWaterMl < minimumVolume) minimumVolume = sample.measuredWaterMl;
        if (sample.measuredWaterMl > maximumVolume) maximumVolume = sample.measuredWaterMl;
        for (uint8_t other = 0; other < index; ++other) {
            const Sample& previous = samples_[other];
            if (!previous.valid) continue;
            if ((sample.measuredWaterMl > previous.measuredWaterMl &&
                 sample.steadyPulseCount <= previous.steadyPulseCount) ||
                (sample.measuredWaterMl < previous.measuredWaterMl &&
                 sample.steadyPulseCount >= previous.steadyPulseCount)) {
                qualityFlags_ |= kQualityNonMonotonic;
            }
        }
    }
    volumeSpanMl_ = maximumVolume - minimumVolume;
    if (volumeSpanMl_ < kRecommendedVolumeSpanMl) {
        qualityFlags_ |= kQualitySmallVolumeSpan;
    }

    const int64_t count = validSampleCount_;
    const int64_t slopeNumerator =
        count * static_cast<int64_t>(sumVolumePulses) -
        static_cast<int64_t>(sumVolume * sumPulses);
    const int64_t slopeDenominator =
        count * static_cast<int64_t>(sumVolumeSquared) -
        static_cast<int64_t>(sumVolume * sumVolume);
    if (slopeNumerator <= 0 || slopeDenominator <= 0) return;
    const int64_t wholePulsesPerMl = slopeNumerator / slopeDenominator;
    if (wholePulsesPerMl > 100) return;
    const int64_t remainder = slopeNumerator % slopeDenominator;
    const int64_t coefficient =
        wholePulsesPerMl * 100000 +
        (remainder * 100000 + slopeDenominator / 2) / slopeDenominator;
    if (coefficient < kMinimumCoefficientX100 || coefficient > kMaximumCoefficientX100) return;
    combinedPulsesPerLiterX100_ = static_cast<uint32_t>(coefficient);
    combinedStartupPulseCount_ = static_cast<uint32_t>(
        (sumStartupPulses + validSampleCount_ / 2U) / validSampleCount_);

    const double slope = static_cast<double>(slopeNumerator) /
                         static_cast<double>(slopeDenominator);
    const double intercept =
        (static_cast<double>(sumPulses) - slope * static_cast<double>(sumVolume)) /
        static_cast<double>(count);
    combinedStartupWaterMlX100_ =
        static_cast<int64_t>(std::llround(-intercept * 100.0 / slope));
    if (combinedStartupWaterMlX100_ < 0) {
        qualityFlags_ |= kQualityNegativeStartupWater;
    } else {
        const int64_t roundedStartupWaterMl =
            (combinedStartupWaterMlX100_ + 50) / 100;
        combinedStartupWaterMl_ = roundedStartupWaterMl >= UINT32_MAX
                                      ? UINT32_MAX
                                      : static_cast<uint32_t>(roundedStartupWaterMl);
    }
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        Sample& sample = samples_[index];
        if (!sample.valid) continue;
        const double predicted = intercept + slope * sample.measuredWaterMl;
        const double signedResidual =
            static_cast<double>(sample.steadyPulseCount) - predicted;
        const double signedResidualPercentX100 =
            predicted > 0.0 ? signedResidual * 10000.0 / predicted : 0.0;
        sample.predictedPulseX100 =
            static_cast<int64_t>(std::llround(predicted * 100.0));
        sample.residualPulseX100 =
            static_cast<int64_t>(std::llround(signedResidual * 100.0));
        sample.residualPercentX100 = roundSaturated(signedResidualPercentX100);
        sample.estimatedSteadyWaterMlX100 = static_cast<int64_t>(std::llround(
            static_cast<double>(sample.steadyPulseCount) * 100.0 / slope));
        sample.estimatedStartupWaterMlX100 =
            static_cast<int64_t>(sample.measuredWaterMl) * 100 -
            sample.estimatedSteadyWaterMlX100;
        if (sample.steadyLaterUnstable) {
            qualityFlags_ |= kQualityPostSteadyUnstable;
        }
        const double residual = std::fabs(signedResidualPercentX100);
        const uint16_t residualX100 = residual >= UINT16_MAX
                                          ? UINT16_MAX
                                          : static_cast<uint16_t>(residual + 0.5);
        if (residualX100 > maximumResidualPercentX100_) {
            maximumResidualPercentX100_ = residualX100;
        }
    }
    if (maximumResidualPercentX100_ > kUnstableResidualPercentX100) {
        qualityFlags_ |= kQualityResidualHigh;
    }
    resultReady_ = true;
}
