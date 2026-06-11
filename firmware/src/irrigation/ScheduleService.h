#pragma once

namespace irrigation {

class EventService;
class IrrigationConfig;
class IrrigationRuntime;

class ScheduleService {
public:
    // Owns automatic plan checks and the future automatic-plan queue. It never
    // opens valves directly; it will request work through IrrigationRuntime.
    bool begin(IrrigationConfig& config, IrrigationRuntime& runtime, EventService& events);
    void handle();

    bool ready() const;

private:
    IrrigationConfig* _config = nullptr;
    IrrigationRuntime* _runtime = nullptr;
    EventService* _events = nullptr;
    bool _ready = false;
};

}  // namespace irrigation
