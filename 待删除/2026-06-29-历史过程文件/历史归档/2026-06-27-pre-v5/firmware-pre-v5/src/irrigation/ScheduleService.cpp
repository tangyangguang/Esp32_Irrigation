#include "ScheduleService.h"

#include "EventService.h"
#include "IrrigationConfig.h"
#include "IrrigationRuntime.h"

namespace irrigation {

bool ScheduleService::begin(IrrigationConfig& config, IrrigationRuntime& runtime, EventService& events) {
    _config = &config;
    _runtime = &runtime;
    _events = &events;
    _ready = true;
    return _ready;
}

void ScheduleService::handle() {
}

bool ScheduleService::ready() const {
    return _ready;
}

}  // namespace irrigation
