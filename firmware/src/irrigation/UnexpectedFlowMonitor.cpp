#include "UnexpectedFlowMonitor.h"

#include <climits>

void UnexpectedFlowMonitor::begin(uint32_t nowMs,
                                  uint32_t pulseCount,
                                  uint16_t delaySec,
                                  uint16_t windowSec,
                                  uint16_t pulseThreshold) {
    delayStartedMs_ = nowMs;
    monitoringStartedMs_ = nowMs;
    lastPulseCount_ = pulseCount;
    delaySec_ = delaySec;
    windowSec_ = windowSec >= 1 && windowSec <= 300 ? windowSec : 1;
    pulseThreshold_ = pulseThreshold == 0 ? 1 : pulseThreshold;
    rollingPulseCount_ = 0;
    monitoring_ = false;
    alarmActive_ = false;
    bucketSeconds_.fill(UINT32_MAX);
    bucketPulses_.fill(0);
    if (delaySec_ == 0) {
        activate(nowMs, pulseCount);
    }
}

UnexpectedFlowMonitor::Update UnexpectedFlowMonitor::observe(uint32_t nowMs,
                                                             uint32_t pulseCount) {
    if (!monitoring_) {
        if (static_cast<uint32_t>(nowMs - delayStartedMs_) <
            static_cast<uint32_t>(delaySec_) * 1000U) {
            lastPulseCount_ = pulseCount;
            return Update::None;
        }
        activate(nowMs, pulseCount);
        return Update::None;
    }

    const uint32_t nowSecond = nowMs / 1000U;
    advanceSecond(nowSecond);
    const uint32_t pulseDelta = pulseCount - lastPulseCount_;
    lastPulseCount_ = pulseCount;
    if (pulseDelta != 0) {
        const std::size_t index = nowSecond % kBucketCount;
        if (bucketSeconds_[index] != nowSecond) {
            bucketSeconds_[index] = nowSecond;
            bucketPulses_[index] = 0;
        }
        const uint32_t room = UINT32_MAX - bucketPulses_[index];
        bucketPulses_[index] += pulseDelta > room ? room : pulseDelta;
        const uint32_t rollingRoom = UINT32_MAX - rollingPulseCount_;
        rollingPulseCount_ += pulseDelta > rollingRoom ? rollingRoom : pulseDelta;
    }

    if (!alarmActive_ && rollingPulseCount_ >= pulseThreshold_) {
        alarmActive_ = true;
        return Update::AlarmRaised;
    }
    if (alarmActive_ && rollingPulseCount_ == 0) {
        alarmActive_ = false;
        return Update::AlarmCleared;
    }
    return Update::None;
}

bool UnexpectedFlowMonitor::alarmActive() const {
    return alarmActive_;
}

bool UnexpectedFlowMonitor::observationReady(uint32_t nowMs) const {
    return monitoring_ &&
           (alarmActive_ ||
            static_cast<uint32_t>(nowMs - monitoringStartedMs_) >=
                static_cast<uint32_t>(windowSec_) * 1000U);
}

uint32_t UnexpectedFlowMonitor::observedPulseCount() const {
    return rollingPulseCount_;
}

void UnexpectedFlowMonitor::activate(uint32_t nowMs, uint32_t pulseCount) {
    monitoring_ = true;
    monitoringStartedMs_ = nowMs;
    lastPulseCount_ = pulseCount;
    currentSecond_ = nowMs / 1000U;
    rollingPulseCount_ = 0;
    bucketSeconds_.fill(UINT32_MAX);
    bucketPulses_.fill(0);
}

void UnexpectedFlowMonitor::advanceSecond(uint32_t nowSecond) {
    if (nowSecond == currentSecond_) {
        return;
    }
    const uint32_t elapsedSeconds = nowSecond - currentSecond_;
    if (elapsedSeconds > windowSec_) {
        rollingPulseCount_ = 0;
        bucketSeconds_.fill(UINT32_MAX);
        bucketPulses_.fill(0);
        currentSecond_ = nowSecond;
        return;
    }
    for (uint32_t second = currentSecond_ + 1U; second <= nowSecond; ++second) {
        const uint32_t expiredSecond = second > windowSec_ ? second - windowSec_ : UINT32_MAX;
        if (expiredSecond != UINT32_MAX) {
            const std::size_t index = expiredSecond % kBucketCount;
            if (bucketSeconds_[index] == expiredSecond) {
                rollingPulseCount_ -= bucketPulses_[index];
                bucketSeconds_[index] = UINT32_MAX;
                bucketPulses_[index] = 0;
            }
        }
    }
    currentSecond_ = nowSecond;
}
