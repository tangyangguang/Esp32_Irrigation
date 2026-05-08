#pragma once

#include <stdint.h>

namespace LeakMonitor {

void begin();
void handle();
bool hasAlert();
bool roadAlert(uint8_t road);
void clearAlerts();

}
