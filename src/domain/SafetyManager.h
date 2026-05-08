#pragma once

namespace SafetyManager {

void begin();
void handle();
bool isLocked();
bool setLocked(bool locked);
bool factoryResetRequested();
void clearFactoryResetRequest();

}
