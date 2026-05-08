#pragma once

namespace MaintenanceService {

void begin();
void handle();
bool requestFactoryReset(bool clearRecords);
bool factoryResetPending();
bool factoryResetClearRecords();

}
