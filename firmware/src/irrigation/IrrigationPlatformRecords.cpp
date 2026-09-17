#include "IrrigationPlatformRecords.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "IrrigationAuditPayload.h"
#include "IrrigationPlatform.h"
#include "WateringRecordCodec.h"

// Audit reason codes, mirrored from IrrigationEvents::ReasonCode. They are a
// stable on-disk contract; the values are duplicated here intentionally so the
// projection can be exercised in native tests without Esp32Base.
namespace AuditReason {
constexpr uint8_t kPausedIndefinitely = 1;
constexpr uint8_t kPausedUntil = 2;
constexpr uint8_t kResumedManually = 3;
constexpr uint8_t kResumedAutomatically = 4;
constexpr uint8_t kPlanBusy = 5;
constexpr uint8_t kPlanStartRejected = 6;
constexpr uint8_t kPlanBusyManualWatering = 7;
constexpr uint8_t kPlanBusyAutomaticWatering = 8;
constexpr uint8_t kPlanBusyZoneFlowLearning = 9;
constexpr uint8_t kPlanPreviousResultPending = 10;
constexpr uint8_t kPlanControllerNotReady = 11;
constexpr uint8_t kPlanInvalidRequest = 12;
constexpr uint8_t kPlanHardwareFailure = 13;
}  // namespace AuditReason

