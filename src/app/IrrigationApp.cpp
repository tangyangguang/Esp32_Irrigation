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
#include "storage/WeatherSnapshotStore.h"
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
    WeatherSnapshotStore::begin();
    FlowMeter::begin();
    FlowMeter::configureFlowRate(SystemConfigStore::current().flowRateWindowSec,
                                 SystemConfigStore::current().flowChartIntervalSec,
                                 SystemConfigStore::current().flowChartHistoryMin);
    FlowCalibration::begin();
    ZoneManager::begin();
    SafetyManager::begin();
    MaintenanceService::begin();
    IrrigationWeb::begin();
    ESP32BASE_LOG_I("irrigation", "application ready");
}

void handle() {
    const Irrigation::SystemConfig& system = SystemConfigStore::current();
    FlowMeter::configureFlowRate(system.flowRateWindowSec, system.flowChartIntervalSec, system.flowChartHistoryMin);
    FlowMeter::handle();
    FlowCalibration::handle(system);
    ZoneManager::handle();
    SafetyManager::handle();
    ValveController::handle();
    MaintenanceService::handle();
}

}
