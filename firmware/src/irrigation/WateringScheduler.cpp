#include "WateringScheduler.h"

bool WateringScheduler::begin(WateringSchedulerStorage& storage) {
    storageReady_ = false;
    timeState_ = TimeState::Unavailable;
    maximumTrustedEpoch_ = 0;
    minuteInitialized_ = false;
    storage_ = &storage;
    state_ = {};
    const SchedulerStorageLoadResult loaded = storage.load(state_);
    if (loaded == SchedulerStorageLoadResult::Missing) {
        state_ = {};
        if (!storage.save(state_)) {
            emit(Event::StorageFault);
            return false;
        }
    } else if (loaded != SchedulerStorageLoadResult::Loaded) {
        emit(Event::StorageFault);
        return false;
    }
    if ((state_.mode == AutomaticWateringMode::Enabled && state_.resumeAtEpoch != 0) ||
        (state_.mode == AutomaticWateringMode::PausedIndefinitely &&
         state_.resumeAtEpoch != 0) ||
        (state_.mode == AutomaticWateringMode::PausedUntil &&
         state_.resumeAtEpoch < kMinimumLocalEpochUtc)) {
        emit(Event::StorageFault);
        return false;
    }
    storageReady_ = true;
    rebaseTimeCheck();
    return true;
}

void WateringScheduler::setCallbacks(StartCallback startCallback,
                                     EventCallback eventCallback,
                                     void* user) {
    startCallback_ = startCallback;
    eventCallback_ = eventCallback;
    callbackUser_ = user;
}

void WateringScheduler::handle(const IrrigationConfig& config,
                               bool timeTrusted,
                               bool timeFromNtp,
                               uint32_t epochSec) {
    if (!storageReady_ || !timeTrusted || epochSec < kMinimumLocalEpochUtc) {
        timeState_ = TimeState::Unavailable;
        minuteInitialized_ = false;
        return;
    }

    const uint32_t rollbackSec = static_cast<uint32_t>(
        config.timeSafety.rtcRollbackThresholdMinutes) * 60U;
    if (!timeFromNtp && maximumTrustedEpoch_ != 0 &&
        epochSec + rollbackSec < maximumTrustedEpoch_) {
        if (timeState_ != TimeState::RtcRollback) {
            emit(Event::RtcRollbackDetected, 0,
                 static_cast<int32_t>((maximumTrustedEpoch_ - epochSec) / 60U));
        }
        timeState_ = TimeState::RtcRollback;
        minuteInitialized_ = false;
        return;
    }

    if (timeFromNtp || epochSec > maximumTrustedEpoch_) {
        maximumTrustedEpoch_ = epochSec;
    }
    timeState_ = TimeState::Ready;

    const uint32_t epochMinute = epochSec / 60U;
    if (!minuteInitialized_) {
        observedEpochMinute_ = epochMinute;
        minuteInitialized_ = true;
        return;
    }
    if (epochMinute == observedEpochMinute_) {
        return;
    }
    if (epochMinute != observedEpochMinute_ + 1U) {
        observedEpochMinute_ = epochMinute;
        return;
    }
    observedEpochMinute_ = epochMinute;

    if (state_.mode == AutomaticWateringMode::PausedUntil &&
        epochSec >= state_.resumeAtEpoch) {
        WateringSchedulerPersistentState next = state_;
        next.mode = AutomaticWateringMode::Enabled;
        next.resumeAtEpoch = 0;
        if (!saveState(next)) {
            return;
        }
        emit(Event::ResumedAutomatically);
    }
    processMinute(config, localDay(epochSec), localMinute(epochSec));
}

bool WateringScheduler::pauseIndefinitely() {
    if (!storageReady_) {
        return false;
    }
    WateringSchedulerPersistentState next = state_;
    next.mode = AutomaticWateringMode::PausedIndefinitely;
    next.resumeAtEpoch = 0;
    if (!saveState(next)) {
        return false;
    }
    rebaseTimeCheck();
    emit(Event::PausedIndefinitely);
    return true;
}

bool WateringScheduler::pauseUntil(uint32_t resumeAtEpoch,
                                   bool timeTrusted,
                                   uint32_t currentEpoch) {
    if (!storageReady_ || !timeTrusted || currentEpoch < kMinimumLocalEpochUtc ||
        resumeAtEpoch <= currentEpoch) {
        return false;
    }
    WateringSchedulerPersistentState next = state_;
    next.mode = AutomaticWateringMode::PausedUntil;
    next.resumeAtEpoch = resumeAtEpoch;
    if (!saveState(next)) {
        return false;
    }
    rebaseTimeCheck();
    emit(Event::PausedUntil, 0, static_cast<int32_t>(resumeAtEpoch - currentEpoch));
    return true;
}

