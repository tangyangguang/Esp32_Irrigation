#include "IrrigationEvents.h"

#include <climits>
#include <cstdio>

namespace {

const ZoneWateringSummary* affectedZone(const WateringSessionSummary& summary) {
    const ZoneWateringSummary* lastStarted = nullptr;
    for (uint8_t index = 0; index < summary.zoneCount && index < summary.zones.size(); ++index) {
        const ZoneWateringSummary& zone = summary.zones[index];
        if (zone.result == ZoneWateringResult::Failed ||
            zone.result == ZoneWateringResult::Stopped) {
            return &zone;
        }
        if (zone.result != ZoneWateringResult::NotStarted) lastStarted = &zone;
    }
    return lastStarted;
}

bool recovered(const Esp32BaseAppEvents::EventRecord& event) {
    return event.eventKind == Esp32BaseAppEvents::EventKind::ConditionRecovered;
}

uint32_t cappedValue(uint32_t value, uint8_t& flags) {
    if (value > static_cast<uint32_t>(INT32_MAX)) {
        flags |= 1U;
        return static_cast<uint32_t>(INT32_MAX);
    }
    return value;
}

}  // namespace

IrrigationEvents::IrrigationEvents()
    : rtcUnavailableCondition_(kRtcUnavailableConditionId, 60000U, 60000U),
      trustedTimeUnavailableCondition_(kTrustedTimeUnavailableConditionId, 300000U, 30000U),
      rtcRollbackCondition_(kRtcRollbackConditionId, 0, 0),
      closedValveFlowCondition_(kClosedValveFlowConditionId, 0, 0) {}

void IrrigationEvents::syncStorageStatus() {
    Esp32BaseAppEvents::AppEventsStatus status{};
    const bool readable = Esp32BaseAppEvents::readStatus(status);
    const bool ready = readable && status.eventStore.ready &&
                       Esp32BaseAppEvents::isEventStoreWritable() &&
                       status.conditionStateLoaded && !status.conditionStateSavePending;
    updateStorageFault(!ready, ready ? "ready" : Esp32BaseAppEvents::lastErrorReason());
}

bool IrrigationEvents::storageFault() const {
    return !storageStateKnown_ || storageFault_;
}

bool IrrigationEvents::readStatus(Esp32BaseAppEvents::AppEventsStatus& status) const {
    return Esp32BaseAppEvents::readStatus(status);
}

bool IrrigationEvents::readLatest(uint32_t offset,
                                  uint32_t limit,
                                  ReadCallback callback,
                                  void* user) const {
    return Esp32BaseAppEvents::readLatest(offset, limit, callback, user);
}

void IrrigationEvents::recordAbnormalWateringStop(const WateringSessionSummary& summary) {
    if (summary.purpose != WateringPurpose::Normal ||
        summary.stopReason == WateringStopReason::None ||
        summary.stopReason == WateringStopReason::Completed ||
        summary.stopReason == WateringStopReason::UserStopped ||
        summary.stopReason == WateringStopReason::LowFlow ||
        summary.stopReason == WateringStopReason::HighFlow) {
        return;
    }
    const ZoneWateringSummary* zone = affectedZone(summary);
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::WateringStoppedAbnormally);
    event.reasonCode = static_cast<uint32_t>(wateringReason(summary.stopReason));
    event.objectId = zone ? zone->zoneId : 0;
    event.value1 = static_cast<int32_t>(cappedValue(zone ? zone->pulseCount : 0, event.flags));
    event.value2 = static_cast<int32_t>(zone ? zone->actualWateringSec : 0);
    event.level = summary.stopReason == WateringStopReason::MaintenanceInterrupted
                      ? Esp32BaseAppEvents::Level::Warning
                      : Esp32BaseAppEvents::Level::Error;
    append(event);
}

