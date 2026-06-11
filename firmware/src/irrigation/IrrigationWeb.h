#pragma once

namespace irrigation {

class EventService;
class IrrigationApi;
class IrrigationConfig;
class IrrigationRuntime;
class RecordsStore;

class IrrigationWeb {
public:
    // Web page boundary only. Formal pages are intentionally not implemented in
    // this skeleton; later handlers must go through services, not hardware.
    bool begin(IrrigationConfig& config,
               IrrigationRuntime& runtime,
               RecordsStore& records,
               EventService& events,
               IrrigationApi& api);
    void handle();

    bool ready() const;

private:
    IrrigationConfig* _config = nullptr;
    IrrigationRuntime* _runtime = nullptr;
    RecordsStore* _records = nullptr;
    EventService* _events = nullptr;
    IrrigationApi* _api = nullptr;
    bool _ready = false;
};

}  // namespace irrigation
