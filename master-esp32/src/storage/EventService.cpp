#include "EventService.h"

#include <stdio.h>

namespace Irrigation {

namespace {
void formatPlanObject(uint8_t planId, char* out, size_t len) {
    snprintf(out, len, "plan:%u", static_cast<unsigned>(planId));
    out[len - 1] = '\0';
}
} // namespace

bool EventService::append(Esp32BaseAppEventLog::Level level,
                          const char* source,
                          const char* type,
                          const char* reason,
                          const char* object,
                          uint16_t code,
                          int32_t value1,
                          int32_t value2,
                          int32_t value3,
                          uint8_t valueMask) {
    Esp32BaseAppEventLog::Event event;
    event.level = level;
    event.source = source;
    event.type = type;
    event.reason = reason;
    event.object = object;
    event.code = code;
    event.value1 = value1;
    event.value2 = value2;
    event.value3 = value3;
    event.valueMask = valueMask;
    event.text = nullptr;
    return Esp32BaseAppEventLog::append(event);
}

bool EventService::planTriggered(uint8_t planId, uint8_t startIndex) {
    char object[24];
    formatPlanObject(planId, object, sizeof(object));
    return append(Esp32BaseAppEventLog::LEVEL_INFO,
                  "plan",
                  "triggered",
                  "start_time",
                  object,
                  0,
                  planId,
                  startIndex,
                  0,
                  Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2);
}

bool EventService::planSkipped(uint8_t planId, uint8_t startIndex, const char* reason) {
    char object[24];
    formatPlanObject(planId, object, sizeof(object));
    return append(Esp32BaseAppEventLog::LEVEL_WARN,
                  "plan",
                  "skipped",
                  reason,
                  object,
                  0,
                  planId,
                  startIndex,
                  0,
                  Esp32BaseAppEventLog::VALUE1 | Esp32BaseAppEventLog::VALUE2);
}

} // namespace Irrigation

