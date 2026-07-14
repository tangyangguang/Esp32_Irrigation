#include "FlowMonitor.h"

#include <climits>

namespace {

bool elapsed(uint32_t nowMs, uint32_t startedMs, uint32_t durationMs) {
    return static_cast<uint32_t>(nowMs - startedMs) >= durationMs;
}

}  // namespace

void FlowMonitor::begin(uint32_t nowMs, uint32_t pulseCount) {
    waitStartedMs_ = nowMs;
    lastPulseObservedMs_ = nowMs;
    lastPulseCount_ = pulseCount;
    flowEstablished_ = false;
}

void FlowMonitor::observe(uint32_t nowMs, uint32_t pulseCount) {
    if (pulseCount != lastPulseCount_) {
        lastPulseCount_ = pulseCount;
        lastPulseObservedMs_ = nowMs;
        flowEstablished_ = true;
    }
}

bool FlowMonitor::flowEstablished() const {
    return flowEstablished_;
}

bool FlowMonitor::flowStartTimedOut(uint32_t nowMs, uint16_t timeoutSec) const {
    return !flowEstablished_ && elapsed(nowMs, waitStartedMs_, static_cast<uint32_t>(timeoutSec) * 1000U);
}

bool FlowMonitor::noFlowTimedOut(uint32_t nowMs, uint16_t timeoutSec) const {
    return flowEstablished_ && elapsed(nowMs, lastPulseObservedMs_, static_cast<uint32_t>(timeoutSec) * 1000U);
}

void FlowMonitor::beginRateWindow(uint32_t nowMs, uint32_t pulseCount) {
    rateWindowStartedMs_ = nowMs;
    rateWindowStartedPulseCount_ = pulseCount;
}

bool FlowMonitor::takeRateSample(uint32_t nowMs,
                                 uint32_t pulseCount,
                                 uint32_t pulsesPerLiterX100,
                                 RateSample& sample) {
    constexpr uint32_t kRateWindowMs = 5000U;
    const uint32_t windowMs = nowMs - rateWindowStartedMs_;
    if (windowMs < kRateWindowMs || pulsesPerLiterX100 == 0) {
        return false;
    }
    const uint32_t pulses = pulseCount - rateWindowStartedPulseCount_;
    const uint64_t numerator = static_cast<uint64_t>(pulses) * 100000ULL * 60000ULL;
    const uint64_t denominator = static_cast<uint64_t>(pulsesPerLiterX100) * windowMs;
    const uint64_t rounded = (numerator + denominator / 2U) / denominator;
    sample.flowMlPerMinute = rounded > UINT32_MAX ? UINT32_MAX
                                                  : static_cast<uint32_t>(rounded);
    sample.pulseCount = pulses;
    sample.windowMs = windowMs;
    beginRateWindow(nowMs, pulseCount);
    return true;
}

bool FlowMonitor::estimateWaterMilliliters(uint32_t pulseCount,
                                           uint32_t pulsesPerLiterX100,
                                           uint32_t& waterMl) {
    if (pulsesPerLiterX100 == 0) {
        waterMl = 0;
        return false;
    }
    const uint64_t numerator = static_cast<uint64_t>(pulseCount) * 100000ULL +
                               static_cast<uint64_t>(pulsesPerLiterX100 / 2U);
    const uint64_t estimate = numerator / pulsesPerLiterX100;
    if (estimate > UINT32_MAX) {
        waterMl = UINT32_MAX;
        return false;
    }
    waterMl = static_cast<uint32_t>(estimate);
    return true;
}
