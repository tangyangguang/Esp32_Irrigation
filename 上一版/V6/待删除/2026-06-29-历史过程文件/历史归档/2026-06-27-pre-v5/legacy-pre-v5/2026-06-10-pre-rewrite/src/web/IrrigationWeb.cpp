#include "web/IrrigationWeb.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>
#include <string.h>

#include "domain/BusinessEventLog.h"
#include "domain/FlowCalibration.h"
#include "domain/FlowMeter.h"
#include "domain/ZoneManager.h"
#include "storage/FlowAlertStore.h"
#include "storage/FlowConfigStore.h"
#include "storage/PlanStore.h"
#include "storage/RecordStore.h"
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
    Esp32BaseWeb::beginResponse(code, "application/json", nullptr);
    Esp32BaseWeb::sendChunk("{\"ok\":false,\"error\":\"");
    Esp32BaseWeb::writeJsonEscaped(error ? error : "error");
    Esp32BaseWeb::sendChunk("\"}");
    Esp32BaseWeb::endResponse();
}

void sendOk() {
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

bool getParamText(const char* name, char* out, size_t len) {
    return out && len > 0 && Esp32BaseWeb::getParam(name, out, len);
}

bool redirectRequested(char* out, size_t len) {
    if (!getParamText("redirect", out, len)) {
        return false;
    }
    return strncmp(out, "/irrigation", 11) == 0;
}

void sendOkOrRedirect() {
    char redirect[80];
    if (redirectRequested(redirect, sizeof(redirect))) {
        Esp32BaseWeb::redirectSeeOther(redirect);
        return;
    }
    sendOk();
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
    Esp32BaseWeb::sendChunk(",\"hasPendingBaseline\":");
    writeBool(config.hasPendingBaseline);
    if (config.hasPendingBaseline) {
        Esp32BaseWeb::sendChunk(",\"pendingBaseline\":");
        writeBaselineJson(config.pendingBaseline);
    }
    Esp32BaseWeb::sendChunk(",\"hasRollbackBaseline\":");
    writeBool(config.hasRollbackBaseline);
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

void writePlanJson(const Irrigation::PlanDefinition& plan) {
    Esp32BaseWeb::sendChunk("{\"exists\":");
    writeBool(plan.exists);
    Esp32BaseWeb::sendChunk(",\"planId\":");
    writeUInt(plan.planId);
    Esp32BaseWeb::sendChunk(",\"zoneId\":");
    writeUInt(plan.zoneId);
    Esp32BaseWeb::sendChunk(",\"slotIndex\":");
    writeUInt(plan.slotIndex);
    Esp32BaseWeb::sendChunk(",\"name\":\"");
    Esp32BaseWeb::writeJsonEscaped(plan.name);
    Esp32BaseWeb::sendChunk("\",\"enabled\":");
    writeBool(plan.enabled);
    Esp32BaseWeb::sendChunk(",\"timeHour\":");
    writeUInt(plan.timeHour);
    Esp32BaseWeb::sendChunk(",\"timeMinute\":");
    writeUInt(plan.timeMinute);
    Esp32BaseWeb::sendChunk(",\"durationSec\":");
    writeUInt(plan.durationSec);
    Esp32BaseWeb::sendChunk(",\"cycleDays\":");
    writeUInt(plan.cycleDays);
    Esp32BaseWeb::sendChunk(",\"cycleMask\":");
    writeUInt(plan.cycleMask);
    Esp32BaseWeb::sendChunk(",\"cycleStartYmd\":");
    writeUInt(plan.cycleStartYmd);
    Esp32BaseWeb::sendChunk("}");
}

void writeRecordJson(const RecordStore::WateringRecord& record) {
    Esp32BaseWeb::sendChunk("{\"recordId\":");
    writeUInt(record.recordId);
    Esp32BaseWeb::sendChunk(",\"zoneId\":");
    writeUInt(record.zoneId);
    Esp32BaseWeb::sendChunk(",\"flowId\":");
    writeUInt(record.configSnapshot.flowId);
    Esp32BaseWeb::sendChunk(",\"taskType\":");
    writeUInt(record.taskType);
    Esp32BaseWeb::sendChunk(",\"startSource\":");
    writeUInt(record.startSource);
    Esp32BaseWeb::sendChunk(",\"stopSource\":");
    writeUInt(record.stopSource);
    Esp32BaseWeb::sendChunk(",\"result\":");
    writeUInt(record.result);
    Esp32BaseWeb::sendChunk(",\"planId\":");
    writeUInt(record.planId);
    Esp32BaseWeb::sendChunk(",\"planName\":\"");
    Esp32BaseWeb::writeJsonEscaped(record.planNameSnapshot);
    Esp32BaseWeb::sendChunk("\",\"targetSec\":");
    writeUInt(record.targetSec);
    Esp32BaseWeb::sendChunk(",\"startedEpoch\":");
    writeUInt(record.startedEpoch);
    Esp32BaseWeb::sendChunk(",\"endedEpoch\":");
    writeUInt(record.endedEpoch);
    Esp32BaseWeb::sendChunk(",\"estimatedMilliliters\":");
    writeUInt(record.estimatedMilliliters);
    Esp32BaseWeb::sendChunk(",\"maxFlowMlPerMin\":");
    writeUInt(record.maxFlowMlPerMin);
    Esp32BaseWeb::sendChunk(",\"minFlowMlPerMin\":");
    writeUInt(record.minFlowMlPerMin);
    Esp32BaseWeb::sendChunk("}");
}

void writeEventJson(const Esp32BaseAppEventRecord& event) {
    Esp32BaseWeb::sendChunk("{\"id\":");
    writeUInt(event.id);
    Esp32BaseWeb::sendChunk(",\"level\":");
    writeUInt(event.level);
    Esp32BaseWeb::sendChunk(",\"epochSec\":");
    writeUInt(event.epochSec);
    Esp32BaseWeb::sendChunk(",\"source\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.source);
    Esp32BaseWeb::sendChunk("\",\"type\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.type);
    Esp32BaseWeb::sendChunk("\",\"reason\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.reason);
    Esp32BaseWeb::sendChunk("\",\"object\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.object);
    Esp32BaseWeb::sendChunk("\",\"code\":");
    writeUInt(event.code);
    Esp32BaseWeb::sendChunk(",\"value1\":");
    writeInt(event.value1);
    Esp32BaseWeb::sendChunk(",\"value2\":");
    writeInt(event.value2);
    Esp32BaseWeb::sendChunk(",\"value3\":");
    writeInt(event.value3);
    Esp32BaseWeb::sendChunk(",\"text\":\"");
    Esp32BaseWeb::writeJsonEscaped(event.text);
    Esp32BaseWeb::sendChunk("\"}");
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

void writeHiddenUInt(const char* name, uint32_t value);
void beginPostForm(const char* action, const char* confirmText, const char* redirectPath, bool editForm = false);
void writeSubmit(const char* label, const char* cssClass = nullptr);

void handleIndexPage() {
    Esp32BaseWeb::sendHeader("灌溉控制");
    Esp32BaseWeb::sendPageTitle("ESP32 灌溉控制", "2 个流量计，最多 6 路水路");
    Esp32BaseWeb::beginPanel("水路状态");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>水路</th><th>状态</th><th>Flow</th><th>流量</th><th>操作</th></tr></thead><tbody>");
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
        Esp32BaseWeb::sendChunk(" ml/min</td><td><div class='actions'>");
        beginPostForm("/api/v1/manual/start", "确认启动该水路手动浇水？", "/irrigation");
        writeHiddenUInt("zoneId", zoneId);
        writeHiddenUInt("durationSec", SystemConfigStore::current().manualDefaultDurationSec);
        writeSubmit("启动");
        Esp32BaseWeb::sendChunk("</form>");
        beginPostForm("/api/v1/manual/stop", "确认停止该水路？", "/irrigation");
        writeHiddenUInt("zoneId", zoneId);
        writeSubmit("停止", "secondary");
        Esp32BaseWeb::sendChunk("</form></div></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div><div class='actions'>");
    beginPostForm("/api/v1/manual/stop-all", "确认停止全部水路？", "/irrigation");
    writeSubmit("全部停止", "danger");
    Esp32BaseWeb::sendChunk("</form></div>");
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

void writeHiddenUInt(const char* name, uint32_t value) {
    Esp32BaseWeb::sendChunk("<input type='hidden' name='");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("' value='");
    writeUInt(value);
    Esp32BaseWeb::sendChunk("'>");
}

void writeRedirectInput(const char* path) {
    Esp32BaseWeb::sendChunk("<input type='hidden' name='redirect' value='");
    Esp32BaseWeb::writeHtmlEscaped(path);
    Esp32BaseWeb::sendChunk("'>");
}

void beginPostForm(const char* action, const char* confirmText, const char* redirectPath, bool editForm) {
    Esp32BaseWeb::sendChunk("<form");
    if (editForm) {
        Esp32BaseWeb::sendChunk(" class='editform'");
    }
    Esp32BaseWeb::sendChunk(" method='post' action='");
    Esp32BaseWeb::sendChunk(action);
    Esp32BaseWeb::sendChunk("' onsubmit=\"return confirm('");
    Esp32BaseWeb::sendChunk(confirmText);
    Esp32BaseWeb::sendChunk("')\">");
    if (redirectPath) {
        writeRedirectInput(redirectPath);
    }
}

void writeSubmit(const char* label, const char* cssClass) {
    Esp32BaseWeb::sendChunk("<input type='submit'");
    if (cssClass) {
        Esp32BaseWeb::sendChunk(" class='");
        Esp32BaseWeb::sendChunk(cssClass);
        Esp32BaseWeb::sendChunk("'");
    }
    Esp32BaseWeb::sendChunk(" value='");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk("'>");
}

void writeTextField(const char* label, const char* name, const char* value, const char* cssClass = "med", uint8_t maxLen = Irrigation::NameMaxBytes - 1) {
    Esp32BaseWeb::sendChunk("<label class='field ");
    Esp32BaseWeb::sendChunk(cssClass);
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk("<input name='");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("' maxlength='");
    writeUInt(maxLen);
    Esp32BaseWeb::sendChunk("' value='");
    Esp32BaseWeb::writeHtmlEscaped(value ? value : "");
    Esp32BaseWeb::sendChunk("'></label>");
}

void writeNumberField(const char* label, const char* name, int32_t value, const char* cssClass = "short") {
    Esp32BaseWeb::sendChunk("<label class='field ");
    Esp32BaseWeb::sendChunk(cssClass);
    Esp32BaseWeb::sendChunk("'>");
    Esp32BaseWeb::writeHtmlEscaped(label);
    writeNumberInput(name, value);
    Esp32BaseWeb::sendChunk("</label>");
}

void writeCheckboxField(const char* label, const char* name, bool checked) {
    Esp32BaseWeb::sendChunk("<label class='field short'><input type='checkbox' name='");
    Esp32BaseWeb::sendChunk(name);
    Esp32BaseWeb::sendChunk("' value='1'");
    writeChecked(checked);
    Esp32BaseWeb::sendChunk("> ");
    Esp32BaseWeb::writeHtmlEscaped(label);
    Esp32BaseWeb::sendChunk("</label>");
}

void writeFlowSelectField(uint8_t currentFlowId) {
    Esp32BaseWeb::sendChunk("<label class='field short'>Flow<select name='flowId'>");
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        Esp32BaseWeb::sendChunk("<option value='");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("'");
        if (flowId == currentFlowId) {
            Esp32BaseWeb::sendChunk(" selected");
        }
        Esp32BaseWeb::sendChunk(">Flow ");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("</option>");
    }
    Esp32BaseWeb::sendChunk("</select></label>");
}

void writeTimeText(uint8_t hour, uint8_t minute) {
    writeUInt(hour);
    Esp32BaseWeb::sendChunk(":");
    if (minute < 10) {
        Esp32BaseWeb::sendChunk("0");
    }
    writeUInt(minute);
}

void writePlanEditForm(uint8_t zoneId, const Irrigation::PlanDefinition& plan, uint32_t defaultYmd) {
    Esp32BaseWeb::sendChunk("<section class='panel statuspage'><h2>计划位置</h2><div class='tablewrap'><table class='kv'><tbody><tr><th>Zone</th><td>");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>Slot</th><td>");
    writeUInt(plan.slotIndex + 1);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>当前状态</th><td>");
    Esp32BaseWeb::sendChunk(plan.exists ? (plan.enabled ? "enabled" : "disabled") : "未创建");
    Esp32BaseWeb::sendChunk("</td></tr></tbody></table></div></section>");
    Esp32BaseWeb::sendChunk("<section class='panel formpanel'><h2>编辑计划</h2>");
    beginPostForm("/api/v1/plans/save", "确认保存计划？", "/irrigation/plans", true);
    if (plan.exists) {
        writeHiddenUInt("planId", plan.planId);
    }
    writeHiddenUInt("zoneId", zoneId);
    writeHiddenUInt("slotIndex", plan.slotIndex);
    Esp32BaseWeb::sendChunk("<input type='hidden' name='enabledPresent' value='1'><div class='fieldgrid'>");
    writeTextField("名称", "name", plan.exists ? plan.name : "计划", "long");
    writeCheckboxField("启用", "enabled", plan.exists && plan.enabled);
    writeNumberField("时", "timeHour", plan.exists ? plan.timeHour : 7, "short");
    writeNumberField("分", "timeMinute", plan.exists ? plan.timeMinute : 0, "short");
    writeNumberField("时长秒", "durationSec", plan.exists ? static_cast<int32_t>(plan.durationSec) : 300, "med");
    writeNumberField("循环天", "cycleDays", plan.exists ? plan.cycleDays : 1, "short");
    writeNumberField("循环掩码", "cycleMask", plan.exists ? static_cast<int32_t>(plan.cycleMask) : 1, "med");
    writeNumberField("起始日期", "cycleStartYmd", plan.exists ? static_cast<int32_t>(plan.cycleStartYmd) : static_cast<int32_t>(defaultYmd), "med");
    Esp32BaseWeb::sendChunk("</div><div class='actions'>");
    writeSubmit("保存");
    Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/plans'>取消</a></div></form></section>");
    if (plan.exists) {
        Esp32BaseWeb::sendChunk("<section class='panel dangerpanel'><h2>删除计划</h2><p class='dangertext'>删除后该 Slot 会变回空计划。</p>");
        beginPostForm("/api/v1/plans/delete", "确认删除计划？", "/irrigation/plans");
        writeHiddenUInt("planId", plan.planId);
        Esp32BaseWeb::sendChunk("<div class='actions'>");
        writeSubmit("删除", "danger");
        Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/plans'>取消</a></div></form></section>");
    }
}

void writeFlowEditPanel(uint8_t flowId, const Irrigation::FlowMeterConfig& flow) {
    const Irrigation::FlowMeterCalibrationProfile& active = flow.activeCalibration;
    Esp32BaseWeb::sendChunk("<section class='panel statuspage'><h2>Flow ");
    writeUInt(flowId);
    Esp32BaseWeb::sendChunk(" 当前信息</h2><div class='tablewrap'><table class='kv'><tbody><tr><th>脉冲 GPIO</th><td>");
    writeUInt(flow.pulsePin);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>状态</th><td>");
    Esp32BaseWeb::sendChunk(flow.enabled ? "enabled" : "disabled");
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>Active</th><td>K=");
    writeInt(active.kUlPerMinPerHz);
    Esp32BaseWeb::sendChunk(" ul/min/Hz, offset=");
    writeInt(active.offsetMilliHz);
    Esp32BaseWeb::sendChunk(" milliHz</td></tr><tr><th>Pending</th><td>");
    if (flow.hasPendingCalibration) {
        Esp32BaseWeb::sendChunk("K=");
        writeInt(flow.pendingCalibration.kUlPerMinPerHz);
        Esp32BaseWeb::sendChunk(" ul/min/Hz, offset=");
        writeInt(flow.pendingCalibration.offsetMilliHz);
        Esp32BaseWeb::sendChunk(" milliHz");
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td></tr></tbody></table></div><div class='actions'><a class='btnlink secondary' href='/irrigation/flows'>返回列表</a></div></section>");
    Esp32BaseWeb::sendChunk("<section class='panel formpanel'><h2>计量参数</h2><p class='muted'>K 表示每 1Hz 脉冲频率对应的流量，Offset 表示频率零点偏移；保存后先进入 pending，确认后再应用。</p>");
    beginPostForm("/api/v1/flows/config", "确认保存 Flow 配置？", "/irrigation/flows", true);
    writeHiddenUInt("flowId", flowId);
    Esp32BaseWeb::sendChunk("<input type='hidden' name='enabledPresent' value='1'><div class='fieldgrid'>");
    writeCheckboxField("启用", "enabled", flow.enabled);
    writeNumberField("K", "kUlPerMinPerHz", active.kUlPerMinPerHz, "med");
    writeNumberField("Offset(mHz)", "offsetMilliHz", active.offsetMilliHz, "med");
    writeNumberField("低频提示(mHz)", "warningFreqMilliHz", static_cast<int32_t>(active.warningFreqMilliHz), "med");
    writeNumberField("有效下限(mHz)", "minValidFreqMilliHz", static_cast<int32_t>(active.minValidFreqMilliHz), "med");
    writeNumberField("有效上限(mHz)", "maxValidFreqMilliHz", static_cast<int32_t>(active.maxValidFreqMilliHz), "med");
    writeNumberField("稳压秒", "pressurizeSec", active.pressurizeSec, "short");
    writeNumberField("采样秒", "sampleWindowSec", active.sampleWindowSec, "short");
    Esp32BaseWeb::sendChunk("</div><div class='actions'>");
    writeSubmit("保存为 pending");
    Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/flows'>取消</a></div></form></section>");
    Esp32BaseWeb::sendChunk("<section class='panel actionpanel'><h2>Pending 操作</h2><p class='muted'>只有现场确认 pending 参数正确后，再把它应用为 active 参数；回滚会恢复上一套 active 参数。</p><div class='actions'>");
    beginPostForm("/api/v1/calibration/pending/apply", "确认应用 pending 校准？", "/irrigation/flows");
    writeHiddenUInt("flowId", flowId);
    writeSubmit("应用 pending", "secondary");
    Esp32BaseWeb::sendChunk("</form>");
    beginPostForm("/api/v1/calibration/rollback/restore", "确认回滚上一套校准？", "/irrigation/flows");
    writeHiddenUInt("flowId", flowId);
    writeSubmit("回滚", "secondary");
    Esp32BaseWeb::sendChunk("</form><a class='btnlink secondary' href='/irrigation/flows'>取消</a></div></section>");
}

void writeZoneEditPanel(uint8_t zoneId, const Irrigation::ZoneConfig& zone, const Irrigation::ZoneStatus& status) {
    Esp32BaseWeb::sendChunk("<section class='panel statuspage'><h2>");
    Esp32BaseWeb::writeHtmlEscaped(zone.name);
    Esp32BaseWeb::sendChunk("</h2><div class='tablewrap'><table class='kv'><tbody><tr><th>Zone</th><td>");
    writeUInt(zoneId);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>阀门 GPIO</th><td>");
    writeUInt(zone.valvePin);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>Flow</th><td>Flow ");
    writeUInt(zone.flowId);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>状态</th><td>");
    Esp32BaseWeb::writeHtmlEscaped(Irrigation::zoneStateName(status.state));
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>Active 基线</th><td>");
    writeUInt(zone.activeBaseline.learnedFlowMlPerMin);
    Esp32BaseWeb::sendChunk(" ml/min, low=");
    writeInt(zone.activeBaseline.lowFlowPermille);
    Esp32BaseWeb::sendChunk(" permille, high=");
    writeInt(zone.activeBaseline.highFlowPermille);
    Esp32BaseWeb::sendChunk(" permille</td></tr><tr><th>Pending 基线</th><td>");
    if (zone.hasPendingBaseline) {
        writeUInt(zone.pendingBaseline.learnedFlowMlPerMin);
        Esp32BaseWeb::sendChunk(" ml/min");
    } else {
        Esp32BaseWeb::sendChunk("-");
    }
    Esp32BaseWeb::sendChunk("</td></tr></tbody></table></div><div class='actions'><a class='btnlink secondary' href='/irrigation/zones'>返回列表</a></div></section>");
    Esp32BaseWeb::sendChunk("<section class='panel formpanel'><h2>基础配置</h2><p class='muted'>启用的 Zone 会出现在首页和本地屏幕中；Flow 归属决定同一 Flow 下互斥运行。</p>");
    beginPostForm("/api/v1/zones/config", "确认保存 Zone 配置？", "/irrigation/zones", true);
    writeHiddenUInt("zoneId", zoneId);
    Esp32BaseWeb::sendChunk("<input type='hidden' name='enabledPresent' value='1'><div class='fieldgrid'>");
    writeTextField("名称", "name", zone.name, "long");
    writeCheckboxField("启用", "enabled", zone.enabled);
    writeFlowSelectField(zone.flowId);
    Esp32BaseWeb::sendChunk("</div><div class='actions'>");
    writeSubmit("保存基础配置");
    Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/zones'>取消</a></div></form></section>");
    Esp32BaseWeb::sendChunk("<section class='panel formpanel'><h2>流量基线</h2><p class='muted'>基线用于判断该水路低流量、高流量和无脉冲异常；保存后先进入 pending，确认后再应用。</p>");
    beginPostForm("/api/v1/zones/baseline/pending/save", "确认保存 Zone 基线为 pending？", "/irrigation/zones", true);
    writeHiddenUInt("zoneId", zoneId);
    Esp32BaseWeb::sendChunk("<div class='fieldgrid'>");
    writeNumberField("正常流量(ml/min)", "learnedFlowMlPerMin", static_cast<int32_t>(zone.activeBaseline.learnedFlowMlPerMin), "med");
    writeNumberField("低阈值(permille)", "lowFlowPermille", zone.activeBaseline.lowFlowPermille, "med");
    writeNumberField("高阈值(permille)", "highFlowPermille", zone.activeBaseline.highFlowPermille, "med");
    writeNumberField("异常确认秒", "flowFaultConfirmSec", zone.activeBaseline.flowFaultConfirmSec, "med");
    writeNumberField("无脉冲超时秒", "noPulseTimeoutSec", zone.activeBaseline.noPulseTimeoutSec, "med");
    Esp32BaseWeb::sendChunk("</div><div class='actions'>");
    writeSubmit("保存基线 pending", "secondary");
    Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/zones'>取消</a></div></form></section>");
    Esp32BaseWeb::sendChunk("<section class='panel actionpanel'><h2>Pending 操作</h2><p class='muted'>应用 pending 后才会成为正式基线；回滚会恢复上一套正式基线。</p><div class='actions'>");
    beginPostForm("/api/v1/zones/baseline/pending/apply", "确认应用 pending 基线？", "/irrigation/zones");
    writeHiddenUInt("zoneId", zoneId);
    writeSubmit("应用 pending", "secondary");
    Esp32BaseWeb::sendChunk("</form>");
    beginPostForm("/api/v1/zones/baseline/rollback/restore", "确认回滚上一套基线？", "/irrigation/zones");
    writeHiddenUInt("zoneId", zoneId);
    writeSubmit("回滚", "secondary");
    Esp32BaseWeb::sendChunk("</form><a class='btnlink secondary' href='/irrigation/zones'>取消</a></div></section>");
}

void handleFlowsPage() {
    Esp32BaseWeb::sendHeader("Flows");
    Esp32BaseWeb::sendPageTitle("Flows", "流量计配置与 K+Offset 校准参数");
    uint8_t selectedFlowId = 0;
    const bool editingFlow = readFlowId(&selectedFlowId);
    if (editingFlow) {
        writeFlowEditPanel(selectedFlowId, FlowConfigStore::get(selectedFlowId));
        Esp32BaseWeb::sendFooter();
        return;
    }
    Esp32BaseWeb::beginPanel("Flow 列表");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>Flow</th><th>GPIO</th><th>状态</th><th>Active</th><th>Pending</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        const Irrigation::FlowMeterConfig& flow = FlowConfigStore::get(flowId);
        const Irrigation::FlowMeterCalibrationProfile& active = flow.activeCalibration;
        Esp32BaseWeb::sendChunk("<tr><td>Flow ");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(flow.pulsePin);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(flow.enabled ? "enabled" : "disabled");
        Esp32BaseWeb::sendChunk("</td><td>K=");
        writeInt(active.kUlPerMinPerHz);
        Esp32BaseWeb::sendChunk(", offset=");
        writeInt(active.offsetMilliHz);
        Esp32BaseWeb::sendChunk("</td><td>");
        if (flow.hasPendingCalibration) {
            Esp32BaseWeb::sendChunk("K=");
            writeInt(flow.pendingCalibration.kUlPerMinPerHz);
            Esp32BaseWeb::sendChunk(", offset=");
            writeInt(flow.pendingCalibration.offsetMilliHz);
        } else {
            Esp32BaseWeb::sendChunk("-");
        }
        Esp32BaseWeb::sendChunk("</td><td><a class='btnlink compact' href='/irrigation/flows?flowId=");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk("'>编辑</a></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleZonesPage() {
    Esp32BaseWeb::sendHeader("Zones");
    Esp32BaseWeb::sendPageTitle("Zones", "6 路水路配置");
    uint8_t selectedZoneId = 0;
    const bool editingZone = readZoneId(&selectedZoneId);
    if (editingZone) {
        writeZoneEditPanel(selectedZoneId, ZoneConfigStore::get(selectedZoneId), ZoneManager::status(selectedZoneId));
        Esp32BaseWeb::sendFooter();
        return;
    }
    Esp32BaseWeb::beginPanel("Zone 列表");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>Zone</th><th>阀门</th><th>Flow</th><th>启用</th><th>状态</th><th>基线</th><th>操作</th></tr></thead><tbody>");
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        const Irrigation::ZoneConfig& zone = ZoneConfigStore::get(zoneId);
        const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
        Esp32BaseWeb::sendChunk("<tr><td>");
        Esp32BaseWeb::writeHtmlEscaped(zone.name);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(zone.valvePin);
        Esp32BaseWeb::sendChunk("</td><td>Flow ");
        writeUInt(zone.flowId);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(zone.enabled ? "yes" : "no");
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(Irrigation::zoneStateName(status.state));
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(zone.activeBaseline.learnedFlowMlPerMin);
        Esp32BaseWeb::sendChunk(" ml/min</td><td><a class='btnlink compact' href='/irrigation/zones?zoneId=");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("'>编辑</a></td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleCalibrationPage() {
    char action[12];
    const bool hasAction = getParamText("action", action, sizeof(action));
    Esp32BaseWeb::sendHeader("Calibration");
    Esp32BaseWeb::sendPageTitle("Calibration", "Flow K+Offset 校准");
    if (hasAction && strcmp(action, "start") == 0) {
        Esp32BaseWeb::beginPanel("开始采样");
        beginPostForm("/api/v1/calibration/start", "确认开始校准采样？", "/irrigation/calibration", true);
        Esp32BaseWeb::sendChunk("<div class='fieldgrid'>");
        writeNumberField("Zone", "zoneId", 1, "short");
        Esp32BaseWeb::sendChunk("</div><div class='actions'>");
        writeSubmit("开始采样");
        Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/calibration'>取消</a></div></form>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::sendFooter();
        return;
    } else if (hasAction && strcmp(action, "sample") == 0) {
        Esp32BaseWeb::beginPanel("提交样本");
        beginPostForm("/api/v1/calibration/sample", "确认提交实际接水量？", "/irrigation/calibration", true);
        Esp32BaseWeb::sendChunk("<div class='fieldgrid'>");
        writeNumberField("实际水量(ml)", "actualMl", 1000, "med");
        Esp32BaseWeb::sendChunk("</div><div class='actions'>");
        writeSubmit("提交样本");
        Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/calibration'>取消</a></div></form>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::sendFooter();
        return;
    } else if (hasAction && strcmp(action, "stop") == 0) {
        Esp32BaseWeb::beginPanel("停止采样");
        beginPostForm("/api/v1/calibration/stop", "确认停止采样？", "/irrigation/calibration");
        Esp32BaseWeb::sendChunk("<div class='actions'>");
        writeSubmit("停止采样", "secondary");
        Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/calibration'>取消</a></div></form>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::sendFooter();
        return;
    } else if (hasAction && strcmp(action, "save") == 0) {
        Esp32BaseWeb::beginPanel("保存推荐值");
        beginPostForm("/api/v1/calibration/pending/save", "确认保存推荐值为 pending？", "/irrigation/calibration");
        Esp32BaseWeb::sendChunk("<div class='actions'>");
        writeSubmit("保存推荐 pending", "secondary");
        Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/calibration'>取消</a></div></form>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::sendFooter();
        return;
    }
    Esp32BaseWeb::beginPanel("校准操作");
    Esp32BaseWeb::sendChunk("<div class='actions'><a class='btnlink' href='/irrigation/calibration?action=start'>开始采样</a><a class='btnlink secondary' href='/irrigation/calibration?action=sample'>提交样本</a><a class='btnlink secondary' href='/irrigation/calibration?action=stop'>停止采样</a><a class='btnlink secondary' href='/irrigation/calibration?action=save'>保存推荐 pending</a></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleSettingsPage() {
    const Irrigation::SystemConfig& config = SystemConfigStore::current();
    char editValue[8];
    const bool editing = getParamText("edit", editValue, sizeof(editValue)) && strcmp(editValue, "1") == 0;
    Esp32BaseWeb::sendHeader("Settings");
    Esp32BaseWeb::sendPageTitle("Settings", "灌溉系统参数");
    if (editing) {
        Esp32BaseWeb::beginPanel("编辑系统配置");
        beginPostForm("/api/v1/config", "确认保存系统配置？", "/irrigation/settings", true);
        Esp32BaseWeb::sendChunk("<div class='fieldgrid'>");
        writeNumberField("最长浇水秒", "maxWateringDurationSec", static_cast<int32_t>(config.maxWateringDurationSec), "med");
        writeNumberField("手动默认秒", "manualDefaultDurationSec", static_cast<int32_t>(config.manualDefaultDurationSec), "med");
        writeNumberField("计划宽限秒", "scheduleGraceSec", config.scheduleGraceSec, "med");
        writeNumberField("排队最大秒", "queuedPlanMaxDelaySec", config.queuedPlanMaxDelaySec, "med");
        writeNumberField("待机漏水窗口秒", "idleLeakWindowSec", config.idleLeakWindowSec, "med");
        writeNumberField("待机漏水脉冲", "idleLeakPulseThreshold", config.idleLeakPulseThreshold, "med");
        Esp32BaseWeb::sendChunk("</div><div class='actions'>");
        writeSubmit("保存");
        Esp32BaseWeb::sendChunk("<a class='btnlink secondary' href='/irrigation/settings'>取消</a></div></form>");
        Esp32BaseWeb::endPanel();
        Esp32BaseWeb::sendFooter();
        return;
    }
    Esp32BaseWeb::beginPanel("系统配置");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='kv'><tbody><tr><th>最长浇水秒</th><td>");
    writeUInt(config.maxWateringDurationSec);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>手动默认秒</th><td>");
    writeUInt(config.manualDefaultDurationSec);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>计划宽限秒</th><td>");
    writeUInt(config.scheduleGraceSec);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>排队最大秒</th><td>");
    writeUInt(config.queuedPlanMaxDelaySec);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>待机漏水窗口秒</th><td>");
    writeUInt(config.idleLeakWindowSec);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>待机漏水脉冲</th><td>");
    writeUInt(config.idleLeakPulseThreshold);
    Esp32BaseWeb::sendChunk("</td></tr></tbody></table></div><div class='actions'><a class='btnlink' href='/irrigation/settings?edit=1'>编辑</a></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handlePlansPage() {
    uint32_t defaultYmd = 20260605UL;
    (void)PlanStore::currentYmd(&defaultYmd);
    uint8_t selectedZoneId = 0;
    uint8_t selectedSlotIndex = 0;
    const bool editingSlot = readZoneId(&selectedZoneId) &&
                             readU8("slotIndex", &selectedSlotIndex) &&
                             selectedSlotIndex < Irrigation::MaxPlansPerZone;
    Esp32BaseWeb::sendHeader("Plans");
    Esp32BaseWeb::sendPageTitle("Plans", "每个 Zone 最多 6 条计划");
    if (editingSlot) {
        const Irrigation::PlanDefinition& selectedPlan = PlanStore::getBySlot(selectedZoneId, selectedSlotIndex);
        writePlanEditForm(selectedZoneId, selectedPlan, defaultYmd);
        Esp32BaseWeb::sendFooter();
        return;
    }
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        Esp32BaseWeb::beginPanel("Zone 计划");
        Esp32BaseWeb::sendChunk("<h3>Zone ");
        writeUInt(zoneId);
        Esp32BaseWeb::sendChunk("</h3><div class='tablewrap'><table class='part'><thead><tr><th>Slot</th><th>计划</th><th>启用</th><th>时间</th><th>时长</th><th>循环</th><th>操作</th></tr></thead><tbody>");
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(zoneId, slot);
            Esp32BaseWeb::sendChunk("<tr><td>");
            writeUInt(slot + 1);
            Esp32BaseWeb::sendChunk("</td><td>");
            if (plan.exists) {
                Esp32BaseWeb::writeHtmlEscaped(plan.name);
            } else {
                Esp32BaseWeb::sendChunk("-");
            }
            Esp32BaseWeb::sendChunk("</td><td>");
            Esp32BaseWeb::sendChunk(plan.exists && plan.enabled ? "yes" : "no");
            Esp32BaseWeb::sendChunk("</td><td>");
            if (plan.exists) {
                writeTimeText(plan.timeHour, plan.timeMinute);
            } else {
                Esp32BaseWeb::sendChunk("-");
            }
            Esp32BaseWeb::sendChunk("</td><td>");
            writeUInt(plan.exists ? plan.durationSec : 300);
            Esp32BaseWeb::sendChunk(" s</td><td>");
            writeUInt(plan.exists ? plan.cycleDays : 1);
            Esp32BaseWeb::sendChunk(" d</td><td><a class='btnlink compact' href='/irrigation/plans?zoneId=");
            writeUInt(zoneId);
            Esp32BaseWeb::sendChunk("&amp;slotIndex=");
            writeUInt(slot);
            Esp32BaseWeb::sendChunk("'>");
            Esp32BaseWeb::sendChunk(plan.exists ? "编辑" : "新建");
            Esp32BaseWeb::sendChunk("</a></td></tr>");
        }
        Esp32BaseWeb::sendChunk("</tbody></table></div>");
        Esp32BaseWeb::endPanel();
    }
    Esp32BaseWeb::sendFooter();
}

struct RecordJsonState {
    bool first;
};

void sendRecordJsonCallback(const RecordStore::WateringRecord& record, void* user) {
    RecordJsonState* state = static_cast<RecordJsonState*>(user);
    if (!state->first) {
        Esp32BaseWeb::sendChunk(",");
    }
    state->first = false;
    writeRecordJson(record);
}

void handleRecordsPage() {
    Esp32BaseWeb::sendHeader("Records");
    Esp32BaseWeb::sendPageTitle("Records", "最近浇水记录");
    Esp32BaseWeb::beginPanel("记录");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>ID</th><th>Zone</th><th>Flow</th><th>结果</th><th>目标</th><th>水量</th><th>开始</th><th>结束</th></tr></thead><tbody>");
    struct PageState {
        bool any;
    } state = {false};
    auto callback = [](const RecordStore::WateringRecord& record, void* user) {
        PageState* s = static_cast<PageState*>(user);
        s->any = true;
        Esp32BaseWeb::sendChunk("<tr><td>");
        writeUInt(record.recordId);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(record.zoneId);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(record.configSnapshot.flowId);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(record.result);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(record.targetSec);
        Esp32BaseWeb::sendChunk(" s</td><td>");
        writeUInt(record.estimatedMilliliters);
        Esp32BaseWeb::sendChunk(" ml</td><td>");
        writeUInt(record.startedEpoch);
        Esp32BaseWeb::sendChunk("</td><td>");
        writeUInt(record.endedEpoch);
        Esp32BaseWeb::sendChunk("</td></tr>");
    };
    (void)RecordStore::readLatest(0, 50, callback, &state);
    if (!state.any) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='8'>暂无记录</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleEventsPage() {
    Esp32BaseWeb::sendHeader("Events");
    Esp32BaseWeb::sendPageTitle("Events", "灌溉业务事件");
    Esp32BaseWeb::beginPanel("事件");
    Esp32BaseWeb::sendChunk("<p>业务事件写入 Esp32Base App Events，不另建第二套事件存储。</p><p><a href='/app/events?source=web'>查看 Web 事件</a> <a href='/app/events?source=monitor'>查看监控事件</a> <a href='/app/events?source=schedule'>查看计划事件</a></p>");
    Esp32BaseWeb::sendChunk("<div class='tablewrap'><table class='part'><thead><tr><th>ID</th><th>Level</th><th>Source</th><th>Type</th><th>Reason</th><th>Object</th><th>Text</th></tr></thead><tbody>");
    struct EventPageState {
        bool any;
    } state = {false};
    auto callback = [](const Esp32BaseAppEventRecord& event, void* user) {
        EventPageState* s = static_cast<EventPageState*>(user);
        if (strncmp(event.object, "zone:", 5) != 0 &&
            strncmp(event.object, "flow:", 5) != 0 &&
            strncmp(event.object, "plan:", 5) != 0 &&
            strncmp(event.object, "system:irrigation", 17) != 0 &&
            strncmp(event.object, "config:", 7) != 0) {
            return;
        }
        s->any = true;
        Esp32BaseWeb::sendChunk("<tr><td>");
        writeUInt(event.id);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(Esp32BaseAppEventLog::levelName(static_cast<Esp32BaseAppEventLog::Level>(event.level)));
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(event.source);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(event.type);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(event.reason);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(event.object);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::writeHtmlEscaped(event.text);
        Esp32BaseWeb::sendChunk("</td></tr>");
    };
    (void)Esp32BaseAppEventLog::readLatest(0, 80, callback, &state);
    if (!state.any) {
        Esp32BaseWeb::sendChunk("<tr><td colspan='7'>暂无业务事件</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table></div>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleStatusApi() {
    Esp32BaseWeb::beginResponse(200, "application/json", nullptr);
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
    Esp32BaseWeb::sendChunk("],\"flowAlerts\":[");
    for (uint8_t flowId = 1; flowId <= Irrigation::MaxFlowMeters; ++flowId) {
        if (flowId > 1) {
            Esp32BaseWeb::sendChunk(",");
        }
        const FlowAlertStore::FlowAlert& alert = FlowAlertStore::get(flowId);
        Esp32BaseWeb::sendChunk("{\"flowId\":");
        writeUInt(flowId);
        Esp32BaseWeb::sendChunk(",\"idleLeakActive\":");
        writeBool(alert.idleLeakActive);
        Esp32BaseWeb::sendChunk(",\"observedPulses\":");
        writeUInt(alert.observedPulses);
        Esp32BaseWeb::sendChunk(",\"pulseThreshold\":");
        writeUInt(alert.pulseThreshold);
        Esp32BaseWeb::sendChunk(",\"windowSec\":");
        writeUInt(alert.windowSec);
        Esp32BaseWeb::sendChunk("}");
    }
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endResponse();
}

void handlePlansApi() {
    Esp32BaseWeb::beginResponse(200, "application/json", nullptr);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"plans\":[");
    bool first = true;
    for (uint8_t zoneId = 1; zoneId <= Irrigation::MaxZones; ++zoneId) {
        for (uint8_t slot = 0; slot < Irrigation::MaxPlansPerZone; ++slot) {
            const Irrigation::PlanDefinition& plan = PlanStore::getBySlot(zoneId, slot);
            if (!first) {
                Esp32BaseWeb::sendChunk(",");
            }
            first = false;
            writePlanJson(plan);
        }
    }
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endResponse();
}

void handlePlanSaveApi() {
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone_id");
        return;
    }
    uint32_t planId = 0;
    Irrigation::PlanDefinition plan = {};
    const bool updating = Esp32BaseWeb::hasParam("planId") && readU32("planId", &planId) && planId != 0;
    uint8_t slotIndex = 0;
    const bool slotSubmitted = Esp32BaseWeb::hasParam("slotIndex");
    if (!updating && slotSubmitted && (!readU8("slotIndex", &slotIndex) || slotIndex >= Irrigation::MaxPlansPerZone)) {
        sendError(400, "invalid_slot_index");
        return;
    }
    const bool creatingAtSlot = !updating && slotSubmitted;
    if (updating) {
        if (!PlanStore::getById(planId, &plan) || plan.zoneId != zoneId) {
            sendError(404, "plan_not_found");
            return;
        }
    } else {
        plan.exists = true;
        plan.zoneId = zoneId;
        plan.enabled = false;
        plan.timeHour = 7;
        plan.timeMinute = 0;
        plan.durationSec = 300;
        plan.cycleDays = 1;
        plan.cycleMask = 1;
        if (!PlanStore::currentYmd(&plan.cycleStartYmd)) {
            plan.cycleStartYmd = 20260605UL;
        }
    }
    if (Esp32BaseWeb::hasParam("name")) {
        char name[Irrigation::NameMaxBytes];
        if (!getParamText("name", name, sizeof(name))) {
            sendError(400, "invalid_name");
            return;
        }
        strlcpy(plan.name, name, sizeof(plan.name));
    }
    if (Esp32BaseWeb::hasParam("enabledPresent") || Esp32BaseWeb::hasParam("enabled")) {
        plan.enabled = readCheckbox("enabled");
    }
    if (Esp32BaseWeb::hasParam("timeHour") && !readU8("timeHour", &plan.timeHour)) {
        sendError(400, "invalid_hour");
        return;
    }
    if (Esp32BaseWeb::hasParam("timeMinute") && !readU8("timeMinute", &plan.timeMinute)) {
        sendError(400, "invalid_minute");
        return;
    }
    if (Esp32BaseWeb::hasParam("durationSec") && !readU32("durationSec", &plan.durationSec)) {
        sendError(400, "invalid_duration");
        return;
    }
    if (Esp32BaseWeb::hasParam("cycleDays") && !readU8("cycleDays", &plan.cycleDays)) {
        sendError(400, "invalid_cycle_days");
        return;
    }
    if (Esp32BaseWeb::hasParam("cycleMask") && !readU32("cycleMask", &plan.cycleMask)) {
        sendError(400, "invalid_cycle_mask");
        return;
    }
    if (Esp32BaseWeb::hasParam("cycleStartYmd") && !readU32("cycleStartYmd", &plan.cycleStartYmd)) {
        sendError(400, "invalid_cycle_start");
        return;
    }
    const Irrigation::ZoneConfig& zone = ZoneConfigStore::get(zoneId);
    if (plan.enabled && (!zone.enabled || !FlowConfigStore::get(zone.flowId).enabled)) {
        sendError(409, "zone_or_flow_disabled");
        return;
    }
    Irrigation::PlanDefinition saved = {};
    if (updating) {
        if (!PlanStore::set(plan.planId, plan)) {
            sendError(400, "plan_rejected");
            return;
        }
        saved = plan;
    } else if (creatingAtSlot ? !PlanStore::createAt(zoneId, slotIndex, plan, &saved) : !PlanStore::create(zoneId, plan, &saved)) {
        sendError(400, "plan_create_rejected");
        return;
    }
    char redirect[80];
    if (redirectRequested(redirect, sizeof(redirect))) {
        Esp32BaseWeb::redirectSeeOther(redirect);
        return;
    }
    Esp32BaseWeb::beginResponse(200, "application/json", nullptr);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"plan\":");
    writePlanJson(saved);
    Esp32BaseWeb::sendChunk("}");
    Esp32BaseWeb::endResponse();
}

void handlePlanDeleteApi() {
    uint32_t planId = 0;
    if (!readU32("planId", &planId) || planId == 0) {
        sendError(400, "invalid_plan_id");
        return;
    }
    if (!PlanStore::remove(planId)) {
        sendError(404, "plan_not_found");
        return;
    }
    sendOkOrRedirect();
}

void handleRecordsApi() {
    RecordJsonState state = {true};
    Esp32BaseWeb::beginResponse(200, "application/json", nullptr);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"count\":");
    writeUInt(RecordStore::count());
    Esp32BaseWeb::sendChunk(",\"records\":[");
    (void)RecordStore::readLatest(0, 50, sendRecordJsonCallback, &state);
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endResponse();
}

struct EventJsonState {
    bool first;
};

void sendEventJsonCallback(const Esp32BaseAppEventRecord& event, void* user) {
    if (strncmp(event.object, "zone:", 5) != 0 &&
        strncmp(event.object, "flow:", 5) != 0 &&
        strncmp(event.object, "plan:", 5) != 0 &&
        strncmp(event.object, "system:irrigation", 17) != 0 &&
        strncmp(event.object, "config:", 7) != 0) {
        return;
    }
    EventJsonState* state = static_cast<EventJsonState*>(user);
    if (!state->first) {
        Esp32BaseWeb::sendChunk(",");
    }
    state->first = false;
    writeEventJson(event);
}

void handleEventsApi() {
    EventJsonState state = {true};
    Esp32BaseWeb::beginResponse(200, "application/json", nullptr);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"count\":");
    writeUInt(Esp32BaseAppEventLog::count());
    Esp32BaseWeb::sendChunk(",\"events\":[");
    (void)Esp32BaseAppEventLog::readLatest(0, 80, sendEventJsonCallback, &state);
    Esp32BaseWeb::sendChunk("]}");
    Esp32BaseWeb::endResponse();
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
    sendOkOrRedirect();
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
    sendOkOrRedirect();
}

void handleStopAllApi() {
    const uint8_t stopped = ZoneManager::stopAll(Irrigation::StopSource::WEB_PAGE, Irrigation::TaskResult::USER_STOPPED);
    char redirect[80];
    if (redirectRequested(redirect, sizeof(redirect))) {
        Esp32BaseWeb::redirectSeeOther(redirect);
        return;
    }
    Esp32BaseWeb::beginResponse(200, "application/json", nullptr);
    Esp32BaseWeb::sendChunk("{\"ok\":true,\"stopped\":");
    writeUInt(stopped);
    Esp32BaseWeb::sendChunk("}");
    Esp32BaseWeb::endResponse();
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
    if (config.enabled && !FlowConfigStore::get(config.flowId).enabled) {
        sendError(409, "flow_disabled");
        return;
    }
    if (!ZoneConfigStore::set(zoneId, config)) {
        sendError(400, "zone_config_rejected");
        return;
    }
    (void)ZoneManager::reloadZone(zoneId);
    sendOkOrRedirect();
}

void handleZoneBaselineSavePendingApi() {
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone_id");
        return;
    }
    if (ZoneManager::isZoneBusy(zoneId)) {
        sendError(409, "zone_running");
        return;
    }
    Irrigation::ZoneFlowBaselineProfile pending = ZoneConfigStore::get(zoneId).hasPendingBaseline
        ? ZoneConfigStore::get(zoneId).pendingBaseline
        : ZoneConfigStore::get(zoneId).activeBaseline;
    if (Esp32BaseWeb::hasParam("learnedFlowMlPerMin") && !readU32("learnedFlowMlPerMin", &pending.learnedFlowMlPerMin)) {
        sendError(400, "invalid_learned_flow");
        return;
    }
    if (Esp32BaseWeb::hasParam("lowFlowPermille") && !readU16("lowFlowPermille", &pending.lowFlowPermille)) {
        sendError(400, "invalid_low_flow");
        return;
    }
    if (Esp32BaseWeb::hasParam("highFlowPermille") && !readU16("highFlowPermille", &pending.highFlowPermille)) {
        sendError(400, "invalid_high_flow");
        return;
    }
    if (Esp32BaseWeb::hasParam("flowFaultConfirmSec") && !readU16("flowFaultConfirmSec", &pending.flowFaultConfirmSec)) {
        sendError(400, "invalid_fault_confirm");
        return;
    }
    if (Esp32BaseWeb::hasParam("noPulseTimeoutSec") && !readU16("noPulseTimeoutSec", &pending.noPulseTimeoutSec)) {
        sendError(400, "invalid_no_pulse_timeout");
        return;
    }
    pending.source = pending.learnedFlowMlPerMin > 0 ? Irrigation::ParameterSource::MANUAL : Irrigation::ParameterSource::NONE;
    pending.updatedAt = ZoneManager::trustedEpoch();
    if (!ZoneConfigStore::savePendingBaseline(zoneId, pending)) {
        sendError(400, "pending_baseline_rejected");
        return;
    }
    sendOkOrRedirect();
}

void handleZoneBaselineApplyApi() {
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone_id");
        return;
    }
    if (ZoneManager::isZoneBusy(zoneId)) {
        sendError(409, "zone_running");
        return;
    }
    Irrigation::ZoneFlowBaselineProfile oldProfile = {};
    Irrigation::ZoneFlowBaselineProfile newProfile = {};
    if (!ZoneConfigStore::applyPendingBaseline(zoneId, &oldProfile, &newProfile)) {
        sendError(409, "apply_baseline_failed");
        return;
    }
    (void)ZoneManager::reloadZone(zoneId);
    BusinessEventLog::appendZoneBaselineApplied(zoneId, oldProfile, newProfile, "web");
    sendOkOrRedirect();
}

void handleZoneBaselineRestoreApi() {
    uint8_t zoneId = 0;
    if (!readZoneId(&zoneId)) {
        sendError(400, "invalid_zone_id");
        return;
    }
    if (ZoneManager::isZoneBusy(zoneId)) {
        sendError(409, "zone_running");
        return;
    }
    Irrigation::ZoneFlowBaselineProfile oldProfile = {};
    Irrigation::ZoneFlowBaselineProfile restoredProfile = {};
    if (!ZoneConfigStore::restoreRollbackBaseline(zoneId, &oldProfile, &restoredProfile)) {
        sendError(409, "restore_baseline_failed");
        return;
    }
    (void)ZoneManager::reloadZone(zoneId);
    BusinessEventLog::appendZoneBaselineRestored(zoneId, oldProfile, restoredProfile, "web");
    sendOkOrRedirect();
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
    sendOkOrRedirect();
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
    sendOkOrRedirect();
}

void handleCalibrationStopApi() {
    if (!FlowCalibration::stop()) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    sendOkOrRedirect();
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
    sendOkOrRedirect();
}

void handleCalibrationSavePendingApi() {
    if (!FlowCalibration::saveCandidate()) {
        sendError(409, FlowCalibration::lastError());
        return;
    }
    sendOkOrRedirect();
}

void handleCalibrationApplyApi() {
    uint8_t flowId = 0;
    if (!readFlowId(&flowId)) {
        sendError(400, "invalid_flow_id");
        return;
    }
    if (ZoneManager::isFlowBusy(flowId)) {
        sendError(409, "flow_running");
        return;
    }
    Irrigation::FlowMeterCalibrationProfile oldProfile = {};
    Irrigation::FlowMeterCalibrationProfile newProfile = {};
    if (!FlowConfigStore::applyPendingCalibration(flowId, &oldProfile, &newProfile)) {
        sendError(409, "apply_pending_failed");
        return;
    }
    BusinessEventLog::appendFlowCalibrationApplied(flowId, oldProfile, newProfile, "web");
    sendOkOrRedirect();
}

void handleCalibrationRestoreApi() {
    uint8_t flowId = 0;
    if (!readFlowId(&flowId)) {
        sendError(400, "invalid_flow_id");
        return;
    }
    if (ZoneManager::isFlowBusy(flowId)) {
        sendError(409, "flow_running");
        return;
    }
    Irrigation::FlowMeterCalibrationProfile oldProfile = {};
    Irrigation::FlowMeterCalibrationProfile restoredProfile = {};
    if (!FlowConfigStore::restoreRollbackCalibration(flowId, &oldProfile, &restoredProfile)) {
        sendError(409, "restore_rollback_failed");
        return;
    }
    BusinessEventLog::appendFlowCalibrationRestored(flowId, oldProfile, restoredProfile, "web");
    sendOkOrRedirect();
}

void handleConfigApi() {
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        const Irrigation::SystemConfig& config = SystemConfigStore::current();
        Esp32BaseWeb::beginResponse(200, "application/json", nullptr);
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
        Esp32BaseWeb::endResponse();
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
    sendOkOrRedirect();
}

}

namespace IrrigationWeb {

void begin() {
    (void)Esp32BaseWeb::addNavItem("/irrigation", "灌溉");
    (void)Esp32BaseWeb::addNavItem("/irrigation/zones", "水路");
    (void)Esp32BaseWeb::addNavItem("/irrigation/flows", "流量计");
    (void)Esp32BaseWeb::addNavItem("/irrigation/plans", "计划");
    (void)Esp32BaseWeb::addNavItem("/irrigation/calibration", "校准");
    (void)Esp32BaseWeb::addNavItem("/irrigation/records", "记录");
    (void)Esp32BaseWeb::addNavItem("/irrigation/events", "事件");
    (void)Esp32BaseWeb::addNavItem("/irrigation/settings", "灌溉设置");
    const bool ok =
        Esp32BaseWeb::addRoute("/irrigation", Esp32BaseWeb::METHOD_GET, handleIndexPage) &&
        Esp32BaseWeb::addRoute("/irrigation/zones", Esp32BaseWeb::METHOD_GET, handleZonesPage) &&
        Esp32BaseWeb::addRoute("/irrigation/flows", Esp32BaseWeb::METHOD_GET, handleFlowsPage) &&
        Esp32BaseWeb::addRoute("/irrigation/plans", Esp32BaseWeb::METHOD_GET, handlePlansPage) &&
        Esp32BaseWeb::addRoute("/irrigation/calibration", Esp32BaseWeb::METHOD_GET, handleCalibrationPage) &&
        Esp32BaseWeb::addRoute("/irrigation/records", Esp32BaseWeb::METHOD_GET, handleRecordsPage) &&
        Esp32BaseWeb::addRoute("/irrigation/events", Esp32BaseWeb::METHOD_GET, handleEventsPage) &&
        Esp32BaseWeb::addRoute("/irrigation/settings", Esp32BaseWeb::METHOD_GET, handleSettingsPage) &&
        Esp32BaseWeb::addRoute("/index", Esp32BaseWeb::METHOD_GET, handleIndexPage) &&
        Esp32BaseWeb::addRoute("/api/v1/status", Esp32BaseWeb::METHOD_GET, handleStatusApi) &&
        Esp32BaseWeb::addRoute("/api/v1/plans", Esp32BaseWeb::METHOD_GET, handlePlansApi) &&
        Esp32BaseWeb::addRoute("/api/v1/plans/save", Esp32BaseWeb::METHOD_POST, handlePlanSaveApi) &&
        Esp32BaseWeb::addRoute("/api/v1/plans/delete", Esp32BaseWeb::METHOD_POST, handlePlanDeleteApi) &&
        Esp32BaseWeb::addRoute("/api/v1/records", Esp32BaseWeb::METHOD_GET, handleRecordsApi) &&
        Esp32BaseWeb::addRoute("/api/v1/events", Esp32BaseWeb::METHOD_GET, handleEventsApi) &&
        Esp32BaseWeb::addRoute("/api/v1/manual/start", Esp32BaseWeb::METHOD_POST, handleManualStartApi) &&
        Esp32BaseWeb::addRoute("/api/v1/manual/stop", Esp32BaseWeb::METHOD_POST, handleManualStopApi) &&
        Esp32BaseWeb::addRoute("/api/v1/manual/stop-all", Esp32BaseWeb::METHOD_POST, handleStopAllApi) &&
        Esp32BaseWeb::addRoute("/api/v1/zones/config", Esp32BaseWeb::METHOD_POST, handleZoneConfigApi) &&
        Esp32BaseWeb::addRoute("/api/v1/zones/baseline/pending/save", Esp32BaseWeb::METHOD_POST, handleZoneBaselineSavePendingApi) &&
        Esp32BaseWeb::addRoute("/api/v1/zones/baseline/pending/apply", Esp32BaseWeb::METHOD_POST, handleZoneBaselineApplyApi) &&
        Esp32BaseWeb::addRoute("/api/v1/zones/baseline/rollback/restore", Esp32BaseWeb::METHOD_POST, handleZoneBaselineRestoreApi) &&
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
