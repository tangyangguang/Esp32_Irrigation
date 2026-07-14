#include "IrrigationEvents.h"

#include <climits>

namespace {

const ZoneWateringSummary* affectedZone(const WateringSessionSummary& summary) {
    const ZoneWateringSummary* lastStarted = nullptr;
    for (uint8_t index = 0; index < summary.zoneCount && index < summary.zones.size(); ++index) {
        const ZoneWateringSummary& zone = summary.zones[index];
        if (zone.result == ZoneWateringResult::Failed ||
            zone.result == ZoneWateringResult::Stopped) {
            return &zone;
        }
        if (zone.result != ZoneWateringResult::NotStarted) {
            lastStarted = &zone;
        }
    }
    return lastStarted;
}

}  // namespace

bool IrrigationEvents::appendAbnormalWateringStop(const WateringSessionSummary& summary) {
    if (summary.stopReason == WateringStopReason::None ||
        summary.stopReason == WateringStopReason::Completed ||
        summary.stopReason == WateringStopReason::UserStopped) {
        return true;
    }

    const ZoneWateringSummary* zone = affectedZone(summary);
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::WateringStoppedAbnormally);
    event.reasonCode = static_cast<uint32_t>(wateringReason(summary.stopReason));
    event.objectId = zone ? zone->zoneId : 0;
    event.value1 = zone && zone->pulseCount > static_cast<uint32_t>(INT32_MAX)
                       ? INT32_MAX
                       : static_cast<int32_t>(zone ? zone->pulseCount : 0);
    event.value2 = static_cast<int32_t>(zone ? zone->actualWateringSec : 0);
    event.flags = zone && zone->pulseCount > static_cast<uint32_t>(INT32_MAX)
                      ? kFlagValue1Capped
                      : 0;
    event.level = summary.stopReason == WateringStopReason::HardwareFailure
                      ? Esp32BaseAppEvents::Level::Error
                      : Esp32BaseAppEvents::Level::Warning;
    return Esp32BaseAppEvents::append(event);
}

bool IrrigationEvents::appendRecordStorageFault(
    ReasonCode operation,
    Esp32BaseRecordStore::StoreState state,
    Esp32BaseRecordStore::StoreError error) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::WateringRecordStorageFault);
    event.reasonCode = static_cast<uint32_t>(operation);
    event.value1 = static_cast<int32_t>(state);
    event.value2 = static_cast<int32_t>(error);
    event.level = Esp32BaseAppEvents::Level::Error;
    return Esp32BaseAppEvents::append(event);
}

bool IrrigationEvents::appendSchedulerEvent(uint32_t eventCode,
                                            ReasonCode reason,
                                            uint8_t planId,
                                            int32_t value,
                                            Esp32BaseAppEvents::Level level) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = eventCode;
    event.reasonCode = static_cast<uint32_t>(reason);
    event.objectId = planId;
    event.value1 = value;
    event.level = level;
    return Esp32BaseAppEvents::append(event);
}

bool IrrigationEvents::appendFlowDeviationEvents(const WateringSessionSummary& summary) {
    bool success = true;
    for (uint8_t index = 0; index < summary.zoneCount && index < summary.zones.size(); ++index) {
        const ZoneWateringSummary& zone = summary.zones[index];
        if (zone.lowFlowDetected && summary.stopReason != WateringStopReason::LowFlow) {
            Esp32BaseAppEvents::EventInput event;
            event.eventCode = static_cast<uint32_t>(EventCode::FlowDeviationDetected);
            event.reasonCode = static_cast<uint32_t>(ReasonCode::LowFlow);
            event.objectId = zone.zoneId;
            event.value1 = static_cast<int32_t>(zone.actualWateringSec);
            event.level = Esp32BaseAppEvents::Level::Warning;
            success = Esp32BaseAppEvents::append(event) && success;
        }
        if (zone.highFlowDetected && summary.stopReason != WateringStopReason::HighFlow) {
            Esp32BaseAppEvents::EventInput event;
            event.eventCode = static_cast<uint32_t>(EventCode::FlowDeviationDetected);
            event.reasonCode = static_cast<uint32_t>(ReasonCode::HighFlow);
            event.objectId = zone.zoneId;
            event.value1 = static_cast<int32_t>(zone.actualWateringSec);
            event.level = Esp32BaseAppEvents::Level::Warning;
            success = Esp32BaseAppEvents::append(event) && success;
        }
    }
    return success;
}

IrrigationEvents::ReasonCode IrrigationEvents::wateringReason(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::FlowStartTimeout:
            return ReasonCode::FlowStartTimeout;
        case WateringStopReason::NoFlowTimeout:
            return ReasonCode::NoFlowTimeout;
        case WateringStopReason::LowFlow:
            return ReasonCode::LowFlow;
        case WateringStopReason::HighFlow:
            return ReasonCode::HighFlow;
        case WateringStopReason::LearningTimeout:
            return ReasonCode::LearningTimeout;
        case WateringStopReason::HardwareFailure:
            return ReasonCode::HardwareFailure;
        case WateringStopReason::MaintenanceInterrupted:
            return ReasonCode::MaintenanceInterrupted;
        default:
            return ReasonCode::HardwareFailure;
    }
}
