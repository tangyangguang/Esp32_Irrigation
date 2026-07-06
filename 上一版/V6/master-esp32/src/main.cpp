#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>

#include "BoardPins.h"
#include "app/RuntimeController.h"
#include "comm/Rs485Master.h"
#include "storage/ConfigStore.h"
#include "storage/HistoryStore.h"
#include "web/WebPages.h"

namespace {
Esp32BaseRs485Port g_rs485(Serial2);
Irrigation::Rs485Master g_master(g_rs485);
Irrigation::ConfigStore g_config;
Irrigation::HistoryStore g_history;
Irrigation::RuntimeController g_runtime(g_master, g_config, g_history);
}

void setup() {
    Serial.begin(115200);
    Wire.begin(IrrigationBoard::I2C_SDA_PIN, IrrigationBoard::I2C_SCL_PIN);

    Esp32Base::setFirmwareInfo("esp32-irrigation-master", "0.1.0");
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::setDeviceName("ESP32 Irrigation");
    Esp32BaseWeb::setHomePath("/irrigation");
    Irrigation::registerWebPages(g_master, g_runtime, g_config, g_history);

    Esp32Base::begin();
    g_config.begin();
    g_history.begin();
    g_master.setOfflineFailureThreshold(g_config.system().offlineFailureThreshold);

    const bool rs485Configured = g_rs485.configure(IrrigationBoard::RS485_RX_PIN,
                                                   IrrigationBoard::RS485_TX_PIN,
                                                   IrrigationBoard::RS485_DIR_PIN,
                                                   g_config.system().rs485Baud,
                                                   SERIAL_8N1,
                                                   IrrigationBoard::RS485_TURNAROUND_DELAY_US);
    if (!rs485Configured || !g_rs485.begin()) {
        ESP32BASE_LOG_E("irrigation", "rs485_begin_failed");
    }
    g_master.begin();
}

void loop() {
    Esp32Base::handle();
    g_master.handle();
    g_runtime.handle();
    delay(5);
}
