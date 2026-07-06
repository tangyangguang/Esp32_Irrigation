#include "web/WebPages.h"

#include "BoardPins.h"
#include "app/RuntimeController.h"
#include "comm/Rs485Master.h"
#include "storage/ConfigStore.h"
#include "storage/HistoryStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <stdlib.h>

namespace Irrigation {
namespace {
Rs485Master* g_master = nullptr;
RuntimeController* g_runtime = nullptr;
ConfigStore* g_config = nullptr;
HistoryStore* g_history = nullptr;

const char* onlineStateName(StationOnlineState state) {
    switch (state) {
    case StationOnlineState::Online:
        return "online";
    case StationOnlineState::Offline:
        return "offline";
    case StationOnlineState::Unknown:
    default:
        return "unknown";
    }
}

const char* historyTypeName(uint8_t type) {
    switch (type) {
    case HISTORY_OPERATION:
        return "operation";
    case HISTORY_WATERING:
        return "watering";
    case HISTORY_EXCEPTION:
        return "exception";
    case HISTORY_CONFIG:
        return "config";
    case HISTORY_CALIBRATION:
        return "calibration";
    default:
        return "unknown";
    }
}

void sendIrrigationNav() {
    Esp32BaseWeb::sendChunk("<p><a href='/irrigation'>Run</a> | <a href='/irrigation/sources'>Sources</a> | <a href='/irrigation/plans'>Plans</a> | <a href='/irrigation/history'>History</a> | <a href='/irrigation/settings'>Settings</a></p>");
}

void sendNumber(uint32_t value) {
    Esp32BaseWeb::sendChunk(String(value).c_str());
}

void sendEscaped(const char* value) {
    Esp32BaseWeb::writeHtmlEscaped(value ? value : "");
}

bool getUintParam(const char* name, uint32_t& value) {
    char text[16] = "";
    if (!Esp32BaseWeb::getParam(name, text, sizeof(text))) {
        return false;
    }
    value = static_cast<uint32_t>(strtoul(text, nullptr, 10));
    return true;
}

bool getTextParam(const char* name, char* out, size_t len) {
    return Esp32BaseWeb::getParam(name, out, len);
}

void sendStationRows() {
    Esp32BaseWeb::sendChunk("<table><thead><tr><th>Addr</th><th>State</th><th>Valves</th><th>Inputs</th><th>Station</th><th>Task</th><th>Fault</th><th>Active</th><th>Remain</th><th>Inputs</th><th>Last result</th><th>Uptime</th></tr></thead><tbody>");
    if (g_master) {
        for (uint8_t addr = IrrigationBoard::STATION_ADDR_MIN; addr <= IrrigationBoard::STATION_ADDR_MAX; ++addr) {
            const StationSnapshot& s = g_master->station(addr);
            Esp32BaseWeb::sendChunk("<tr><td>");
            sendNumber(addr);
            Esp32BaseWeb::sendChunk("</td><td>");
            Esp32BaseWeb::sendChunk(onlineStateName(s.online));
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.valveCount);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.inputCount);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.stationState);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.taskState);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.faultCode);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.activeTaskId);
            Esp32BaseWeb::sendChunk(" / valve ");
            sendNumber(s.activeValveNo);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.remainingSec);
            Esp32BaseWeb::sendChunk("s</td><td>");
            sendNumber(s.inputBits);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.lastResultCode);
            Esp32BaseWeb::sendChunk(" / task ");
            sendNumber(s.lastResultTaskId);
            Esp32BaseWeb::sendChunk("</td><td>");
            sendNumber(s.uptimeSec);
            Esp32BaseWeb::sendChunk("s</td></tr>");
        }
    }
    Esp32BaseWeb::sendChunk("</tbody></table>");
}

void handleIrrigationPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }

    Esp32BaseWeb::sendHeader("Irrigation");
    sendIrrigationNav();
    Esp32BaseWeb::sendChunk("<h2>Irrigation</h2>");
    Esp32BaseWeb::sendChunk("<p>RS485: ");
    Esp32BaseWeb::sendChunk((g_master && g_master->rs485Ready()) ? "ready" : "not ready");
    Esp32BaseWeb::sendChunk("</p><p>Last action: ");
    if (g_runtime) {
        sendEscaped(g_runtime->lastMessage());
    }
    Esp32BaseWeb::sendChunk("</p>");

    Esp32BaseWeb::sendChunk("<form method='post' action='/api/irrigation/manual/start' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("<h3>Manual run</h3>");
    Esp32BaseWeb::sendChunk("Source <input name='source' type='number' min='1' max='4' value='1'> ");
    Esp32BaseWeb::sendChunk("Zone <input name='zone' type='number' min='1' max='8' value='1'> ");
    Esp32BaseWeb::sendChunk("Duration <input name='duration' type='number' min='1' max='86400' value='60'> ");
    Esp32BaseWeb::sendChunk("<input type='submit' value='Start'></form>");
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/irrigation/manual/stop' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("Source <input name='source' type='number' min='1' max='4' value='1'> ");
    Esp32BaseWeb::sendChunk("<input type='submit' value='Stop'></form>");
    sendStationRows();
    Esp32BaseWeb::sendFooter();
}

void handleSourcesPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }

    Esp32BaseWeb::sendHeader("Sources");
    sendIrrigationNav();
    Esp32BaseWeb::sendChunk("<h2>Sources</h2>");
    if (!g_config) {
        Esp32BaseWeb::sendChunk("<p>Config store is not ready.</p>");
        Esp32BaseWeb::sendFooter();
        return;
    }

    Esp32BaseWeb::sendChunk("<table><thead><tr><th>Source</th><th>Enabled</th><th>Station</th><th>Main valve</th><th>Pump</th><th>Flow</th><th>Low level</th><th>Zones</th></tr></thead><tbody>");
    for (uint8_t sourceId = 1; sourceId <= MAX_SOURCES; ++sourceId) {
        const SourceConfig& source = g_config->source(sourceId);
        Esp32BaseWeb::sendChunk("<tr><td>");
        sendEscaped(source.name);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(source.enabled ? "yes" : "no");
        Esp32BaseWeb::sendChunk("</td><td>");
        sendNumber(source.stationAddr);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(source.mainValveEnabled ? "yes" : "no");
        Esp32BaseWeb::sendChunk(" / ");
        sendNumber(source.mainValveNo);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(source.pumpEnabled ? "yes" : "no");
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(source.flowMeterEnabled ? "yes" : "no");
        Esp32BaseWeb::sendChunk(" / input ");
        sendNumber(source.flowInputNo);
        Esp32BaseWeb::sendChunk("</td><td>");
        Esp32BaseWeb::sendChunk(source.lowLevelEnabled ? "yes" : "no");
        Esp32BaseWeb::sendChunk(" / input ");
        sendNumber(source.lowLevelInputNo);
        Esp32BaseWeb::sendChunk("</td><td>");
        for (uint8_t zoneId = 1; zoneId <= MAX_ZONES_PER_SOURCE; ++zoneId) {
            const ZoneConfig& zone = g_config->zone(sourceId, zoneId);
            if (zoneId > 1) {
                Esp32BaseWeb::sendChunk(", ");
            }
            sendNumber(zone.zoneId);
            Esp32BaseWeb::sendChunk(":V");
            sendNumber(zone.valveNo);
            if (!zone.enabled) {
                Esp32BaseWeb::sendChunk("(off)");
            }
        }
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table>");

    Esp32BaseWeb::sendChunk("<form method='post' action='/api/irrigation/source/save' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("<h3>Edit source</h3>");
    Esp32BaseWeb::sendChunk("Source <input name='source' type='number' min='1' max='4' value='1'> ");
    Esp32BaseWeb::sendChunk("Name <input name='name' maxlength='23' value='Source 1'> ");
    Esp32BaseWeb::sendChunk("Enabled <input name='enabled' type='number' min='0' max='1' value='1'> ");
    Esp32BaseWeb::sendChunk("Station <input name='station' type='number' min='1' max='15' value='1'> ");
    Esp32BaseWeb::sendChunk("Main valve <input name='main_enabled' type='number' min='0' max='1' value='0'> ");
    Esp32BaseWeb::sendChunk("Main valve no <input name='main_valve' type='number' min='1' max='8' value='8'> ");
    Esp32BaseWeb::sendChunk("Pump <input name='pump_enabled' type='number' min='0' max='1' value='0'> ");
    Esp32BaseWeb::sendChunk("Flow meter <input name='flow_enabled' type='number' min='0' max='1' value='1'> ");
    Esp32BaseWeb::sendChunk("Flow input <input name='flow_input' type='number' min='1' max='4' value='1'> ");
    Esp32BaseWeb::sendChunk("Low level <input name='low_enabled' type='number' min='0' max='1' value='0'> ");
    Esp32BaseWeb::sendChunk("Low input <input name='low_input' type='number' min='1' max='4' value='2'> ");
    Esp32BaseWeb::sendChunk("Low active <input name='low_mode' type='number' min='0' max='1' value='1'> ");
    Esp32BaseWeb::sendChunk("Main hold <input name='main_hold' type='number' min='0' max='1000' value='700'> ");
    Esp32BaseWeb::sendChunk("Branch hold <input name='hold' type='number' min='0' max='1000' value='700'> ");
    Esp32BaseWeb::sendChunk("<input type='submit' value='Save'></form>");
    Esp32BaseWeb::sendChunk("<form method='post' action='/api/irrigation/zone/save' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("<h3>Edit zone</h3>");
    Esp32BaseWeb::sendChunk("Source <input name='source' type='number' min='1' max='4' value='1'> ");
    Esp32BaseWeb::sendChunk("Zone <input name='zone' type='number' min='1' max='8' value='1'> ");
    Esp32BaseWeb::sendChunk("Name <input name='name' maxlength='23' value='Zone 1'> ");
    Esp32BaseWeb::sendChunk("Enabled <input name='enabled' type='number' min='0' max='1' value='1'> ");
    Esp32BaseWeb::sendChunk("Valve <input name='valve' type='number' min='1' max='8' value='1'> ");
    Esp32BaseWeb::sendChunk("Manual duration <input name='duration' type='number' min='1' max='86400' value='60'> ");
    Esp32BaseWeb::sendChunk("<input type='submit' value='Save'></form>");
    Esp32BaseWeb::sendFooter();
}

void handlePlansPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Plans");
    sendIrrigationNav();
    Esp32BaseWeb::sendChunk("<h2>Plans</h2><p>No watering plan is configured in firmware yet. Manual run is available.</p>");
    Esp32BaseWeb::sendFooter();
}

void handleHistoryPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("History");
    sendIrrigationNav();
    Esp32BaseWeb::sendChunk("<h2>History</h2>");
    if (!g_history || g_history->count() == 0) {
        Esp32BaseWeb::sendChunk("<p>No records.</p>");
        Esp32BaseWeb::sendFooter();
        return;
    }
    Esp32BaseWeb::sendChunk("<table><thead><tr><th>Seq</th><th>Uptime</th><th>Type</th><th>Source</th><th>Zone</th><th>Result</th><th>Task</th><th>Planned</th><th>Actual</th><th>Message</th></tr></thead><tbody>");
    for (uint8_t i = 0; i < g_history->count(); ++i) {
        const HistoryRecord& record = g_history->record(i);
        Esp32BaseWeb::sendChunk("<tr><td>");
        sendNumber(record.seq);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendNumber(record.uptimeMs / 1000UL);
        Esp32BaseWeb::sendChunk("s</td><td>");
        Esp32BaseWeb::sendChunk(historyTypeName(record.type));
        Esp32BaseWeb::sendChunk("</td><td>");
        sendNumber(record.sourceId);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendNumber(record.zoneId);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendNumber(record.resultCode);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendNumber(record.taskId);
        Esp32BaseWeb::sendChunk("</td><td>");
        sendNumber(record.plannedSec);
        Esp32BaseWeb::sendChunk("s</td><td>");
        sendNumber(record.actualSec);
        Esp32BaseWeb::sendChunk("s</td><td>");
        sendEscaped(record.message);
        Esp32BaseWeb::sendChunk("</td></tr>");
    }
    Esp32BaseWeb::sendChunk("</tbody></table>");
    Esp32BaseWeb::sendFooter();
}

void handleSettingsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("Settings");
    sendIrrigationNav();
    Esp32BaseWeb::sendChunk("<h2>Settings</h2>");
    if (!g_config) {
        Esp32BaseWeb::sendChunk("<p>Config store is not ready.</p>");
        Esp32BaseWeb::sendFooter();
        return;
    }
    const SystemConfig& system = g_config->system();
    Esp32BaseWeb::sendChunk("<table><tbody><tr><th>Max run</th><td>");
    sendNumber(system.maxRunMinutes);
    Esp32BaseWeb::sendChunk(" min</td></tr><tr><th>RS485 baud</th><td>");
    sendNumber(system.rs485Baud);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>Offline threshold</th><td>");
    sendNumber(system.offlineFailureThreshold);
    Esp32BaseWeb::sendChunk("</td></tr><tr><th>Main valve lead</th><td>");
    sendNumber(system.mainValveOpenLeadSec);
    Esp32BaseWeb::sendChunk("s</td></tr><tr><th>Main valve close delay</th><td>");
    sendNumber(system.mainValveCloseDelaySec);
    Esp32BaseWeb::sendChunk("s</td></tr><tr><th>Pump start delay</th><td>");
    sendNumber(system.pumpStartDelaySec);
    Esp32BaseWeb::sendChunk("s</td></tr><tr><th>Pump stop lead</th><td>");
    sendNumber(system.pumpStopLeadSec);
    Esp32BaseWeb::sendChunk("s");
    Esp32BaseWeb::sendChunk("</td></tr></tbody></table>");

    Esp32BaseWeb::sendChunk("<form method='post' action='/api/irrigation/settings/save' onsubmit='return once(this)'>");
    Esp32BaseWeb::sendChunk("<h3>Edit settings</h3>");
    Esp32BaseWeb::sendChunk("Max run minutes <input name='max_run' type='number' min='1' max='1440' value='");
    sendNumber(system.maxRunMinutes);
    Esp32BaseWeb::sendChunk("'> RS485 baud <input name='baud' type='number' min='9600' max='921600' value='");
    sendNumber(system.rs485Baud);
    Esp32BaseWeb::sendChunk("'> Offline threshold <input name='offline' type='number' min='1' max='20' value='");
    sendNumber(system.offlineFailureThreshold);
    Esp32BaseWeb::sendChunk("'> Main valve lead <input name='main_lead' type='number' min='0' max='60' value='");
    sendNumber(system.mainValveOpenLeadSec);
    Esp32BaseWeb::sendChunk("'> Main close delay <input name='main_close' type='number' min='0' max='60' value='");
    sendNumber(system.mainValveCloseDelaySec);
    Esp32BaseWeb::sendChunk("'> Pump start delay <input name='pump_start' type='number' min='0' max='60' value='");
    sendNumber(system.pumpStartDelaySec);
    Esp32BaseWeb::sendChunk("'> Pump stop lead <input name='pump_stop' type='number' min='0' max='60' value='");
    sendNumber(system.pumpStopLeadSec);
    Esp32BaseWeb::sendChunk("'> <input type='submit' value='Save'></form>");
    Esp32BaseWeb::sendChunk("<p>RS485 baud is applied at boot. Other settings take effect immediately.</p>");
    Esp32BaseWeb::sendFooter();
}

