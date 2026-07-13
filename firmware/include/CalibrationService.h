#pragma once

#include "IrrigationTypes.h"

namespace Irrigation {

constexpr uint8_t kCalibrationMaxVolumeSamples = 3;
constexpr uint8_t kCalibrationStableFlowSamples = 5;
constexpr uint32_t kCalibrationDefaultMaxDurationSec = 10UL * 60UL;
constexpr uint32_t kCalibrationMaxDurationSec = 60UL * 60UL;

enum class CalibrationMode : uint8_t {
    None = 0,
    FlowMeterVolume = 1,
    ZoneStandardFlow = 2,
};

struct VolumeCalibrationSample {
    bool used;
    uint32_t runId;
    uint8_t zoneId;
    uint32_t pulses;
    uint32_t measuredMl;
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
    bool standardFlowStable;
    uint8_t standardFlowSampleCount;
    uint32_t standardFlowAverageMlPerMin;
    uint32_t standardFlowMinMlPerMin;
    uint32_t standardFlowMaxMlPerMin;
    uint8_t volumeSampleCount;
    VolumeCalibrationSample volumeSamples[kCalibrationMaxVolumeSamples];
    bool volumeFitReady;
    bool volumeFitAcceptable;
    uint32_t fittedPulsesPerLiter;
    int32_t fittedStartupOffsetPulses;
    uint32_t fitMaxErrorPermille;
    uint32_t fitWaterSpreadMl;
};

class CalibrationService {
public:
    static void begin();
    static void handle();

    static bool startVolumeCalibration(uint8_t zoneId, uint32_t maxDurationSec, RunReason& reason);
    static bool startStandardFlowCalibration(uint8_t zoneId, uint32_t maxDurationSec, RunReason& reason);
    static bool stop();
    static void clearResult();

    static bool addVolumeSample(uint32_t measuredMl);
    static bool saveFittedPulsesPerLiter();
    static void clearVolumeSamples();
    static bool saveZoneStandardFlow(uint8_t zoneId, uint32_t flowMlPerMin);

    static CalibrationSnapshot snapshot();
    static const char* lastError();

private:
    static bool startSession(CalibrationMode mode, uint8_t zoneId, uint32_t durationSec, RunReason& reason);
    static void setLastError(const char* error);
};

} // namespace Irrigation
