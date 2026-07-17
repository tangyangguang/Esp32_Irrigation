#pragma once

#include <array>
#include <cstdint>

class UnexpectedFlowMonitor {
public:
    enum class Update : uint8_t {
        None,
        AlarmRaised,
        AlarmCleared,
    };

    void begin(uint32_t nowMs,
               uint32_t pulseCount,
               uint16_t delaySec,
               uint16_t windowSec,
               uint16_t pulseThreshold);
    Update observe(uint32_t nowMs, uint32_t pulseCount);
    bool alarmActive() const;
    bool observationReady(uint32_t nowMs) const;
    uint16_t delayRemainingSec(uint32_t nowMs) const;
    uint16_t windowRemainingSec(uint32_t nowMs) const;
    uint32_t observedDurationMs(uint32_t nowMs) const;
    uint32_t observedPulseCount() const;

private:
    static constexpr std::size_t kBucketCount = 301;

    void activate(uint32_t nowMs, uint32_t pulseCount);
    void advanceSecond(uint32_t nowSecond);

    std::array<uint32_t, kBucketCount> bucketSeconds_{};
    std::array<uint32_t, kBucketCount> bucketPulses_{};
    uint32_t delayStartedMs_ = 0;
    uint32_t monitoringStartedMs_ = 0;
    uint32_t lastPulseCount_ = 0;
    uint32_t currentSecond_ = 0;
    uint32_t rollingPulseCount_ = 0;
    uint16_t delaySec_ = 0;
    uint16_t windowSec_ = 1;
    uint16_t pulseThreshold_ = 1;
    bool monitoring_ = false;
    bool alarmActive_ = false;
};
