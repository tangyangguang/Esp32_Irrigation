#include "IrrigationHardware.h"

#include "IrrigationConfig.h"

namespace irrigation {

bool IrrigationHardware::begin(const IrrigationConfig& config) {
    (void)config;
    _ready = true;
    return _ready;
}

void IrrigationHardware::handle() {
}

void IrrigationHardware::closeAllOutputs() {
}

bool IrrigationHardware::ready() const {
    return _ready;
}

}  // namespace irrigation