void IrrigationEvents::recordFlowDeviationEvent(
    const ZoneWateringSummary& zone,
    ReasonCode reason,
    bool stopped) {
    const bool low = reason == ReasonCode::LowFlow;
    if (!low && reason != ReasonCode::HighFlow) return;
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::FlowDeviation);
    event.reasonCode = static_cast<uint32_t>(reason);
    event.objectId = zone.zoneId;
    event.value1 = static_cast<int32_t>(cappedValue(
        low ? zone.lowFlowDetectedMlPerMinute
            : zone.highFlowDetectedMlPerMinute,
        event.flags));
    event.value2 = static_cast<int32_t>(
        cappedValue(zone.baselineFlowMlPerMinute, event.flags));
    if (stopped) event.flags |= kFlagWateringStopped;
    event.level = stopped ? Esp32BaseAppEvents::Level::Error
                          : Esp32BaseAppEvents::Level::Warning;
    append(event);
}

void IrrigationEvents::recordAutomaticWateringPaused(bool indefinitely,
                                                      uint32_t resumeAtEpoch) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::AutomaticWateringStateChanged);
    event.reasonCode = static_cast<uint32_t>(indefinitely ? ReasonCode::PausedIndefinitely
                                                          : ReasonCode::PausedUntil);
    event.value1 = static_cast<int32_t>(resumeAtEpoch);
    event.level = Esp32BaseAppEvents::Level::Info;
    append(event);
}

void IrrigationEvents::recordAutomaticWateringResumed(bool automatically) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::AutomaticWateringStateChanged);
    event.reasonCode = static_cast<uint32_t>(automatically ? ReasonCode::ResumedAutomatically
                                                           : ReasonCode::ResumedManually);
    event.level = Esp32BaseAppEvents::Level::Info;
    append(event);
}

void IrrigationEvents::recordAutomaticPlanSkipped(uint8_t planId, bool busy) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::AutomaticPlanSkipped);
    event.reasonCode = static_cast<uint32_t>(busy ? ReasonCode::PlanBusy
                                                  : ReasonCode::PlanStartRejected);
    event.objectId = planId;
    event.level = Esp32BaseAppEvents::Level::Warning;
    append(event);
}

void IrrigationEvents::recordFlowCalibrationSaved(
    uint32_t previousCoefficientX100,
    uint32_t coefficientX100) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::FlowCalibrationSaved);
    event.reasonCode = static_cast<uint32_t>(ReasonCode::CalibrationCoefficientSaved);
    event.value1 = static_cast<int32_t>(
        cappedValue(previousCoefficientX100, event.flags));
    event.value2 = static_cast<int32_t>(
        cappedValue(coefficientX100, event.flags));
    event.level = Esp32BaseAppEvents::Level::Info;
    append(event);
}

void IrrigationEvents::recordZoneFlowSaved(
    uint8_t zoneId,
    uint32_t previousFlowMlPerMinute,
    uint32_t flowMlPerMinute) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::ZoneFlowSaved);
    event.reasonCode = static_cast<uint32_t>(ReasonCode::ZoneFlowSaved);
    event.objectId = zoneId;
    event.value1 = static_cast<int32_t>(
        cappedValue(previousFlowMlPerMinute, event.flags));
    event.value2 = static_cast<int32_t>(
        cappedValue(flowMlPerMinute, event.flags));
    event.level = Esp32BaseAppEvents::Level::Info;
    append(event);
}

void IrrigationEvents::recordConfigurationChanged(ConfigurationChange change,
                                                  uint8_t objectId) {
    ReasonCode reason = ReasonCode::SystemParametersUpdated;
    switch (change) {
        case ConfigurationChange::PlanCreated: reason = ReasonCode::PlanCreated; break;
        case ConfigurationChange::PlanUpdated: reason = ReasonCode::PlanUpdated; break;
        case ConfigurationChange::PlanDeleted: reason = ReasonCode::PlanDeleted; break;
        case ConfigurationChange::ZoneUpdated: reason = ReasonCode::ZoneUpdated; break;
        case ConfigurationChange::SystemParametersUpdated: break;
    }
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::ConfigurationChanged);
    event.reasonCode = static_cast<uint32_t>(reason);
    event.objectId = objectId;
    event.level = Esp32BaseAppEvents::Level::Info;
    append(event);
}

