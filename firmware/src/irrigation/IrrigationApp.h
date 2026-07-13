#pragma once

#include "IrrigationConfigStore.h"
#include "WateringController.h"

class IrrigationApp {
public:
    static IrrigationApp& instance();

    bool begin();
    void handle();

    bool baseReady() const;
    bool businessReady() const;

private:
    IrrigationApp();
    void advanceBusiness();

    bool started_ = false;
    bool baseReady_ = false;
    bool businessReady_ = false;
    IrrigationConfigStore configStore_;
    WateringController wateringController_;
};
