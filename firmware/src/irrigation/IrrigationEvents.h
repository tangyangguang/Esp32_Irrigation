#pragma once

#include <Esp32Base.h>

#include <cstdint>

#include "IrrigationTypes.h"

class IrrigationEvents {
public:
    enum class EventCode : uint32_t {
        WateringStoppedAbnormally = 1001,
        WateringRecordStorageFault = 1101,
    };

    enum class ReasonCode : uint32_t {
        FlowStartTimeout = 1,
        NoFlowTimeout = 2,
        HardwareFailure = 3,
        MaintenanceInterrupted = 4,
        RecordStoreBegin = 101,
        RecordStoreReload = 102,
        RecordStartTimeUnavailable = 103,
        RecordAppendFailed = 104,
        RecordClearFailed = 105,
    };

    static constexpr uint16_t kFlagValue1Capped = 1U << 0U;

    static bool appendAbnormalWateringStop(const WateringSessionSummary& summary);
    static bool appendRecordStorageFault(ReasonCode operation,
                                         Esp32BaseRecordStore::StoreState state,
                                         Esp32BaseRecordStore::StoreError error);

private:
    static ReasonCode wateringReason(WateringStopReason reason);
};
