#ifndef IRRIGATION_CONFIG_STORE_H
#define IRRIGATION_CONFIG_STORE_H

#include "IrrigationTypes.h"

namespace Irrigation {

class ConfigStore {
public:
    void begin();

    const SystemConfig& system() const;
    const SourceConfig& source(uint8_t sourceId) const;
    const ZoneConfig& zone(uint8_t sourceId, uint8_t zoneId) const;
    bool sourceValid(uint8_t sourceId) const;
    bool zoneValid(uint8_t sourceId, uint8_t zoneId) const;
    bool saveSystem(const SystemConfig& config);
    bool saveSource(const SourceConfig& config);
    bool saveZone(const ZoneConfig& config);

private:
    void setDefaults();
    void load();
    static const SourceConfig& emptySource();
    static const ZoneConfig& emptyZone();

    SystemConfig _system;
    SourceConfig _sources[MAX_SOURCES];
    ZoneConfig _zones[MAX_SOURCES][MAX_ZONES_PER_SOURCE];
};

}  // namespace Irrigation

#endif