bool WateringScheduler::resumeManually() {
    if (!storageReady_) {
        return false;
    }
    WateringSchedulerPersistentState next = state_;
    next.mode = AutomaticWateringMode::Enabled;
    next.resumeAtEpoch = 0;
    if (!saveState(next)) {
        return false;
    }
    rebaseTimeCheck();
    emit(Event::ResumedManually);
    return true;
}

void WateringScheduler::rebaseTimeCheck() {
    minuteInitialized_ = false;
}

void WateringScheduler::disable() {
    storageReady_ = false;
    timeState_ = TimeState::Unavailable;
    minuteInitialized_ = false;
}

AutomaticWateringState WateringScheduler::automaticState() const {
    return {state_.mode, state_.resumeAtEpoch};
}

WateringScheduler::TimeState WateringScheduler::timeState() const {
    return timeState_;
}

bool WateringScheduler::storageReady() const {
    return storageReady_;
}

uint32_t WateringScheduler::localDay(uint32_t epochSec) {
    return (epochSec + kUtcOffsetSec) / kSecondsPerDay;
}

uint16_t WateringScheduler::localMinute(uint32_t epochSec) {
    return static_cast<uint16_t>(((epochSec + kUtcOffsetSec) % kSecondsPerDay) / 60U);
}

uint32_t WateringScheduler::scheduleBit(uint8_t planIndex, uint8_t startIndex) {
    return 1UL << (planIndex * kPlanStartTimeCount + startIndex);
}

bool WateringScheduler::saveState(const WateringSchedulerPersistentState& next) {
    if (!storage_ || !storage_->save(next)) {
        storageReady_ = false;
        emit(Event::StorageFault);
        return false;
    }
    state_ = next;
    return true;
}

bool WateringScheduler::markProcessed(uint32_t day, uint32_t bit) {
    WateringSchedulerPersistentState next = state_;
    if (next.currentLocalDay != day) {
        if (next.currentLocalDay != 0 && next.currentLocalDay < day) {
            next.previousLocalDay = next.currentLocalDay;
            next.previousProcessedMask = next.currentProcessedMask;
        }
        next.currentLocalDay = day;
        next.currentProcessedMask = 0;
    }
    next.currentProcessedMask |= bit;
    return saveState(next);
}

bool WateringScheduler::wasProcessed(uint32_t day, uint32_t bit) const {
    if (state_.currentLocalDay == day) {
        return (state_.currentProcessedMask & bit) != 0;
    }
    return state_.previousLocalDay == day && (state_.previousProcessedMask & bit) != 0;
}

void WateringScheduler::processMinute(const IrrigationConfig& config,
                                      uint32_t day,
                                      uint16_t minute) {
    for (uint8_t planIndex = 0; planIndex < config.plans.size(); ++planIndex) {
        const WateringPlan& plan = config.plans[planIndex];
        if (!plan.configured || !plan.scheduleEnabled) {
            continue;
        }
        for (uint8_t startIndex = 0; startIndex < plan.startMinutes.size(); ++startIndex) {
            if (plan.startMinutes[startIndex] != minute) {
                continue;
            }
            const uint32_t bit = scheduleBit(planIndex, startIndex);
            if (wasProcessed(day, bit) || !markProcessed(day, bit)) {
                return;
            }
            if (state_.mode != AutomaticWateringMode::Enabled) {
                return;
            }
            if (!startCallback_) {
                emit(Event::PlanStartRejected, plan.id,
                     static_cast<int32_t>(WateringStartResult::NotReady));
                return;
            }
            const WateringStartResult result = startCallback_(makeRequest(config, plan),
                                                               callbackUser_);
            if (result == WateringStartResult::Busy ||
                result == WateringStartResult::PreviousResultPending) {
                emit(Event::PlanSkippedBusy, plan.id, static_cast<int32_t>(result));
            } else if (result != WateringStartResult::Started) {
                emit(Event::PlanStartRejected, plan.id, static_cast<int32_t>(result));
            }
            return;
        }
    }
}

WateringRequest WateringScheduler::makeRequest(const IrrigationConfig& config,
                                               const WateringPlan& plan) const {
    WateringRequest request{};
    request.source = WateringSource::AutomaticPlan;
    request.purpose = WateringPurpose::Normal;
    request.planId = plan.id;
    for (uint8_t zoneIndex = 0; zoneIndex < config.zones.size(); ++zoneIndex) {
        const uint16_t durationMinutes = plan.zoneDurationMinutes[zoneIndex];
        if (!config.zones[zoneIndex].enabled || durationMinutes == 0) {
            continue;
        }
        WateringStep& step = request.steps[request.stepCount++];
        step.zoneId = config.zones[zoneIndex].id;
        step.targetDurationSec = static_cast<uint32_t>(durationMinutes) * 60U;
    }
    return request;
}

void WateringScheduler::emit(Event event, uint8_t planId, int32_t value) {
    if (eventCallback_) {
        eventCallback_(event, planId, value, callbackUser_);
    }
}
