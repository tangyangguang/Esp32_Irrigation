#include "domain/SafetyManager.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"
#include "domain/ZoneManager.h"
#include "io/ButtonInput.h"
#include "storage/SystemConfigStore.h"

namespace {

ButtonInput g_stopAll(IrrigationPins::StopAllButton);
ButtonInput g_zone1(IrrigationPins::Road1UpButton);
ButtonInput g_zone2(IrrigationPins::Road2DownButton);
ButtonInput g_startOk(IrrigationPins::StartOkButton);
ButtonInput g_menuBack(IrrigationPins::MenuBackButton);
ButtonInput g_lock(IrrigationPins::LockButton);
ButtonInput g_factoryReset(IrrigationPins::FactoryResetButton, true, 30, 3000, true);

bool g_locked = false;
bool g_factoryResetRequested = false;

void handleStopButtons() {
    if (g_stopAll.wasPressed()) {
        (void)ZoneManager::stopAll(Irrigation::StopSource::LOCAL_BUTTON);
    }
    if (g_zone1.wasPressed()) {
        if (!ZoneManager::stopZone(1, Irrigation::StopSource::LOCAL_BUTTON)) {
            (void)ZoneManager::startManual(1, SystemConfigStore::current().manualDefaultDurationSec, Irrigation::StartSource::LOCAL_BUTTON);
        }
    }
    if (g_zone2.wasPressed()) {
        if (!ZoneManager::stopZone(2, Irrigation::StopSource::LOCAL_BUTTON)) {
            (void)ZoneManager::startManual(2, SystemConfigStore::current().manualDefaultDurationSec, Irrigation::StartSource::LOCAL_BUTTON);
        }
    }
}

void handleNormalButtons() {
    if (g_lock.wasPressed()) {
        (void)SafetyManager::setLocked(!g_locked);
    }

    if (g_locked) {
        (void)g_startOk.wasPressed();
        (void)g_menuBack.wasPressed();
        return;
    }

    if (g_startOk.wasPressed()) {
        for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
            if (ZoneManager::config(zoneId).enabled) {
                (void)ZoneManager::startManual(zoneId, SystemConfigStore::current().manualDefaultDurationSec, Irrigation::StartSource::LOCAL_BUTTON);
            }
        }
    }
    if (g_menuBack.wasPressed()) {
        ESP32BASE_LOG_I("button", "menu_back pressed");
    }
}

}

namespace SafetyManager {

void begin() {
    g_locked = false;
    g_factoryResetRequested = false;
    g_stopAll.begin();
    g_zone1.begin();
    g_zone2.begin();
    g_startOk.begin();
    g_menuBack.begin();
    g_lock.begin();
    g_factoryReset.begin();
    ESP32BASE_LOG_I("safety", "ready locked=%s", g_locked ? "yes" : "no");
}

void handle() {
    const uint32_t now = millis();
    g_stopAll.handle(now);
    g_zone1.handle(now);
    g_zone2.handle(now);
    g_startOk.handle(now);
    g_menuBack.handle(now);
    g_lock.handle(now);
    g_factoryReset.handle(now);

    handleStopButtons();
    handleNormalButtons();

    if (g_factoryReset.wasLongPressed()) {
        if (!g_lock.isDown()) {
            ESP32BASE_LOG_W("safety", "factory reset gpio0 long press ignored: lock button not held");
            return;
        }
        g_factoryResetRequested = true;
        ESP32BASE_LOG_W("safety", "factory reset requested by lock+gpio0 long press");
    }
}

bool isLocked() {
    return g_locked;
}

bool setLocked(bool locked) {
    if (g_locked == locked) {
        return true;
    }
    g_locked = locked;
    return true;
}

bool factoryResetRequested() {
    return g_factoryResetRequested;
}

void clearFactoryResetRequest() {
    g_factoryResetRequested = false;
}

}
