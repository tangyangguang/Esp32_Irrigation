#include "web/IrrigationWeb.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>

#include "domain/BusinessEventLog.h"
#include "domain/FlowCalibration.h"
#include "domain/FlowMeter.h"
#include "domain/ZoneManager.h"
#include "storage/FlowConfigStore.h"
#include "storage/SystemConfigStore.h"
#include "storage/ZoneConfigStore.h"

namespace {

void writeUInt(uint32_t value) {
    char text[16];
    snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    Esp32BaseWeb::sendChunk(text);
}

void writeInt(int32_t value) {
    char text[16];
    snprintf(text, sizeof(text), "%ld", static_cast<long>(value));
    Esp32BaseWeb::sendChunk(text);
}

void writeBool(bool value) {
    Esp32BaseWeb::sendChunk(value ? "true" : "false");
}

void sendError(int code, const char* error) {
    Esp32BaseWeb::beginJson(code);
    Esp32BaseWeb::sendChunk("{\"ok\":false,\"error\":\"");
    Esp32BaseWeb::writeJsonEscaped(error ? error : "error");
    Esp32BaseWeb::sendChunk("\"}");
    Esp32BaseWeb::endJson();
}

void sendOk() {
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

bool getParamText(const char* name, char* out, size_t len) {
    return out && len > 0 && Esp32BaseWeb::getParam(name, out, len);
}

bool readU32(const char* name, uint32_t* out) {
    char text[24];
    if (!out || !getParamText(name, text, sizeof(text)) || text[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = strtoul(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool readI32(const char* name, int32_t* out) {
    char text[24];
    if (!out || !getParamText(name, text, sizeof(text)) || text[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const long value = strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    *out = static_cast<int32_t>(value);
    return true;
}

bool readU8(const char* name, uint8_t* out) {
    uint32_t value = 0;
    if (!out || !readU32(name, &value) || value > 255) {
        return false;
    }
    *out = static_cast<uint8_t>(value);
    return true;
}

bool readU16(const char* name, uint16_t* out) {
    uint32_t value = 0;
    if (!out || !readU32(name, &value) || value > 65535UL) {
        return false;
    }
    *out = static_cast<uint16_t>(value);
    return true;
}

bool readCheckbox(const char* name) {
    char value[8];
    if (!getParamText(name, value, sizeof(value))) {
        return false;
    }
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
}

bool readZoneId(uint8_t* zoneId) {
    return readU8("zoneId", zoneId) && Irrigation::validZoneId(*zoneId);
}

bool readFlowId(uint8_t* flowId) {
    return readU8("flowId", flowId) && *flowId >= 1 && *flowId <= Irrigation::MaxFlowMeters;
}

bool flowHasEnabledZones(uint8_t flowId) {
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneConfigStore::get(zoneId);
        if (zone.enabled && zone.flowId == flowId) {
            return true;
        }
    }
    return false;
}

void writeBaselineJson(const Irrigation::ZoneFlowBaselineProfile& baseline) {
    Esp32BaseWeb::sendChunk("{\"source\":");
    writeUInt(static_cast<uint8_t>(baseline.source));
    Esp32BaseWeb::sendChunk(",\"learnedFlowMlPerMin\":");
    writeUInt(baseline.learnedFlowMlPerMin);
    Esp32BaseWeb::sendChunk(",\"lowFlowPermille\":");
    writeUInt(baseline.lowFlowPermille);
    Esp32BaseWeb::sendChunk(",\"highFlowPermille\":");
    writeUInt(baseline.highFlowPermille);
    Esp32BaseWeb::sendChunk(",\"flowFaultConfirmSec\":");
    writeUInt(baseline.flowFaultConfirmSec);
    Esp32BaseWeb::sendChunk(",\"lowFlowAction\":");
    writeUInt(static_cast<uint8_t>(baseline.lowFlowAction));
    Esp32BaseWeb::sendChunk(",\"highFlowAction\":");
    writeUInt(static_cast<uint8_t>(baseline.highFlowAction));
    Esp32BaseWeb::sendChunk(",\"noPulseTimeoutSec\":");
    writeUInt(baseline.noPulseTimeoutSec);
    Esp32BaseWeb::sendChunk("}");
}

void writeCalibrationJson(const Irrigation::FlowMeterCalibrationProfile& profile) {
    Esp32BaseWeb::sendChunk("{\"source\":");
    writeUInt(static_cast<uint8_t>(profile.source));
    Esp32BaseWeb::sendChunk(",\"kUlPerMinPerHz\":");
    writeInt(profile.kUlPerMinPerHz);
    Esp32BaseWeb::sendChunk(",\"offsetMilliHz\":");
    writeInt(profile.offsetMilliHz);
    Esp32BaseWeb::sendChunk(",\"warningFreqMilliHz\":");
    writeUInt(profile.warningFreqMilliHz);
    Esp32BaseWeb::sendChunk(",\"minValidFreqMilliHz\":");
    writeUInt(profile.minValidFreqMilliHz);
    Esp32BaseWeb::sendChunk(",\"maxValidFreqMilliHz\":");
    writeUInt(profile.maxValidFreqMilliHz);
    Esp32BaseWeb::sendChunk(",\"pressurizeSec\":");
    writeUInt(profile.pressurizeSec);
    Esp32BaseWeb::sendChunk(",\"sampleWindowSec\":");
    writeUInt(profile.sampleWindowSec);
    Esp32BaseWeb::sendChunk("}");
}

void writeZoneConfigJson(const Irrigation::ZoneConfig& config) {
    Esp32BaseWeb::sendChunk("{\"zoneId\":");
    writeUInt(config.zoneId);
    Esp32BaseWeb::sendChunk(",\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(config.name);
    Esp32BaseWeb::sendChunk("\",\"valvePin\":");
    writeUInt(config.valvePin);
    Esp32BaseWeb::sendChunk(",\"flowId\":");
    writeUInt(config.flowId);
    Esp32BaseWeb::sendChunk(",\"enabled\":");
    writeBool(config.enabled);
    Esp32BaseWeb::sendChunk(",\"hasLearnedBaseline\":");
    writeBool(config.hasLearnedBaseline);
    Esp32BaseWeb::sendChunk(",\"activeBaseline\":");
    writeBaselineJson(config.activeBaseline);
    Esp32BaseWeb::sendChunk("}");
}

void writeFlowConfigJson(const Irrigation::FlowMeterConfig& config) {
    Esp32BaseWeb::sendChunk("{\"flowId\":");
    writeUInt(config.flowId);
    Esp32BaseWeb::sendChunk(",\"pulsePin\":");
    writeUInt(config.pulsePin);
    Esp32BaseWeb::sendChunk(",\"enabled\":");
    writeBool(config.enabled);
    Esp32BaseWeb::sendChunk(",\"activeCalibration\":");
    writeCalibrationJson(config.activeCalibration);
    Esp32BaseWeb::sendChunk(",\"hasPendingCalibration\":");
    writeBool(config.hasPendingCalibration);
    Esp32BaseWeb::sendChunk(",\"hasRollbackCalibration\":");
    writeBool(config.hasRollbackCalibration);
    Esp32BaseWeb::sendChunk("}");
}

void writeStatusJson(const Irrigation::ZoneStatus& status) {
    Esp32BaseWeb::sendChunk("{\"zoneId\":");
    writeUInt(status.zoneId);
    Esp32BaseWeb::sendChunk(",\"state\":\"");
    Esp32BaseWeb::writeJsonEscaped(Irrigation::zoneStateName(status.state));
    Esp32BaseWeb::sendChunk("\",\"enabled\":");
    writeBool(status.enabled);
    Esp32BaseWeb::sendChunk(",\"busy\":");
    writeBool(status.busy);
    const char* reason = ZoneManager::blockedReason(status.zoneId);
    Esp32BaseWeb::sendChunk(",\"canStart\":");
    writeBool(reason && strcmp(reason, "none") == 0);
    Esp32BaseWeb::sendChunk(",\"blockedReason\":\"");
    Esp32BaseWeb::writeJsonEscaped(reason ? reason : "unknown");
    Esp32BaseWeb::sendChunk("\"");
    Esp32BaseWeb::sendChunk(",\"errorActive\":");
    writeBool(status.errorActive);
    Esp32BaseWeb::sendChunk(",\"errorCode\":");
    writeUInt(static_cast<uint8_t>(status.errorCode));
    Esp32BaseWeb::sendChunk(",\"targetSec\":");
    writeUInt(status.targetSec);
    Esp32BaseWeb::sendChunk(",\"elapsedSec\":");
    writeUInt(status.elapsedSec);
    Esp32BaseWeb::sendChunk(",\"remainingSec\":");
    writeUInt(status.remainingSec);
    Esp32BaseWeb::sendChunk(",\"pulses\":");
    writeUInt(status.pulses);
    Esp32BaseWeb::sendChunk(",\"estimatedMilliliters\":");
    writeUInt(status.estimatedMilliliters);
    Esp32BaseWeb::sendChunk(",\"flowMlPerMin\":");
    writeUInt(status.flowMlPerMin);
    Esp32BaseWeb::sendChunk(",\"flowRateReady\":");
    writeBool(status.flowRateReady);
    Esp32BaseWeb::sendChunk("}");
}

void handleIndexPage() {
    Esp32BaseWeb::sendHeader("灌溉控制");
    Esp32BaseWeb::sendPageTitle("ESP32 灌溉控制", "2 个流量计，最多 6 路水路");
    Esp32BaseWeb::beginPanel("水路状态");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table><thead><tr><th>水路</th><th>状态</th><th>Flow</th><th>流量</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& config = ZoneConfigStore::get(zoneId);
        if (!config.enabled) {
            continue;
        }
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(config.name);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(Irrigation::zoneStateName(status.state));
        Esp32BaseWeb::sendChunk("</td><td>Flow ");
        writeUInt(config.flowId);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(status.flowMlPerMin);
        Esp32BaseWeb::sendChunk(" ml/min</td><td><form method='post' action='/api/v1/manual/start' onsubmit=\"return confirm('确认启动该水路手动浇水？')\"><input type='hidden' name='zoneId' value='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='durationSec' value='");
        writeUInt(SystemConfigStore::current().manualDefaultDurationSec);
        Esp32BaseWeb::sendChunk("'><input type='submit' value='启动'></form><form method='post' action='/api/v1/manual/stop' onsubmit=\"return confirm('确认停止该水路？')\"><input type='hidden' name='zoneId' value='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'><input type='submit' value='停止'></form></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><form method='post' action='/api/v1/manual/stop-all' onsubmit=\"return confirm('确认停止全部水路？')\"><input type='submit' value='全部停止'></form>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void writeChecked(bool checked) {
    if (checked) {
        Esp32BaseWeb::sendChunk(" checked");
    }
}

void writeNumberInput(const char* name, int32_t value) {
    Esp32BaseWeb::sendChunk("<input name='");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("' type='number' value='");
    writeInt(value);
    Esp32BaseWeb::sendChunk("'>");
}

void handleFlowsPage() {
    Esp32BaseWeb::sendHeader("Flows");
    Esp32BaseWeb::sendPageTitle("Flows", "流量计配置与 K+Offset 校准参数");
    Esp32BaseWeb::beginPanel("Flow 配置");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table><thead><tr><th>Flow</th><th>GPIO</th><th>状态</th><th>Active</th><th>Pending</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        const Irrigation::FlowMeterConfig& flow = FlowConfigStore::get(flowId);
        const Irrigation::FlowMeterCalibrationProfile& active = flow.activeCalibration;
        Esp32BaseWeb::sendChunk("<tr><td>Flow ");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("</td><td>GPIO ");
        writeUInt(flow.pulsePin);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(flow.enabled ? "enabled" : "disabled");
        Esp32BaseWeb::sendChunk("</td><td>K=");
        writeInt(active.kUlPerMinPerHz);
        Esp32BaseWeb::sendChunk(" offset=");
        writeInt(active.offsetMilliHz);
        Esp32BaseWeb::sendChunk("</td><td>");
        if (flow.hasPendingCalibration) {
            Esp32BaseWeb::sendChunk("K=");
            writeInt(flow.pendingCalibration.kUlPerMinPerHz);
            Esp32BaseWeb::sendChunk(" offset=");
            writeInt(flow.pendingCalibration.offsetMilliHz);
        } else {
            Esp32BaseWeb::sendChunk("-");
        }
        Esp32BaseWeb::sendChunk("</td><td><form method='post' action='/api/v1/flows/config' onsubmit=\"return confirm('确认保存 Flow 配置？')\"><input type='hidden' name='flowId' value='");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='enabledPresent' value='1'><label><input type='checkbox' name='enabled' value='1'");
        writeChecked(flow.enabled);
        Esp32BaseWeb::sendChunk(">启用</label><br>K ");
        writeNumberInput("kUlPerMinPerHz", active.kUlPerMinPerHz);
        Esp32BaseWeb::sendChunk("<br>Offset ");
        writeNumberInput("offsetMilliHz", active.offsetMilliHz);
        Esp32BaseWeb::sendChunk("<br>Min Hz(milli) ");
        writeNumberInput("minValidFreqMilliHz", static_cast<int32_t>(active.minValidFreqMilliHz));
        Esp32BaseWeb::sendChunk("<br><input type='submit' value='保存为 pending'></form><form method='post' action='/api/v1/calibration/pending/apply' onsubmit=\"return confirm('确认应用 pending 校准？')\"><input type='hidden' name='flowId' value='");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("'><input type='submit' value='应用 pending'></form><form method='post' action='/api/v1/calibration/rollback/restore' onsubmit=\"return confirm('确认回滚上一套校准？')\"><input type='hidden' name='flowId' value='");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("'><input type='submit' value='回滚'></form></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleZonesPage() {
    Esp32BaseWeb::sendHeader("Zones");
    Esp32BaseWeb::sendPageTitle("Zones", "6 路水路配置");
    Esp32BaseWeb::beginPanel("Zone 配置");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table><thead><tr><th>Zone</th><th>阀门</th><th>Flow</th><th>状态</th><th>配置</th></tr></thead><tbody>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneConfigStore::get(zoneId);
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</td><td>GPIO ");
        writeUInt(zone.valvePin);
        Esp32BaseWeb::sendChunk("</td><td>Flow ");
        writeUInt(zone.flowId);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(Irrigation::zoneStateName(status.state));
        Esp32BaseWeb::sendChunk("</td><td><form method='post' action='/api/v1/zones/config' onsubmit=\"return confirm('确认保存 Zone 配置？')\"><input type='hidden' name='zoneId' value='");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'><input type='hidden' name='enabledPresent' value='1'>名称 <input name='name' maxlength='31' value='");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("'><br><label><input type='checkbox' name='enabled' value='1'");
        writeChecked(zone.enabled);
        Esp32BaseWeb::sendChunk(">启用</label><br>Flow ");
        writeNumberInput("flowId", zone.flowId);
        Esp32BaseWeb::sendChunk("<br>正常 ml/min ");
        writeNumberInput("learnedFlowMlPerMin", static_cast<int32_t>(zone.activeBaseline.learnedFlowMlPerMin));
        Esp32BaseWeb::sendChunk("<br>低阈值 permille ");
        writeNumberInput("lowFlowPermille", zone.activeBaseline.lowFlowPermille);
        Esp32BaseWeb::sendChunk("<br>高阈值 permille ");
        writeNumberInput("highFlowPermille", zone.activeBaseline.highFlowPermille);
        Esp32BaseWeb::sendChunk("<br>确认秒 ");
        writeNumberInput("flowFaultConfirmSec", zone.activeBaseline.flowFaultConfirmSec);
        Esp32BaseWeb::sendChunk("<br>无脉冲秒 ");
        writeNumberInput("noPulseTimeoutSec", zone.activeBaseline.noPulseTimeoutSec);
        Esp32BaseWeb::sendChunk("<br><input type='submit' value='保存'></form></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleCalibrationPage() {
    Esp32BaseWeb::sendHeader("Calibration");
    Esp32BaseWeb::sendPageTitle("Calibration", "Flow K+Offset 校准");
    Esp32BaseWeb::beginPanel("采样");
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/calibration/start' onsubmit=\"return confirm('确认开始校准采样？')\">Zone ");
    writeNumberInput("zoneId", 1);
    Esp32BaseWeb::sendChunk("<input type='submit' value='开始采样'></form><form method='post' action='/api/v1/calibration/stop' onsubmit=\"return confirm('确认停止采样？')\"><input type='submit' value='停止采样'></form><form method='post' action='/api/v1/calibration/sample' onsubmit=\"return confirm('确认提交实际接水量？')\">实际 ml ");
    writeNumberInput("actualMl", 1000);
    Esp32BaseWeb::sendChunk("<input type='submit' value='提交样本'></form><form method='post' action='/api/v1/calibration/pending/save' onsubmit=\"return confirm('确认保存推荐值为 pending？')\"><input type='submit' value='保存推荐 pending'></form>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleSettingsPage() {
    const Irrigation::SystemConfig& config = SystemConfigStore::current();
    Esp32BaseWeb::sendHeader("Settings");
    Esp32BaseWeb::sendPageTitle("Settings", "灌溉系统参数");
    Esp32BaseWeb::beginPanel("系统配置");
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/v1/config' onsubmit=\"return confirm('确认保存系统配置？')\">最长秒 ");
    writeNumberInput("maxWateringDurationSec", static_cast<int32_t>(config.maxWateringDurationSec));
    Esp32BaseWeb::sendChunk("<br>手动默认秒 ");
    writeNumberInput("manualDefaultDurationSec", static_cast<int32_t>(config.manualDefaultDurationSec));
    Esp32BaseWeb::sendChunk("<br>计划宽限秒 ");
    writeNumberInput("scheduleGraceSec", config.scheduleGraceSec);
    Esp32BaseWeb::sendChunk("<br>排队最大秒 ");
    writeNumberInput("queuedPlanMaxDelaySec", config.queuedPlanMaxDelaySec);
    Esp32BaseWeb::sendChunk("<br>待机漏水窗口秒 ");
    writeNumberInput("idleLeakWindowSec", config.idleLeakWindowSec);
    Esp32BaseWeb::sendChunk("<br>待机漏水脉冲 ");
    writeNumberInput("idleLeakPulseThreshold", config.idleLeakPulseThreshold);
    Esp32BaseWeb::sendChunk("<br><input type='submit' value='保存'></form>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleStatusApi() {
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"zones\":[");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        if (zoneId > 1) {
            Esp32BaseWeb::sendChunk(",");
        }
        writeStatusJson(ZoneManager::status(zoneId));
    }
    Esp32BaseWeb::sendChunk("],\"zoneConfigs\":[");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        if (zoneId > 1) {
            Esp32BaseWeb::sendChunk(",");
        }
        writeZoneConfigJson(ZoneConfigStore::get(zoneId));
    }
    Esp32BaseWeb::sendChunk("],\"flowConfigs\":[");
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        if (flowId > 1) {
            Esp32BaseWeb::sendChunk(",");
        }
        writeFlowConfigJson(FlowConfigStore::get(flowId));
    }
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endJson();
}

void handleManualStartApi() {
    uint8_t zoneId = 0;
    uint32_t durationSec = 0;
    if (!readZoneId(&zoneId) || !readU32("durationSec", &durationSec)) {
        sendError(400, "invalid_manual_start");
        return;
    }
    if (!ZoneManager::startManual(zoneId, durationSec, Irrigation::StartSource::WEB_PAGE)) {
        sendError(409, "manual_start_rejected");
        return;
    }
    sendOk();
}

void handleManualStopApi() {
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone_id");
        return;
    }
    if (!ZoneManager::stopZone(zoneId, Irrigation::StopSource::WEB_PAGE, Irrigation::TaskResult::USER_STOPPED)) {
        sendError(409, "manual_stop_rejected");
        return;
    }
    sendOk();
}

void handleStopAllApi() {
    const uint8_t stopped = ZoneManager::stopAll(Irrigation::StopSource::WEB_PAGE, Irrigation::TaskResult::USER_STOPPED);
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"stopped\":");
    writeUInt(stopped);
    Esp32BaseWeb::sendChunk("}");
    Esp32BaseWeb::endJson();
}

void handleZoneConfigApi() {
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone_id");
        return;
    }
    if (ZoneManager::isZoneBusy(zoneId)) {
        sendError(409, "zone_running");
        return;
    }
    Irrigation::ZoneConfig config = ZoneConfigStore::get(zoneId);
    if (Esp32BaseWeb::hasParam("name")) {
        char name[Irrigation::NameMaxBytes];
        if (!getParamText("name", name, sizeof(name))) {
            sendError(400, "invalid_name");
            return;
        }
        strlcpy(config.name, name, sizeof(config.name));
    }
    if (Esp32BaseWeb::hasParam("enabledPresent") || Esp32BaseWeb::hasParam("enabled")) {
        config.enabled = readCheckbox("enabled");
    }
    if (Esp32BaseWeb::hasParam("flowId") && !readU8("flowId", &config.flowId)) {
        sendError(400, "invalid_flow_id");
        return;
    }
    if (Esp32BaseWeb::hasParam("learnedFlowMlPerMin") && !readU32("learnedFlowMlPerMin", &config.activeBaseline.learnedFlowMlPerMin)) {
        sendError(400, "invalid_learned_flow");
        return;
    }
    if (Esp32BaseWeb::hasParam("lowFlowPermille") && !readU16("lowFlowPermille", &config.activeBaseline.lowFlowPermille)) {
        sendError(400, "invalid_low_flow");
        return;
    }
    if (Esp32BaseWeb::hasParam("highFlowPermille") && !readU16("highFlowPermille", &config.activeBaseline.highFlowPermille)) {
        sendError(400, "invalid_high_flow");
        return;
    }
    if (Esp32BaseWeb::hasParam("flowFaultConfirmSec") && !readU16("flowFaultConfirmSec", &config.activeBaseline.flowFaultConfirmSec)) {
        sendError(400, "invalid_fault_confirm");
        return;
    }
    if (Esp32BaseWeb::hasParam("noPulseTimeoutSec") && !readU16("noPulseTimeoutSec", &config.activeBaseline.noPulseTimeoutSec)) {
        sendError(400, "invalid_no_pulse_timeout");
        return;
    }
    if (config.enabled && !FlowConfigStore::get(config.flowId).enabled) {
        sendError(409, "flow_disabled");
        return;
    }
    if (!ZoneConfigStore::set(zoneId, config)) {
        sendError(400, "zone_config_rejected");
        return;
    }
    (void)ZoneManager::reloadZone(zoneId);
    sendOk();
}

void handleFlowConfigApi() {
    uint8_t flowId = 0;
    if (!readFlowId(&flowId)) {
        sendError(400, "invalid_flow_id");
        return;
    }
    Irrigation::FlowMeterConfig config = FlowConfigStore::get(flowId);
    const bool enabledSubmitted = Esp32BaseWeb::hasParam("enabledPresent") || Esp32BaseWeb::hasParam("enabled");
    bool requestedEnabled = config.enabled;
    if (enabledSubmitted) {
        requestedEnabled = readCheckbox("enabled");
        config.enabled = requestedEnabled;
        if (!requestedEnabled && flowHasEnabledZones(flowId)) {
            sendError(409, "flow_has_enabled_zones");
            return;
        }
    }
    bool calibrationChanged = false;
    Irrigation::FlowMeterCalibrationProfile pending = config.hasPendingCalibration
        ? config.pendingCalibration
        : config.activeCalibration;
    if (Esp32BaseWeb::hasParam("kUlPerMinPerHz") && !readI32("kUlPerMinPerHz", &config.activeCalibration.kUlPerMinPerHz)) {
        sendError(400, "invalid_k");
        return;
    }
    if (Esp32BaseWeb::hasParam("offsetMilliHz") && !readI32("offsetMilliHz", &config.activeCalibration.offsetMilliHz)) {
        sendError(400, "invalid_offset");
        return;
    }
    if (Esp32BaseWeb::hasParam("warningFreqMilliHz") && !readU32("warningFreqMilliHz", &config.activeCalibration.warningFreqMilliHz)) {
        sendError(400, "invalid_warning_freq");
        return;
    }
    if (Esp32BaseWeb::hasParam("minValidFreqMilliHz") && !readU32("minValidFreqMilliHz", &config.activeCalibration.minValidFreqMilliHz)) {
        sendError(400, "invalid_min_freq");
        return;
    }
    if (Esp32BaseWeb::hasParam("maxValidFreqMilliHz") && !readU32("maxValidFreqMilliHz", &config.activeCalibration.maxValidFreqMilliHz)) {
        sendError(400, "invalid_max_freq");
        return;
    }
    if (Esp32BaseWeb::hasParam("pressurizeSec") && !readU16("pressurizeSec", &config.activeCalibration.pressurizeSec)) {
        sendError(400, "invalid_pressurize");
        return;
    }
    if (Esp32BaseWeb::hasParam("sampleWindowSec") && !readU16("sampleWindowSec", &config.activeCalibration.sampleWindowSec)) {
        sendError(400, "invalid_sample_window");
        return;
    }
    if (Esp32BaseWeb::hasParam("kUlPerMinPerHz")) {
        pending.kUlPerMinPerHz = config.activeCalibration.kUlPerMinPerHz;
        calibrationChanged = true;
    }
    if (Esp32BaseWeb::hasParam("offsetMilliHz")) {
        pending.offsetMilliHz = config.activeCalibration.offsetMilliHz;
        calibrationChanged = true;
    }
    if (Esp32BaseWeb::hasParam("warningFreqMilliHz")) {
        pending.warningFreqMilliHz = config.activeCalibration.warningFreqMilliHz;
        calibrationChanged = true;
    }
    if (Esp32BaseWeb::hasParam("minValidFreqMilliHz")) {
        pending.minValidFreqMilliHz = config.activeCalibration.minValidFreqMilliHz;
        calibrationChanged = true;
    }
    if (Esp32BaseWeb::hasParam("maxValidFreqMilliHz")) {
        pending.maxValidFreqMilliHz = config.activeCalibration.maxValidFreqMilliHz;
        calibrationChanged = true;
    }
    if (Esp32BaseWeb::hasParam("pressurizeSec")) {
        pending.pressurizeSec = config.activeCalibration.pressurizeSec;
        calibrationChanged = true;
    }
    if (Esp32BaseWeb::hasParam("sampleWindowSec")) {
        pending.sampleWindowSec = config.activeCalibration.sampleWindowSec;
        calibrationChanged = true;
    }
    if (calibrationChanged) {
        pending.source = Irrigation::ParameterSource::MANUAL;
        if (!FlowConfigStore::savePendingCalibration(flowId, pending)) {
            sendError(400, "pending_calibration_rejected");
            return;
        }
        config = FlowConfigStore::get(flowId);
        if (enabledSubmitted) {
            config.enabled = requestedEnabled;
        }
    }
    if (!FlowConfigStore::set(flowId, config)) {
        sendError(400, "flow_config_rejected");
        return;
    }
    sendOk();
}

void handleCalibrationStartApi() {
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone_id");
        return;
    }
    if (!FlowCalibration::start(zoneId, SystemConfigStore::current())) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    sendOk();
}

void handleCalibrationStopApi() {
    if (!FlowCalibration::stop()) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    sendOk();
}

void handleCalibrationSampleApi() {
    uint32_t actualMl = 0;
    if (!readU32("actualMl", &actualMl) || actualMl == 0) {
        sendError(400, "invalid_actual_ml");
        return;
    }
    if (!FlowCalibration::submitActualMilliliters(actualMl)) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    sendOk();
}

void handleCalibrationSavePendingApi() {
    if (!FlowCalibration::saveCandidate()) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    sendOk();
}

void handleCalibrationApplyApi() {
    uint8_t flowId = 0;
    if (!readFlowId(&flowId)) {
        sendError(400, "invalid_flow_id");
        return;
    }
    Irrigation::FlowMeterCalibrationProfile oldProfile = {};
    Irrigation::FlowMeterCalibrationProfile newProfile = {};
    if (!FlowConfigStore::applyPendingCalibration(flowId, &oldProfile, &newProfile)) {
        sendError(409, "apply_pending_failed");
        return;
    }
    BusinessEventLog::appendFlowCalibrationApplied(flowId, oldProfile, newProfile, "web");
    sendOk();
}

void handleCalibrationRestoreApi() {
    uint8_t flowId = 0;
    if (!readFlowId(&flowId)) {
        sendError(400, "invalid_flow_id");
        return;
    }
    Irrigation::FlowMeterCalibrationProfile oldProfile = {};
    Irrigation::FlowMeterCalibrationProfile restoredProfile = {};
    if (!FlowConfigStore::restoreRollbackCalibration(flowId, &oldProfile, &restoredProfile)) {
        sendError(409, "restore_rollback_failed");
        return;
    }
    BusinessEventLog::appendFlowCalibrationRestored(flowId, oldProfile, restoredProfile, "web");
    sendOk();
}

void handleConfigApi() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        const Irrigation::SystemConfig& config = SystemConfigStore::current();
        Esp32BaseWeb::beginJson(200);
        Esp32BaseWeb::sendChunk("{\"ok\":true,\"maxWateringDurationSec\":");
        writeUInt(config.maxWateringDurationSec);
        Esp32BaseWeb::sendChunk(",\"scheduleGraceSec\":");
        writeUInt(config.scheduleGraceSec);
        Esp32BaseWeb::sendChunk(",\"queuedPlanMaxDelaySec\":");
        writeUInt(config.queuedPlanMaxDelaySec);
        Esp32BaseWeb::sendChunk(",\"manualDefaultDurationSec\":");
        writeUInt(config.manualDefaultDurationSec);
        Esp32BaseWeb::sendChunk(",\"idleLeakWindowSec\":");
        writeUInt(config.idleLeakWindowSec);
        Esp32BaseWeb::sendChunk(",\"idleLeakPulseThreshold\":");
        writeUInt(config.idleLeakPulseThreshold);
        Esp32BaseWeb::sendChunk("}");
        Esp32BaseWeb::endJson();
        return;
    }
    Irrigation::SystemConfig config = SystemConfigStore::current();
    if (Esp32BaseWeb::hasParam("maxWateringDurationSec") && !readU32("maxWateringDurationSec", &config.maxWateringDurationSec)) {
        sendError(400, "invalid_max_duration");
        return;
    }
    if (Esp32BaseWeb::hasParam("scheduleGraceSec") && !readU16("scheduleGraceSec", &config.scheduleGraceSec)) {
        sendError(400, "invalid_schedule_grace");
        return;
    }
    if (Esp32BaseWeb::hasParam("queuedPlanMaxDelaySec") && !readU16("queuedPlanMaxDelaySec", &config.queuedPlanMaxDelaySec)) {
        sendError(400, "invalid_queue_delay");
        return;
    }
    if (Esp32BaseWeb::hasParam("manualDefaultDurationSec") && !readU32("manualDefaultDurationSec", &config.manualDefaultDurationSec)) {
        sendError(400, "invalid_manual_default");
        return;
    }
    if (Esp32BaseWeb::hasParam("idleLeakWindowSec") && !readU16("idleLeakWindowSec", &config.idleLeakWindowSec)) {
        sendError(400, "invalid_idle_leak_window");
        return;
    }
    if (Esp32BaseWeb::hasParam("idleLeakPulseThreshold") && !readU16("idleLeakPulseThreshold", &config.idleLeakPulseThreshold)) {
        sendError(400, "invalid_idle_leak_pulse");
        return;
    }
    if (!SystemConfigStore::set(config)) {
        sendError(400, "system_config_rejected");
        return;
    }
    sendOk();
}

}

namespace IrrigationWeb {

void begin() {
    (void)Esp32BaseWeb::addNavItem("/irrigation", "灌溉");
    (void)Esp32BaseWeb::addNavItem("/irrigation/zones", "水路");
    (void)Esp32BaseWeb::addNavItem("/irrigation/flows", "流量计");
    (void)Esp32BaseWeb::addNavItem("/irrigation/calibration", "校准");
    (void)Esp32BaseWeb::addNavItem("/irrigation/settings", "灌溉设置");
    const bool ok =
        Esp32BaseWeb::addRoute("/irrigation", Esp32BaseWeb::METHOD_GET, handleIndexPage) &&
        Esp32BaseWeb::addRoute("/irrigation/zones", Esp32BaseWeb::METHOD_GET, handleZonesPage) &&
        Esp32BaseWeb::addRoute("/irrigation/flows", Esp32BaseWeb::METHOD_GET, handleFlowsPage) &&
        Esp32BaseWeb::addRoute("/irrigation/calibration", Esp32BaseWeb::METHOD_GET, handleCalibrationPage) &&
        Esp32BaseWeb::addRoute("/irrigation/settings", Esp32BaseWeb::METHOD_GET, handleSettingsPage) &&
        Esp32BaseWeb::addRoute("/index", Esp32BaseWeb::METHOD_GET, handleIndexPage) &&
        Esp32BaseWeb::addRoute("/api/v1/status", Esp32BaseWeb::METHOD_GET, handleStatusApi) &&
        Esp32BaseWeb::addRoute("/api/v1/manual/start", Esp32BaseWeb::METHOD_POST, handleManualStartApi) &&
        Esp32BaseWeb::addRoute("/api/v1/manual/stop", Esp32BaseWeb::METHOD_POST, handleManualStopApi) &&
        Esp32BaseWeb::addRoute("/api/v1/manual/stop-all", Esp32BaseWeb::METHOD_POST, handleStopAllApi) &&
        Esp32BaseWeb::addRoute("/api/v1/zones/config", Esp32BaseWeb::METHOD_POST, handleZoneConfigApi) &&
        Esp32BaseWeb::addRoute("/api/v1/flows/config", Esp32BaseWeb::METHOD_POST, handleFlowConfigApi) &&
        Esp32BaseWeb::addRoute("/api/v1/calibration/start", Esp32BaseWeb::METHOD_POST, handleCalibrationStartApi) &&
        Esp32BaseWeb::addRoute("/api/v1/calibration/stop", Esp32BaseWeb::METHOD_POST, handleCalibrationStopApi) &&
        Esp32BaseWeb::addRoute("/api/v1/calibration/sample", Esp32BaseWeb::METHOD_POST, handleCalibrationSampleApi) &&
        Esp32BaseWeb::addRoute("/api/v1/calibration/pending/save", Esp32BaseWeb::METHOD_POST, handleCalibrationSavePendingApi) &&
        Esp32BaseWeb::addRoute("/api/v1/calibration/pending/apply", Esp32BaseWeb::METHOD_POST, handleCalibrationApplyApi) &&
        Esp32BaseWeb::addRoute("/api/v1/calibration/rollback/restore", Esp32BaseWeb::METHOD_POST, handleCalibrationRestoreApi) &&
        Esp32BaseWeb::addRoute("/api/v1/config", Esp32BaseWeb::METHOD_GET, handleConfigApi) &&
        Esp32BaseWeb::addRoute("/api/v1/config", Esp32BaseWeb::METHOD_POST, handleConfigApi);
    if (!ok) {
        BusinessEventLog::appendWebRouteRegistrationFailed(1);
    }
}

}
