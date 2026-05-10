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

#include <string.h>

namespace IrrigationApp {

namespace {

uint8_t wifiStateCode() {
#if ESP32BASE_ENABLE_WIFI
    return static_cast<uint8_t>(Esp32BaseWiFi::state());
#else
    return 0;
#endif
}

uint8_t otaStatusCode() {
#if ESP32BASE_ENABLE_OTA
    return static_cast<uint8_t>(Esp32BaseOta::status());
#else
    return 0;
#endif
}

void appendBaseEvent(const char* event, const char* data, void*) {
    (void)data;
    if (!event) {
        return;
    }
#if ESP32BASE_ENABLE_WIFI
    if (strcmp(event, Esp32BaseWiFi::EVENT_CONNECTED) == 0 ||
        strcmp(event, Esp32BaseWiFi::EVENT_DISCONNECTED) == 0 ||
        strcmp(event, Esp32BaseWiFi::EVENT_CONFIG_PORTAL) == 0 ||
        strcmp(event, Esp32BaseWiFi::EVENT_FAILED) == 0) {
        (void)EventStore::append(EventStore::TYPE_WIFI_STATUS_CHANGED,
                                 EventStore::SOURCE_SYSTEM,
                                 0,
                                 wifiStateCode(),
                                 Esp32BaseWiFi::rssi(),
                                 0,
                                 Esp32BaseWiFi::stateName());
        return;
    }
#endif
#if ESP32BASE_ENABLE_OTA
    if (strcmp(event, Esp32BaseOta::EVENT_START) == 0 ||
        strcmp(event, Esp32BaseOta::EVENT_SUCCESS) == 0 ||
        strcmp(event, Esp32BaseOta::EVENT_FAILED) == 0) {
        const char* text = strcmp(event, Esp32BaseOta::EVENT_FAILED) == 0 ? Esp32BaseOta::lastError() : event;
        (void)EventStore::append(EventStore::TYPE_OTA_STATUS_CHANGED,
                                 EventStore::SOURCE_SYSTEM,
                                 0,
                                 otaStatusCode(),
                                 Esp32BaseOta::bytesProcessed(),
                                 Esp32BaseOta::totalSize(),
                                 text);
    }
#endif
}

void subscribeBaseEvents() {
#if ESP32BASE_ENABLE_BUS
#if ESP32BASE_ENABLE_WIFI
    (void)Esp32BaseBus::subscribe(Esp32BaseWiFi::EVENT_CONNECTED, appendBaseEvent);
    (void)Esp32BaseBus::subscribe(Esp32BaseWiFi::EVENT_DISCONNECTED, appendBaseEvent);
    (void)Esp32BaseBus::subscribe(Esp32BaseWiFi::EVENT_CONFIG_PORTAL, appendBaseEvent);
    (void)Esp32BaseBus::subscribe(Esp32BaseWiFi::EVENT_FAILED, appendBaseEvent);
#endif
#if ESP32BASE_ENABLE_OTA
    (void)Esp32BaseBus::subscribe(Esp32BaseOta::EVENT_START, appendBaseEvent);
    (void)Esp32BaseBus::subscribe(Esp32BaseOta::EVENT_SUCCESS, appendBaseEvent);
    (void)Esp32BaseBus::subscribe(Esp32BaseOta::EVENT_FAILED, appendBaseEvent);
#endif
#endif
}

}

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
    subscribeBaseEvents();
    (void)EventStore::append(EventStore::TYPE_BOOT,
                             EventStore::SOURCE_SYSTEM,
                             0,
                             Esp32BaseWatchdog::wasWatchdogReset() ? 1 : 0,
                             Esp32BaseSystem::bootCount(),
                             Esp32BaseSystem::restartLogCount(),
                             Esp32BaseSystem::resetReason());
#if ESP32BASE_ENABLE_WIFI
    (void)EventStore::append(EventStore::TYPE_WIFI_STATUS_CHANGED,
                             EventStore::SOURCE_SYSTEM,
                             0,
                             wifiStateCode(),
                             Esp32BaseWiFi::rssi(),
                             0,
                             Esp32BaseWiFi::stateName());
#endif
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
