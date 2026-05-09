#include "app/IrrigationApp.h"

#include <Esp32Base.h>

#include "domain/FlowMeter.h"
#include "domain/LeakMonitor.h"
#include "domain/MaintenanceService.h"
#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/WateringPlanScheduler.h"
#include "domain/WateringSession.h"
#include "storage/PlanStore.h"
#include "storage/PlanSkipStore.h"
#include "storage/EventStore.h"
#include "storage/RecordStore.h"
#include "storage/SettingsStore.h"
#include "web/IrrigationWeb.h"

namespace IrrigationApp {

void beginHardwareSafety() {
    ValveController::begin();
    ValveController::allOff("early boot");
}

void begin() {
    SettingsStore::begin();
    PlanStore::begin();
    PlanSkipStore::begin();
    RecordStore::begin();
    EventStore::begin();
    FlowMeter::begin();
    SafetyManager::begin();
    WateringSession::begin();
    WateringPlanScheduler::begin();
    LeakMonitor::begin();
    MaintenanceService::begin();
    IrrigationWeb::begin();
    (void)EventStore::append(EventStore::TYPE_BOOT, EventStore::SOURCE_SYSTEM, 0, 0, 0, 0, "app ready");
    ESP32BASE_LOG_I("irrigation", "application shell ready");
}

void handle() {
    FlowMeter::handle();
    SafetyManager::handle();
    WateringSession::handle();
    WateringPlanScheduler::handle();
    LeakMonitor::handle();
    ValveController::handle();
    MaintenanceService::handle();
}

}
