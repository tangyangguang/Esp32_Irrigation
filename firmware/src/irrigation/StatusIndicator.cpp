#include "StatusIndicator.h"

#include <Arduino.h>

#include "BoardPins.h"

StatusIndicator& StatusIndicator::instance() {
    static StatusIndicator indicator;
    return indicator;
}

void StatusIndicator::begin(uint32_t nowMs) {
    // LED1 is wired from 3.3 V through R17 to GPIO13 and is active-low.
    digitalWrite(BoardPins::kStatusLedPin, HIGH);
    pinMode(BoardPins::kStatusLedPin, OUTPUT);
    initialized_ = true;
    outputOn_ = false;
    mode_ = Mode::Startup;
    modeStartedMs_ = nowMs;
    handle(nowMs);
}

void StatusIndicator::setMode(Mode mode, uint32_t nowMs) {
    if (mode_ == mode) return;
    mode_ = mode;
    modeStartedMs_ = nowMs;
    handle(nowMs);
}

void StatusIndicator::handle(uint32_t nowMs) {
    if (!initialized_) return;
    write(desiredOn(nowMs));
}

StatusIndicator::Mode StatusIndicator::mode() const {
    return mode_;
}

bool StatusIndicator::desiredOn(uint32_t nowMs) const {
    return outputOnForMode(mode_, nowMs - modeStartedMs_);
}

void StatusIndicator::write(bool on) {
    if (outputOn_ == on) return;
    digitalWrite(BoardPins::kStatusLedPin, on ? LOW : HIGH);
    outputOn_ = on;
}
