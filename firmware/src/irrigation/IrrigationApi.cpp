#include "IrrigationApi.h"

#include <Esp32Base.h>
#include <stdio.h>

#include "EventService.h"
#include "IrrigationConfig.h"
#include "IrrigationRuntime.h"
#include "RecordsStore.h"

namespace irrigation {

namespace {

IrrigationApi* g_api = nullptr;

void handleStatusApi() {
    if (!g_api) {
        Esp32BaseWeb::sendJson(503, "{\"ok\":false,\"error\":\"api_not_ready\"}");
        return;
    }
    g_api->sendStatusJson();
}

const char* boolText(bool value) {
    return value ? "true" : "false";
}

}  // namespace

bool IrrigationApi::begin(IrrigationConfig& config,
                          IrrigationRuntime& runtime,
                          RecordsStore& records,
                          EventService& events) {
    _config = &config;
    _runtime = &runtime;
    _records = &records;
    _events = &events;
    g_api = this;
    _ready = Esp32BaseWeb::addApi("/api/status", handleStatusApi);
    return _ready;
}

void IrrigationApi::handle() {
}

bool IrrigationApi::ready() const {
    return _ready;
}

void IrrigationApi::sendStatusJson() const {
    if (!_config || !_runtime || !_records) {
        Esp32BaseWeb::sendJson(503, "{\"ok\":false,\"error\":\"api_not_ready\"}");
        return;
    }

    const IrrigationConfigSnapshot& config = _config->snapshot();
    const IrrigationRuntimeStatus runtime = _runtime->status();

    char json[512];
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,"
             "\"runtime\":{\"ready\":%s,\"state\":\"%s\",\"activeZoneId\":%u,"
             "\"durationMin\":%u,\"startedAtMs\":%lu,\"deadlineMs\":%lu},"
             "\"config\":{\"autoMode\":\"%s\",\"pumpStartEnabled\":%s,"
             "\"lowLevelEnabled\":%s,\"pulsesPerLiter\":%lu},"
             "\"records\":{\"ready\":%s}}",
             boolText(_runtime->ready()),
             IrrigationRuntime::runStateKey(runtime.state),
             static_cast<unsigned>(runtime.activeZoneId),
             static_cast<unsigned>(runtime.durationMin),
             static_cast<unsigned long>(runtime.startedAtMs),
             static_cast<unsigned long>(runtime.deadlineMs),
             IrrigationConfig::autoModeKey(config.system.autoMode),
             boolText(config.system.pumpStartEnabled),
             boolText(config.system.lowLevelEnabled),
             static_cast<unsigned long>(config.flowValve.pulsesPerLiter),
             boolText(_records->ready()));

    Esp32BaseWeb::sendJson(200, json);
}

}  // namespace irrigation
