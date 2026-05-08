#include "domain/MaintenanceService.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/WateringSession.h"
#include "storage/EventStore.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace {

bool g_pending = false;
bool g_clearRecords = false;
uint32_t g_requestedMs = 0;

}

namespace MaintenanceService {

void begin() {
    g_pending = false;
    g_clearRecords = false;
    g_requestedMs = 0;
}

void handle() {
    if (!g_pending || millis() - g_requestedMs < 750UL) {
        return;
    }

    WateringSession::stopAll(WateringSession::REASON_EMERGENCY_STOP, "factory reset");
    ValveController::allOff("factory reset");

    const bool settingsOk = SettingsStore::clear();
    const bool plansOk = PlanStore::clear();
    const bool recordsOk = !g_clearRecords || RecordStore::clear();
    const bool eventsOk = !g_clearRecords || EventStore::clear();
    (void)EventStore::append(EventStore::TYPE_FACTORY_RESET_EXECUTED,
                             EventStore::SOURCE_SYSTEM,
                             0,
                             g_clearRecords ? 1 : 0,
                             settingsOk && plansOk && recordsOk && eventsOk ? 1 : 0,
                             0,
                             "factory reset");
    SafetyManager::clearFactoryResetRequest();

    ESP32BASE_LOG_W("maintenance", "factory reset settings=%s plans=%s records=%s events=%s clearRecords=%s",
                    settingsOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    recordsOk ? "ok" : "fail",
                    eventsOk ? "ok" : "fail",
                    g_clearRecords ? "yes" : "no");
    Esp32BaseConfig::flushAll();
    Esp32BaseSystem::restart("irrigation factory reset");
}

bool requestFactoryReset(bool clearRecords) {
    if (g_pending) {
        return false;
    }
    g_pending = true;
    g_clearRecords = clearRecords;
    g_requestedMs = millis();
    (void)EventStore::append(EventStore::TYPE_FACTORY_RESET_REQUESTED,
                             EventStore::SOURCE_WEB,
                             0,
                             clearRecords ? 1 : 0,
                             0,
                             0,
                             "factory reset");
    ESP32BASE_LOG_W("maintenance", "factory reset requested clearRecords=%s",
                    clearRecords ? "yes" : "no");
    return true;
}

bool factoryResetPending() {
    return g_pending;
}

bool factoryResetClearRecords() {
    return g_clearRecords;
}

}
