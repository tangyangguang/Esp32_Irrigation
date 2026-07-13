#include "ZoneService.h"

#include <stdio.h>

#include "ConfigStore.h"
#include "IrrigationConfig.h"

namespace Irrigation {

namespace {
char g_lastError[40] = "ok";

void copyZoneName(char* target, size_t targetSize, const char* value) {
    snprintf(target, targetSize, "%s", value != nullptr && value[0] != '\0' ? value : "水路");
    target[targetSize - 1] = '\0';
}
} // namespace

const ZoneConfig* ZoneService::find(uint8_t zoneId) {
    const uint8_t index = zoneIndexFromId(zoneId);
    if (index >= kMaxZones) {
        setLastError("zone_id_invalid");
        return nullptr;
    }
    return &ConfigStore::config().zones[index];
}

bool ZoneService::snapshot(uint8_t zoneId, ZoneSnapshot& out) {
    const ZoneConfig* zone = find(zoneId);
    if (zone == nullptr) {
        return false;
    }

    out.id = zone->id;
    out.enabled = zone->enabled;
    out.name = zone->name;
    out.standardFlowMlPerMin = zone->standardFlowMlPerMin;
    out.lastRunEpoch = 0;
    setLastError("ok");
    return true;
}

bool ZoneService::setEnabled(uint8_t zoneId, bool enabled) {
    const uint8_t index = zoneIndexFromId(zoneId);
    if (index >= kMaxZones) {
        setLastError("zone_id_invalid");
        return false;
    }

    IrrigationConfig next = ConfigStore::config();
    next.zones[index].enabled = enabled;
    if (!ConfigStore::save(next)) {
        setLastError(ConfigStore::lastError());
        return false;
    }

    setLastError("ok");
    return true;
}

bool ZoneService::saveZone(const ZoneConfig& zone) {
    const uint8_t index = zoneIndexFromId(zone.id);
    if (index >= kMaxZones) {
        setLastError("zone_id_invalid");
        return false;
    }

    IrrigationConfig next = ConfigStore::config();
    ZoneConfig& target = next.zones[index];
    target.enabled = zone.enabled;
    copyZoneName(target.name, sizeof(target.name), zone.name);
    target.standardFlowMlPerMin = zone.standardFlowMlPerMin;

    const char* error = nullptr;
    if (!validateConfig(next, &error)) {
        setLastError(error);
        return false;
    }

    if (!ConfigStore::save(next)) {
        setLastError(ConfigStore::lastError());
        return false;
    }

    setLastError("ok");
    return true;
}

const char* ZoneService::lastError() {
    return g_lastError;
}

void ZoneService::setLastError(const char* error) {
    snprintf(g_lastError, sizeof(g_lastError), "%s", error != nullptr ? error : "unknown");
    g_lastError[sizeof(g_lastError) - 1] = '\0';
}

} // namespace Irrigation
