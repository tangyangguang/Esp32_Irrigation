#include "domain/MaintenanceService.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/ZoneManager.h"
#include "storage/EventStore.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/ScheduleSkipStore.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"
#include "storage/ZoneErrorStore.h"

namespace {

bool g_pending = false;
bool g_clearRecords = false;
uint32_t g_requestedMs = 0;
Irrigation::EventSource g_requestSource = Irrigation::EventSource::SYSTEM;
static constexpr uint32_t kFactoryResetResponseDelayMs = 750UL;

}

namespace MaintenanceService {

void begin() {
    g_pending = false;
    g_clearRecords = false;
    g_requestedMs = 0;
    g_requestSource = Irrigation::EventSource::SYSTEM;
}

void handle() {
    if (!g_pending && SafetyManager::factoryResetRequested()) {
        g_pending = true;
        g_clearRecords = false;
        g_requestedMs = millis();
        g_requestSource = Irrigation::EventSource::BUTTON;
        ESP32BASE_LOG_W("maintenance", "factory reset requested by button");
    }
    if (!g_pending || millis() - g_requestedMs < kFactoryResetResponseDelayMs) {
        return;
    }

    (void)ZoneManager::stopAll(Irrigation::StopSource::FACTORY_RESET, Irrigation::TaskResult::FACTORY_RESET_PROTECTED);
    ValveController::allOff("factory reset");

    const bool systemOk = SystemConfigStore::clear();
    const bool zonesOk = ZoneConfigStore::clear();
    const bool errorsOk = ZoneErrorStore::clear();
    const bool plansOk = PlanStore::clear();
    const bool skipsOk = ScheduleSkipStore::clear();
    const bool recordsOk = !g_clearRecords || RecordStore::clear();
    if (!g_clearRecords) {
        (void)EventStore::append(Irrigation::EventType::FACTORY_RESET_EXECUTED,
                                 g_requestSource,
                                 0,
                                 0,
                                 systemOk && zonesOk && errorsOk && plansOk && skipsOk && recordsOk ? 1 : 0,
                                 0,
                                 "factory reset");
    }
    const bool eventsOk = !g_clearRecords || EventStore::clear();
    SafetyManager::clearFactoryResetRequest();

    ESP32BASE_LOG_W("maintenance", "factory reset system=%s zones=%s errors=%s plans=%s skips=%s records=%s events=%s clearRecords=%s",
                    systemOk ? "ok" : "fail",
                    zonesOk ? "ok" : "fail",
                    errorsOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    skipsOk ? "ok" : "fail",
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
    g_requestSource = Irrigation::EventSource::WEB;
    (void)EventStore::append(Irrigation::EventType::FACTORY_RESET_REQUESTED,
                             Irrigation::EventSource::WEB,
                             0,
                             clearRecords ? 1 : 0,
                             0,
                             0,
                             "factory reset");
    return true;
}

bool factoryResetPending() {
    return g_pending;
}

bool factoryResetClearRecords() {
    return g_clearRecords;
}

}
