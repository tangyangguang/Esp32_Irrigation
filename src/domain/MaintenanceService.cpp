#include "domain/MaintenanceService.h"

#include <Arduino.h>
#include <Esp32Base.h>

#include "domain/BusinessEventLog.h"
#include "domain/PlanExecutionTracker.h"
#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/ZoneManager.h"
#include "storage/FlowAlertStore.h"
#include "storage/PlanStore.h"
#include "storage/ScheduleSkipStore.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"
#include "storage/ZoneErrorStore.h"

namespace {

bool g_pending = false;
uint32_t g_requestedMs = 0;
const char* g_requestSource = "runtime";
static constexpr uint32_t kFactoryResetResponseDelayMs = 750UL;

}

namespace MaintenanceService {

void begin() {
    g_pending = false;
    g_requestedMs = 0;
    g_requestSource = "runtime";
}

void handle() {
    if (!g_pending && SafetyManager::factoryResetRequested()) {
        g_pending = true;
        g_requestedMs = millis();
        g_requestSource = "button";
        BusinessEventLog::appendFactoryResetRequested(g_requestSource);
        ESP32BASE_LOG_W("maintenance", "factory reset requested by button");
    }
    if (!g_pending || millis() - g_requestedMs < kFactoryResetResponseDelayMs) {
        return;
    }

    (void)ZoneManager::stopAll(Irrigation::StopSource::FACTORY_RESET, Irrigation::TaskResult::FACTORY_RESET_PROTECTED);
    ValveController::allOff("factory reset");

    const bool systemOk = SystemConfigStore::clear();
    const bool flowAlertsOk = FlowAlertStore::clear();
    const bool zonesOk = ZoneConfigStore::clear();
    const bool errorsOk = ZoneErrorStore::clear();
    const bool plansOk = PlanStore::clear();
    const bool planExecOk = PlanExecutionTracker::clearPersistent();
    const bool skipsOk = ScheduleSkipStore::clear();
    BusinessEventLog::appendFactoryResetExecuted(systemOk && flowAlertsOk && zonesOk && errorsOk && plansOk && planExecOk && skipsOk,
                                                 g_requestSource);
    SafetyManager::clearFactoryResetRequest();

    ESP32BASE_LOG_W("maintenance", "factory reset system=%s flowAlerts=%s zones=%s errors=%s plans=%s planExec=%s skips=%s",
                    systemOk ? "ok" : "fail",
                    flowAlertsOk ? "ok" : "fail",
                    zonesOk ? "ok" : "fail",
                    errorsOk ? "ok" : "fail",
                    plansOk ? "ok" : "fail",
                    planExecOk ? "ok" : "fail",
                    skipsOk ? "ok" : "fail");
    Esp32BaseConfig::flushAll();
    Esp32BaseSystem::restart("irrigation factory reset");
}

bool factoryResetPending() {
    return g_pending;
}

}
