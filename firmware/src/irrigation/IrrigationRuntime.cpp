#include "IrrigationRuntime.h"

#include "EventService.h"
#include "FlowMeterService.h"
#include "IrrigationConfig.h"
#include "IrrigationHardware.h"
#include "RecordsStore.h"

namespace irrigation {

bool IrrigationRuntime::begin(IrrigationConfig& config,
                              IrrigationHardware& hardware,
                              FlowMeterService& flow,
                              RecordsStore& records,
                              EventService& events) {
    _config = &config;
    _hardware = &hardware;
    _flow = &flow;
    _records = &records;
    _events = &events;
    _state = IrrigationRunState::Idle;
    _ready = true;
    return _ready;
}

void IrrigationRuntime::handle() {
}

IrrigationRunState IrrigationRuntime::state() const {
    return _state;
}

bool IrrigationRuntime::ready() const {
    return _ready;
}

}  // namespace irrigation
