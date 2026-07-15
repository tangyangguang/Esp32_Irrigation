#pragma once

#include <array>
#include <cstdint>

#include "IrrigationTypes.h"

class CalibrationStabilityDetector {
public:
    static constexpr uint8_t kMaximumWindows = 10;

    void begin(uint32_t nowMs,
               uint32_t pulseCount,
               const CalibrationStabilityConfig& config);
    void observe(uint32_t nowMs, uint32_t pulseCount);

    bool steadyDetected() const;
    bool steadyLaterUnstable() const;
    uint32_t steadyStartedMs() const;
    uint32_t steadyStartedPulseCount() const;
    uint32_t stableAverageRateX100() const;
    uint32_t latestRateX100() const;
    uint8_t collectedWindowCount() const;
    const CalibrationStabilityConfig& config() const;

private:
    uint32_t toleranceForRate(uint32_t rateX100) const;
    void appendWindow(uint32_t startedMs,
                      uint32_t startedPulseCount,
                      uint32_t rateX100);
    void evaluateCandidate();

    CalibrationStabilityConfig config_{};
    std::array<uint32_t, kMaximumWindows> ratesX100_{};
    std::array<uint32_t, kMaximumWindows> windowStartedMs_{};
    std::array<uint32_t, kMaximumWindows> windowStartedPulseCount_{};
    uint32_t currentWindowStartedMs_ = 0;
    uint32_t currentWindowStartedPulseCount_ = 0;
    uint32_t steadyStartedMs_ = 0;
    uint32_t steadyStartedPulseCount_ = 0;
    uint32_t stableAverageRateX100_ = 0;
    uint32_t latestRateX100_ = 0;
    uint8_t collectedWindowCount_ = 0;
    bool steadyDetected_ = false;
    bool steadyLaterUnstable_ = false;
};
