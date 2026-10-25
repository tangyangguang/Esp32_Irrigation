#pragma once

#include <cstdint>

#include "IrrigationTypes.h"

// Persists the last *normally completed* watering's 30 s whole-task flow
// overview to a single LittleFS file. Only one overview is kept; the next
// completed watering overwrites it. All methods run on the service task;
// failures never block watering or record keeping.
class WateringFlowOverviewStore {
public:
    struct Snapshot {
        bool valid = false;
        uint32_t startedEpoch = 0;
        uint32_t recordId = 0;
        uint8_t zoneCount = 0;
        uint16_t pointCount = 0;
        std::array<uint16_t, BoardPins::kZoneCount> zonePointCount{};
        std::array<uint32_t, BoardPins::kZoneCount> zoneBaselineFlow{};
        std::array<uint32_t, kFlowOverviewSampleCount> points{};
    };

    void begin();

    // Load the persisted overview (if any) into the given snapshot.
    const Snapshot& loaded() const { return loaded_; }
    bool hasLoaded() const { return loaded_.valid; }

    // Atomically persist an overview. zonePointCount has zoneCount entries and
    // points has pointCount entries (pointCount <= kFlowOverviewSampleCount).
    bool save(uint32_t recordId,
              uint32_t startedEpoch,
              uint8_t zoneCount,
              const uint16_t* zonePointCount,
              const uint32_t* zoneBaselineFlow,
              const uint32_t* points,
              uint16_t pointCount);

    // Remove the file (filesystem format / explicit discard).
    void discard();

private:
    Snapshot loaded_{};
};
