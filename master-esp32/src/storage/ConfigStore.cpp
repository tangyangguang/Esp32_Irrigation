#include "storage/ConfigStore.h"

#include <Esp32Base.h>
#include <stdio.h>
#include <string.h>

namespace Irrigation {
namespace {
constexpr const char* NS_SYSTEM = "irr_sys";
constexpr const char* NS_SOURCE = "irr_src";
constexpr const char* NS_ZONE = "irr_zone";

void sourceKey(uint8_t sourceId, char* out, size_t len) {
    snprintf(out, len, "s%u", sourceId);
}

void zoneKey(uint8_t sourceId, uint8_t zoneId, char* out, size_t len) {
    snprintf(out, len, "s%uz%u", sourceId, zoneId);
}

template <typename T>
bool validObject(const T& value, uint32_t magic) {
    return value.magic == magic && value.version == 1;
}
}

void ConfigStore::begin() {
    setDefaults();
    load();
}

const SystemConfig& ConfigStore::system() const {
    return _system;
}

const SourceConfig& ConfigStore::source(uint8_t sourceId) const {
    if (!sourceValid(sourceId)) {
        return emptySource();
    }
    return _sources[sourceId - 1];
}

const ZoneConfig& ConfigStore::zone(uint8_t sourceId, uint8_t zoneId) const {
    if (!zoneValid(sourceId, zoneId)) {
        return emptyZone();
    }
    return _zones[sourceId - 1][zoneId - 1];
}

bool ConfigStore::sourceValid(uint8_t sourceId) const {
    return sourceId >= 1 && sourceId <= MAX_SOURCES;
}

bool ConfigStore::zoneValid(uint8_t sourceId, uint8_t zoneId) const {
    return sourceValid(sourceId) && zoneId >= 1 && zoneId <= MAX_ZONES_PER_SOURCE;
}

bool ConfigStore::saveSystem(const SystemConfig& config) {
    _system = config;
    return Esp32BaseConfig::setPod(NS_SYSTEM, "main", _system);
}

bool ConfigStore::saveSource(const SourceConfig& config) {
    if (!sourceValid(config.sourceId)) {
        return false;
    }
    char key[8];
    sourceKey(config.sourceId, key, sizeof(key));
    _sources[config.sourceId - 1] = config;
    return Esp32BaseConfig::setPod(NS_SOURCE, key, _sources[config.sourceId - 1]);
}

bool ConfigStore::saveZone(const ZoneConfig& config) {
    if (!zoneValid(config.sourceId, config.zoneId)) {
        return false;
    }
    char key[12];
    zoneKey(config.sourceId, config.zoneId, key, sizeof(key));
    _zones[config.sourceId - 1][config.zoneId - 1] = config;
    return Esp32BaseConfig::setPod(NS_ZONE, key, _zones[config.sourceId - 1][config.zoneId - 1]);
}

void ConfigStore::setDefaults() {
    _system = SystemConfig();
    for (uint8_t s = 0; s < MAX_SOURCES; ++s) {
        SourceConfig source;
        source.sourceId = s + 1;
        source.stationAddr = s + 1;
        snprintf(source.name, sizeof(source.name), "Source %u", source.sourceId);
        _sources[s] = source;

        for (uint8_t z = 0; z < MAX_ZONES_PER_SOURCE; ++z) {
            ZoneConfig zone;
            zone.sourceId = s + 1;
            zone.zoneId = z + 1;
            zone.valveNo = z + 1;
            snprintf(zone.name, sizeof(zone.name), "Zone %u", zone.zoneId);
            _zones[s][z] = zone;
        }
    }
}

void ConfigStore::load() {
    SystemConfig storedSystem;
    if (Esp32BaseConfig::getPod(NS_SYSTEM, "main", storedSystem, _system) &&
        validObject(storedSystem, _system.magic)) {
        _system = storedSystem;
    }

    for (uint8_t s = 1; s <= MAX_SOURCES; ++s) {
        char key[12];
        SourceConfig source;
        sourceKey(s, key, sizeof(key));
        if (Esp32BaseConfig::getPod(NS_SOURCE, key, source, _sources[s - 1]) &&
            validObject(source, _sources[s - 1].magic)) {
            _sources[s - 1] = source;
        }
        for (uint8_t z = 1; z <= MAX_ZONES_PER_SOURCE; ++z) {
            ZoneConfig zone;
            zoneKey(s, z, key, sizeof(key));
            if (Esp32BaseConfig::getPod(NS_ZONE, key, zone, _zones[s - 1][z - 1]) &&
                validObject(zone, _zones[s - 1][z - 1].magic)) {
                _zones[s - 1][z - 1] = zone;
            }
        }
    }
}

const SourceConfig& ConfigStore::emptySource() {
    static const SourceConfig empty;
    return empty;
}

const ZoneConfig& ConfigStore::emptyZone() {
    static const ZoneConfig empty;
    return empty;
}

}  // namespace Irrigation

