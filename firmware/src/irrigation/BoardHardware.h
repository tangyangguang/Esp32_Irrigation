#pragma once

#include <cstdint>

#include "WateringHardware.h"

class BoardHardware final : public WateringHardware {
public:
    static BoardHardware& instance();

    bool begin(uint32_t pwmFrequencyHz);
    void safeShutdown() override;

    bool configureValvePwmFrequency(uint32_t frequencyHz);
    bool openValve(uint8_t zoneId, uint8_t dutyPercent = 100) override;
    bool setActiveValveDuty(uint8_t dutyPercent) override;
    void closeValves() override;
    bool setPumpSignal(bool active) override;

    bool initialized() const;
    uint8_t activeZoneId() const;
    bool pumpSignalActive() const;
    uint32_t flowPulseCount() const override;

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
