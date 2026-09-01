#include "IrrigationEvents.h"

#include <cstdio>

#include "IrrigationRecordSync.h"

IrrigationEvents::IrrigationEvents()
    : rtcUnavailableCondition_(kRtcUnavailableConditionId, 60000U, 60000U),
      trustedTimeUnavailableCondition_(kTrustedTimeUnavailableConditionId,
                                       300000U, 30000U),
      rtcRollbackCondition_(kRtcRollbackConditionId, 0U, 0U),
      closedValveFlowCondition_(kClosedValveFlowConditionId, 0U, 0U) {}

bool IrrigationEvents::begin() {
    storageFault_ = !auditStore_.begin();
    return !storageFault_;
}

IrrigationAuditStore& IrrigationEvents::auditStore() { return auditStore_; }

void IrrigationEvents::syncStorageStatus() {
    storageFault_ = !auditStore_.isReady() || !auditStore_.isWritable();
}

bool IrrigationEvents::resetConditionHistory() {
    if (!Esp32BaseConditions::forgetAll()) return false;
    rtcUnavailableState_ = ConditionDisplayState::Unknown;
    trustedTimeUnavailableState_ = ConditionDisplayState::Unknown;
    rtcRollbackState_ = ConditionDisplayState::Unknown;
    closedValveFlowState_ = ConditionDisplayState::Unknown;
    return true;
}

bool IrrigationEvents::storageFault() const { return storageFault_; }

bool IrrigationEvents::readStatus(EventStatus& status) const {
    Esp32BaseConditions::ConditionsStatus conditions;
    if (!auditStore_.readStatus(status.eventStore) ||
        !Esp32BaseConditions::readStatus(conditions)) return false;
    status.conditionStateLoaded = conditions.stateLoaded;
    status.conditionStateSavePending = false;
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
        if (status.active && status.purpose == WateringPurpose::FlowCalibration)
            return ReasonCode::PlanBusyFlowCalibration;
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
    payload.kind = IrrigationAuditPayload::Kind::AutomaticRun;
    payload.reason = static_cast<uint8_t>(automaticSkipReason(result, status));
    payload.flags = 3U;  // skipped
    payload.objectId = planId;
    append(payload);
}

void IrrigationEvents::recordAutomaticRun(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const WateringSessionSummary& summary) {
    if (summary.purpose != WateringPurpose::Normal ||
        summary.source != WateringSource::AutomaticPlan) return;
    IrrigationAuditPayload payload;
    payload.kind = IrrigationAuditPayload::Kind::AutomaticRun;
    payload.reason = static_cast<uint8_t>(summary.stopReason);
    payload.flags = summary.result == WateringResult::Completed
                        ? 0U
                        : summary.result == WateringResult::Stopped ? 1U : 2U;
    payload.objectId = summary.planId;
    append(startTime, payload);
}

void IrrigationEvents::recordFlowCalibrationSaved(
    uint32_t,
    uint32_t coefficientX100,
    uint32_t pulseCount,
    uint32_t waterMl) {
    IrrigationAuditPayload payload;
    payload.kind = IrrigationAuditPayload::Kind::CalibrationSaved;
    payload.reason = static_cast<uint8_t>(ReasonCode::CalibrationCoefficientSaved);
    payload.value1 = coefficientX100;
    payload.value2 = pulseCount;
    payload.value3 = waterMl;
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
    else return;  // Zone/system settings remain available as current state.
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

void IrrigationEvents::observeClosedValveFlow(
    Esp32BaseConditions::ObservedState state,
    uint32_t,
    uint32_t,
    uint16_t,
    uint16_t) {
    observe(closedValveFlowCondition_, state, closedValveFlowState_);
}

void IrrigationEvents::observe(
    Esp32BaseConditions::ConditionTracker& tracker,
    Esp32BaseConditions::ObservedState observed,
    ConditionDisplayState& display) {
    const auto result = Esp32BaseConditions::observe(tracker, observed);
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
            storageFault_ = true;
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
    const bool stored = IrrigationRecordSync::instance().appendAudit(payload);
    if (!stored) storageFault_ = true;
    return stored;
}

bool IrrigationEvents::append(
    const Esp32BaseRecordStore::RecordStartTime& startTime,
    const IrrigationAuditPayload& payload) {
    const bool stored =
        IrrigationRecordSync::instance().appendAudit(startTime, payload);
    if (!stored) storageFault_ = true;
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
        case IrrigationAuditPayload::Kind::AutomaticRun:
            event.eventCode = static_cast<uint32_t>(EventCode::AutomaticPlanSkipped);
            event.level = stored.payload.flags == 0U ? Level::Info : Level::Warning;
            break;
        case IrrigationAuditPayload::Kind::AutomaticStateChanged:
            event.eventCode = static_cast<uint32_t>(EventCode::AutomaticWateringStateChanged);
            break;
        case IrrigationAuditPayload::Kind::PlansChanged:
            event.eventCode = static_cast<uint32_t>(EventCode::ConfigurationChanged);
            break;
        case IrrigationAuditPayload::Kind::CalibrationSaved:
            event.eventCode = static_cast<uint32_t>(EventCode::FlowCalibrationSaved);
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
               : Category::SettingsAndCalibration;
}

const char* IrrigationEvents::categoryName(Category categoryValue) {
    switch (categoryValue) {
        case Category::AutomaticWatering: return "自动计划";
        case Category::SettingsAndCalibration: return "设置与校准";
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
        case EventCode::FlowCalibrationSaved:
            std::snprintf(out, length, "流量校准结果已保存");
            return;
        case EventCode::ZoneFlowSaved:
            std::snprintf(out, length, "水路 %lu 的基准流量已保存",
                          static_cast<unsigned long>(event.objectId));
            return;
        case EventCode::ConfigurationChanged:
            std::snprintf(out, length, "灌溉设置已修改");
            return;
    }
}

void IrrigationEvents::formatSummary(const EventRecord& event,
                                     char* out,
                                     std::size_t length) {
    if (!out || length == 0U) return;
    switch (static_cast<EventCode>(event.eventCode)) {
        case EventCode::AutomaticPlanSkipped:
            std::snprintf(out, length,
                          event.flags == 3U ? "本次计划已跳过且不会补执行。"
                                            : "本次自动浇水已经形成完整记录。");
            break;
        case EventCode::FlowCalibrationSaved:
            std::snprintf(out, length, "当前流量系数为 %lu.%02lu P/L。",
                          static_cast<unsigned long>(event.value1 / 100),
                          static_cast<unsigned long>(event.value1 % 100));
            break;
        case EventCode::ZoneFlowSaved:
            std::snprintf(out, length, "当前基准流量为 %ld ml/min。",
                          static_cast<long>(event.value2));
            break;
        default:
            std::snprintf(out, length, "当前设置已保存。");
            break;
    }
}
