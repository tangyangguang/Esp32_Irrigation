#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

enum class CalibrationMode : uint8_t {
    None = 0,
    FlowMeterVolume = 1,
    ZoneStandardFlow = 2,
};

struct CalibrationSnapshot {
    CalibrationMode mode;
    bool running;
    bool resultReady;
    uint32_t runId;
    uint8_t zoneId;
    uint32_t durationSec;
    uint32_t pulses;
    uint32_t measuredMl;
    uint32_t computedPulsesPerLiter;
    uint32_t suggestedFlowMlPerMin;
};

class CalibrationService {
public:
    static void begin();
    static void handle();

    static bool startVolumeCalibration(uint8_t zoneId, uint32_t durationSec, RunReason& reason);
    static bool startStandardFlowCalibration(uint8_t zoneId, uint32_t durationSec, RunReason& reason);
    static bool stop();
    static void clearResult();

    static bool savePulsesPerLiter(uint32_t measuredMl);
    static bool saveZoneStandardFlow(uint8_t zoneId, uint32_t flowMlPerMin);

    static CalibrationSnapshot snapshot();
    static const char* lastError();

private:
    static bool startSession(CalibrationMode mode, uint8_t zoneId, uint32_t durationSec, RunReason& reason);
    static void setLastError(const char* error);
};

} // namespace Irrigation