void handleManualStartApi() {
    if (!Esp32BaseWeb::checkPostAllowed("irrigation_manual_start")) {
        return;
    }
    uint32_t source = 0;
    uint32_t zone = 0;
    uint32_t duration = 0;
    if (!g_runtime ||
        !getUintParam("source", source) ||
        !getUintParam("zone", zone) ||
        !getUintParam("duration", duration)) {
        Esp32BaseWeb::sendText(400, "bad request");
        return;
    }
    if (!g_runtime->startZone(static_cast<uint8_t>(source), static_cast<uint8_t>(zone), duration)) {
        Esp32BaseWeb::redirectSeeOther("/irrigation?start=rejected");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation?start=sent");
}

void handleManualStopApi() {
    if (!Esp32BaseWeb::checkPostAllowed("irrigation_manual_stop")) {
        return;
    }
    uint32_t source = 0;
    if (!g_runtime || !getUintParam("source", source)) {
        Esp32BaseWeb::sendText(400, "bad request");
        return;
    }
    if (!g_runtime->stopSource(static_cast<uint8_t>(source))) {
        Esp32BaseWeb::redirectSeeOther("/irrigation?stop=rejected");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation?stop=sent");
}

void handleSourceSaveApi() {
    if (!Esp32BaseWeb::checkPostAllowed("irrigation_source_save")) {
        return;
    }
    uint32_t sourceId = 0;
    uint32_t enabled = 0;
    uint32_t station = 0;
    uint32_t mainEnabled = 0;
    uint32_t mainValve = 0;
    uint32_t pumpEnabled = 0;
    uint32_t flowEnabled = 0;
    uint32_t flowInput = 0;
    uint32_t lowEnabled = 0;
    uint32_t lowInput = 0;
    uint32_t lowMode = 0;
    uint32_t mainHold = 0;
    uint32_t hold = 0;
    if (!g_config ||
        !getUintParam("source", sourceId) ||
        !getUintParam("enabled", enabled) ||
        !getUintParam("station", station) ||
        !getUintParam("main_enabled", mainEnabled) ||
        !getUintParam("main_valve", mainValve) ||
        !getUintParam("pump_enabled", pumpEnabled) ||
        !getUintParam("flow_enabled", flowEnabled) ||
        !getUintParam("flow_input", flowInput) ||
        !getUintParam("low_enabled", lowEnabled) ||
        !getUintParam("low_input", lowInput) ||
        !getUintParam("low_mode", lowMode) ||
        !getUintParam("main_hold", mainHold) ||
        !getUintParam("hold", hold) ||
        !g_config->sourceValid(static_cast<uint8_t>(sourceId)) ||
        station < IrrigationBoard::STATION_ADDR_MIN ||
        station > IrrigationBoard::STATION_ADDR_MAX ||
        mainValve < 1 || mainValve > 8 ||
        flowInput < 1 || flowInput > 4 ||
        lowInput < 1 || lowInput > 4 ||
        lowMode > 1 ||
        mainHold > 1000 ||
        hold > 1000) {
        Esp32BaseWeb::sendText(400, "bad request");
        return;
    }
    SourceConfig source = g_config->source(static_cast<uint8_t>(sourceId));
    source.enabled = enabled ? 1 : 0;
    source.stationAddr = static_cast<uint8_t>(station);
    source.mainValveEnabled = mainEnabled ? 1 : 0;
    source.mainValveNo = static_cast<uint8_t>(mainValve);
    source.pumpEnabled = pumpEnabled ? 1 : 0;
    source.flowMeterEnabled = flowEnabled ? 1 : 0;
    source.flowInputNo = static_cast<uint8_t>(flowInput);
    source.lowLevelEnabled = lowEnabled ? 1 : 0;
    source.lowLevelInputNo = static_cast<uint8_t>(lowInput);
    source.lowLevelActiveMode = static_cast<uint8_t>(lowMode);
    source.mainValveHoldDutyPermille = static_cast<uint16_t>(mainHold);
    source.branchValveHoldDutyPermille = static_cast<uint16_t>(hold);
    getTextParam("name", source.name, sizeof(source.name));
    if (!g_config->saveSource(source)) {
        Esp32BaseWeb::sendText(500, "save failed");
        return;
    }
    if (g_history) {
        g_history->add(HISTORY_CONFIG, source.sourceId, 0, 0, 0, 0, 0, "source saved");
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation/sources?saved=1");
}

void handleZoneSaveApi() {
    if (!Esp32BaseWeb::checkPostAllowed("irrigation_zone_save")) {
        return;
    }
    uint32_t sourceId = 0;
    uint32_t zoneId = 0;
    uint32_t enabled = 0;
    uint32_t valve = 0;
    uint32_t duration = 0;
    if (!g_config ||
        !getUintParam("source", sourceId) ||
        !getUintParam("zone", zoneId) ||
        !getUintParam("enabled", enabled) ||
        !getUintParam("valve", valve) ||
        !getUintParam("duration", duration) ||
        !g_config->zoneValid(static_cast<uint8_t>(sourceId), static_cast<uint8_t>(zoneId)) ||
        valve < 1 || valve > 8 ||
        duration == 0 || duration > 86400) {
        Esp32BaseWeb::sendText(400, "bad request");
        return;
    }
    ZoneConfig zone = g_config->zone(static_cast<uint8_t>(sourceId), static_cast<uint8_t>(zoneId));
    zone.enabled = enabled ? 1 : 0;
    zone.valveNo = static_cast<uint8_t>(valve);
    zone.lastManualDurationSec = static_cast<uint16_t>(duration > 65535 ? 65535 : duration);
    getTextParam("name", zone.name, sizeof(zone.name));
    if (!g_config->saveZone(zone)) {
        Esp32BaseWeb::sendText(500, "save failed");
        return;
    }
    if (g_history) {
        g_history->add(HISTORY_CONFIG, zone.sourceId, zone.zoneId, 0, 0, 0, 0, "zone saved");
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation/sources?saved=1");
}

void handleSettingsSaveApi() {
    if (!Esp32BaseWeb::checkPostAllowed("irrigation_settings_save")) {
        return;
    }
    uint32_t maxRun = 0;
    uint32_t baud = 0;
    uint32_t offline = 0;
    uint32_t mainLead = 0;
    uint32_t mainClose = 0;
    uint32_t pumpStart = 0;
    uint32_t pumpStop = 0;
    if (!g_config ||
        !getUintParam("max_run", maxRun) ||
        !getUintParam("baud", baud) ||
        !getUintParam("offline", offline) ||
        !getUintParam("main_lead", mainLead) ||
        !getUintParam("main_close", mainClose) ||
        !getUintParam("pump_start", pumpStart) ||
        !getUintParam("pump_stop", pumpStop) ||
        maxRun == 0 || maxRun > 1440 ||
        baud < 9600 || baud > 921600 ||
        offline == 0 || offline > 20 ||
        mainLead > 60 ||
        mainClose > 60 ||
        pumpStart > 60 ||
        pumpStop > 60) {
        Esp32BaseWeb::sendText(400, "bad request");
        return;
    }
    SystemConfig system = g_config->system();
    system.maxRunMinutes = static_cast<uint16_t>(maxRun);
    system.rs485Baud = baud;
    system.offlineFailureThreshold = static_cast<uint8_t>(offline);
    system.mainValveOpenLeadSec = static_cast<uint8_t>(mainLead);
    system.mainValveCloseDelaySec = static_cast<uint8_t>(mainClose);
    system.pumpStartDelaySec = static_cast<uint8_t>(pumpStart);
    system.pumpStopLeadSec = static_cast<uint8_t>(pumpStop);
    if (!g_config->saveSystem(system)) {
        Esp32BaseWeb::sendText(500, "save failed");
        return;
    }
    if (g_master) {
        g_master->setOfflineFailureThreshold(system.offlineFailureThreshold);
    }
    if (g_history) {
        g_history->add(HISTORY_CONFIG, 0, 0, 0, 0, 0, 0, "settings saved; baud applies after reboot");
    }
    Esp32BaseWeb::redirectSeeOther("/irrigation/settings?saved=1");
}

void handleStatusApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::beginJson(200);
    Esp32BaseWeb::sendChunk("\"rs485Ready\":");
    Esp32BaseWeb::sendChunk((g_master && g_master->rs485Ready()) ? "true" : "false");
    Esp32BaseWeb::sendChunk(",\"stations\":[");
    if (g_master) {
        for (uint8_t addr = IrrigationBoard::STATION_ADDR_MIN; addr <= IrrigationBoard::STATION_ADDR_MAX; ++addr) {
            if (addr != IrrigationBoard::STATION_ADDR_MIN) {
                Esp32BaseWeb::sendChunk(",");
            }
            const StationSnapshot& s = g_master->station(addr);
            Esp32BaseWeb::sendChunk("{\"addr\":");
            sendNumber(addr);
            Esp32BaseWeb::sendChunk(",\"online\":\"");
            Esp32BaseWeb::sendChunk(onlineStateName(s.online));
            Esp32BaseWeb::sendChunk("\",\"valveCount\":");
            sendNumber(s.valveCount);
            Esp32BaseWeb::sendChunk(",\"inputCount\":");
            sendNumber(s.inputCount);
            Esp32BaseWeb::sendChunk(",\"stationState\":");
            sendNumber(s.stationState);
            Esp32BaseWeb::sendChunk(",\"taskState\":");
            sendNumber(s.taskState);
            Esp32BaseWeb::sendChunk(",\"faultCode\":");
            sendNumber(s.faultCode);
            Esp32BaseWeb::sendChunk(",\"activeTaskId\":");
            sendNumber(s.activeTaskId);
            Esp32BaseWeb::sendChunk(",\"activeValveNo\":");
            sendNumber(s.activeValveNo);
            Esp32BaseWeb::sendChunk(",\"remainingSec\":");
            sendNumber(s.remainingSec);
            Esp32BaseWeb::sendChunk(",\"inputBits\":");
            sendNumber(s.inputBits);
            Esp32BaseWeb::sendChunk(",\"lastResultCode\":");
            sendNumber(s.lastResultCode);
            Esp32BaseWeb::sendChunk(",\"uptimeSec\":");
            sendNumber(s.uptimeSec);
            Esp32BaseWeb::sendChunk("}");
        }
    }
    Esp32BaseWeb::sendChunk("]");
    Esp32BaseWeb::endJson();
}
}

void registerWebPages(Rs485Master& master, RuntimeController& runtime, ConfigStore& config, HistoryStore& history) {
    g_master = &master;
    g_runtime = &runtime;
    g_config = &config;
    g_history = &history;
    Esp32BaseWeb::addPage("/irrigation", "Irrigation", handleIrrigationPage);
    Esp32BaseWeb::addPage("/irrigation/sources", "Sources", handleSourcesPage);
    Esp32BaseWeb::addPage("/irrigation/plans", "Plans", handlePlansPage);
    Esp32BaseWeb::addPage("/irrigation/history", "History", handleHistoryPage);
    Esp32BaseWeb::addPage("/irrigation/settings", "Settings", handleSettingsPage);
    Esp32BaseWeb::addApi("/api/irrigation/status", handleStatusApi);
    Esp32BaseWeb::addApi("/api/irrigation/manual/start", handleManualStartApi);
    Esp32BaseWeb::addApi("/api/irrigation/manual/stop", handleManualStopApi);
    Esp32BaseWeb::addApi("/api/irrigation/source/save", handleSourceSaveApi);
    Esp32BaseWeb::addApi("/api/irrigation/zone/save", handleZoneSaveApi);
    Esp32BaseWeb::addApi("/api/irrigation/settings/save", handleSettingsSaveApi);
}

}  // namespace Irrigation
