#pragma once
#include <cstdint>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define RISING 1
#define IRAM_ATTR
void digitalWrite(uint8_t pin, int value);
void pinMode(uint8_t pin, int mode);
bool ledcAttachChannel(uint8_t pin, uint32_t frequency, uint8_t bits, uint8_t channel);
bool ledcWriteChannel(uint8_t channel, uint32_t duty);
uint32_t ledcChangeFrequency(uint8_t pin, uint32_t frequency, uint8_t bits);
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
