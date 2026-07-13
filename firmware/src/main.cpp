#include <Arduino.h>

#include "irrigation/IrrigationApp.h"

void setup() {
    IrrigationApp::instance().begin();
}

void loop() {
    IrrigationApp::instance().handle();
}
