#include "io/ButtonInput.h"

#include <Arduino.h>

ButtonInput::ButtonInput(uint8_t pin, bool activeLow, uint16_t debounceMs, uint16_t longPressMs, bool suppressInitialPress)
    : m_pin(pin),
      m_activeLow(activeLow),
      m_debounceMs(debounceMs),
      m_longPressMs(longPressMs),
      m_suppressInitialPress(suppressInitialPress),
      m_stablePressed(false),
      m_lastRawPressed(false),
      m_pressEvent(false),
      m_longPressEvent(false),
      m_longPressFired(false),
      m_lastRawChangeMs(0),
      m_stableSinceMs(0) {
}

void ButtonInput::begin() {
    pinMode(m_pin, m_activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    const uint32_t now = millis();
    m_lastRawPressed = readPressed();
    m_stablePressed = m_lastRawPressed;
    m_lastRawChangeMs = now;
    m_stableSinceMs = now;
    m_pressEvent = false;
    m_longPressEvent = false;
    m_longPressFired = m_suppressInitialPress && m_stablePressed;
}

void ButtonInput::handle(uint32_t nowMs) {
    const bool rawPressed = readPressed();
    if (rawPressed != m_lastRawPressed) {
        m_lastRawPressed = rawPressed;
        m_lastRawChangeMs = nowMs;
    }

    if ((nowMs - m_lastRawChangeMs) >= m_debounceMs && rawPressed != m_stablePressed) {
        m_stablePressed = rawPressed;
        m_stableSinceMs = nowMs;
        if (m_stablePressed) {
            m_pressEvent = true;
            m_longPressFired = false;
        } else {
            m_longPressFired = false;
        }
    }

    if (m_stablePressed && !m_longPressFired && (nowMs - m_stableSinceMs) >= m_longPressMs) {
        m_longPressFired = true;
        m_longPressEvent = true;
    }
}

bool ButtonInput::wasPressed() {
    const bool value = m_pressEvent;
    m_pressEvent = false;
    return value;
}

bool ButtonInput::wasLongPressed() {
    const bool value = m_longPressEvent;
    m_longPressEvent = false;
    return value;
}

bool ButtonInput::isDown() const {
    return m_stablePressed;
}

bool ButtonInput::readPressed() const {
    const int value = digitalRead(m_pin);
    return m_activeLow ? value == LOW : value == HIGH;
}
