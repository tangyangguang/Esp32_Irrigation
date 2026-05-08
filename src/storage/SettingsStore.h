#pragma once

#include <stdint.h>

namespace SettingsStore {

enum ExecutionMode : uint8_t {
    MODE_SIMULTANEOUS = 0,
    MODE_SEQUENTIAL = 1,
};

struct RoadConfig {
    char name[12];
    uint16_t pulsePerLiter;
    uint16_t calibrationX1000;
};

struct Settings {
    uint8_t enabledRoads;
    ExecutionMode defaultMode;
    uint16_t quickDurationSec[2];
    uint8_t flowNoPulseTimeoutSec;
    uint8_t idleLeakWindowSec;
    uint8_t idleLeakPulseThreshold;
    bool keypadLocked;
    RoadConfig roads[2];
};

void begin();
const Settings& current();
bool clear();

uint8_t enabledRoads();
bool isRoadEnabled(uint8_t road);
ExecutionMode defaultExecutionMode();
const char* executionModeName(ExecutionMode mode);
bool parseExecutionMode(const char* text, ExecutionMode* mode);

bool setEnabledRoads(uint8_t roads);
bool setDefaultExecutionMode(ExecutionMode mode);
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
