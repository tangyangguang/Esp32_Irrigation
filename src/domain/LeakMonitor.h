#pragma once

#include <stdint.h>

#include "storage/EventStore.h"

namespace LeakMonitor {

void begin();
void handle();
bool hasAlert();
bool roadAlert(uint8_t road);
void clearAlerts(EventStore::Source source = EventStore::SOURCE_SYSTEM);

}
