#include "domain/MaintenanceService.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/BusinessEventLog.h"
#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/ZoneManager.h"
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
const char* g_requestSource = "runtime";
static constexpr uint32_t kFactoryResetResponseDelayMs = 750UL;

}

namespace MaintenanceService {

void begin() {
    g_pending = false;
    g_clearRecords = false;
    g_requestedMs = 0;
    g_requestSource = "runtime";
}

void handle() {
    if (!g_pending && SafetyManager::factoryResetRequested()) {
        g_pending = true;
        g_clearRecords = false;
        g_requestedMs = millis();
        g_requestSource = "button";
        BusinessEventLog::appendFactoryResetRequested(false, g_requestSource);
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
    const bool eventsOk = !g_clearRecords || Esp32BaseAppEventLog::clear();
    if (!g_clearRecords) {
        BusinessEventLog::appendFactoryResetExecuted(systemOk && zonesOk && errorsOk && plansOk && skipsOk && recordsOk && eventsOk,
                                                     false,
                                                     g_requestSource);
    }
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
    g_requestSource = "web";
    BusinessEventLog::appendFactoryResetRequested(clearRecords, g_requestSource);
    return true;
}

bool factoryResetPending() {
    return g_pending;
}

bool factoryResetClearRecords() {
    return g_clearRecords;
}

}
