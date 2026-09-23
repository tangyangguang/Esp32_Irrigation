#include "IrrigationPlatform.h"

#include <Esp32Base.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <time.h>
#include <sys/time.h>

#include <cstdio>
#include <cstring>

#include <PlatformPublishPolicy.h>
#include <ports/Esp32MqttPort.h>
#include <ports/Esp32RecordStorage.h>
#include <ports/Esp32Diagnostics.h>

#include "BoardPins.h"
#include "IrrigationApp.h"
#include "IrrigationConfig.h"
#include "IrrigationEvents.h"
#include "IrrigationIotSecrets.h"
#include "IrrigationAuditStore.h"
#include "IrrigationPlatformRecords.h"
#include "WateringRecordStore.h"
#include "IrrigationParameterConfig.h"
#include "IrrigationTypes.h"
#include "WateringScheduler.h"
#include "IrrigationSdkModel.generated.h"

namespace IrrigationPlatform {
namespace {

using namespace iot_device;

constexpr size_t kTopicBytes = 129;
constexpr size_t kWillBytes = 512;
constexpr size_t kOutputBytes = 4097;
constexpr size_t kCommandSlots = 6;
constexpr size_t kCommandFrameBytes = 1024;
constexpr size_t kWateringScratchBytes =
    RecordStream::HeaderBytes +
    WateringRecordCodec::kPayloadSize;  // IR/v1 header + watering payload
constexpr size_t kAuditScratchBytes =
    RecordStream::HeaderBytes + 20;  // IR/v1 header + audit payload


char g_deviceId[40] = {};
char g_bootId[64] = {};
Esp32Diagnostics g_diagnostics;

char g_topic[kTopicBytes];
char g_will[kWillBytes];
char g_output[kOutputBytes];

bool g_configured = false;
bool g_stopping = false;
const char* g_configureResult = "not_run";

bool randomBytes(uint8_t output[16], void*) {
    esp_fill_random(output, 16);
    return true;
}

bool utcNow(char value[25], void*) {
    if (!Esp32BaseTime::isRealTime() ||
        Esp32BaseTime::snapshot().source != Esp32BaseTime::SOURCE_NTP) {
        return false;
    }
    timeval now{};
    gettimeofday(&now, nullptr);
    tm utc{};
    if (!gmtime_r(&now.tv_sec, &utc) ||
        strftime(value, 25, "%Y-%m-%dT%H:%M:%S", &utc) != 19) {
        return false;
    }
    return snprintf(value + 19, 6, ".%03uZ",
                    static_cast<unsigned>(now.tv_usec / 1000)) == 5;
}

SessionIo g_io = {randomBytes, utcNow, Esp32MqttPort::publish,
                  Esp32MqttPort::reconnect, nullptr};

ConnectionSession g_session(
    PlatformIdentity{"test", "irrigation-controller",
                     "irrigation-controller",
                     static_cast<uint32_t>(1),
                     "irrigation-controller-6-zone", g_deviceId},
    SessionBuffers{g_topic, g_topic, g_topic, sizeof(g_topic), g_will,
                   sizeof(g_will), g_output, sizeof(g_output)},
    g_io);

Esp32MqttPort g_port(g_session);

DynamicJsonDocument g_stateDoc(6144);
StaticJsonDocument<512> g_recordDoc;
StaticJsonDocument<1024> g_inputDoc;
StaticJsonDocument<1024> g_compareDoc;

ModelPublisher g_publisher(
    model_irrigation_controller_6_zone::contract, g_session, g_io, g_recordDoc,
    g_topic, sizeof(g_topic), g_output, sizeof(g_output),
    IrrigationPlatformRecords::codecs(),
    IrrigationPlatformRecords::codecCount());

// 统一发布策略：连接后逐帧门控发布，失败自动重试，避免一轮内联发挤爆 outbox。
// 定义在此供下方匿名命名空间内的发布函数与公共 begin()/poll() 共用。
PlatformPublishPolicy g_publishPolicy(
    model_irrigation_controller_6_zone::contract);
bool g_policyConnected = false;


// ---- commands -------------------------------------------------------------

CommandSlot g_commandSlots[kCommandSlots];
uint8_t g_commandFrames[kCommandSlots * kCommandFrameBytes];

struct PendingCommand {
    CommandHandle handle{};
    char capability[40] = {};
    char commandId[37] = {};
    bool active = false;
    bool runningSent = false;
    WateringSource source = WateringSource::LocalWeb;
};
PendingCommand g_pending[kCommandSlots];

const char* kKeyPlan = "parameter.plan";
const char* kKeyZone = "parameter.zone";
const char* kKeyZoneBaseline = "parameter.zone-baseline";
const char* kKeySystemField = "parameter.system-field";
const char* kKeyAutomatic = "parameter.automatic-watering";
const char* kKeyStartManual = "operation.start-manual";
const char* kKeyStop = "operation.stop";
const char* kKeySingleOutput = "operation.single-output";

bool keyIs(const char* key, const char* expected) {
    return key && std::strcmp(key, expected) == 0;
}

const char* configSaveReason(IrrigationApp::ConfigSaveError error) {
    switch (error) {
        case IrrigationApp::ConfigSaveError::Ok:
            return nullptr;
        case IrrigationApp::ConfigSaveError::RevisionMismatch:
            return "revision_conflict";
        case IrrigationApp::ConfigSaveError::ZoneUnavailable:
            return "zone_unavailable";
        case IrrigationApp::ConfigSaveError::Busy:
            return "busy";
        case IrrigationApp::ConfigSaveError::AuditUnavailable:
            return "persistence_error";
        case IrrigationApp::ConfigSaveError::Persistence:
            return "persistence_error";
        case IrrigationApp::ConfigSaveError::NotReady:
            return "controller_unavailable";
        case IrrigationApp::ConfigSaveError::InvalidValue:
        default:
            return "invalid_request";
    }
}

// Read one plan object into a firmware WateringPlan. Only enabled zones carry
// durations in the projection; disabled-zone durations no longer exist.
bool parsePlanObject(JsonObjectConst object, const IrrigationConfig& current,
                     WateringPlan& plan) {
    plan = {};
    const int id = object["id"].as<int>();
    if (id < 1 || id > static_cast<int>(kWateringPlanCount)) return false;
    plan.id = static_cast<uint8_t>(id);
    plan.configured = true;
    plan.startMinutes.fill(kUnusedStartMinute);

    const char* name = object["name"].as<const char*>();
    if (!name) return false;
    std::snprintf(plan.name.data(), plan.name.size(), "%s", name);
    plan.scheduleEnabled = object["automaticEnabled"].as<bool>();

    JsonArrayConst starts = object["startMinutes"].as<JsonArrayConst>();
    if (starts.isNull() || starts.size() > kPlanStartTimeCount) return false;
    uint8_t m = 0;
    for (JsonVariantConst minute : starts) {
        if (!minute.is<int>()) return false;
        const int value = minute.as<int>();
        if (value < 0 || value >= 24 * 60) return false;
        plan.startMinutes[m++] = static_cast<uint16_t>(value);
    }

    JsonArrayConst zones = object["zones"].as<JsonArrayConst>();
    if (zones.isNull() || zones.size() > BoardPins::kZoneCount) return false;
    for (JsonVariantConst z : zones) {
        const int zoneId = z["zoneId"].as<int>();
        const int minutes = z["durationMinutes"].as<int>();
        if (!BoardPins::isValidZoneId(static_cast<uint8_t>(zoneId)) ||
            !current.zones[BoardPins::zoneIndex(static_cast<uint8_t>(zoneId))].enabled) {
            return false;
        }
        plan.zoneDurationMinutes[BoardPins::zoneIndex(static_cast<uint8_t>(zoneId))] =
            static_cast<uint16_t>(minutes);
    }
    return true;
}

// Build a watering request from a start-manual / single-output command.
WateringStartResult submitWatering(const CommandView& cmd, PendingCommand& pc) {
    IrrigationApp& app = IrrigationApp::instance();
    WateringRequest request{};
    request.source = WateringSource::WechatMiniprogram;
    // commandId is a fixed 36-byte (UUID text) field with no trailing NUL.
    std::memset(request.commandId.data(), 0, request.commandId.size());
    std::strncpy(request.commandId.data(), cmd.commandId,
                 request.commandId.size());

    JsonArrayConst zones = cmd.parameters["zones"].as<JsonArrayConst>();
    if (keyIs(cmd.capability->key, kKeySingleOutput)) {
        const uint8_t zoneId =
            static_cast<uint8_t>(cmd.parameters["zoneId"].as<int>());
        const char* mode = cmd.parameters["mode"].as<const char*>();
        request.stepCount = 1;
        request.targetMode = WateringTargetMode::Volume;
        request.steps[0].zoneId = zoneId;
        if (mode && std::strcmp(mode, "duration") == 0) {
            request.targetMode = WateringTargetMode::Duration;
            request.steps[0].targetDurationSec =
                cmd.parameters["durationSeconds"].as<uint32_t>();
        } else {
            request.steps[0].targetWaterMl =
                cmd.parameters["targetWaterMl"].as<uint32_t>();
        }
    } else {
        request.targetMode = WateringTargetMode::Duration;
        uint8_t count = 0;
        for (JsonVariantConst z : zones) {
            if (count >= BoardPins::kZoneCount) return WateringStartResult::InvalidRequest;
            request.steps[count].zoneId =
                static_cast<uint8_t>(z["zoneId"].as<int>());
            request.steps[count].targetDurationSec =
                z["durationMinutes"].as<uint32_t>() * 60U;
            ++count;
        }
        request.stepCount = count;
    }
    return app.startWatering(request);
}

const char* startResultReason(WateringStartResult result) {
    switch (result) {
        case WateringStartResult::Started:
            return nullptr;
        case WateringStartResult::Busy:
            return "busy";
        case WateringStartResult::NotReady:
            return "controller_unavailable";
        case WateringStartResult::PreviousResultPending:
            return "busy";
        case WateringStartResult::HardwareFailure:
            return "hardware_failure";
        default:
            return "invalid_request";
    }
}

CommandDecision decideCommand(const CommandView& cmd, void*) {
    IrrigationApp& app = IrrigationApp::instance();
    const char* key = cmd.capability->key;

    if (keyIs(key, kKeyStop)) {
        return {true, nullptr};  // idle stop is idempotent success
    }
    if (keyIs(key, kKeyPlan) || keyIs(key, kKeyZone) ||
        keyIs(key, kKeyZoneBaseline) || keyIs(key, kKeySystemField) ||
        keyIs(key, kKeyAutomatic)) {
        if (!app.businessReady()) return {false, "controller_unavailable"};
        return {true, nullptr};
    }
    if (keyIs(key, kKeyStartManual) || keyIs(key, kKeySingleOutput)) {
        if (!app.businessReady()) return {false, "controller_unavailable"};
        if (app.wateringActive()) return {false, "busy"};
        // Re-validate by actually attempting admission in execute; decide only
        // guards the obvious exclusions so the same business validator runs.
        return {true, nullptr};
    }
    return {false, "unknown_capability"};
}

void executeCommand(CommandHandle handle, const CommandView& cmd, void*);

CommandInbox g_inbox(
    model_irrigation_controller_6_zone::contract, g_session, g_publisher, g_io,
    g_inputDoc, g_compareDoc, g_commandSlots, kCommandSlots, g_commandFrames,
    kCommandFrameBytes, decideCommand, executeCommand, nullptr);

void executeCommand(CommandHandle handle, const CommandView& cmd, void*) {
    IrrigationApp& app = IrrigationApp::instance();
    PendingCommand& pc = g_pending[handle.index];
    pc = {};
    pc.handle = handle;
    std::snprintf(pc.capability, sizeof(pc.capability), "%s",
                  cmd.capability->key);
    std::snprintf(pc.commandId, sizeof(pc.commandId), "%s", cmd.commandId);
    pc.active = true;
    const char* key = cmd.capability->key;

    if (keyIs(key, kKeyPlan)) {
        const IrrigationConfig* current = app.configuration();
        if (!current) {
            g_inbox.progress(handle, ProgressStatus::Failed,
                             "persistence_error");
            pc.active = false;
            return;
        }
        const uint32_t expectedRevision =
            cmd.parameters["revision"].as<uint32_t>();
        const char* action = cmd.parameters["action"].as<const char*>();
        IrrigationApp::ConfigSaveError result = IrrigationApp::ConfigSaveError::InvalidValue;

        if (action && std::strcmp(action, "delete") == 0) {
            const int id = cmd.parameters["id"].as<int>();
            if (id < 1 || id > static_cast<int>(kWateringPlanCount)) {
                g_inbox.progress(handle, ProgressStatus::Failed,
                                 "invalid_request");
                pc.active = false;
                return;
            }
            WateringPlan plan{};
            plan.id = static_cast<uint8_t>(id);
            result = app.savePlanSlot(plan, true, expectedRevision);
        } else if (action && std::strcmp(action, "set-enabled") == 0) {
            const int id = cmd.parameters["id"].as<int>();
            if (id < 1 || id > static_cast<int>(kWateringPlanCount)) {
                g_inbox.progress(handle, ProgressStatus::Failed,
                                 "invalid_request");
                pc.active = false;
                return;
            }
            WateringPlan plan = current->plans[id - 1];
            plan.id = static_cast<uint8_t>(id);
            plan.scheduleEnabled = cmd.parameters["automaticEnabled"].as<bool>();
            result = app.savePlanSlot(plan, false, expectedRevision);
        } else if (action && std::strcmp(action, "upsert") == 0) {
            WateringPlan plan{};
            if (parsePlanObject(cmd.parameters["plan"].as<JsonObjectConst>(),
                                *current, plan)) {
                result = app.savePlanSlot(plan, false, expectedRevision);
            }
        }

        if (result == IrrigationApp::ConfigSaveError::Ok) {
            g_inbox.progress(handle, ProgressStatus::Succeeded);
        } else {
            g_inbox.progress(handle, ProgressStatus::Failed,
                             configSaveReason(result));
        }
        pc.active = false;
        return;
    }

    if (keyIs(key, kKeyZone)) {
        const uint32_t revision = cmd.parameters["revision"].as<uint32_t>();
        const int zoneId = cmd.parameters["zoneId"].as<int>();
        const IrrigationConfig* current = app.configuration();
        if (!current || zoneId < 1 || zoneId > BoardPins::kZoneCount) {
            g_inbox.progress(handle, ProgressStatus::Failed, "invalid_request");
            pc.active = false;
            return;
        }
        const std::size_t zi = BoardPins::zoneIndex(static_cast<uint8_t>(zoneId));
        char name[kObjectNameCapacity]{};
        bool enabled = current->zones[zi].enabled;
        if (cmd.parameters["name"].is<const char*>()) {
            std::snprintf(name, sizeof(name), "%s",
                          cmd.parameters["name"].as<const char*>());
        } else {
            std::snprintf(name, sizeof(name), "%s",
                          current->zones[zi].name.data());
        }
        if (cmd.parameters["enabled"].is<bool>()) {
            enabled = cmd.parameters["enabled"].as<bool>();
        }
        const IrrigationApp::ConfigSaveError result = app.saveZoneInfo(
            static_cast<uint8_t>(zoneId), name, enabled, revision);
        g_inbox.progress(handle,
                         result == IrrigationApp::ConfigSaveError::Ok
                             ? ProgressStatus::Succeeded
                             : ProgressStatus::Failed,
                         configSaveReason(result));
        pc.active = false;
        return;
    }

    if (keyIs(key, kKeyZoneBaseline)) {
        const uint32_t revision = cmd.parameters["revision"].as<uint32_t>();
        const int zoneId = cmd.parameters["zoneId"].as<int>();
        uint32_t pulseRate = 0;
        if (cmd.parameters["baselinePulseRateX10000"].is<int>()) {
            const int value = cmd.parameters["baselinePulseRateX10000"].as<int>();
            if (value < 0) {
                g_inbox.progress(handle, ProgressStatus::Failed, "invalid_request");
                pc.active = false;
                return;
            }
            pulseRate = static_cast<uint32_t>(value);
        }
        const IrrigationApp::ConfigSaveError result = app.setZoneBaseline(
            static_cast<uint8_t>(zoneId), pulseRate, revision);
        g_inbox.progress(handle,
                         result == IrrigationApp::ConfigSaveError::Ok
                             ? ProgressStatus::Succeeded
                             : ProgressStatus::Failed,
                         configSaveReason(result));
        pc.active = false;
        return;
    }

    if (keyIs(key, kKeySystemField)) {
        const char* field = cmd.parameters["field"].as<const char*>();
        const JsonVariantConst value = cmd.parameters["value"];
        bool ok = false;
        if (value.is<int>() || value.is<uint32_t>() || value.is<long>()) {
            ok = app.applyRemoteSystemField(field, true, value.as<int32_t>(),
                                            false, false, nullptr);
        } else if (value.is<bool>()) {
            ok = app.applyRemoteSystemField(field, false, 0,
                                            true, value.as<bool>(), nullptr);
        } else if (value.is<const char*>()) {
            ok = app.applyRemoteSystemField(field, false, 0,
                                            false, false,
                                            value.as<const char*>());
        }
        g_inbox.progress(handle,
                         ok ? ProgressStatus::Succeeded : ProgressStatus::Failed,
                         ok ? nullptr : "invalid_field_value");
        pc.active = false;
        return;
    }

    if (keyIs(key, kKeyAutomatic)) {
        const char* mode = cmd.parameters["mode"].as<const char*>();
        bool ok = false;
        if (mode && std::strcmp(mode, "enabled") == 0) {
            ok = app.resumeAutomaticWatering();
        } else if (mode && std::strcmp(mode, "paused-indefinitely") == 0) {
            ok = app.pauseAutomaticWateringIndefinitely();
        } else if (mode && std::strcmp(mode, "paused-until") == 0) {
            const uint32_t resume =
                cmd.parameters["resumeAtEpoch"].as<uint32_t>();
                ok = app.pauseAutomaticWateringUntil(resume);
        }
        g_inbox.progress(handle, ok ? ProgressStatus::Succeeded
                                    : ProgressStatus::Failed,
                         ok ? nullptr : "persistence_error");
        pc.active = false;
        return;
    }

    if (keyIs(key, kKeyStop)) {
        // Stop is an independent instant command: it succeeds even when idle.
        app.stopWatering();
        g_inbox.progress(handle, ProgressStatus::Succeeded);
        pc.active = false;
        return;
    }

    // start-manual / single-output are process commands.
    const WateringStartResult result = submitWatering(cmd, pc);
    if (result != WateringStartResult::Started) {
        g_inbox.progress(handle, ProgressStatus::Failed,
                         startResultReason(result));
        pc.active = false;
        return;
    }
    // running progress is emitted by progressPoll once the controller is active.
}

void progressPoll() {
    IrrigationApp& app = IrrigationApp::instance();
    for (PendingCommand& pc : g_pending) {
        if (!pc.active) continue;
        const bool isProcess =
            keyIs(pc.capability, kKeyStartManual) ||
            keyIs(pc.capability, kKeySingleOutput);
        if (!isProcess) { pc.active = false; continue; }
        if (!pc.runningSent && app.wateringActive()) {
            if (g_inbox.progress(pc.handle, ProgressStatus::Running))
                pc.runningSent = true;
            continue;
        }
        if (app.wateringActive()) continue;
        // Read the terminal result only once the process has gone idle so the
        // status reflects this command's outcome rather than a stale value.
        const WateringStatus status = app.wateringStatus();
        ProgressStatus terminal = ProgressStatus::Succeeded;
        const char* reason = nullptr;
        if (status.lastResult == WateringResult::Stopped) {
            terminal = ProgressStatus::Canceled;
        } else if (status.lastResult == WateringResult::Failed ||
                   status.lastResult == WateringResult::Incomplete) {
            terminal = ProgressStatus::Failed;
            reason = "watering_failed";
        }
        if (g_inbox.progress(pc.handle, terminal, reason)) pc.active = false;
    }
}

// ---- identity -------------------------------------------------------------

bool buildDeviceId() {
    // Runs before Esp32Base::begin() (MQTT must be claimed pre-begin), so the
    // WiFi driver is not initialised yet and esp_read_mac(ESP_MAC_WIFI_STA)
    // fails. Read the eFuse base MAC directly; on single-MAC ESP32 it is the
    // STA MAC and the stable device identity.
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;
    // "esp32-irr-" (10 chars) + 12 hex chars = 22, NUL excluded.
    const int written = snprintf(
        g_deviceId, sizeof(g_deviceId), "esp32-irr-%02x%02x%02x%02x%02x%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return written == 22 && static_cast<size_t>(written) < sizeof(g_deviceId);
}

void buildBootId() {
    const auto now = Esp32BaseTime::snapshot();
    snprintf(g_bootId, sizeof(g_bootId), "%s-%lu-%08lx", g_deviceId,
             static_cast<unsigned long>(Esp32BaseSystem::bootCount()),
             static_cast<unsigned long>(now.bootId));
}

// ---- time helpers ---------------------------------------------------------

void writeIso(char out[25], uint32_t epochSec) {
    if (!epochSec) {
        out[0] = '\0';
        return;
    }
    time_t seconds = static_cast<time_t>(epochSec);
    tm utc{};
    gmtime_r(&seconds, &utc);
    strftime(out, 25, "%Y-%m-%dT%H:%M:%S.000Z", &utc);
}

// ---- state projection -----------------------------------------------------

const char* sourceKey(WateringSource source) {
    switch (source) {
        case WateringSource::LocalWeb:
            return "local_web";
        case WateringSource::WechatMiniprogram:
            return "wechat_miniprogram";
        case WateringSource::AutomaticPlan:
            return "device_schedule";
    }
    return "local_web";
}

const char* nextAutomaticStatus(NextAutomaticWateringStatus status) {
    switch (status) {
        case NextAutomaticWateringStatus::Available:
            return "available";
        case NextAutomaticWateringStatus::NoEnabledPlans:
            return "no-enabled-plans";
        case NextAutomaticWateringStatus::TimeUnavailable:
            return "time-unavailable";
        case NextAutomaticWateringStatus::RtcRollback:
            return "rtc-rollback";
        case NextAutomaticWateringStatus::PausedIndefinitely:
            return "paused-indefinitely";
    }
    return "time-unavailable";
}

const char* activityKind(const WateringStatus& s) {
    if (!s.active) return "idle";
    if (s.purpose == WateringPurpose::ZoneFlowLearning) return "learning";
    if (s.source == WateringSource::AutomaticPlan) return "automatic";
    if (s.targetMode == WateringTargetMode::Volume) return "single-output";
    return "manual";
}

const char* activityPhase(WateringState state) {
    switch (state) {
        case WateringState::StartingZone:
            return "starting";
        case WateringState::WaitingForFlow:
            return "waiting-flow";
        case WateringState::WateringZone:
            return "watering";
        case WateringState::StoppingZone:
            return "stopping";
        case WateringState::SwitchingZone:
            return "switching";
        default:
            return "idle";
    }
}

void projectOverview(const IrrigationApp& app, const WateringStatus& status) {
    g_stateDoc.clear();
    g_stateDoc["activity"] = status.active ? "active" : "idle";
    const bool critical =
        !app.businessReady() || app.unexpectedFlowAlarm() ||
        app.schedulerStorageFault();
    const bool warning =
        app.recordStorageFault() || app.eventStorageFault() ||
        app.checkpointStorageFault() || app.schedulerTimeState() ==
                                            WateringScheduler::TimeState::RtcRollback;
    g_stateDoc["health"] = critical ? "critical" : warning ? "warning" : "normal";
}

void projectZones(const IrrigationConfig& config) {
    g_stateDoc.clear();
    JsonArray zones = g_stateDoc.createNestedArray("zones");
    for (const ZoneConfig& zone : config.zones) {
        if (!zone.enabled) continue;
        JsonObject item = zones.createNestedObject();
        item["zoneId"] = zone.id;
        item["name"] = zone.name.data();
    }
}

void projectZoneMaintenance(const IrrigationConfig& config) {
    g_stateDoc.clear();
    g_stateDoc["revision"] = config.revision;
    JsonArray zones = g_stateDoc.createNestedArray("zones");
    for (const ZoneConfig& zone : config.zones) {
        if (!zone.enabled) continue;
        JsonObject item = zones.createNestedObject();
        item["zoneId"] = zone.id;
        if (zone.baselinePulseRateX10000) {
            item["baselinePulseRateX10000"] = zone.baselinePulseRateX10000;
        } else {
            item["baselinePulseRateX10000"] = nullptr;
        }
        item["baselineFlowMlPerMinute"] = nullptr;
    }
}

void projectCalibration(const IrrigationConfig& config) {
    g_stateDoc.clear();
    g_stateDoc["coefficientPulsesPerLiterX100"] =
        config.flowMeter.pulsesPerLiterX100;
}

void projectSystemParameters(const IrrigationConfig& config) {
    g_stateDoc.clear();
    JsonObject valve = g_stateDoc.createNestedObject("valve");
    valve["pullInTimeMs"] = config.valveDrive.pullInTimeMs;
    valve["switchDelayMs"] = config.valveDrive.switchDelayMs;
    valve["pwmFrequencyHz"] = config.valveDrive.pwmFrequencyHz;
    valve["holdDutyPercent"] = config.valveDrive.holdDutyPercent;
    JsonObject pump = g_stateDoc.createNestedObject("pump");
    pump["enabled"] = config.pump.enabled;
    pump["startDelayMs"] = config.pump.startDelayMs;
    pump["stopToValveCloseDelayMs"] = config.pump.stopToValveCloseDelayMs;
    JsonObject meter = g_stateDoc.createNestedObject("meter");
    meter["pulsesPerLiterX100"] = config.flowMeter.pulsesPerLiterX100;
    meter["flowStartTimeoutSeconds"] = config.flowProtection.flowStartTimeoutSec;
    meter["noFlowTimeoutSeconds"] = config.flowProtection.noFlowTimeoutSec;
    JsonObject flow = g_stateDoc.createNestedObject("flow");
    flow["unexpectedFlowDelaySeconds"] =
        config.flowProtection.unexpectedFlowDelaySec;
    flow["unexpectedFlowWindowSeconds"] =
        config.flowProtection.unexpectedFlowWindowSec;
    flow["unexpectedFlowPulseCount"] =
        config.flowProtection.unexpectedFlowPulseCount;
    flow["deviationConfirmSeconds"] =
        config.flowProtection.flowDeviationConfirmSec;
    flow["lowFlowPercent"] = config.flowProtection.lowFlowPercent;
    flow["highFlowPercent"] = config.flowProtection.highFlowPercent;
    flow["lowFlowAction"] =
        config.flowProtection.lowFlowAction == FlowAlertAction::StopWatering
            ? "stop"
            : "alert";
    flow["highFlowAction"] =
        config.flowProtection.highFlowAction == FlowAlertAction::StopWatering
            ? "stop"
            : "alert";
    JsonObject limits = g_stateDoc.createNestedObject("limits");
    limits["maximumZoneDurationMinutes"] =
        config.runLimits.maximumZoneDurationMinutes;
    limits["maximumSingleOutputLiters"] =
        config.runLimits.maximumSingleOutputLiters;
    JsonObject system = g_stateDoc.createNestedObject("system");
    system["rtcRollbackThresholdMinutes"] =
        config.timeSafety.rtcRollbackThresholdMinutes;
    system["aliveCheckpointHours"] = config.timeSafety.aliveCheckpointHours;
}

void projectPlans(const IrrigationConfig& config) {
    g_stateDoc.clear();
    g_stateDoc["revision"] = config.revision;
    JsonArray plans = g_stateDoc.createNestedArray("plans");
    for (const WateringPlan& plan : config.plans) {
        if (!plan.configured) continue;
        JsonObject item = plans.createNestedObject();
        item["id"] = plan.id;
        item["name"] = plan.name.data();
        item["automaticEnabled"] = plan.scheduleEnabled;
        JsonArray starts = item.createNestedArray("startMinutes");
        for (uint16_t minute : plan.startMinutes) {
            if (minute != kUnusedStartMinute) starts.add(minute);
        }
        JsonArray zones = item.createNestedArray("zones");
        for (const ZoneConfig& zone : config.zones) {
            if (!zone.enabled) continue;
            const uint16_t minutes =
                plan.zoneDurationMinutes[BoardPins::zoneIndex(zone.id)];
            if (!minutes) continue;
            JsonObject z = zones.createNestedObject();
            z["zoneId"] = zone.id;
            z["durationMinutes"] = minutes;
        }
    }
}

void projectAutomatic(const IrrigationApp& app) {
    g_stateDoc.clear();
    const AutomaticWateringState state = app.automaticWateringState();
    switch (state.mode) {
        case AutomaticWateringMode::PausedIndefinitely:
            g_stateDoc["mode"] = "paused-indefinitely";
            g_stateDoc["resumeAtEpoch"] = nullptr;
            break;
        case AutomaticWateringMode::PausedUntil:
            g_stateDoc["mode"] = "paused-until";
            g_stateDoc["resumeAtEpoch"] = state.resumeAtEpoch;
            break;
        default:
            g_stateDoc["mode"] = "enabled";
            g_stateDoc["resumeAtEpoch"] = nullptr;
            break;
    }
}

void projectRuntime(const IrrigationApp& app, const WateringStatus& s) {
    const auto* config = app.configuration();
    g_stateDoc.clear();
    g_stateDoc["ready"] = app.businessReady();
    const char* reason = "none";
    if (!app.businessReady()) {
        reason = app.configurationLoadResult() ==
                         IrrigationConfigStore::LoadResult::StorageUnavailable
                     ? "configuration_unavailable"
                     : "startup_check_failed";
        if (app.schedulerStorageFault()) reason = "scheduler_storage_unavailable";
    }
    g_stateDoc["readyReason"] = reason;

    JsonObject time = g_stateDoc.createNestedObject("time");
    const auto now = Esp32BaseTime::snapshot();
    time["trusted"] = now.synced;
    time["source"] = now.source == Esp32BaseTime::SOURCE_NTP
                         ? "ntp"
                         : now.source == Esp32BaseTime::SOURCE_RTC ? "rtc"
                                                                   : "none";
    if (now.synced)
        time["epoch"] = static_cast<long long>(now.epochSec);
    else
        time["epoch"] = nullptr;
    time["rtcAvailable"] = app.schedulerTimeState() !=
                           WateringScheduler::TimeState::Unavailable;
    time["rtcRollback"] = app.schedulerTimeState() ==
                          WateringScheduler::TimeState::RtcRollback;

    JsonObject nextAuto = g_stateDoc.createNestedObject("nextAutomatic");
    if (config) {
        const NextAutomaticWatering next =
            app.nextAutomaticWatering();
        nextAuto["status"] = nextAutomaticStatus(next.status);
        if (next.planId)
            nextAuto["planId"] = next.planId;
        else
            nextAuto["planId"] = nullptr;
        if (next.scheduledEpoch)
            nextAuto["scheduledAtEpoch"] =
                static_cast<long long>(next.scheduledEpoch);
        else
            nextAuto["scheduledAtEpoch"] = nullptr;
    } else {
        nextAuto["status"] = "time-unavailable";
        nextAuto["planId"] = nullptr;
        nextAuto["scheduledAtEpoch"] = nullptr;
    }

    JsonObject activity = g_stateDoc.createNestedObject("activity");
    activity["kind"] = activityKind(s);
    activity["commandId"] = nullptr;
    if (s.active)
        activity["zoneId"] = s.activeZoneId;
    else
        activity["zoneId"] = nullptr;
    activity["phase"] = s.active ? activityPhase(s.state) : "idle";
    activity["activityId"] = nullptr;
    activity["requestedDurationMs"] = nullptr;
    activity["elapsedMs"] = s.elapsedMs;
    activity["remainingMs"] = s.active ? s.currentZoneRemainingMs * 1000U : 0;
    activity["deadlineAt"] = nullptr;
    activity["timeQuality"] = now.synced ? "synchronized" : "uncertain";
    activity["clockUncertaintyMs"] = nullptr;
    activity["targetWaterMl"] = nullptr;
    activity["pulseCount"] = s.pulseCount;
    activity["estimatedWaterMl"] = s.totalEstimatedWaterMl;
    if (s.active)
        activity["flowMlPerMinute"] = s.currentFlowMlPerMinute;
    else
        activity["flowMlPerMinute"] = nullptr;
    activity["lowFlowActive"] = false;
    activity["highFlowActive"] = false;
    JsonArray steps = activity.createNestedArray("steps");
    if (s.active) {
        for (uint8_t i = 0; i < s.stepCount; ++i) {
            const auto& z = s.zones[i];
            if (z.zoneId == 0) continue;
            JsonObject step = steps.createNestedObject();
            step["zoneId"] = z.zoneId;
            step["targetDurationMs"] =
                static_cast<long long>(z.plannedDurationSec) * 1000U;
            if (z.targetWaterMl)
                step["targetWaterMl"] = z.targetWaterMl;
            else
                step["targetWaterMl"] = nullptr;
            const char* st = "pending";
            if (i < s.currentStepIndex)
                st = "completed";
            else if (i == s.currentStepIndex)
                st = "current";
            step["status"] = st;
        }
    }

    JsonArray faults = g_stateDoc.createNestedArray("faults");
    if (app.schedulerTimeState() == WateringScheduler::TimeState::RtcRollback)
        faults.add("rtc_rollback");
    if (app.unexpectedFlowAlarm()) faults.add("unexpected_flow");
    if (app.schedulerStorageFault()) faults.add("scheduler_storage");
    if (app.recordStorageFault()) faults.add("record_storage");
    if (app.eventStorageFault()) faults.add("event_storage");
}

// Build the diagnostics value into g_stateDoc. Returns false if the read
// cannot produce a frame; the token is returned for commit-after-queue.
bool buildDiagnostics(DiagnosticsFrameToken& token) {
    if (!g_diagnostics.read(g_stateDoc, g_bootId, token)) return false;

    g_stateDoc["bootNo"] = Esp32BaseSystem::bootCount();
    const auto mqtt = Esp32BaseMqtt::diagnostics();
    g_stateDoc["mqttAtt"] = mqtt.connectAttempts;
    const auto status = Esp32BaseMqtt::status();
    g_stateDoc["mqttErr"] = status.lastError == Esp32BaseMqtt::ERROR_NONE
                               ? nullptr
                               : Esp32BaseMqtt::errorName(status.lastError);
    g_stateDoc["wdt"] = Esp32BaseWatchdog::lifetimeResetCount();
    return true;
}

bool buildSnapshot(const char* key) {
    IrrigationApp& app = IrrigationApp::instance();
    const IrrigationConfig* config = app.configuration();
    const WateringStatus status = app.wateringStatus();

    if (!std::strcmp(key, "state.overview")) {
        projectOverview(app, status);
        return true;
    }
    if (!std::strcmp(key, "state.runtime")) {
        projectRuntime(app, status);
        return true;
    }
    if (!std::strcmp(key, "state.diagnostics")) return true;  // token handled by caller
    if (!config) return false;  // config-backed frames need evidence
    if (!std::strcmp(key, "state.zones")) {
        projectZones(*config);
        return true;
    }
    if (!std::strcmp(key, "state.zone-maintenance")) {
        projectZoneMaintenance(*config);
        return true;
    }
    if (!std::strcmp(key, "state.calibration")) {
        projectCalibration(*config);
        return true;
    }
    if (!std::strcmp(key, "state.system-parameters")) {
        projectSystemParameters(*config);
        return true;
    }
    if (!std::strcmp(key, "parameter.plan")) {
        projectPlans(*config);
        return true;
    }
    if (!std::strcmp(key, "parameter.automatic-watering")) {
        projectAutomatic(app);
        return true;
    }
    return false;
}

// Publish one due snapshot frame. Uses the policy's per-connection sequence
// and is retried automatically on failure.
bool publishDueSnapshot(uint32_t nowMs) {
    const auto& contract = model_irrigation_controller_6_zone::contract;
    for (size_t index = 0; index < contract.capabilityCount; ++index) {
        if (!g_publishPolicy.stateDue(index, nowMs)) continue;
        const char* key = contract.capabilities[index].key;

        // Write-only maintenance commands: they are command channels, never
        // published as observations. The policy marks every snapshot dirty on
        // connect, so skip them explicitly; leaving them due would look like a
        // build failure and trigger the 5s queue-retry block that stalls all
        // later first frames.
        if (!std::strcmp(key, "parameter.zone") ||
            !std::strcmp(key, "parameter.zone-baseline") ||
            !std::strcmp(key, "parameter.system-field")) {
            g_publishPolicy.stateQueued(index, nowMs);  // clears dirty, no frame
            continue;
        }

        DiagnosticsFrameToken diagToken;
        const bool isDiagnostics = !std::strcmp(key, "state.diagnostics");
        if (isDiagnostics) {
            if (!buildDiagnostics(diagToken)) {
                g_publishPolicy.stateQueueFailed(nowMs);
                return false;
            }
        } else if (!buildSnapshot(key)) {
            g_publishPolicy.stateQueueFailed(nowMs);
            return false;
        }

        if (!g_publisher.state(key, g_publishPolicy.stateSequence(), g_stateDoc)) {
            g_publishPolicy.stateQueueFailed(nowMs);
            return false;
        }
        if (!g_publishPolicy.stateQueued(index, nowMs)) return false;
        if (isDiagnostics) g_diagnostics.commit(diagToken);  // only after queued
        return true;
    }
    return false;
}

// Round-robin state publishing cadence.
void publishStates(uint32_t nowMs) {
    // Re-mark frequently-changing snapshots dirty; the policy merges/throttles.
    g_publishPolicy.markStateDirty("state.runtime");
    g_publishPolicy.markOverviewDirty();
    g_publishPolicy.markStateDirty("parameter.plan");
    g_publishPolicy.markStateDirty("parameter.automatic-watering");
    if (g_diagnostics.networkChanged(g_bootId))
        g_publishPolicy.markDiagnosticsDirty();

    g_publishPolicy.poll(nowMs);

    // Drain due frames with a small burst cap so a single loop never floods the
    // transport. When the outbox/inflight window is full, publishDueSnapshot
    // reports failure and the policy blocks retries briefly; remaining frames
    // go on later loops instead of being dropped.
    for (uint8_t sent = 0; sent < 4; ++sent) {
        if (!publishDueSnapshot(nowMs)) break;
    }
}

}  // namespace

WateringRecordStore* g_wateringStore = nullptr;
IrrigationAuditStore* g_auditStore = nullptr;
iot_device::RecordStream* g_recordStreams[2] = {};
void bindStores(WateringRecordStore& watering, IrrigationAuditStore& audit) {
    g_wateringStore = &watering;
    g_auditStore = &audit;
    g_recordStreams[0] = &watering.recordStream();
    g_recordStreams[1] = &audit.recordStream();
    g_session.setRecordStreams(g_recordStreams, 2);
}

bool configured() { return g_configured; }

bool configure() {
    if (g_configured) return true;
    if (!buildDeviceId()) {
        g_configureResult = "device_id_unavailable";
        ESP32BASE_LOG_E("irrigation", "platform_device_id_unavailable");
        return false;
    }
    if (IRRIGATION_IOT_MQTT_HOST[0] == '\0' ||
        IRRIGATION_IOT_MQTT_CA_PEM[0] == '\0') {
        g_configureResult = "secrets_empty";
        ESP32BASE_LOG_W("irrigation",
                        "platform_mqtt_unconfigured_local_only");
        return false;
    }

    Esp32BaseMqtt::ConnectionConfig mqttConfig;
    mqttConfig.host = IRRIGATION_IOT_MQTT_HOST;
    mqttConfig.port = IRRIGATION_IOT_MQTT_PORT;
    mqttConfig.security = Esp32BaseMqtt::TLS;
    mqttConfig.username = IRRIGATION_IOT_MQTT_USERNAME;
    mqttConfig.password = IRRIGATION_IOT_MQTT_PASSWORD;
    mqttConfig.tls.caCertificatePem = IRRIGATION_IOT_MQTT_CA_PEM;
    mqttConfig.tls.caCertificateLength =
        std::strlen(IRRIGATION_IOT_MQTT_CA_PEM) + 1;

    if (!g_port.configure(mqttConfig)) {
        g_configureResult = "port_configure_rejected";
        ESP32BASE_LOG_E("irrigation", "platform_mqtt_configure_rejected");
        return false;
    }
    g_configureResult = "ok";
    g_configured = true;
    ESP32BASE_LOG_I("irrigation", "platform_configured device=%s", g_deviceId);
    return true;
}

void begin() {
    if (!g_configured) {
        ESP32BASE_LOG_E("irrigation", "platform_configure_failed reason=%s", g_configureResult);
        return;
    }
    buildBootId();
    if (!g_publisher.begin(ESP32BASE_MQTT_MAX_TOPIC_BYTES,
                           ESP32BASE_MQTT_MAX_PAYLOAD_BYTES)) {
        ESP32BASE_LOG_E("irrigation", "platform_publisher_begin_failed");
    }
    if (!g_publishPolicy.begin()) {
        ESP32BASE_LOG_E("irrigation", "platform_publish_policy_begin_failed");
    }
}

void poll() {
    if (!g_configured) return;
    // 诊断窗口的现有采样点：每轮主循环推进一次，不新增定时器。
    // 必须在 read->state->commit 之前，保证三者同轮连续、中间无 observe。
    {
        const bool connected = Esp32BaseWiFi::isConnected();
        g_diagnostics.observe(g_bootId, static_cast<uint32_t>(ESP.getFreeHeap()),
                              connected, connected ? Esp32BaseWiFi::rssi() : 0);
    }
    g_inbox.begin();
    g_port.poll();
    g_inbox.poll(millis());
    progressPoll();

    iot_device::RecordStream::PublishFact publish = nullptr;
    void* publishCtx = nullptr;
    if (g_session.ready() && !g_stopping) {
        publish = &ModelPublisher::publishRecord;
        publishCtx = &g_publisher;
    }
    // Each store owns its SDK RecordStream and its single local durable store.
    if (g_wateringStore) g_wateringStore->poll(publish, publishCtx);
    if (g_auditStore) g_auditStore->poll(publish, publishCtx);

    const uint32_t now = millis();
    if (!g_session.ready() || g_stopping) {
        // Drop the policy when offline so a reconnect re-marks all snapshots dirty.
        if (g_policyConnected) {
            g_publishPolicy.disconnected();
            g_policyConnected = false;
        }
        return;
    }

    if (!g_policyConnected) {
        g_publishPolicy.connected(now);  // marks every state/parameter snapshot dirty
        g_policyConnected = true;
    }
    publishStates(now);
}

}  // namespace IrrigationPlatform
