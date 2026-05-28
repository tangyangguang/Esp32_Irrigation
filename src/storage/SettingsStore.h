#pragma once

#include <stdint.h>

namespace SettingsStore {

struct RoadConfig {
    char name[12];
    uint16_t pulsePerLiter;
    uint16_t calibrationX1000;
};

struct Settings {
    // Bit mask, not count: bit0 enables road 1 ... bit3 enables road 4.
    uint8_t roadEnabledMask;
    uint16_t quickDurationSec[4];
    uint8_t flowNoPulseTimeoutSec;
    uint8_t idleLeakWindowSec;
    uint8_t idleLeakPulseThreshold;
    bool keypadLocked;
    RoadConfig roads[4];
};

void begin();
const Settings& current();
bool clear();

uint8_t enabledRoads();
uint8_t roadEnabledMask();
bool isRoadEnabled(uint8_t road);

bool setEnabledRoads(uint8_t roads);
bool setRoadEnabled(uint8_t road, bool enabled);
bool setRoadEnabledMask(uint8_t mask);
bool setQuickDurationSec(uint8_t road, uint16_t seconds);
bool setFlowNoPulseTimeoutSec(uint8_t seconds);
bool setIdleLeakWindowSec(uint8_t seconds);
bool setIdleLeakPulseThreshold(uint8_t pulses);
bool setKeypadLocked(bool locked);
bool setRoadName(uint8_t road, const char* name);
bool setRoadPulsePerLiter(uint8_t road, uint16_t pulsePerLiter);
bool setRoadCalibrationX1000(uint8_t road, uint16_t calibrationX1000);
uint32_t estimateMilliliters(uint8_t road, uint32_t pulses);

}
