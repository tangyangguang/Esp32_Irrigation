#pragma once

#include "domain/ZoneTypes.h"

class ZoneTaskRunner {
public:
    void reset();
    bool start(Irrigation::TaskType type,
               Irrigation::StartSource source,
               uint32_t planId,
               uint8_t planSlot,
               const char* planName,
               uint32_t targetSec,
               uint32_t pulseCount,
               uint32_t epoch,
               uint32_t nowMs,
               const Irrigation::ZoneConfig& config);
    bool active() const;
    const Irrigation::ActiveTask& task() const;
    const Irrigation::TaskRuntime& runtime() const;
    const Irrigation::FinishedTask& finished() const;
    void markPulse(uint32_t pulseCount, uint32_t nowMs);
    void finish(Irrigation::TaskResult result,
                Irrigation::StopSource source,
                Irrigation::StopScope scope,
                uint32_t pulseCount,
                uint32_t epoch,
                uint32_t nowMs);
    uint32_t elapsedMs(uint32_t nowMs) const;
    uint32_t pulseDelta(uint32_t pulseCount) const;

private:
    Irrigation::ActiveTask m_task = {};
    Irrigation::TaskRuntime m_runtime = {};
    Irrigation::FinishedTask m_finished = {};
};
