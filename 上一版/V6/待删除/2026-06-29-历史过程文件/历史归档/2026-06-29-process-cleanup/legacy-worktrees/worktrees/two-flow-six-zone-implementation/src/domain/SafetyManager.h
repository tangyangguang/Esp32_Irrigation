#pragma once

namespace SafetyManager {

void begin();
void handle();
bool factoryResetRequested();
void clearFactoryResetRequest();

}
