#include <cassert>
#include <cstdio>
#include <vector>
#include <utility>
#include "BoardHardware.h"
#include "BoardPins.h"

int levels[40]{};
bool attachOk = true, writeOk = true, frequencyOk = true;
std::vector<std::pair<int, int>> writes;
void digitalWrite(uint8_t pin, int value) { levels[pin] = value; writes.emplace_back(pin, value); }
void pinMode(uint8_t, int) {}
bool ledcAttachChannel(uint8_t, uint32_t, uint8_t, uint8_t) { return attachOk; }
bool ledcWriteChannel(uint8_t, uint32_t duty) {
    if (duty) assert(levels[BoardPins::kValveDriverShutdownPin] == 1 || BoardHardware::instance().activeZoneId() != 0);
    return writeOk;
}
uint32_t ledcChangeFrequency(uint8_t pin, uint32_t frequency, uint8_t) {
    bool valid = false;
    for (auto valvePin : BoardPins::kValvePins) valid |= pin == valvePin;
    assert(valid); // Core 3 takes GPIO, not channel.
    return frequencyOk ? frequency : 0;
}
void safe(const BoardHardware& hardware) {
    assert(levels[BoardPins::kValveDriverShutdownPin] == 1);
    assert(levels[BoardPins::kPumpSignalPin] == 1);
    assert(hardware.activeZoneId() == 0 && !hardware.pumpSignalActive());
}
int main() {
    auto& hardware = BoardHardware::instance();
    assert(hardware.begin(10000)); safe(hardware);
    assert(!hardware.setPumpSignal(true));
    assert(!hardware.openValve(7));
    assert(hardware.openValve(1)); assert(hardware.setPumpSignal(true));
    writes.clear(); hardware.safeShutdown(); safe(hardware);
    assert(writes.front().first == BoardPins::kValveDriverShutdownPin && writes.front().second == 1);
    assert(hardware.configureValvePwmFrequency(20000));
    attachOk = false; assert(!hardware.begin(10000)); safe(hardware);
    attachOk = true; assert(hardware.begin(10000));
    writeOk = false; assert(!hardware.openValve(2)); safe(hardware); assert(!hardware.initialized());
    writeOk = true; assert(hardware.begin(10000)); assert(hardware.openValve(2)); assert(hardware.setPumpSignal(true));
    writeOk = false; assert(!hardware.setActiveValveDuty(50)); safe(hardware);
    writeOk = true; assert(hardware.begin(10000)); frequencyOk = false;
    assert(!hardware.configureValvePwmFrequency(15000)); safe(hardware); assert(!hardware.initialized());
    assert(!hardware.begin(999)); safe(hardware);
    puts("Core 3 PWM safety: initialization, GPIO frequency, active-low pump and injected failures passed");
}
