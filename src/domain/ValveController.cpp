#include "domain/ValveController.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"

namespace {

static constexpr uint8_t kValvePins[] = {
    IrrigationPins::Valve1,
    IrrigationPins::Valve2,
};

static bool g_ready = false;
static bool g_open[2] = {false, false};

bool roadIndex(uint8_t road, uint8_t* index) {
    if (road < 1 || road > IrrigationPins::MaxRoads) {
        return false;
    }
    *index = road - 1;
    return true;
}

void writeValve(uint8_t index, bool open) {
    digitalWrite(kValvePins[index], open ? HIGH : LOW);
    g_open[index] = open;
}

bool canLog() {
    return Esp32Base::isReady();
}

}

namespace ValveController {

void begin() {
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        pinMode(kValvePins[i], OUTPUT);
        writeValve(i, false);
    }
    g_ready = true;
}

void handle() {
}

bool setRoad(uint8_t road, bool open, const char* reason) {
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        if (canLog()) {
            ESP32BASE_LOG_W("valve", "invalid road=%u action=%s reason=%s",
                            static_cast<unsigned>(road),
                            open ? "open" : "close",
                            reason ? reason : "");
        }
        return false;
    }
    if (!g_ready) {
        begin();
    }
    if (g_open[index] != open) {
        writeValve(index, open);
        if (canLog()) {
            ESP32BASE_LOG_I("valve", "road=%u state=%s reason=%s",
                            static_cast<unsigned>(road),
                            open ? "open" : "closed",
                            reason ? reason : "");
        }
    }
    return true;
}

bool off(uint8_t road, const char* reason) {
    return setRoad(road, false, reason);
}

void allOff(const char* reason) {
    if (!g_ready) {
        begin();
    }
    for (uint8_t i = 0; i < IrrigationPins::MaxRoads; ++i) {
        if (g_open[i]) {
            writeValve(i, false);
        } else {
            digitalWrite(kValvePins[i], LOW);
        }
    }
    if (canLog()) {
        ESP32BASE_LOG_I("valve", "all closed reason=%s", reason ? reason : "");
    }
}

bool isOpen(uint8_t road) {
    uint8_t index = 0;
    if (!roadIndex(road, &index)) {
        return false;
    }
    return g_open[index];
}

}
