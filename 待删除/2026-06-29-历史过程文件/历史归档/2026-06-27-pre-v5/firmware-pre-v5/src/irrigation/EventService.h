#pragma once

#include <stdint.h>

namespace irrigation {

enum class IrrigationEventLevel : uint8_t {
    Info,
    Warning,
    Fault,
};

class EventService {
public:
    // Business wrapper for Esp32BaseAppEventLog. System diagnostics stay in
    // Esp32Base; this service is only for irrigation events.
    bool begin();
    void handle();

    bool ready() const;
    bool append(IrrigationEventLevel level,
                const char* type,
                const char* reason,
                const char* object,
                const char* text = nullptr,
                uint16_t code = 0,
                int32_t value1 = 0,
                int32_t value2 = 0,
                int32_t value3 = 0,
                uint8_t valueMask = 0);
    bool info(const char* type, const char* reason, const char* object, const char* text = nullptr);
    bool warning(const char* reason, const char* object, const char* text = nullptr);
    bool fault(const char* reason, const char* object, const char* text = nullptr);

private:
    bool _ready = false;
};

}  // namespace irrigation
