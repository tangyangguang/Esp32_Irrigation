#pragma once

#include <stdint.h>

class ButtonInput {
public:
    ButtonInput(uint8_t pin, bool activeLow = true, uint16_t debounceMs = 30, uint16_t longPressMs = 3000, bool suppressInitialPress = false);

    void begin();
    void handle(uint32_t nowMs);

    bool wasPressed();
    bool wasLongPressed();
    bool isDown() const;

private:
    bool readPressed() const;

    uint8_t m_pin;
    bool m_activeLow;
    uint16_t m_debounceMs;
    uint16_t m_longPressMs;
    bool m_suppressInitialPress;
    bool m_stablePressed;
    bool m_lastRawPressed;
    bool m_pressEvent;
    bool m_longPressEvent;
    bool m_longPressFired;
    uint32_t m_lastRawChangeMs;
    uint32_t m_stableSinceMs;
};
