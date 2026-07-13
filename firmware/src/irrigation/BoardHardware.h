#pragma once

#include <cstdint>

class BoardHardware {
public:
    static BoardHardware& instance();

    bool begin(uint32_t pwmFrequencyHz);
    void safeShutdown();

    bool configureValvePwmFrequency(uint32_t frequencyHz);
    bool openValve(uint8_t zoneId, uint8_t dutyPercent = 100);
    bool setActiveValveDuty(uint8_t dutyPercent);
    void closeValves();
    bool setPumpSignal(bool active);

    bool initialized() const;
    uint8_t activeZoneId() const;
    bool pumpSignalActive() const;
    uint32_t flowPulseCount() const;

private:
    BoardHardware() = default;

    static void onFlowPulse();
    static uint8_t dutyToRaw(uint8_t dutyPercent);
    void writeAllValveDuties(uint8_t rawDuty);

    static volatile uint32_t flowPulseCount_;
    bool initialized_ = false;
    bool pumpSignalActive_ = false;
    uint8_t activeZoneId_ = 0;
};
