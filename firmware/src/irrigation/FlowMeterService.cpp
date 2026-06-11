#include "FlowMeterService.h"

namespace irrigation {

bool FlowMeterService::begin() {
    _snapshot = {};
    _ready = true;
    return _ready;
}

void FlowMeterService::handle() {
}

FlowMeterSnapshot FlowMeterService::snapshot() const {
    return _snapshot;
}

}  // namespace irrigation
