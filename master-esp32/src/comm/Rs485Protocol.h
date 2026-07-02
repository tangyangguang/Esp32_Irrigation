#ifndef IRRIGATION_RS485_PROTOCOL_H
#define IRRIGATION_RS485_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace Irrigation {

constexpr uint8_t FRAME_MAGIC0 = 0xA5;
constexpr uint8_t FRAME_MAGIC1 = 0x5A;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t FRAME_MAX_PAYLOAD = 64;
constexpr size_t FRAME_MAX_SIZE = 2 + 6 + FRAME_MAX_PAYLOAD + 2;

constexpr uint8_t FLAG_RESPONSE = 0x01;
constexpr uint8_t FLAG_ERROR = 0x02;
constexpr uint8_t FLAG_BUSY = 0x04;

enum Command : uint8_t {
    CMD_PING = 0x01,
    CMD_GET_STATUS = 0x02,
    CMD_START_TASK = 0x03,
    CMD_STOP_TASK = 0x04,
    CMD_GET_LAST_RESULT = 0x05,
    CMD_GET_INPUTS = 0x06,
    CMD_GET_INFO = 0x07,
};

enum ErrorCode : uint8_t {
    ERR_BAD_COMMAND = 1,
    ERR_BAD_LENGTH = 2,
    ERR_BAD_PAYLOAD = 3,
    ERR_BAD_CRC = 4,
    ERR_BUSY = 5,
    ERR_INVALID_STATE = 6,
    ERR_INVALID_TASK = 7,
    ERR_UNSUPPORTED_VERSION = 8,
    ERR_INTERNAL = 9,
};

struct Frame {
    uint8_t addr = 0;
    uint8_t seq = 0;
    uint8_t cmd = 0;
    uint8_t flags = 0;
    uint8_t len = 0;
    uint8_t payload[FRAME_MAX_PAYLOAD] = {};
};

class FrameParser {
public:
    bool feed(uint8_t byte, Frame& frame);
    void reset();

private:
    uint8_t _buffer[FRAME_MAX_SIZE] = {};
    size_t _len = 0;
};

uint16_t crc16Modbus(const uint8_t* data, size_t len);
size_t encodeFrame(uint8_t addr,
                   uint8_t seq,
                   uint8_t cmd,
                   uint8_t flags,
                   const uint8_t* payload,
                   uint8_t payloadLen,
                   uint8_t* out,
                   size_t outLen);
bool decodeFrame(const uint8_t* data, size_t len, Frame& frame);
uint32_t readLe32(const uint8_t* data);
uint16_t readLe16(const uint8_t* data);
void writeLe32(uint8_t* data, uint32_t value);
void writeLe16(uint8_t* data, uint16_t value);

}  // namespace Irrigation

#endif
