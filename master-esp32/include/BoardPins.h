#ifndef IRRIGATION_BOARD_PINS_H
#define IRRIGATION_BOARD_PINS_H

#include <stdint.h>

namespace IrrigationBoard {
constexpr int8_t RS485_RX_PIN = 16;
constexpr int8_t RS485_TX_PIN = 17;
constexpr int8_t RS485_DIR_PIN = 13;
constexpr int8_t I2C_SDA_PIN = 21;
constexpr int8_t I2C_SCL_PIN = 22;

constexpr uint32_t RS485_BAUD = 115200;
constexpr uint32_t RS485_TURNAROUND_DELAY_US = 50;
constexpr uint8_t STATION_ADDR_MIN = 1;
constexpr uint8_t STATION_ADDR_MAX = 15;
}  // namespace IrrigationBoard

#endif

