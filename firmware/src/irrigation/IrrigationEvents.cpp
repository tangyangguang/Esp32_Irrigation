#include "IrrigationEvents.h"
#include <runtime/Esp32BaseTime.h>

#include <cstdio>

#include "IrrigationRecords.h"

IrrigationEvents::IrrigationEvents()
    : rtcUnavailableCondition_(kRtcUnavailableConditionId, 60000U, 60000U),
      trustedTimeUnavailableCondition_(kTrustedTimeUnavailableConditionId,
                                       300000U, 30000U),
      rtcRollbackCondition_(kRtcRollbackConditionId, 0U, 0U),
      closedValveFlowCondition_(kClosedValveFlowConditionId, 0U, 0U) {}

bool IrrigationEvents::begin() {
    return auditStore_.begin();
}

IrrigationAuditStore& IrrigationEvents::auditStore() { return auditStore_; }

bool IrrigationEvents::resetConditionHistory() {
    if (!Esp32BaseConditions::forgetAll()) return false;
    conditionFaults_ = 0;
    rtcUnavailableState_ = ConditionDisplayState::Unknown;
    trustedTimeUnavailableState_ = ConditionDisplayState::Unknown;
    rtcRollbackState_ = ConditionDisplayState::Unknown;
    closedValveFlowState_ = ConditionDisplayState::Unknown;
    return true;
}

bool IrrigationEvents::storageFault() const {
    return conditionFaults_ != 0 || !IrrigationRecords::instance().writable(
        IrrigationRecords::StoreKind::Audit);
}

bool IrrigationEvents::readStatus(EventStatus& status) const {
    Esp32BaseConditions::ConditionsStatus conditions;
    if (!auditStore_.readStatus(status.eventStore) ||
        !Esp32BaseConditions::readStatus(conditions)) return false;
    status.conditionStateLoaded = conditions.stateLoaded;
    return true;
}

bool IrrigationEvents::readLatest(uint32_t offset,
                                  uint32_t limit,
                                  ReadCallback callback,
                                  void* user) const {
    if (!callback) return false;
    struct Context {
        ReadCallback callback;
        void* user;
    } context{callback, user};
    auto adapter = [](const StoredIrrigationAuditRecord& stored, void* raw) {
        auto* context = static_cast<Context*>(raw);
        context->callback(present(stored), context->user);
    };
    return const_cast<IrrigationAuditStore&>(auditStore_)
        .readLatest(offset, limit, adapter, &context);
}

void IrrigationEvents::recordAutomaticWateringPaused(bool indefinitely,
                                                      uint32_t resumeAtEpoch) {
    IrrigationAuditPayload payload;
    payload.kind = IrrigationAuditPayload::Kind::AutomaticStateChanged;
    payload.reason = static_cast<uint8_t>(indefinitely
                                              ? ReasonCode::PausedIndefinitely
                                              : ReasonCode::PausedUntil);
    payload.value1 = resumeAtEpoch;
    append(payload);
}

void IrrigationEvents::recordAutomaticWateringResumed(bool automatically) {
    IrrigationAuditPayload payload;
    payload.kind = IrrigationAuditPayload::Kind::AutomaticStateChanged;
    payload.reason = static_cast<uint8_t>(automatically
                                              ? ReasonCode::ResumedAutomatically
                                              : ReasonCode::ResumedManually);
    append(payload);
}

IrrigationEvents::ReasonCode IrrigationEvents::automaticSkipReason(
    WateringStartResult result,
    const WateringStatus& status) {
    if (result == WateringStartResult::Busy) {
        if (status.active && status.purpose == WateringPurpose::ZoneFlowLearning)
            return ReasonCode::PlanBusyZoneFlowLearning;
        if (status.active && status.source == WateringSource::AutomaticPlan)
            return ReasonCode::PlanBusyAutomaticWatering;
        if (status.active) return ReasonCode::PlanBusyManualWatering;
        return ReasonCode::PlanBusy;
    }
    if (result == WateringStartResult::PreviousResultPending)
        return ReasonCode::PlanPreviousResultPending;
    if (result == WateringStartResult::NotReady)
        return ReasonCode::PlanControllerNotReady;
    if (result == WateringStartResult::InvalidRequest)
        return ReasonCode::PlanInvalidRequest;
    if (result == WateringStartResult::HardwareFailure)
        return ReasonCode::PlanHardwareFailure;
    return ReasonCode::PlanStartRejected;
}

