#pragma once

#include <stdint.h>

namespace WeatherSnapshotStore {

static constexpr uint8_t ConditionMaxBytes = 24;
static constexpr uint8_t DayLabelMaxBytes = 12;

struct ForecastDay {
    char label[DayLabelMaxBytes];
    int16_t lowTempC;
    int16_t highTempC;
    uint8_t rainProbabilityPercent;
};

struct Snapshot {
    bool exists;
    uint32_t updatedEpoch;
    int16_t currentTempC;
    char condition[ConditionMaxBytes];
    uint8_t rainProbability24hPercent;
    uint8_t windLevel;
    ForecastDay days[3];
};

void begin();
bool get(Snapshot* out);
bool set(const Snapshot& snapshot);
bool clear();
bool validate(const Snapshot& snapshot);
bool isStale(const Snapshot& snapshot, uint32_t nowEpoch);

}
