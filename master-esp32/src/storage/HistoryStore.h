#ifndef IRRIGATION_HISTORY_STORE_H
#define IRRIGATION_HISTORY_STORE_H

#include "IrrigationTypes.h"

namespace Irrigation {

enum HistoryType : uint8_t {
    HISTORY_OPERATION = 1,
    HISTORY_WATERING = 2,
    HISTORY_EXCEPTION = 3,
    HISTORY_CONFIG = 4,
    HISTORY_CALIBRATION = 5,
};

class HistoryStore {
public:
    void begin();
    void add(uint8_t type,
             uint8_t sourceId,
             uint8_t zoneId,
             uint8_t resultCode,
             uint16_t taskId,
             uint32_t plannedSec,
             uint32_t actualSec,
             const char* message);
    uint8_t count() const;
    const HistoryRecord& record(uint8_t index) const;

private:
    void resetMemory();
    void load();
    bool persistHeader();
    bool persistRecord(uint8_t physical);

    HistoryRecord _records[MAX_HISTORY_RECORDS];
    uint8_t _count = 0;
    uint8_t _next = 0;
    uint32_t _seq = 0;
    bool _persistent = false;
};

}  // namespace Irrigation

#endif