void IrrigationEvents::recordWateringRecordSaveFailed(
    ReasonCode reason,
    Esp32BaseRecordStore::StoreState state,
    Esp32BaseRecordStore::StoreError error) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::WateringRecordSaveFailed);
    event.reasonCode = static_cast<uint32_t>(reason);
    event.value1 = static_cast<int32_t>(state);
    event.value2 = static_cast<int32_t>(error);
    event.level = Esp32BaseAppEvents::Level::Error;
    append(event);
}

void IrrigationEvents::recordSchedulerStateSaveFailed() {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::SchedulerStateSaveFailed);
    event.reasonCode = static_cast<uint32_t>(ReasonCode::SchedulerStateStorage);
    event.level = Esp32BaseAppEvents::Level::Error;
    append(event);
}

void IrrigationEvents::observeRtcAvailability(bool available, uint8_t statusCode) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::RtcUnavailable);
    event.reasonCode = static_cast<uint32_t>(ReasonCode::RtcUnavailable);
    event.value1 = statusCode;
    event.level = available ? Esp32BaseAppEvents::Level::Info
                            : Esp32BaseAppEvents::Level::Warning;
    observe(rtcUnavailableCondition_,
            available ? Esp32BaseAppEvents::ObservedConditionState::Inactive
                      : Esp32BaseAppEvents::ObservedConditionState::Active,
            event,
            rtcUnavailableState_);
}

void IrrigationEvents::observeTrustedTime(bool trusted) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::TrustedTimeUnavailable);
    event.reasonCode = static_cast<uint32_t>(ReasonCode::TrustedTimeUnavailable);
    event.level = trusted ? Esp32BaseAppEvents::Level::Info
                          : Esp32BaseAppEvents::Level::Error;
    observe(trustedTimeUnavailableCondition_,
            trusted ? Esp32BaseAppEvents::ObservedConditionState::Inactive
                    : Esp32BaseAppEvents::ObservedConditionState::Active,
            event,
            trustedTimeUnavailableState_);
}

void IrrigationEvents::observeRtcRollback(Esp32BaseAppEvents::ObservedConditionState state) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::RtcRollback);
    event.reasonCode = static_cast<uint32_t>(ReasonCode::RtcRollback);
    event.level = state == Esp32BaseAppEvents::ObservedConditionState::Active
                      ? Esp32BaseAppEvents::Level::Error
                      : Esp32BaseAppEvents::Level::Info;
    observe(rtcRollbackCondition_, state, event, rtcRollbackState_);
}

void IrrigationEvents::observeClosedValveFlow(
    Esp32BaseAppEvents::ObservedConditionState state,
    uint32_t pulseCount,
    uint16_t windowSec,
    uint16_t thresholdPulseCount) {
    Esp32BaseAppEvents::EventInput event;
    event.eventCode = static_cast<uint32_t>(EventCode::ClosedValveFlow);
    event.reasonCode = static_cast<uint32_t>(ReasonCode::ClosedValveFlow);
    event.objectId = thresholdPulseCount;
    event.value1 = pulseCount > static_cast<uint32_t>(INT32_MAX)
                       ? INT32_MAX
                       : static_cast<int32_t>(pulseCount);
    event.value2 = windowSec;
    if (pulseCount > static_cast<uint32_t>(INT32_MAX)) event.flags |= kFlagValue1Capped;
    event.level = state == Esp32BaseAppEvents::ObservedConditionState::Active
                      ? Esp32BaseAppEvents::Level::Error
                      : Esp32BaseAppEvents::Level::Info;
    observe(closedValveFlowCondition_, state, event, closedValveFlowState_);
}

IrrigationEvents::ConditionDisplayState IrrigationEvents::conditionState(
    uint8_t conditionId) const {
    switch (conditionId) {
        case kRtcUnavailableConditionId: return rtcUnavailableState_;
        case kTrustedTimeUnavailableConditionId: return trustedTimeUnavailableState_;
        case kRtcRollbackConditionId: return rtcRollbackState_;
        case kClosedValveFlowConditionId: return closedValveFlowState_;
        default: return ConditionDisplayState::Unknown;
    }
}

