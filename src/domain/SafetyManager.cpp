#include "domain/SafetyManager.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"
#include "domain/ValveController.h"
#include "domain/WateringSession.h"
#include "io/ButtonInput.h"
#include "storage/EventStore.h"
#include "storage/SettingsStore.h"

namespace {

ButtonInput g_stopAll(IrrigationPins::StopAllButton);
ButtonInput g_road1(IrrigationPins::Road1UpButton);
ButtonInput g_road2(IrrigationPins::Road2DownButton);
ButtonInput g_startOk(IrrigationPins::StartOkButton);
ButtonInput g_menuBack(IrrigationPins::MenuBackButton);
ButtonInput g_lock(IrrigationPins::LockButton);
ButtonInput g_factoryReset(IrrigationPins::FactoryResetButton, true, 30, 3000);

bool g_locked = false;
bool g_factoryResetRequested = false;

void handleStopButtons() {
    if (g_stopAll.wasPressed()) {
        WateringSession::stopAll(WateringSession::REASON_EMERGENCY_STOP, "button stop all");
    }
    if (g_road1.wasPressed()) {
        WateringSession::stopRoad(ValveController::Road1, WateringSession::REASON_MANUAL_STOP, "button stop r1");
    }
    if (g_road2.wasPressed()) {
        WateringSession::stopRoad(ValveController::Road2, WateringSession::REASON_MANUAL_STOP, "button stop r2");
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
        const SettingsStore::Settings& settings = SettingsStore::current();
        const uint16_t road1Sec = SettingsStore::isRoadEnabled(1) ? settings.quickDurationSec[0] : 0;
        const uint16_t road2Sec = SettingsStore::isRoadEnabled(2) ? settings.quickDurationSec[1] : 0;
        WateringSession::startManual(road1Sec, road2Sec, settings.defaultMode, RecordStore::SOURCE_BUTTON, "button start");
    }
    if (g_menuBack.wasPressed()) {
        ESP32BASE_LOG_I("button", "menu_back pressed");
    }
}

}

namespace SafetyManager {

void begin() {
    g_locked = SettingsStore::current().keypadLocked;
    ValveController::begin();
    ValveController::allOff("safety begin");

    g_stopAll.begin();
    g_road1.begin();
    g_road2.begin();
    g_startOk.begin();
    g_menuBack.begin();
    g_lock.begin();
    g_factoryReset.begin();

    ESP32BASE_LOG_I("safety", "ready locked=%s", g_locked ? "yes" : "no");
}

void handle() {
    const uint32_t now = millis();
    g_stopAll.handle(now);
    g_road1.handle(now);
    g_road2.handle(now);
    g_startOk.handle(now);
    g_menuBack.handle(now);
    g_lock.handle(now);
    g_factoryReset.handle(now);

    handleStopButtons();
    handleNormalButtons();

    if (g_factoryReset.wasLongPressed()) {
        g_factoryResetRequested = true;
        (void)EventStore::append(EventStore::TYPE_FACTORY_RESET_REQUESTED,
                                 EventStore::SOURCE_BUTTON,
                                 0,
                                 0,
                                 0,
                                 0,
                                 "gpio0 long press");
        ESP32BASE_LOG_W("safety", "factory reset confirmation requested");
    }
}

bool isLocked() {
    return g_locked;
}

bool setLocked(bool locked) {
    if (g_locked == locked) {
        return true;
    }
    if (!SettingsStore::setKeypadLocked(locked)) {
        ESP32BASE_LOG_W("safety", "keypad lock save failed");
        return false;
    }
    g_locked = locked;
    (void)EventStore::append(EventStore::TYPE_CONFIG_CHANGED,
                             EventStore::SOURCE_BUTTON,
                             0,
                             0,
                             locked ? 1 : 0,
                             0,
                             "keypad lock");
    ESP32BASE_LOG_I("safety", "keypad %s", g_locked ? "locked" : "unlocked");
    return true;
}

bool factoryResetRequested() {
    return g_factoryResetRequested;
}

void clearFactoryResetRequest() {
    g_factoryResetRequested = false;
}

}
