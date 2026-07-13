#pragma once

#include <cstdint>

class WateringHardware {
public:
    virtual bool openValve(uint8_t zoneId, uint8_t dutyPercent) = 0;
    virtual bool setActiveValveDuty(uint8_t dutyPercent) = 0;
    virtual void closeValves() = 0;
    virtual bool setPumpSignal(bool active) = 0;
    virtual void safeShutdown() = 0;
    virtual uint32_t flowPulseCount() const = 0;

protected:
    ~WateringHardware() = default;
};
