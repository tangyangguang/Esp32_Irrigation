#include "domain/LocalControl.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "Pins.h"
#include "domain/ZoneManager.h"
#include "io/ButtonInput.h"
#include "storage/SystemConfigStore.h"

namespace {

static constexpr uint32_t kConfirmWindowMs = 5000UL;

enum class PendingAction : uint8_t {
    NONE = 0,
    START_ZONE = 1,
    STOP_ZONE = 2,
    STOP_ALL = 3,
};

ButtonInput g_prev(IrrigationPins::ButtonPrevZone);
ButtonInput g_next(IrrigationPins::ButtonNextZone);
ButtonInput g_select(IrrigationPins::ButtonSelect);
ButtonInput g_stopAll(IrrigationPins::ButtonStopAll);
ButtonInput g_info(IrrigationPins::ButtonInfo);

uint8_t g_selectedZoneId = 0;
uint8_t g_infoPage = 0;
PendingAction g_pendingAction = PendingAction::NONE;
uint8_t g_pendingZoneId = 0;
uint32_t g_confirmDeadlineMs = 0;

bool zoneEnabled(uint8_t zoneId) {
    return Irrigation::validZoneId(zoneId) && ZoneManager::config(zoneId).enabled;
}

uint8_t firstEnabledZone() {
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        if (zoneEnabled(zoneId)) {
            return zoneId;
        }
    }
    return 0;
}

uint8_t nextEnabledZone(uint8_t from, bool reverse) {
    if (firstEnabledZone() == 0) {
        return 0;
    }
    uint8_t current = Irrigation::validZoneId(from) ? from : firstEnabledZone();
    for (uint8_t step = 0; step < Irrigation::MaxZones; ++step) {
        current = reverse
            ? static_cast<uint8_t>(current <= 1 ? Irrigation::MaxZones : current - 1)
            : static_cast<uint8_t>(current >= Irrigation::MaxZones ? 1 : current + 1);
        if (zoneEnabled(current)) {
            return current;
        }
    }
    return firstEnabledZone();
}

void cancelPending() {
    g_pendingAction = PendingAction::NONE;
    g_pendingZoneId = 0;
    g_confirmDeadlineMs = 0;
}

void ensureSelectedZone() {
    if (!zoneEnabled(g_selectedZoneId)) {
        g_selectedZoneId = firstEnabledZone();
        cancelPending();
    }
}

bool pendingMatches(PendingAction action, uint8_t zoneId, uint32_t nowMs) {
    return g_pendingAction == action &&
           g_pendingZoneId == zoneId &&
           g_confirmDeadlineMs != 0 &&
           nowMs <= g_confirmDeadlineMs;
}

void requestConfirm(PendingAction action, uint8_t zoneId, uint32_t nowMs) {
    g_pendingAction = action;
    g_pendingZoneId = zoneId;
    g_confirmDeadlineMs = nowMs + kConfirmWindowMs;
    ESP32BASE_LOG_I("local", "confirm action=%u zone=%u", static_cast<unsigned>(action), static_cast<unsigned>(zoneId));
}

void handleSelectionButtons() {
    if (g_prev.wasPressed()) {
        g_selectedZoneId = nextEnabledZone(g_selectedZoneId, true);
        cancelPending();
    }
    if (g_next.wasPressed()) {
        g_selectedZoneId = nextEnabledZone(g_selectedZoneId, false);
        cancelPending();
    }
    if (g_info.wasPressed()) {
        g_infoPage = static_cast<uint8_t>((g_infoPage + 1U) % 4U);
        cancelPending();
        ESP32BASE_LOG_I("local", "info_page=%u selected_zone=%u", static_cast<unsigned>(g_infoPage), static_cast<unsigned>(g_selectedZoneId));
    }
}

void handleSelectButton(uint32_t nowMs) {
    if (!g_select.wasPressed() || g_selectedZoneId == 0) {
        return;
    }
    const bool running = ZoneManager::isZoneBusy(g_selectedZoneId);
    const PendingAction action = running ? PendingAction::STOP_ZONE : PendingAction::START_ZONE;
    if (pendingMatches(action, g_selectedZoneId, nowMs)) {
        if (running) {
            (void)ZoneManager::stopZone(g_selectedZoneId, Irrigation::StopSource::LOCAL_BUTTON);
        } else {
            (void)ZoneManager::startManual(g_selectedZoneId,
                                           SystemConfigStore::current().manualDefaultDurationSec,
                                           Irrigation::StartSource::LOCAL_BUTTON);
        }
        cancelPending();
        return;
    }
    requestConfirm(action, g_selectedZoneId, nowMs);
}

void handleStopAllButton(uint32_t nowMs) {
    if (!g_stopAll.wasPressed()) {
        return;
    }
    if (pendingMatches(PendingAction::STOP_ALL, 0, nowMs)) {
        (void)ZoneManager::stopAll(Irrigation::StopSource::LOCAL_BUTTON);
        cancelPending();
        return;
    }
    requestConfirm(PendingAction::STOP_ALL, 0, nowMs);
}

}

namespace LocalControl {

void begin() {
    g_prev.begin();
    g_next.begin();
    g_select.begin();
    g_stopAll.begin();
    g_info.begin();
    g_selectedZoneId = firstEnabledZone();
    cancelPending();
    ESP32BASE_LOG_I("local", "ready selected_zone=%u", static_cast<unsigned>(g_selectedZoneId));
}

void handle() {
    const uint32_t nowMs = millis();
    g_prev.handle(nowMs);
    g_next.handle(nowMs);
    g_select.handle(nowMs);
    g_stopAll.handle(nowMs);
    g_info.handle(nowMs);
    ensureSelectedZone();
    if (g_confirmDeadlineMs != 0 && nowMs > g_confirmDeadlineMs) {
        cancelPending();
    }
    handleSelectionButtons();
    handleSelectButton(nowMs);
    handleStopAllButton(nowMs);
}

uint8_t selectedZoneId() {
    return g_selectedZoneId;
}

const char* pendingActionName() {
    switch (g_pendingAction) {
        case PendingAction::START_ZONE: return "confirm_start";
        case PendingAction::STOP_ZONE: return "confirm_stop_zone";
        case PendingAction::STOP_ALL: return "confirm_stop_all";
        case PendingAction::NONE:
        default: return "none";
    }
}

}
