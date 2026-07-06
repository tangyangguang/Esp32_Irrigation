#pragma once

#include "EventService.h"
#include "FlowMeterService.h"
#include "IrrigationApi.h"
#include "IrrigationConfig.h"
#include "IrrigationHardware.h"
#include "IrrigationRuntime.h"
#include "IrrigationWeb.h"
#include "RecordsStore.h"
#include "ScheduleService.h"

namespace irrigation {

class IrrigationApp {
public:
    // Application composition root. It wires modules together and drives the
    // service loop; business behavior stays inside focused services.
    bool begin();
    void handle();

    bool ready() const;

private:
    IrrigationConfig _config;
    IrrigationHardware _hardware;
    FlowMeterService _flow;
    RecordsStore _records;
    EventService _events;
    IrrigationRuntime _runtime;
    ScheduleService _schedule;
    IrrigationApi _api;
    IrrigationWeb _web;
    bool _ready = false;
};

}  // namespace irrigation
