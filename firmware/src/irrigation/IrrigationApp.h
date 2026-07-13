#pragma once

class IrrigationApp {
public:
    static IrrigationApp& instance();

    bool begin();
    void handle();

    bool baseReady() const;
    bool businessReady() const;

private:
    IrrigationApp() = default;
    void advanceBusiness();

    bool started_ = false;
    bool baseReady_ = false;
    bool businessReady_ = false;
};
