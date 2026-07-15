#pragma once

#include <cstdint>

#include "IrrigationTypes.h"

enum class SchedulerStorageLoadResult : uint8_t {
    Loaded,
    Missing,
    Invalid,
    Error,
};

struct WateringSchedulerPersistentState {
    AutomaticWateringMode mode = AutomaticWateringMode::Enabled;
    uint32_t resumeAtEpoch = 0;
    uint32_t currentLocalDay = 0;
    uint32_t currentProcessedMask = 0;
    uint32_t previousLocalDay = 0;
    uint32_t previousProcessedMask = 0;
};

enum class NextAutomaticWateringStatus : uint8_t {
    Available = 0,
    NoEnabledPlans,
    TimeUnavailable,
    RtcRollback,
    PausedIndefinitely,
};

struct NextAutomaticWatering {
    constexpr NextAutomaticWatering(
        NextAutomaticWateringStatus valueStatus =
            NextAutomaticWateringStatus::TimeUnavailable,
        uint8_t valuePlanId = 0,
        uint32_t valueScheduledEpoch = 0)
        : status(valueStatus),
          planId(valuePlanId),
          scheduledEpoch(valueScheduledEpoch) {}

    NextAutomaticWateringStatus status;
    uint8_t planId;
    uint32_t scheduledEpoch;
};

class WateringSchedulerStorage {
public:
    virtual ~WateringSchedulerStorage() = default;
    virtual SchedulerStorageLoadResult load(WateringSchedulerPersistentState& state) = 0;
    virtual bool save(const WateringSchedulerPersistentState& state) = 0;
    virtual bool clear() = 0;
};

class WateringScheduler {
public:
    enum class TimeState : uint8_t {
        Unavailable,
        Ready,
        RtcRollback,
    };

    enum class Event : uint8_t {
        PausedIndefinitely,
        PausedUntil,
        ResumedManually,
        ResumedAutomatically,
        PlanSkippedBusy,
        PlanStartRejected,
        StorageFault,
    };

    using StartCallback = WateringStartResult (*)(const WateringRequest& request, void* user);
    using EventCallback = void (*)(Event event, uint8_t planId, int32_t value, void* user);

    bool begin(WateringSchedulerStorage& storage);
    void setCallbacks(StartCallback startCallback,
                      EventCallback eventCallback,
                      void* user = nullptr);
    void handle(const IrrigationConfig& config,
                bool timeTrusted,
                bool timeFromNtp,
                uint32_t epochSec);

    bool pauseIndefinitely();
    bool pauseUntil(uint32_t resumeAtEpoch, bool timeTrusted, uint32_t currentEpoch);
    bool resumeManually();
    void setTrustedEpochBaseline(uint32_t epochSec);
    void rebaseTimeCheck();
    void disable();

    AutomaticWateringState automaticState() const;
    NextAutomaticWatering nextAutomaticWatering(const IrrigationConfig& config,
                                                 uint32_t currentEpoch) const;
    TimeState timeState() const;
    bool storageReady() const;

private:
    static constexpr uint32_t kSecondsPerDay = 24UL * 60UL * 60UL;
    static constexpr uint32_t kUtcOffsetSec = 8UL * 60UL * 60UL;
    static constexpr uint32_t kMinimumLocalEpochUtc = 1767196800UL;

    static uint32_t localDay(uint32_t epochSec);
    static uint16_t localMinute(uint32_t epochSec);
    static uint32_t scheduleBit(uint8_t planIndex, uint8_t startIndex);
    bool saveState(const WateringSchedulerPersistentState& next);
    bool markProcessed(uint32_t day, uint32_t bit);
    bool wasProcessed(uint32_t day, uint32_t bit) const;
    void processMinute(const IrrigationConfig& config, uint32_t day, uint16_t minute);
    WateringRequest makeRequest(const IrrigationConfig& config,
                                const WateringPlan& plan) const;
    void emit(Event event, uint8_t planId = 0, int32_t value = 0);

    WateringSchedulerStorage* storage_ = nullptr;
    StartCallback startCallback_ = nullptr;
    EventCallback eventCallback_ = nullptr;
    void* callbackUser_ = nullptr;
    WateringSchedulerPersistentState state_{};
    TimeState timeState_ = TimeState::Unavailable;
    uint32_t maximumTrustedEpoch_ = 0;
    uint32_t observedEpochMinute_ = 0;
    bool storageReady_ = false;
    bool minuteInitialized_ = false;
};