IrrigationEvents::Category IrrigationEvents::category(
    const Esp32BaseAppEvents::EventRecord& event) {
    switch (static_cast<EventCode>(event.eventCode)) {
        case EventCode::AutomaticWateringStateChanged:
        case EventCode::AutomaticPlanSkipped:
        case EventCode::SchedulerStateSaveFailed:
            return Category::AutomaticWatering;
        case EventCode::FlowCalibrationSaved:
        case EventCode::ZoneFlowSaved:
        case EventCode::ConfigurationChanged:
            return Category::SettingsAndCalibration;
        case EventCode::RtcRollback:
        case EventCode::RtcUnavailable:
        case EventCode::TrustedTimeUnavailable:
        case EventCode::WateringRecordSaveFailed:
            return Category::TimeAndStorage;
        default:
            return Category::WateringAndFlow;
    }
}

const char* IrrigationEvents::categoryName(Category value) {
    switch (value) {
        case Category::AutomaticWatering: return "自动计划";
        case Category::SettingsAndCalibration: return "设置与校准";
        case Category::TimeAndStorage: return "时间与存储";
        case Category::WateringAndFlow:
        default: return "浇水与流量";
    }
}

const char* IrrigationEvents::levelName(Esp32BaseAppEvents::Level level) {
    switch (level) {
        case Esp32BaseAppEvents::Level::Warning: return "警告";
        case Esp32BaseAppEvents::Level::Error: return "错误";
        case Esp32BaseAppEvents::Level::Info:
        default: return "信息";
    }
}

