#include "app/IrrigationApp.h"

#include <Esp32Base.h>

#include "domain/FlowMeter.h"
#include "domain/FlowCalibration.h"
#include "domain/MaintenanceService.h"
#include "domain/SafetyManager.h"
#include "domain/ValveController.h"
#include "domain/ZoneManager.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
#include "storage/ScheduleSkipStore.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"
#include "storage/ZoneErrorStore.h"
#include "web/IrrigationWeb.h"

namespace IrrigationApp {

void beginHardwareSafety() {
    ValveController::begin();
    ValveController::allOff("early boot");
}

void registerAppConfig() {
    SystemConfigStore::registerAppConfig();
}

void begin() {
    RecordStore::begin();
    SystemConfigStore::begin();
    ZoneConfigStore::begin();
    ZoneErrorStore::begin();
    PlanStore::begin();
    ScheduleSkipStore::begin();
    FlowMeter::begin();
    FlowCalibration::begin();
    ZoneManager::begin();
    SafetyManager::begin();
    MaintenanceService::begin();
    IrrigationWeb::begin();
    ESP32BASE_LOG_I("irrigation", "application ready");
}

void handle() {
    FlowMeter::handle();
    FlowCalibration::handle(SystemConfigStore::current());
    ZoneManager::handle();
    SafetyManager::handle();
    ValveController::handle();
    MaintenanceService::handle();
}

}
