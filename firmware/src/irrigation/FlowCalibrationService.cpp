#include "FlowCalibrationService.h"

namespace {

constexpr uint32_t kMinimumMeasuredWaterMl = 1000;
constexpr uint32_t kMinimumCoefficientX100 = 1;
constexpr uint32_t kMaximumCoefficientX100 = 10000000;
constexpr uint16_t kUnstableDeviationPercentX100 = 800;

uint32_t calculateCoefficient(uint64_t pulses, uint64_t measuredWaterMl) {
    if (pulses == 0 || measuredWaterMl == 0) {
        return 0;
    }
    const uint64_t value = (pulses * 100000ULL + measuredWaterMl / 2U) /
                           measuredWaterMl;
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
}

}  // namespace

void FlowCalibrationService::clear() {
    samples_ = {};
    totalPulses_ = 0;
    totalMeasuredWaterMl_ = 0;
    combinedPulsesPerLiterX100_ = 0;
    pendingPulseCount_ = 0;
    sampleCount_ = 0;
    pendingMeasurement_ = false;
}

bool FlowCalibrationService::captureFinishedSession(
    const WateringSessionSummary& summary) {
    if (summary.purpose != WateringPurpose::FlowCalibration ||
        summary.zoneCount != 1 || !summary.anyFlowEstablished ||
        summary.zones[0].pulseCount == 0 || pendingMeasurement_) {
        return false;
    }
    pendingPulseCount_ = summary.zones[0].pulseCount;
    pendingMeasurement_ = true;
    return true;
}

bool FlowCalibrationService::addPendingMeasurement(uint32_t measuredWaterMl) {
    if (!pendingMeasurement_ || sampleCount_ >= samples_.size() ||
        measuredWaterMl < kMinimumMeasuredWaterMl) {
        return false;
    }
    const uint32_t coefficient = calculateCoefficient(pendingPulseCount_, measuredWaterMl);
    if (coefficient < kMinimumCoefficientX100 || coefficient > kMaximumCoefficientX100) {
        return false;
    }
    Sample& sample = samples_[sampleCount_++];
    sample.pulseCount = pendingPulseCount_;
    sample.measuredWaterMl = measuredWaterMl;
    sample.pulsesPerLiterX100 = coefficient;
    totalPulses_ += pendingPulseCount_;
    totalMeasuredWaterMl_ += measuredWaterMl;
    pendingPulseCount_ = 0;
    pendingMeasurement_ = false;
    recalculate();
    return true;
}

bool FlowCalibrationService::hasPendingMeasurement() const {
    return pendingMeasurement_;
}

uint8_t FlowCalibrationService::sampleCount() const {
    return sampleCount_;
}

const FlowCalibrationService::Sample* FlowCalibrationService::sample(uint8_t index) const {
    return index < sampleCount_ ? &samples_[index] : nullptr;
}

uint32_t FlowCalibrationService::combinedPulsesPerLiterX100() const {
    return combinedPulsesPerLiterX100_;
}

bool FlowCalibrationService::samplesUnstable() const {
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        if (samples_[index].deviationPercentX100 > kUnstableDeviationPercentX100) {
            return true;
        }
    }
    return false;
}

void FlowCalibrationService::recalculate() {
    combinedPulsesPerLiterX100_ = calculateCoefficient(totalPulses_,
                                                       totalMeasuredWaterMl_);
    for (uint8_t index = 0; index < sampleCount_; ++index) {
        Sample& sample = samples_[index];
        const uint32_t difference = sample.pulsesPerLiterX100 > combinedPulsesPerLiterX100_
                                        ? sample.pulsesPerLiterX100 - combinedPulsesPerLiterX100_
                                        : combinedPulsesPerLiterX100_ - sample.pulsesPerLiterX100;
        const uint64_t deviation =
            (static_cast<uint64_t>(difference) * 10000ULL +
             combinedPulsesPerLiterX100_ / 2U) /
            combinedPulsesPerLiterX100_;
        sample.deviationPercentX100 = deviation > UINT16_MAX
                                          ? UINT16_MAX
                                          : static_cast<uint16_t>(deviation);
    }
}
