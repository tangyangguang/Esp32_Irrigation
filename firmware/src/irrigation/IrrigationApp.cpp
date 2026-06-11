#include "IrrigationApp.h"

namespace irrigation {

bool IrrigationApp::begin() {
    bool ok = true;
    ok = _config.begin() && ok;
    ok = _hardware.begin(_config) && ok;
    ok = _flow.begin(_hardware.flowInputPin()) && ok;
    ok = _records.begin() && ok;
    ok = _events.begin() && ok;
    ok = _runtime.begin(_config, _hardware, _flow, _records, _events) && ok;
    ok = _schedule.begin(_config, _runtime, _events) && ok;
    ok = _api.begin(_config, _runtime, _records, _events) && ok;
    ok = _web.begin(_config, _runtime, _records, _events, _api) && ok;

    _ready = ok;
    return _ready;
}

void IrrigationApp::handle() {
    _config.handle();
    _hardware.handle();
    _flow.handle();
    _runtime.handle();
    _schedule.handle();
    _records.handle();
    _events.handle();
    _api.handle();
    _web.handle();
}

bool IrrigationApp::ready() const {
    return _ready;
}

}  // namespace irrigation
