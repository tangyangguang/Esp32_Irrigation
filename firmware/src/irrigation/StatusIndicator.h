#pragma once

#include <cstdint>

class StatusIndicator {
public:
    enum class Mode : uint8_t {
        Startup,
        ReadyIdle,
        Active,
        Critical,
    };

    static StatusIndicator& instance();

    void begin(uint32_t nowMs);
    void setMode(Mode mode, uint32_t nowMs);
    void handle(uint32_t nowMs);

    Mode mode() const;
    static bool outputOnForMode(Mode mode, uint32_t elapsedMs) {
        switch (mode) {
            case Mode::ReadyIdle:
                return true;
            case Mode::Startup:
                return (elapsedMs / 1000U) % 2U == 0U;
            case Mode::Active:
                return (elapsedMs / 500U) % 2U == 0U;
            case Mode::Critical:
                return (elapsedMs / 125U) % 2U == 0U;
        }
        return false;
    }

private:
    StatusIndicator() = default;

    bool desiredOn(uint32_t nowMs) const;
    void write(bool on);

    Mode mode_ = Mode::Startup;
    uint32_t modeStartedMs_ = 0;
    bool initialized_ = false;
    bool outputOn_ = false;
};
