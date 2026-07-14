#pragma once

#include <cstdint>

#include <Esp32Base.h>

class DeviceAliveCheckpoint {
public:
    bool begin();
    void handle(const Esp32BaseTime::Snapshot& now,
                uint8_t intervalHours,
                bool wateringActive,
                uint64_t activitySequence);

    uint32_t lastKnownAliveEpoch() const;
    bool storageFault() const;

private:
    bool save(uint32_t epochSec);

    uint32_t checkpointEpoch_ = 0;
    uint32_t lastActivityEpoch_ = 0;
    uint64_t observedActivitySequence_ = 0;
    bool timeInitialized_ = false;
    bool storageFault_ = false;
};