void IrrigationEvents::formatTitle(const Esp32BaseAppEvents::EventRecord& event,
                                   char* out,
                                   std::size_t length) {
    if (!out || length == 0) return;
    switch (static_cast<EventCode>(event.eventCode)) {
        case EventCode::WateringStoppedAbnormally:
            switch (static_cast<ReasonCode>(event.reasonCode)) {
                case ReasonCode::FlowStartTimeout:
                    std::snprintf(out, length, "水路 %lu 未检测到水流", static_cast<unsigned long>(event.objectId)); return;
                case ReasonCode::NoFlowTimeout:
                    std::snprintf(out, length, "水路 %lu 浇水时水流中断", static_cast<unsigned long>(event.objectId)); return;
                case ReasonCode::LowFlow:
                    std::snprintf(out, length, "水路 %lu 因流量过低停止", static_cast<unsigned long>(event.objectId)); return;
                case ReasonCode::HighFlow:
                    std::snprintf(out, length, "水路 %lu 因流量过高停止", static_cast<unsigned long>(event.objectId)); return;
                case ReasonCode::MaintenanceInterrupted:
                    std::snprintf(out, length, "浇水因维护操作中断"); return;
                default:
                    std::snprintf(out, length, "浇水因设备故障停止"); return;
            }
        case EventCode::AutomaticWateringStateChanged:
            if (event.reasonCode == static_cast<uint32_t>(ReasonCode::ResumedManually)) std::snprintf(out, length, "自动浇水已手动恢复");
            else if (event.reasonCode == static_cast<uint32_t>(ReasonCode::ResumedAutomatically)) std::snprintf(out, length, "自动浇水已自动恢复");
            else if (event.reasonCode == static_cast<uint32_t>(ReasonCode::PausedUntil)) {
                char time[32]{};
                if (Esp32BaseTime::formatEpoch(static_cast<uint32_t>(event.value1),
                                               time,
                                               sizeof(time),
                                               "%Y-%m-%d %H:%M")) {
                    std::snprintf(out, length, "自动浇水暂停至 %s", time);
                } else {
                    std::snprintf(out, length, "自动浇水已暂停");
                }
            }
            else std::snprintf(out, length, "自动浇水已暂停");
            return;
        case EventCode::AutomaticPlanSkipped:
            std::snprintf(out, length, "计划 %lu 本次未执行", static_cast<unsigned long>(event.objectId)); return;
        case EventCode::RtcRollback:
            std::snprintf(out, length, recovered(event) ? "设备时间已恢复正常" : "设备时间发生倒退"); return;
        case EventCode::FlowDeviation:
            std::snprintf(out, length,
                          event.reasonCode == static_cast<uint32_t>(ReasonCode::LowFlow)
                              ? "水路 %lu 流量偏低" : "水路 %lu 流量偏高",
                          static_cast<unsigned long>(event.objectId)); return;
        case EventCode::ClosedValveFlow:
            std::snprintf(out, length, recovered(event) ? "关阀后水流已恢复正常" : "关阀后检测到水流"); return;
        case EventCode::FlowCalibrationSaved:
            std::snprintf(out, length, "流量校准结果已保存"); return;
        case EventCode::ZoneFlowSaved:
            std::snprintf(out,
                          length,
                          event.value2 == 0
                              ? "水路 %lu 的基准流量已清除"
                              : "水路 %lu 的基准流量已保存",
                          static_cast<unsigned long>(event.objectId)); return;
        case EventCode::ConfigurationChanged:
            switch (static_cast<ReasonCode>(event.reasonCode)) {
                case ReasonCode::PlanCreated: std::snprintf(out, length, "计划 %lu 已创建", static_cast<unsigned long>(event.objectId)); return;
                case ReasonCode::PlanUpdated: std::snprintf(out, length, "计划 %lu 已修改", static_cast<unsigned long>(event.objectId)); return;
                case ReasonCode::PlanDeleted: std::snprintf(out, length, "计划 %lu 已删除", static_cast<unsigned long>(event.objectId)); return;
                case ReasonCode::ZoneUpdated: std::snprintf(out, length, "水路 %lu 设置已修改", static_cast<unsigned long>(event.objectId)); return;
                default: std::snprintf(out, length, "系统参数已修改"); return;
            }
        case EventCode::WateringRecordSaveFailed:
            std::snprintf(out, length, "浇水记录保存失败"); return;
        case EventCode::SchedulerStateSaveFailed:
            std::snprintf(out, length, "自动计划状态保存失败"); return;
        case EventCode::RtcUnavailable:
            std::snprintf(out, length, recovered(event) ? "硬件时钟已恢复" : "硬件时钟不可用"); return;
        case EventCode::TrustedTimeUnavailable:
            std::snprintf(out, length, recovered(event) ? "设备时间已恢复" : "设备时间不可用"); return;
        default:
            std::snprintf(out, length, "未知事件"); return;
    }
}

