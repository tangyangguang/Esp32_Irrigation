#include "CalibrationStabilityDetector.h"

#include <climits>

void CalibrationStabilityDetector::begin(
    uint32_t nowMs,
    uint32_t pulseCount,
    const CalibrationStabilityConfig& config) {
    config_ = config;
    ratesX100_.fill(0);
    windowStartedMs_.fill(0);
    windowStartedPulseCount_.fill(0);
    currentWindowStartedMs_ = nowMs;
    currentWindowStartedPulseCount_ = pulseCount;
    steadyStartedMs_ = 0;
    steadyStartedPulseCount_ = 0;
    stableAverageRateX100_ = 0;
    latestRateX100_ = 0;
    collectedWindowCount_ = 0;
    steadyDetected_ = false;
    steadyLaterUnstable_ = false;
}

void CalibrationStabilityDetector::observe(uint32_t nowMs, uint32_t pulseCount) {
    const uint32_t windowMs = nowMs - currentWindowStartedMs_;
    const uint32_t requiredMs = static_cast<uint32_t>(config_.windowSec) * 1000U;
    if (windowMs < requiredMs || windowMs == 0) return;

    const uint32_t pulses = pulseCount - currentWindowStartedPulseCount_;
    const uint64_t scaledRate =
        (static_cast<uint64_t>(pulses) * 100000ULL + windowMs / 2U) / windowMs;
    const uint32_t rateX100 = scaledRate > UINT32_MAX
                                  ? UINT32_MAX
                                  : static_cast<uint32_t>(scaledRate);
    const uint32_t startedMs = currentWindowStartedMs_;
    const uint32_t startedPulseCount = currentWindowStartedPulseCount_;
    currentWindowStartedMs_ = nowMs;
    currentWindowStartedPulseCount_ = pulseCount;
    latestRateX100_ = rateX100;

    if (steadyDetected_) {
        const uint32_t difference = rateX100 > stableAverageRateX100_
                                        ? rateX100 - stableAverageRateX100_
                                        : stableAverageRateX100_ - rateX100;
        const uint32_t percentToleranceX100 = static_cast<uint32_t>(
            (static_cast<uint64_t>(stableAverageRateX100_) *
                 config_.allowedVariationPercent +
             50U) /
            100U);
        const uint32_t onePulseToleranceX100 =
            (100U + static_cast<uint32_t>(config_.windowSec) - 1U) /
            static_cast<uint32_t>(config_.windowSec);
        const uint32_t tolerance = percentToleranceX100 > onePulseToleranceX100
                                       ? percentToleranceX100
                                       : onePulseToleranceX100;
        if (rateX100 == 0 || difference > tolerance) {
            steadyLaterUnstable_ = true;
        }
        return;
    }

    appendWindow(startedMs, startedPulseCount, rateX100);
    evaluateCandidate();
}

bool CalibrationStabilityDetector::steadyDetected() const { return steadyDetected_; }
bool CalibrationStabilityDetector::steadyLaterUnstable() const {
    return steadyLaterUnstable_;
}
uint32_t CalibrationStabilityDetector::steadyStartedMs() const {
    return steadyStartedMs_;
}
uint32_t CalibrationStabilityDetector::steadyStartedPulseCount() const {
    return steadyStartedPulseCount_;
}
uint32_t CalibrationStabilityDetector::stableAverageRateX100() const {
    return stableAverageRateX100_;
}
uint32_t CalibrationStabilityDetector::latestRateX100() const {
    return latestRateX100_;
}
uint8_t CalibrationStabilityDetector::collectedWindowCount() const {
    return collectedWindowCount_;
}
const CalibrationStabilityConfig& CalibrationStabilityDetector::config() const {
    return config_;
}

void CalibrationStabilityDetector::appendWindow(uint32_t startedMs,
                                                uint32_t startedPulseCount,
                                                uint32_t rateX100) {
    const uint8_t limit = config_.requiredWindows;
    if (collectedWindowCount_ < limit) {
        ratesX100_[collectedWindowCount_] = rateX100;
        windowStartedMs_[collectedWindowCount_] = startedMs;
        windowStartedPulseCount_[collectedWindowCount_] = startedPulseCount;
        ++collectedWindowCount_;
        return;
    }
    for (uint8_t index = 1; index < limit; ++index) {
        ratesX100_[index - 1U] = ratesX100_[index];
        windowStartedMs_[index - 1U] = windowStartedMs_[index];
        windowStartedPulseCount_[index - 1U] = windowStartedPulseCount_[index];
    }
    ratesX100_[limit - 1U] = rateX100;
    windowStartedMs_[limit - 1U] = startedMs;
    windowStartedPulseCount_[limit - 1U] = startedPulseCount;
}

void CalibrationStabilityDetector::evaluateCandidate() {
    if (collectedWindowCount_ < config_.requiredWindows) return;
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0;
    uint64_t total = 0;
    for (uint8_t index = 0; index < config_.requiredWindows; ++index) {
        const uint32_t rate = ratesX100_[index];
        if (rate == 0) return;
        minimum = rate < minimum ? rate : minimum;
        maximum = rate > maximum ? rate : maximum;
        total += rate;
    }
    const uint32_t average = static_cast<uint32_t>(
        (total + config_.requiredWindows / 2U) / config_.requiredWindows);
    const uint32_t onePulseToleranceX100 =
        (100U + static_cast<uint32_t>(config_.windowSec) - 1U) /
        static_cast<uint32_t>(config_.windowSec);
    const uint32_t percentToleranceX100 = static_cast<uint32_t>(
        (static_cast<uint64_t>(average) * config_.allowedVariationPercent + 50U) /
        100U);
    const uint32_t tolerance = percentToleranceX100 > onePulseToleranceX100
                                   ? percentToleranceX100
                                   : onePulseToleranceX100;
    if (maximum - minimum > tolerance) return;
    steadyDetected_ = true;
    steadyStartedMs_ = windowStartedMs_[0];
    steadyStartedPulseCount_ = windowStartedPulseCount_[0];
    stableAverageRateX100_ = average;
}
