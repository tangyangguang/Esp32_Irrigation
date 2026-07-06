#include "domain/ValveController.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/ZoneTypes.h"
#include "Pins.h"

namespace {

static constexpr uint8_t kValvePins[] = {
    IrrigationPins::Valve1,
    IrrigationPins::Valve2,
    IrrigationPins::Valve3,
    IrrigationPins::Valve4,
    IrrigationPins::Valve5,
    IrrigationPins::Valve6,
};

static constexpr uint8_t kPwmChannels[] = {0, 1, 2, 3, 4, 5};

static bool g_ready = false;
static bool g_open[Irrigation::MaxZones] = {};
static uint32_t g_openedMs[Irrigation::MaxZones] = {};

bool zoneIndex(uint8_t zoneId, uint8_t* index) {
    if (zoneId < 1 || zoneId > Irrigation::MaxZones) {
        return false;
    }
    *index = zoneId - 1;
    return true;
}

void writeValve(uint8_t index, bool open) {
    ledcWrite(kPwmChannels[index], open ? 255 : 0);
    g_open[index] = open;
    g_openedMs[index] = open ? millis() : 0;
}

bool canLog() {
    return Esp32Base::isReady();
}

}

namespace ValveController {

void begin() {
    for (uint8_t i = 0; i < Irrigation::MaxZones; ++i) {
        pinMode(kValvePins[i], OUTPUT);
        ledcSetup(kPwmChannels[i], IrrigationPins::ValvePwmFrequency, 8);
        ledcAttachPin(kValvePins[i], kPwmChannels[i]);
        writeValve(i, false);
    }
    g_ready = true;
}

void handle() {
    const uint32_t now = millis();
    const uint32_t holdDuty = static_cast<uint32_t>(IrrigationPins::ValveHoldDutyPercent) * 255UL / 100UL;
    for (uint8_t i = 0; i < Irrigation::MaxZones; ++i) {
        if (g_open[i] && g_openedMs[i] != 0 && now - g_openedMs[i] >= IrrigationPins::ValvePullInMs) {
            ledcWrite(kPwmChannels[i], holdDuty);
        }
    }
}

bool setZone(uint8_t zoneId, bool open, const char* reason) {
    uint8_t index = 0;
    if (!zoneIndex(zoneId, &index)) {
        if (canLog()) {
            ESP32BASE_LOG_W("valve", "invalid zone=%u action=%s reason=%s",
                            static_cast<unsigned>(zoneId),
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
            ESP32BASE_LOG_I("valve", "zone=%u state=%s reason=%s",
                            static_cast<unsigned>(zoneId),
                            open ? "open" : "closed",
                            reason ? reason : "");
        }
    }
    return true;
}

bool off(uint8_t zoneId, const char* reason) {
    return setZone(zoneId, false, reason);
}

void allOff(const char* reason) {
    if (!g_ready) {
        begin();
    }
    for (uint8_t i = 0; i < Irrigation::MaxZones; ++i) {
        if (g_open[i]) {
            writeValve(i, false);
        } else {
            ledcWrite(kPwmChannels[i], 0);
        }
    }
    if (canLog()) {
        ESP32BASE_LOG_I("valve", "all closed reason=%s", reason ? reason : "");
    }
}

bool isOpen(uint8_t zoneId) {
    uint8_t index = 0;
    if (!zoneIndex(zoneId, &index)) {
        return false;
    }
    return g_open[index];
}

}
