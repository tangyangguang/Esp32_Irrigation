#pragma once

#include "domain/ZoneTaskRunner.h"
#include "domain/ZoneTypes.h"
#include "storage/ZoneErrorStore.h"

#ifdef DISABLED
#undef DISABLED
#endif

class Zone {
public:
    void begin(const Irrigation::ZoneConfig& config, const ZoneErrorStore::ZoneError& error, uint32_t nowMs);
    void applyConfig(const Irrigation::ZoneConfig& config, uint32_t epoch, uint32_t nowMs);
    void tick(uint32_t pulseCount, uint32_t flowMlPerMin, bool flowRateReady, uint32_t epoch, uint32_t nowMs);
    bool start(Irrigation::TaskType type,
               Irrigation::StartSource source,
               uint32_t planId,
               uint8_t planSlot,
               const char* planName,
               uint32_t targetSec,
               uint32_t maxWateringDurationSec,
               uint16_t flowSampleWindowSec,
               uint32_t pulseCount,
               uint32_t epoch,
               uint32_t nowMs);
    bool stop(Irrigation::StopSource source, Irrigation::StopScope scope, Irrigation::TaskResult result, uint32_t pulseCount, uint32_t epoch, uint32_t nowMs);
    bool markLeak(uint32_t pulseCount, uint32_t epoch, uint32_t nowMs);
    bool clearError(uint32_t nowMs);
    bool checkIdleLeak(uint32_t pulseCount, uint32_t nowMs, uint16_t windowSec, uint16_t threshold, uint32_t* observedPulses);
    void resetLeakWindow(uint32_t pulseCount, uint32_t nowMs);
    Irrigation::ZoneStatus status(uint32_t pulseCount, uint64_t flowRatePerMinuteX1000, uint32_t flowMlPerMin, bool flowRateReady, uint32_t nowMs) const;
    const Irrigation::ZoneConfig& config() const;
    Irrigation::ZoneState state() const;
    bool isBusy() const;
    bool isError() const;
    bool isIdle() const;

private:
    void refreshStateFromConfigAndError();
    void finish(Irrigation::TaskResult result, Irrigation::StopSource source, Irrigation::StopScope scope, uint32_t pulseCount, uint32_t epoch, uint32_t nowMs);
    void persistError(Irrigation::ZoneErrorCode code, Irrigation::StopSource source, Irrigation::TaskResult result);
    Irrigation::ZoneConfig m_config = {};
    Irrigation::ZoneState m_state = Irrigation::ZoneState::DISABLED;
    ZoneTaskRunner m_runner;
    ZoneErrorStore::ZoneError m_error = {};
    uint32_t m_leakWindowStartedMs = 0;
    uint32_t m_leakWindowStartPulses = 0;
};
