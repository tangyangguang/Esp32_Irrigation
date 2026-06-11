#include "IrrigationApi.h"

#include "EventService.h"
#include "IrrigationConfig.h"
#include "IrrigationRuntime.h"
#include "RecordsStore.h"

namespace irrigation {

bool IrrigationApi::begin(IrrigationConfig& config,
                          IrrigationRuntime& runtime,
                          RecordsStore& records,
                          EventService& events) {
    _config = &config;
    _runtime = &runtime;
    _records = &records;
    _events = &events;
    _ready = true;
    return _ready;
}

void IrrigationApi::handle() {
}

bool IrrigationApi::ready() const {
    return _ready;
}

}  // namespace irrigation
