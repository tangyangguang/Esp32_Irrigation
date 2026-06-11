#include "EventService.h"

namespace irrigation {

bool EventService::begin() {
    _ready = true;
    return _ready;
}

void EventService::handle() {
}

bool EventService::ready() const {
    return _ready;
}

}  // namespace irrigation