void IrrigationEvents::recordAutomaticPlanSkipped(
    uint8_t planId,
    const char*,
    WateringStartResult result,
    const WateringStatus& status) {
    if (result == WateringStartResult::Started) return;
    IrrigationAuditPayload payload;
    payload.kind = IrrigationAuditPayload::Kind::PlanSkipped;
    payload.reason = static_cast<uint8_t>(automaticSkipReason(result, status));
    payload.flags = 3U;  // skipped
    payload.objectId = planId;
    append(payload);
}

void IrrigationEvents::recordZoneFlowSaved(
    uint8_t zoneId,
    uint32_t,
    uint32_t pulseRateX10000,
    uint32_t flowMlPerMinute) {
    IrrigationAuditPayload payload;
    payload.kind = IrrigationAuditPayload::Kind::ZoneBaselineSaved;
    payload.reason = static_cast<uint8_t>(ReasonCode::ZoneFlowSaved);
    payload.objectId = zoneId;
    payload.value1 = pulseRateX10000;
    payload.value2 = flowMlPerMinute;
    append(payload);
}

void IrrigationEvents::recordConfigurationChanged(
    ConfigurationChange change,
    uint8_t objectId,
    const IrrigationConfig* config) {
    ReasonCode reason;
    if (change == ConfigurationChange::PlanCreated) reason = ReasonCode::PlanCreated;
    else if (change == ConfigurationChange::PlanUpdated) reason = ReasonCode::PlanUpdated;
    else if (change == ConfigurationChange::PlanDeleted) reason = ReasonCode::PlanDeleted;
    else if (change == ConfigurationChange::ZoneUpdated) reason = ReasonCode::ZoneUpdated;
    else if (change == ConfigurationChange::SystemParametersUpdated) reason = ReasonCode::SystemParametersUpdated;
    else return;
    IrrigationAuditPayload payload;
    payload.kind = IrrigationAuditPayload::Kind::PlansChanged;
    payload.reason = static_cast<uint8_t>(reason);
    payload.objectId = objectId;
    if (config) {
        payload.value1 = config->revision;
        for (const WateringPlan& plan : config->plans)
            if (plan.configured && plan.id >= 1U && plan.id <= 8U)
                payload.value2 |= 1UL << (plan.id - 1U);
    }
    append(payload);
}

void IrrigationEvents::observeRtcAvailability(bool available, uint8_t) {
    observe(rtcUnavailableCondition_,
            available ? Esp32BaseConditions::ObservedState::Inactive
                      : Esp32BaseConditions::ObservedState::Active,
            rtcUnavailableState_);
}

void IrrigationEvents::observeTrustedTime(bool trusted) {
    observe(trustedTimeUnavailableCondition_,
            trusted ? Esp32BaseConditions::ObservedState::Inactive
                    : Esp32BaseConditions::ObservedState::Active,
            trustedTimeUnavailableState_);
}

void IrrigationEvents::observeRtcRollback(
    Esp32BaseConditions::ObservedState state) {
    observe(rtcRollbackCondition_, state, rtcRollbackState_);
}

void IrrigationEvents::observeClosedValveFlow(Esp32BaseConditions::ObservedState state,
    uint32_t pulses, uint32_t, uint16_t windowSec, uint16_t) {
    const auto previous = closedValveFlowState_;
    observe(closedValveFlowCondition_, state, closedValveFlowState_);
    const bool active = closedValveFlowState_ == ConditionDisplayState::Active &&
                        previous != ConditionDisplayState::Active && previous != ConditionDisplayState::ConfirmingRecovery;
    const bool recovered = closedValveFlowState_ == ConditionDisplayState::Normal &&
        (previous == ConditionDisplayState::Active || previous == ConditionDisplayState::ConfirmingRecovery);
    if (!active && !recovered) return;
    IrrigationAuditPayload payload{}; payload.kind = IrrigationAuditPayload::Kind::ClosedFlowChanged;
    payload.flags = active ? 1 : 0; payload.value1 = pulses; payload.value2 = windowSec;
    append(payload);
}

