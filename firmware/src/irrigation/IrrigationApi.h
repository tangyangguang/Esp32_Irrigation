#pragma once

namespace irrigation {

class EventService;
class IrrigationConfig;
class IrrigationRuntime;
class RecordsStore;

class IrrigationApi {
public:
    // JSON/API boundary only. It must expose atomic operations and route all
    // control decisions through IrrigationRuntime.
    bool begin(IrrigationConfig& config,
               IrrigationRuntime& runtime,
               RecordsStore& records,
               EventService& events);
    void handle();

    bool ready() const;

    void sendStatusJson() const;

private:
    IrrigationConfig* _config = nullptr;
    IrrigationRuntime* _runtime = nullptr;
    RecordsStore* _records = nullptr;
    EventService* _events = nullptr;
    bool _ready = false;
};

}  // namespace irrigation