void IrrigationEvents::formatSummary(const Esp32BaseAppEvents::EventRecord& event,
                                     char* out,
                                     std::size_t length) {
    if (!out || length == 0) return;
    switch (static_cast<EventCode>(event.eventCode)) {
        case EventCode::WateringStoppedAbnormally:
            std::snprintf(out, length, "本次浇水没有完成。"); return;
        case EventCode::AutomaticWateringStateChanged:
            if (event.reasonCode == static_cast<uint32_t>(ReasonCode::PausedIndefinitely)) std::snprintf(out, length, "自动计划将保持暂停。");
            else if (event.reasonCode == static_cast<uint32_t>(ReasonCode::PausedUntil)) std::snprintf(out, length, "到达恢复时间后自动恢复。");
            else std::snprintf(out, length, "自动计划可以继续运行。");
            return;
        case EventCode::AutomaticPlanSkipped:
            std::snprintf(out, length,
                          event.reasonCode == static_cast<uint32_t>(ReasonCode::PlanBusy)
                              ? "设备正在执行其他操作。" : "设备当前无法启动浇水。"); return;
        case EventCode::RtcRollback:
            std::snprintf(out, length, recovered(event)
                          ? "时间已经校正，自动计划可以继续运行。"
                          : "自动计划已停止，等待时间校正。"); return;
        case EventCode::FlowDeviation: {
            const uint32_t actual =
                event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1);
            const uint32_t baseline =
                event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2);
            const uint32_t difference =
                actual >= baseline ? actual - baseline : baseline - actual;
            std::snprintf(out,
                          length,
                          "检测 %lu.%03lu L/min，基准 %lu.%03lu L/min，%s %lu.%03lu L/min；本次%s。",
                          static_cast<unsigned long>(actual / 1000U),
                          static_cast<unsigned long>(actual % 1000U),
                          static_cast<unsigned long>(baseline / 1000U),
                          static_cast<unsigned long>(baseline % 1000U),
                          event.reasonCode ==
                                  static_cast<uint32_t>(ReasonCode::LowFlow)
                              ? "低"
                              : "高",
                          static_cast<unsigned long>(difference / 1000U),
                          static_cast<unsigned long>(difference % 1000U),
                          (event.flags & kFlagWateringStopped) != 0
                              ? "已停止"
                              : "继续浇水");
            return;
        }
        case EventCode::ClosedValveFlow:
            if (recovered(event)) {
                std::snprintf(out,
                              length,
                              "连续 %ld 秒未检测到脉冲，关阀后水流已恢复正常。",
                              static_cast<long>(event.value2));
            } else {
                std::snprintf(
                    out,
                    length,
                    "%ld 秒内检测到 %ld 个脉冲，报警阈值为 %lu 个；请检查阀门和管路。",
                    static_cast<long>(event.value2),
                    static_cast<long>(event.value1),
                    static_cast<unsigned long>(event.objectId));
            }
            return;
        case EventCode::FlowCalibrationSaved: {
            const uint32_t previous =
                event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1);
            const uint32_t current =
                event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2);
            std::snprintf(out,
                          length,
                          "稳态流量系数由 %lu.%02lu 调整为 %lu.%02lu P/L。",
                          static_cast<unsigned long>(previous / 100U),
                          static_cast<unsigned long>(previous % 100U),
                          static_cast<unsigned long>(current / 100U),
                          static_cast<unsigned long>(current % 100U));
            return;
        }
        case EventCode::ZoneFlowSaved: {
            const uint32_t previous =
                event.value1 < 0 ? 0 : static_cast<uint32_t>(event.value1);
            const uint32_t current =
                event.value2 < 0 ? 0 : static_cast<uint32_t>(event.value2);
            if (current == 0) {
                std::snprintf(out,
                              length,
                              "原基准 %lu.%03lu L/min 已清除，高低流量报警停用。",
                              static_cast<unsigned long>(previous / 1000U),
                              static_cast<unsigned long>(previous % 1000U));
            } else if (previous == 0) {
                std::snprintf(out,
                              length,
                              "新基准为 %lu.%03lu L/min，后续浇水开始监测高低流量。",
                              static_cast<unsigned long>(current / 1000U),
                              static_cast<unsigned long>(current % 1000U));
            } else {
                std::snprintf(out,
                              length,
                              "基准由 %lu.%03lu 调整为 %lu.%03lu L/min。",
                              static_cast<unsigned long>(previous / 1000U),
                              static_cast<unsigned long>(previous % 1000U),
                              static_cast<unsigned long>(current / 1000U),
                              static_cast<unsigned long>(current % 1000U));
            }
            return;
        }
        case EventCode::ConfigurationChanged:
            std::snprintf(out, length, "新设置从之后的操作生效。"); return;
        case EventCode::WateringRecordSaveFailed:
            std::snprintf(out, length, "本次浇水记录可能丢失。"); return;
        case EventCode::SchedulerStateSaveFailed:
            std::snprintf(out, length, "为避免重复执行，自动计划已停止。"); return;
        case EventCode::RtcUnavailable:
            std::snprintf(out, length, recovered(event)
                          ? "设备已恢复读取硬件时钟。"
                          : "设备无法读取硬件时钟，断网时可能无法确定时间。"); return;
        case EventCode::TrustedTimeUnavailable:
            std::snprintf(out, length, recovered(event)
                          ? "设备已重新获得可信时间。"
                          : "设备无法确定当前时间，自动计划已停止。"); return;
        default:
            std::snprintf(out, length, "没有可用的业务说明。"); return;
    }
}