namespace {

// Copy the fixed watering payload; the first four business bytes are
// the run duration (little endian), matching IrrigationStoredFact.
struct WateringFact {
    uint32_t durationSec = 0;
    WateringRecordPayload watering{};
    IrrigationAuditPayload audit{};
    bool wateringKind = true;
};

bool decodeWateringFact(const uint8_t* bytes, size_t length,
                        WateringFact& out) {
    if (length != 4 + WateringRecordCodec::kPayloadSize) return false;
    out.durationSec = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                      (uint32_t(bytes[2]) << 16) |
                      (uint32_t(bytes[3]) << 24);
    out.wateringKind = true;
    return WateringRecordCodec::decode(
        bytes + 4, WateringRecordCodec::kPayloadSize, out.watering);
}

bool decodeAuditFact(const uint8_t* bytes, size_t length, WateringFact& out) {
    out.wateringKind = false;
    return IrrigationAuditCodec::decode(bytes, length, out.audit);
}

const char* stopReasonName(WateringStopReason reason) {
    switch (reason) {
        case WateringStopReason::Completed:
            return "completed";
        case WateringStopReason::UserStopped:
            return "user_stopped";
        case WateringStopReason::FlowStartTimeout:
            return "flow_start_timeout";
        case WateringStopReason::NoFlowTimeout:
            return "no_flow_timeout";
        case WateringStopReason::LowFlow:
            return "low_flow";
        case WateringStopReason::HighFlow:
            return "high_flow";
        case WateringStopReason::LearningTimeout:
            return "learning_timeout";
        case WateringStopReason::HardwareFailure:
            return "hardware_failure";
        case WateringStopReason::MaintenanceInterrupted:
            return "maintenance_interrupted";
        case WateringStopReason::TargetVolumeTimeout:
            return "target_volume_timeout";
        case WateringStopReason::RebootInterrupted:
            return "reboot_interrupted";
        case WateringStopReason::None:
        default:
            return "none";
    }
}

const char* zoneResultName(ZoneWateringResult result) {
    switch (result) {
        case ZoneWateringResult::Completed:
            return "completed";
        case ZoneWateringResult::Stopped:
            return "stopped";
        case ZoneWateringResult::Failed:
            return "failed";
        default:
            return "not-started";
    }
}

bool formatEpoch(uint32_t epoch, char* out, size_t outSize) {
    time_t t = static_cast<time_t>(epoch);
    tm utc{};
    if (!gmtime_r(&t, &utc)) return false;
    return std::strftime(out, outSize, "%Y-%m-%dT%H:%M:%S.000Z", &utc) > 0;
}

void addZone(JsonArray zones, const ZoneWateringRecord& source, uint8_t index) {
    if (source.plannedDurationSec == 0U) return;
    JsonObject zone = zones.createNestedObject();
    zone["zoneId"] = static_cast<uint8_t>(index + 1U);
    zone["targetSeconds"] = source.plannedDurationSec;
    if (source.targetWaterMl)
        zone["targetWaterMl"] = source.targetWaterMl;
    else
        zone["targetWaterMl"] = nullptr;
    zone["actualSeconds"] = source.actualWateringSec;
    zone["zoneResult"] = zoneResultName(source.result);
    zone["pulseCount"] = source.pulseCount;
    zone["estimatedWaterMl"] = source.estimatedWaterMl;
    if (source.flags &
        WateringRecordCodec::kZoneFlagFlowBaselineAvailable) {
        zone["baselinePulseRateX10000"] = source.baselinePulseRateX10000;
        zone["baselineFlowMlPerMinute"] = source.baselineFlowMlPerMinute;
    } else {
        zone["baselinePulseRateX10000"] = nullptr;
        zone["baselineFlowMlPerMinute"] = nullptr;
    }
    if (source.result != ZoneWateringResult::NotStarted)
        zone["averageFlowMlPerMinute"] = source.averageFlowMlPerMinute;
    else
        zone["averageFlowMlPerMinute"] = nullptr;
    if (source.suggestedBaselinePulseRateX10000)
        zone["suggestedBaselinePulseRateX10000"] =
            source.suggestedBaselinePulseRateX10000;
    else
        zone["suggestedBaselinePulseRateX10000"] = nullptr;
    zone["lowFlowDetected"] =
        (source.flags & WateringRecordCodec::kZoneFlagLowFlow) != 0;
    zone["highFlowDetected"] =
        (source.flags & WateringRecordCodec::kZoneFlagHighFlow) != 0;
}

// Shared body for completed/stopped/failed; observedAt is supplied by the
// envelope, so completedAt is derived only when it can be trusted.
bool decodeWateringCommon(const WateringFact& fact, JsonDocument& doc) {
    const WateringRecordPayload& w = fact.watering;
    JsonObject data = doc.to<JsonObject>();

    const bool timed = w.startedEpoch != 0;
    char startedAt[25]{};
    char completedAt[25]{};
    uint32_t completedEpoch = w.startedEpoch + fact.durationSec;
    if (timed) {
        formatEpoch(w.startedEpoch, startedAt, sizeof(startedAt));
        formatEpoch(completedEpoch, completedAt, sizeof(completedAt));
    }

    const char* sourceKey = "local_web";
    if (w.source == WateringSource::AutomaticPlan)
        sourceKey = "device_schedule";
    else if (w.source == WateringSource::WechatMiniprogram)
        sourceKey = "wechat_miniprogram";
    data["sourceKey"] = sourceKey;

    if (w.commandId[0] != '\0') {
        char commandId[37]{};
        std::memcpy(commandId, w.commandId.data(), 36);
        data["relatedCommandId"] = commandId;
    } else {
        data["relatedCommandId"] = nullptr;
    }
    if (w.source == WateringSource::AutomaticPlan && w.planId)
        data["planId"] = static_cast<int>(w.planId);
    else
        data["planId"] = nullptr;
    data["startedAt"] = timed ? startedAt : nullptr;
    data["completedAt"] = timed ? completedAt : nullptr;
    data["durationSeconds"] = fact.durationSec;
    data["timeQuality"] = timed ? "trusted" : "unknown";
    data["result"] = w.result == WateringResult::Completed ? "completed"
                   : w.result == WateringResult::Stopped   ? "stopped"
                                                           : "failed";
    data["reason"] =
        stopReasonName(static_cast<WateringStopReason>(w.stopReason));
    JsonArray zones = data["zones"].to<JsonArray>();
    for (uint8_t i = 0; i < w.zones.size(); ++i)
        addZone(zones, w.zones[i], i);
    return !doc.overflowed();
}

bool decodeCompleted(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    return decodeWateringFact(b, n, fact) &&
           fact.watering.result == WateringResult::Completed &&
           decodeWateringCommon(fact, doc);
}
bool decodeStopped(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    return decodeWateringFact(b, n, fact) &&
           fact.watering.result == WateringResult::Stopped &&
           decodeWateringCommon(fact, doc);
}
bool decodeFailed(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    return decodeWateringFact(b, n, fact) &&
           fact.watering.result != WateringResult::Completed &&
           fact.watering.result != WateringResult::Stopped &&
           decodeWateringCommon(fact, doc);
}

const char* automaticSkipReason(uint8_t reason) {
    switch (reason) {
        case AuditReason::kPlanBusyManualWatering:
            return "busy_manual_watering";
        case AuditReason::kPlanBusyAutomaticWatering:
            return "busy_automatic_watering";
        case AuditReason::kPlanBusyZoneFlowLearning:
            return "busy_zone_flow_learning";
        case AuditReason::kPlanPreviousResultPending:
            return "previous_result_pending";
        case AuditReason::kPlanControllerNotReady:
            return "controller_not_ready";
        case AuditReason::kPlanInvalidRequest:
            return "invalid_request";
        case AuditReason::kPlanHardwareFailure:
            return "hardware_failure";
        case AuditReason::kPlanBusy:
            return "busy";
        default:
            return "start_rejected";
    }
}

bool decodeAutomaticRun(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    if (!decodeAuditFact(b, n, fact)) return false;
    const IrrigationAuditPayload& a = fact.audit;
    if (a.kind != IrrigationAuditPayload::Kind::PlanSkipped) return false;
    JsonObject data = doc.to<JsonObject>();
    data["actionKey"] = "automatic.plan-run";
    data["sourceKey"] = "device_schedule";
    // This firmware records only skipped plan start points as automatic-run
    // completion evidence; the reason carries the skip cause.
    data["status"] = "skipped";
    data["reason"] = automaticSkipReason(a.reason);
    data["startedAt"] = nullptr;
    data["durationSeconds"] = nullptr;
    data["endedAt"] = nullptr;
    JsonObject parameters = data["parameters"].to<JsonObject>();
    parameters["planId"] = static_cast<int>(a.objectId);
    return !doc.overflowed();
}

bool decodeAutomaticPaused(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    if (!decodeAuditFact(b, n, fact)) return false;
    const IrrigationAuditPayload& a = fact.audit;
    if (a.kind !=
        IrrigationAuditPayload::Kind::AutomaticStateChanged)
        return false;
    JsonObject data = doc.to<JsonObject>();
    if (a.reason == AuditReason::kPausedIndefinitely) {
        data["mode"] = "paused-indefinitely";
        data["resumeAtEpoch"] = nullptr;
    } else if (a.reason == AuditReason::kPausedUntil) {
        data["mode"] = "paused-until";
        data["resumeAtEpoch"] = a.value1;
    } else {
        return false;
    }
    return !doc.overflowed();
}

bool decodeAutomaticResumed(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    if (!decodeAuditFact(b, n, fact)) return false;
    const IrrigationAuditPayload& a = fact.audit;
    if (a.kind !=
        IrrigationAuditPayload::Kind::AutomaticStateChanged)
        return false;
    JsonObject data = doc.to<JsonObject>();
    if (a.reason == AuditReason::kResumedAutomatically)
        data["mode"] = "expired";
    else if (a.reason == AuditReason::kResumedManually)
        data["mode"] = "enabled";
    else
        return false;
    data["resumeAtEpoch"] = nullptr;
    return !doc.overflowed();
}

bool decodePlansChanged(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    if (!decodeAuditFact(b, n, fact)) return false;
    const IrrigationAuditPayload& a = fact.audit;
    if (a.kind != IrrigationAuditPayload::Kind::PlansChanged) return false;
    JsonObject data = doc.to<JsonObject>();
    data["revision"] = a.value1;
    JsonArray planIds = data["planIds"].to<JsonArray>();
    for (uint8_t planId = 1; planId <= 8; ++planId)
        if (a.value2 & (1UL << (planId - 1U))) planIds.add(planId);
    return !doc.overflowed();
}

bool decodeZoneBaseline(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    if (!decodeAuditFact(b, n, fact)) return false;
    const IrrigationAuditPayload& a = fact.audit;
    if (a.kind !=
        IrrigationAuditPayload::Kind::ZoneBaselineSaved)
        return false;
    JsonObject data = doc.to<JsonObject>();
    data["zoneId"] = static_cast<uint8_t>(a.objectId);
    data["baselinePulseRateX10000"] = a.value1;
    data["baselineFlowMlPerMinute"] = a.value2;
    return !doc.overflowed();
}

bool decodeZoneChanged(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    if (!decodeAuditFact(b, n, fact)) return false;
    const IrrigationAuditPayload& a = fact.audit;
    if (a.kind != IrrigationAuditPayload::Kind::ZoneChanged) return false;
    JsonObject data = doc.to<JsonObject>();
    data["revision"] = a.value1;
    JsonArray zones = data.createNestedArray("zones");
    JsonObject zone = zones.createNestedObject();
    zone["zoneId"] = static_cast<uint8_t>(a.objectId);
    zone["enabled"] = (a.flags & 1U) != 0U;
    return !doc.overflowed();
}

bool decodeSystemFieldChanged(const uint8_t* b, size_t n, JsonDocument& doc) {
    WateringFact fact;
    if (!decodeAuditFact(b, n, fact)) return false;
    const IrrigationAuditPayload& a = fact.audit;
    if (a.kind != IrrigationAuditPayload::Kind::SystemFieldChanged) return false;
    if (a.objectId < 1U || a.objectId > 22U) return false;
    JsonObject data = doc.to<JsonObject>();
    data["fieldIndex"] = static_cast<uint8_t>(a.objectId);
    return !doc.overflowed();
}

// Business data length excludes the 4 leading duration bytes (which the SDK
// surfaces through the envelope timing). Watering facts are 4 + payload bytes.
constexpr size_t kWateringDataBytes =
    4 + WateringRecordCodec::kPayloadSize;
constexpr size_t kAuditDataBytes = IrrigationAuditCodec::kPayloadSize;

const iot_device::RecordCodec kCodecs[] = {
    {IrrigationPlatform::FactWateringCompleted, "watering.completed",
     kWateringDataBytes, decodeCompleted},
    {IrrigationPlatform::FactWateringStopped, "watering.stopped",
     kWateringDataBytes, decodeStopped},
    {IrrigationPlatform::FactWateringFailed, "watering.failed",
     kWateringDataBytes, decodeFailed},
    {IrrigationPlatform::FactAutomaticRunCompleted,
     "operation.automatic-run.completed", kAuditDataBytes,
     decodeAutomaticRun},
    {IrrigationPlatform::FactAutomaticPaused, "automatic.paused",
     kAuditDataBytes, decodeAutomaticPaused},
    {IrrigationPlatform::FactAutomaticResumed, "automatic.resumed",
     kAuditDataBytes, decodeAutomaticResumed},
    {IrrigationPlatform::FactPlansChanged, "configuration.plans-changed",
     kAuditDataBytes, decodePlansChanged},
    {IrrigationPlatform::FactZoneBaselineSaved, "zone.baseline-saved",
     kAuditDataBytes, decodeZoneBaseline},
    {IrrigationPlatform::FactZoneChanged, "configuration.zone-changed",
     kAuditDataBytes, decodeZoneChanged},
    {IrrigationPlatform::FactSystemFieldChanged,
     "configuration.system-field-changed", kAuditDataBytes,
     decodeSystemFieldChanged},
};

}  // namespace

namespace IrrigationPlatformRecords {

const iot_device::RecordCodec* codecs() { return kCodecs; }
size_t codecCount() { return sizeof(kCodecs) / sizeof(kCodecs[0]); }

}  // namespace IrrigationPlatformRecords
