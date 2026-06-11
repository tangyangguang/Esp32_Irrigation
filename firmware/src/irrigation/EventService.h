#pragma once

namespace irrigation {

class EventService {
public:
    // Business wrapper for Esp32BaseAppEventLog. System diagnostics stay in
    // Esp32Base; this service is only for irrigation events.
    bool begin();
    void handle();

    bool ready() const;

private:
    bool _ready = false;
};

}  // namespace irrigation
