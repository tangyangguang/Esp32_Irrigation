#include "IrrigationWeb.h"

#include "EventService.h"
#include "IrrigationApi.h"
#include "IrrigationConfig.h"
#include "IrrigationRuntime.h"
#include "RecordsStore.h"

namespace irrigation {

bool IrrigationWeb::begin(IrrigationConfig& config,
                          IrrigationRuntime& runtime,
                          RecordsStore& records,
                          EventService& events,
                          IrrigationApi& api) {
    _config = &config;
    _runtime = &runtime;
    _records = &records;
    _events = &events;
    _api = &api;
    _ready = true;
    return _ready;
}

void IrrigationWeb::handle() {
}

bool IrrigationWeb::ready() const {
    return _ready;
}

}  // namespace irrigation
