#include "FlowMonitor.h"

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
