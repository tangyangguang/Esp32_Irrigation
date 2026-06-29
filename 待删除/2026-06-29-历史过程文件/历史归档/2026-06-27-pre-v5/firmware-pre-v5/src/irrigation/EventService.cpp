#include "EventService.h"

#include <Esp32Base.h>

namespace irrigation {

namespace {

constexpr const char* kEventSource = "irrigation";

#if ESP32BASE_ENABLE_APP_EVENTS
Esp32BaseAppEventLog::Level toBaseLevel(IrrigationEventLevel level) {
    switch (level) {
        case IrrigationEventLevel::Fault:
            return Esp32BaseAppEventLog::LEVEL_ERROR;
        case IrrigationEventLevel::Warning:
            return Esp32BaseAppEventLog::LEVEL_WARN;
        case IrrigationEventLevel::Info:
        default:
            return Esp32BaseAppEventLog::LEVEL_INFO;
    }
}
#endif

}  // namespace

bool EventService::begin() {
    _ready = true;
    return _ready;
}

void EventService::handle() {
}

bool EventService::ready() const {
    return _ready;
}

bool EventService::append(IrrigationEventLevel level,
                          const char* type,
                          const char* reason,
                          const char* object,
                          const char* text,
                          uint16_t code,
                          int32_t value1,
                          int32_t value2,
                          int32_t value3,
                          uint8_t valueMask) {
#if ESP32BASE_ENABLE_APP_EVENTS
    Esp32BaseAppEventLog::Event event;
    event.level = toBaseLevel(level);
    event.source = kEventSource;
    event.type = type;
    event.reason = reason;
    event.object = object;
    event.text = text;
    event.code = code;
    event.value1 = value1;
    event.value2 = value2;
    event.value3 = value3;
    event.valueMask = valueMask;
    return Esp32BaseAppEventLog::append(event);
#else
    (void)level;
    (void)type;
    (void)reason;
    (void)object;
    (void)text;
    (void)code;
    (void)value1;
    (void)value2;
    (void)value3;
    (void)valueMask;
    return false;
#endif
}

bool EventService::info(const char* type, const char* reason, const char* object, const char* text) {
    return append(IrrigationEventLevel::Info, type, reason, object, text);
}

bool EventService::warning(const char* reason, const char* object, const char* text) {
    return append(IrrigationEventLevel::Warning, "warning", reason, object, text);
}

bool EventService::fault(const char* reason, const char* object, const char* text) {
    return append(IrrigationEventLevel::Fault, "fault", reason, object, text);
}

}  // namespace irrigation
