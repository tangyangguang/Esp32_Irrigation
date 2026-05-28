#include "domain/MaintenanceService.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/WateringSession.h"
#include "storage/EventStore.h"
#include "storage/PlanResultStore.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"

namespace {

bool g_pending = false;
bool g_clearRecords = false;
uint32_t g_requestedMs = 0;
EventStore::Source g_requestSource = EventStore::SOURCE_SYSTEM;
static constexpr uint32_t kFactoryResetResponseDelayMs = 750UL;

}

namespace MaintenanceService {

void begin() {
    g_pending = false;
    g_clearRecords = false;
    g_requestedMs = 0;
    g_requestSource = EventStore::SOURCE_SYSTEM;
}

void handle() {
    if (!g_pending && SafetyManager::factoryResetRequested()) {
        g_pending = true;
        g_clearRecords = false;
        g_requestedMs = millis();
        g_requestSource = EventStore::SOURCE_BUTTON;
        ESP32BASE_LOG_W("maintenance", "factory reset requested by gpio0 long press");
    }
    if (!g_pending || millis() - g_requestedMs < kFactoryResetResponseDelayMs) {
        return;
    }

    WateringSession::stopAll(RecordStore::SOURCE_FACTORY_RESET, RecordStore::RESULT_FACTORY_RESET_PROTECTED, "factory reset");
    ValveController::allOff("factory reset");

    const bool settingsOk = SettingsStore::clear();
    const bool plansOk = PlanStore::clear();
    const bool planResultsOk = PlanResultStore::clear();
    const bool recordsOk = !g_clearRecords || RecordStore::clear();
    if (!g_clearRecords) {
        (void)EventStore::append(EventStore::TYPE_FACTORY_RESET_EXECUTED,
                                 g_requestSource,
                                 0,
                                 0,
                                 settingsOk && plansOk && planResultsOk && recordsOk ? 1 : 0,
                                 0,
                                 "factory reset");
    }
    const bool eventsOk = !g_clearRecords || EventStore::clear();
    SafetyManager::clearFactoryResetRequest();

    ESP32BASE_LOG_W("maintenance", "factory reset settings=%s plans=%s planResults=%s records=%s events=%s clearRecords=%s",
                    settingsOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    planResultsOk ? "ok" : "fail",
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
    g_requestSource = EventStore::SOURCE_WEB;
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
