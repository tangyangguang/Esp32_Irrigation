#include "FlowCalibrationService.h"

#include <climits>
#include <cmath>

namespace {

constexpr uint32_t kMinimumCoefficientX100 = 1;
constexpr uint32_t kMaximumCoefficientX100 = 10000000;
constexpr uint32_t kRecommendedVolumeSpanMl = 500;
constexpr uint16_t kUnstableResidualPercentX100 = 800;

}  // namespace

void FlowCalibrationService::clear() {
    samples_ = {};
    combinedPulsesPerLiterX100_ = 0;
    volumeSpanMl_ = 0;
    pendingPulseCount_ = 0;
    pendingElapsedSec_ = 0;
    resultUpdatedEpoch_ = 0;
    appliedEpoch_ = 0;
    appliedCoefficientX100_ = 0;
    maximumResidualPercentX100_ = 0;
    sampleCount_ = 0;
    validSampleCount_ = 0;
    zoneId_ = 0;
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
        pendingMeasurement_ || sampleCount_ >= samples_.size() ||
        (zoneId_ != 0 && zoneId_ != summary.zones[0].zoneId)) {
        return false;
    }
    zoneId_ = summary.zones[0].zoneId;
    pendingPulseCount_ = summary.zones[0].pulseCount;
    pendingElapsedSec_ = summary.elapsedSec;
    pendingStopReason_ = summary.stopReason;
    const bool normalFinish = summary.stopReason == WateringStopReason::UserStopped ||
                              summary.stopReason == WateringStopReason::Completed;
    if (pendingPulseCount_ == 0 || !normalFinish) {
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
    sample.pulseCount = pendingPulseCount_;
    sample.measuredWaterMl = measuredWaterMl;
    sample.elapsedSec = pendingElapsedSec_;
    sample.stopReason = pendingStopReason_;
    sample.valid = valid;
    pendingPulseCount_ = 0;
    pendingElapsedSec_ = 0;
    pendingStopReason_ = WateringStopReason::None;
    pendingMeasurement_ = false;
    if (valid) recalculate(resultEpoch);
    return true;
}

bool FlowCalibrationService::hasPendingMeasurement() const {
    return pendingMeasurement_;
}

uint32_t FlowCalibrationService::pendingPulseCount() const { return pendingPulseCount_; }
uint32_t FlowCalibrationService::pendingElapsedSec() const { return pendingElapsedSec_; }
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

uint8_t FlowCalibrationService::zoneId() const {
    return zoneId_;
}

bool FlowCalibrationService::resultReady() const {
    return resultReady_;
}

uint32_t FlowCalibrationService::combinedPulsesPerLiterX100() const {
    return combinedPulsesPerLiterX100_;
}

uint32_t FlowCalibrationService::volumeSpanMl() const {
    return volumeSpanMl_;
}

uint16_t FlowCalibrationService::maximumResidualPercentX100() const {
    return maximumResidualPercentX100_;
}

uint8_t FlowCalibrationService::qualityFlags() const {
    return qualityFlags_;
}

bool FlowCalibrationService::samplesUnstable() const {
    return (qualityFlags_ & (kQualitySmallVolumeSpan |
                             kQualityNonMonotonic |
                             kQualityResidualHigh)) != 0;
}

uint32_t FlowCalibrationService::resultUpdatedEpoch() const { return resultUpdatedEpoch_; }
uint32_t FlowCalibrationService::appliedEpoch() const { return appliedEpoch_; }
uint32_t FlowCalibrationService::appliedCoefficientX100() const {
    return appliedCoefficientX100_;
}

void FlowCalibrationService::recalculate(uint32_t resultEpoch) {
    combinedPulsesPerLiterX100_ = 0;
    volumeSpanMl_ = 0;
    maximumResidualPercentX100_ = 0;
    validSampleCount_ = 0;
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        if (samples_[index].valid) ++validSampleCount_;
    }
    qualityFlags_ = validSampleCount_ == 2 ? kQualityOnlyTwoSamples : 0;
    resultReady_ = false;
    resultUpdatedEpoch_ = resultEpoch;
    if (validSampleCount_ < 2) return;

    uint64_t sumVolume = 0;
    uint64_t sumPulses = 0;
    uint64_t sumVolumeSquared = 0;
    uint64_t sumVolumePulses = 0;
    uint32_t minimumVolume = UINT32_MAX;
    uint32_t maximumVolume = 0;
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        const Sample& sample = samples_[index];
        if (!sample.valid) continue;
        sumVolume += sample.measuredWaterMl;
        sumPulses += sample.pulseCount;
        sumVolumeSquared += static_cast<uint64_t>(sample.measuredWaterMl) *
                            sample.measuredWaterMl;
        sumVolumePulses += static_cast<uint64_t>(sample.measuredWaterMl) *
                           sample.pulseCount;
        if (sample.measuredWaterMl < minimumVolume) minimumVolume = sample.measuredWaterMl;
        if (sample.measuredWaterMl > maximumVolume) maximumVolume = sample.measuredWaterMl;
        for (uint8_t other = 0; other < index; ++other) {
            const Sample& previous = samples_[other];
            if (!previous.valid) continue;
            if ((sample.measuredWaterMl > previous.measuredWaterMl &&
                 sample.pulseCount <= previous.pulseCount) ||
                (sample.measuredWaterMl < previous.measuredWaterMl &&
                 sample.pulseCount >= previous.pulseCount)) {
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

    const double slope = static_cast<double>(slopeNumerator) /
                         static_cast<double>(slopeDenominator);
    const double intercept =
        (static_cast<double>(sumPulses) - slope * static_cast<double>(sumVolume)) /
        static_cast<double>(count);
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        const Sample& sample = samples_[index];
        if (!sample.valid) continue;
        const double predicted = intercept + slope * sample.measuredWaterMl;
        const double residual =
            std::fabs(predicted - sample.pulseCount) * 10000.0 /
            static_cast<double>(sample.pulseCount);
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
