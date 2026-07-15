#pragma once

#include <Esp32Base.h>

#include <cstdint>

#include "IrrigationTypes.h"

class IrrigationEvents {
public:
    enum class EventCode : uint32_t {
        WateringStoppedAbnormally = 1001,
        AutomaticWateringStateChanged = 1002,
        AutomaticPlanSkipped = 1003,
        RtcRollbackDetected = 1004,
        FlowDeviationDetected = 1005,
        UnexpectedFlowDetected = 1006,
        FlowCalibrationSaved = 1007,
        ZoneFlowLearned = 1008,
        ConfigurationChanged = 1009,
        WateringRecordStorageFault = 1101,
        SchedulerStorageFault = 1102,
    };

    enum class ReasonCode : uint32_t {
        FlowStartTimeout = 1,
        NoFlowTimeout = 2,
        HardwareFailure = 3,
        MaintenanceInterrupted = 4,
        LowFlow = 5,
        HighFlow = 6,
        UnexpectedFlow = 7,
        LearningTimeout = 8,
        RecordStoreBegin = 101,
        RecordStoreRegistration = 102,
        RecordStartTimeUnavailable = 103,
        RecordAppendFailed = 104,
        RecordStoreAfterFormat = 105,
        PausedIndefinitely = 201,
        PausedUntil = 202,
        ResumedManually = 203,
        ResumedAutomatically = 204,
        PlanBusy = 211,
        PlanStartRejected = 212,
        SchedulerStateStorage = 221,
        RtcRollback = 231,
        CalibrationCoefficientSaved = 301,
        ZoneFlowLearned = 302,
        ConfigurationSaved = 401,
    };

    static constexpr uint16_t kFlagValue1Capped = 1U << 0U;

    static bool appendAbnormalWateringStop(const WateringSessionSummary& summary);
    static bool appendRecordStorageFault(ReasonCode operation,
                                         Esp32BaseRecordStore::StoreState state,
                                         Esp32BaseRecordStore::StoreError error);
    static bool appendSchedulerEvent(uint32_t eventCode,
                                     ReasonCode reason,
                                     uint8_t planId,
                                     int32_t value,
                                     Esp32BaseAppEvents::Level level);
    static bool appendFlowDeviationEvents(const WateringSessionSummary& summary);

private:
    static ReasonCode wateringReason(WateringStopReason reason);
};