void IrrigationEvents::observe(
    Esp32BaseConditions::ConditionTracker& tracker,
    Esp32BaseConditions::ObservedState observed,
    ConditionDisplayState& display) {
    const auto result = Esp32BaseConditions::observe(tracker, observed);
    const uint8_t faultBit = static_cast<uint8_t>(1U << (tracker.conditionId() - 1U));
    // Only a confirmed observation clears this tracker's readiness error.
    // A different healthy condition must not hide it.
    if (result == Esp32BaseConditions::ObservationResult::Activated ||
        result == Esp32BaseConditions::ObservationResult::Recovered ||
        result == Esp32BaseConditions::ObservationResult::ConditionUnchanged)
        conditionFaults_ &= static_cast<uint8_t>(~faultBit);
    switch (result) {
        case Esp32BaseConditions::ObservationResult::Activated:
            display = ConditionDisplayState::Active;
            break;
        case Esp32BaseConditions::ObservationResult::Recovered:
            display = ConditionDisplayState::Normal;
            break;
        case Esp32BaseConditions::ObservationResult::ActivationConfirmationPending:
            display = ConditionDisplayState::ConfirmingActivation;
            break;
        case Esp32BaseConditions::ObservationResult::RecoveryConfirmationPending:
            display = ConditionDisplayState::ConfirmingRecovery;
            break;
        case Esp32BaseConditions::ObservationResult::ObservationUnknown:
            display = ConditionDisplayState::Unknown;
            break;
        case Esp32BaseConditions::ObservationResult::ConditionUnchanged: {
            bool active = false;
            display = Esp32BaseConditions::isActive(tracker.conditionId(), active)
                          ? active ? ConditionDisplayState::Active
                                   : ConditionDisplayState::Normal
                          : ConditionDisplayState::Unknown;
            break;
        }
        default:
            conditionFaults_ |= faultBit;
            display = ConditionDisplayState::Unknown;
            break;
    }
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

bool IrrigationEvents::append(const IrrigationAuditPayload& payload) {
    const bool stored = IrrigationRecords::instance().appendAudit(payload);
    return stored;
}

IrrigationEvents::EventRecord IrrigationEvents::present(
    const StoredIrrigationAuditRecord& stored) {
    EventRecord event;
    event.recordId = stored.recordId;
    event.timing = stored.timing;
    event.reasonCode = stored.payload.reason;
    event.objectId = stored.payload.objectId;
    event.value1 = static_cast<int32_t>(stored.payload.value1);
    event.value2 = static_cast<int32_t>(stored.payload.value2);
    event.flags = stored.payload.flags;
    switch (stored.payload.kind) {
        case IrrigationAuditPayload::Kind::ClosedFlowChanged:
            event.eventCode = static_cast<uint32_t>(EventCode::ClosedValveFlow);
            event.level = stored.payload.flags ? Level::Warning : Level::Info;
            break;
        case IrrigationAuditPayload::Kind::PlanSkipped:
            event.eventCode = static_cast<uint32_t>(EventCode::AutomaticPlanSkipped);
            event.level = stored.payload.flags == 0U ? Level::Info : Level::Warning;
            break;
        case IrrigationAuditPayload::Kind::AutomaticStateChanged:
            event.eventCode = static_cast<uint32_t>(EventCode::AutomaticWateringStateChanged);
            break;
        case IrrigationAuditPayload::Kind::PlansChanged:
            event.eventCode = static_cast<uint32_t>(EventCode::ConfigurationChanged);
            break;
        case IrrigationAuditPayload::Kind::ZoneBaselineSaved:
            event.eventCode = static_cast<uint32_t>(EventCode::ZoneFlowSaved);
            break;
    }
    return event;
}

IrrigationEvents::Category IrrigationEvents::category(const EventRecord& event) {
    const EventCode code = static_cast<EventCode>(event.eventCode);
    return code == EventCode::AutomaticWateringStateChanged ||
                   code == EventCode::AutomaticPlanSkipped
               ? Category::AutomaticWatering
               : Category::Settings;
}

const char* IrrigationEvents::categoryName(Category categoryValue) {
    switch (categoryValue) {
        case Category::AutomaticWatering: return "自动计划";
        case Category::Settings: return "设置与维护";
        case Category::TimeAndStorage: return "时间与存储";
        default: return "浇水与流量";
    }
}
const char* IrrigationEvents::levelName(Level level) {
    return level == Level::Error ? "错误" : level == Level::Warning ? "警告" : "信息";
}
uint8_t IrrigationEvents::wateringPlanId(const EventRecord& event) {
    return static_cast<EventCode>(event.eventCode) == EventCode::AutomaticPlanSkipped &&
                   event.objectId <= kWateringPlanCount
               ? static_cast<uint8_t>(event.objectId)
               : 0U;
}

void IrrigationEvents::formatTitle(const EventRecord& event,
                                   char* out,
                                   std::size_t length,
                                   const char*,
                                   const char*) {
    if (!out || length == 0U) return;
    switch (static_cast<EventCode>(event.eventCode)) {
        case EventCode::AutomaticWateringStateChanged:
            std::snprintf(out, length,
                          event.reasonCode == static_cast<uint32_t>(ReasonCode::ResumedManually) ||
                                  event.reasonCode == static_cast<uint32_t>(ReasonCode::ResumedAutomatically)
                              ? "自动浇水已恢复"
                              : "自动浇水已暂停");
            return;
        case EventCode::AutomaticPlanSkipped:
            std::snprintf(out, length, "%s %lu %s",
                          event.flags == 3U ? "自动计划" : "自动计划运行",
                          static_cast<unsigned long>(event.objectId),
                          event.flags == 3U ? "未执行" : "已结束");
            return;
        case EventCode::ZoneFlowSaved:
            std::snprintf(out, length, "水路 %lu 的基准流量已保存",
                          static_cast<unsigned long>(event.objectId));
            return;
        case EventCode::ClosedValveFlow:
            std::snprintf(out, length, "%s", event.flags ? "关阀后水流异常" : "关阀后水流已恢复"); return;
        case EventCode::ConfigurationChanged:
            std::snprintf(out, length, "灌溉设置已修改");
            return;
        default:
            std::snprintf(out, length, "未识别的历史事项");
            return;
    }
}

void IrrigationEvents::formatSummary(const EventRecord& event, char* out, std::size_t length) {
    if (!out || !length) return;
    if (event.eventCode == static_cast<uint32_t>(EventCode::ClosedValveFlow)) {
        std::snprintf(out, length, "%s", event.flags ? "关闭输出后仍检测到持续水流，请检查阀门、水源及现场管路。" : "完整观察窗口已无脉冲，当前异常解除；历史不代表现在状态。"); return;
    }
    const char* message = "设置已更新，仅影响之后的任务。";
    switch (static_cast<ReasonCode>(event.reasonCode)) {
        case ReasonCode::PausedIndefinitely: message = "自动浇水持续暂停，手动恢复后生效；当前任务不受影响。"; break;
        case ReasonCode::PausedUntil: {
            char time[32]{};
            Esp32BaseTime::formatEpoch(static_cast<uint32_t>(event.value1), time, sizeof(time), "%m-%d %H:%M");
            std::snprintf(out, length, "暂停至 %s；暂停期间的计划不会补执行。", time); return;
        }
        case ReasonCode::ResumedManually: message = "用户恢复自动浇水，将按之后的启动时间执行。"; break;
        case ReasonCode::ResumedAutomatically: message = "暂停时间已到，自动浇水恢复；错过的计划不补执行。"; break;
        case ReasonCode::PlanBusyManualWatering: message = "当时正在手动浇水，本次计划未执行，不会补浇。"; break;
        case ReasonCode::PlanBusyAutomaticWatering: message = "当时另一计划正在运行，本次计划未执行，不会补浇。"; break;
        case ReasonCode::PlanBusyZoneFlowLearning: message = "当时正在学习水路基准，本次计划未执行。"; break;
        case ReasonCode::PlanPreviousResultPending: message = "上一任务的结果尚未完成保存，本次计划未执行。"; break;
        case ReasonCode::PlanControllerNotReady: message = "当时设备或记录存储未就绪，本次计划未执行。"; break;
        case ReasonCode::PlanInvalidRequest: message = "计划参数或水路配置无效，本次计划未执行。"; break;
        case ReasonCode::PlanHardwareFailure: message = "控制输出启动失败，本次计划未执行，请检查设备。"; break;
        case ReasonCode::PlanStartRejected: case ReasonCode::PlanBusy: message = "设备拒绝本次计划启动，不会自动补执行。"; break;
        case ReasonCode::ZoneFlowSaved: std::snprintf(out, length, "基准设置为 %.3f L/min，用于高低流量判断；不是计量系数校准。", event.value2 / 1000.0); return;
        case ReasonCode::PlanCreated: message = "新建计划；保存不会立即出水。"; break;
        case ReasonCode::PlanUpdated: message = "更新计划；正在执行的任务仍使用启动时的配置。"; break;
        case ReasonCode::PlanDeleted: message = "计划已删除，不再按该计划自动启动。"; break;
        case ReasonCode::ZoneUpdated: message = "水路名称或启用状态已更新；正在执行的任务不受影响。"; break;
        case ReasonCode::SystemParametersUpdated: message = "设备参数已更新，后续任务使用新参数。"; break;
        default: break;
    }
    std::snprintf(out, length, "%s", message);
}