void IrrigationEvents::append(const Esp32BaseAppEvents::EventInput& event) {
    handleDiscreteResult(Esp32BaseAppEvents::appendDiscreteEvent(event));
}

void IrrigationEvents::observe(Esp32BaseAppEvents::ConditionStateTracker& tracker,
                               Esp32BaseAppEvents::ObservedConditionState state,
                               const Esp32BaseAppEvents::EventInput& event,
                               ConditionDisplayState& displayState) {
    const Esp32BaseAppEvents::ConditionObservationResult result =
        Esp32BaseAppEvents::observeConditionState(tracker, state, event);
    switch (result) {
        case Esp32BaseAppEvents::ConditionObservationResult::ConditionUnchanged:
        case Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored:
        case Esp32BaseAppEvents::ConditionObservationResult::RecoveryEventStored:
            displayState =
                state == Esp32BaseAppEvents::ObservedConditionState::Active
                    ? ConditionDisplayState::Active
                    : state == Esp32BaseAppEvents::ObservedConditionState::Inactive
                          ? ConditionDisplayState::Normal
                          : ConditionDisplayState::Unknown;
            break;
        case Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending:
            displayState = ConditionDisplayState::ConfirmingActivation;
            break;
        case Esp32BaseAppEvents::ConditionObservationResult::RecoveryConfirmationPending:
            displayState = ConditionDisplayState::ConfirmingRecovery;
            break;
        case Esp32BaseAppEvents::ConditionObservationResult::ObservationUnknown:
            displayState = ConditionDisplayState::Unknown;
            break;
        default:
            displayState = ConditionDisplayState::Unknown;
            break;
    }
    handleConditionResult(result);
}

void IrrigationEvents::handleDiscreteResult(
    Esp32BaseAppEvents::DiscreteEventAppendResult result) {
    if (result == Esp32BaseAppEvents::DiscreteEventAppendResult::Stored) return;
    updateStorageFault(true, "discrete_event_write_failed");
}

void IrrigationEvents::handleConditionResult(
    Esp32BaseAppEvents::ConditionObservationResult result) {
    switch (result) {
        case Esp32BaseAppEvents::ConditionObservationResult::ConditionUnchanged:
        case Esp32BaseAppEvents::ConditionObservationResult::ActivationConfirmationPending:
        case Esp32BaseAppEvents::ConditionObservationResult::RecoveryConfirmationPending:
        case Esp32BaseAppEvents::ConditionObservationResult::ActivationEventStored:
        case Esp32BaseAppEvents::ConditionObservationResult::RecoveryEventStored:
        case Esp32BaseAppEvents::ConditionObservationResult::ObservationUnknown:
            return;
        default:
            updateStorageFault(true, "condition_event_write_failed");
            return;
    }
}

void IrrigationEvents::updateStorageFault(bool fault, const char* reason) {
    if (!storageStateKnown_) {
        storageStateKnown_ = true;
        storageFault_ = fault;
        if (fault) ESP32BASE_LOG_E("irrigation", "event_storage_unavailable reason=%s", reason ? reason : "unknown");
        return;
    }
    if (storageFault_ == fault) return;
    storageFault_ = fault;
    if (fault) {
        ESP32BASE_LOG_E("irrigation", "event_storage_unavailable reason=%s", reason ? reason : "unknown");
    } else {
        ESP32BASE_LOG_W("irrigation", "event_storage_recovered");
    }
}

IrrigationEvents::ReasonCode IrrigationEvents::wateringReason(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::FlowStartTimeout: return ReasonCode::FlowStartTimeout;
        case WateringStopReason::NoFlowTimeout: return ReasonCode::NoFlowTimeout;
        case WateringStopReason::LowFlow: return ReasonCode::LowFlow;
        case WateringStopReason::HighFlow: return ReasonCode::HighFlow;
        case WateringStopReason::MaintenanceInterrupted: return ReasonCode::MaintenanceInterrupted;
        case WateringStopReason::HardwareFailure:
        default: return ReasonCode::HardwareFailure;
    }
}
