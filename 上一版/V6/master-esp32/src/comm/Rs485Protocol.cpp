#include "comm/Rs485Protocol.h"

#include <string.h>

namespace Irrigation {

uint16_t crc16Modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001) != 0) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

size_t encodeFrame(uint8_t addr,
                   uint8_t seq,
                   uint8_t cmd,
                   uint8_t flags,
                   const uint8_t* payload,
                   uint8_t payloadLen,
                   uint8_t* out,
                   size_t outLen) {
    if (!out || payloadLen > FRAME_MAX_PAYLOAD || outLen < static_cast<size_t>(payloadLen + 10)) {
        return 0;
    }
    if (payloadLen > 0 && !payload) {
        return 0;
    }

    out[0] = FRAME_MAGIC0;
    out[1] = FRAME_MAGIC1;
    out[2] = PROTOCOL_VERSION;
    out[3] = addr;
    out[4] = seq;
    out[5] = cmd;
    out[6] = flags;
    out[7] = payloadLen;
    if (payloadLen > 0) {
        memcpy(&out[8], payload, payloadLen);
    }
    const uint16_t crc = crc16Modbus(&out[2], static_cast<size_t>(6 + payloadLen));
    out[8 + payloadLen] = static_cast<uint8_t>(crc);
    out[9 + payloadLen] = static_cast<uint8_t>(crc >> 8);
    return static_cast<size_t>(10 + payloadLen);
}

bool decodeFrame(const uint8_t* data, size_t len, Frame& frame) {
    if (!data || len < 10 || data[0] != FRAME_MAGIC0 || data[1] != FRAME_MAGIC1) {
        return false;
    }
    const uint8_t payloadLen = data[7];
    if (payloadLen > FRAME_MAX_PAYLOAD || len != static_cast<size_t>(10 + payloadLen)) {
        return false;
    }
    if (data[2] != PROTOCOL_VERSION) {
        return false;
    }
    const uint16_t got = static_cast<uint16_t>(data[8 + payloadLen]) |
                         static_cast<uint16_t>(data[9 + payloadLen] << 8);
    const uint16_t expected = crc16Modbus(&data[2], static_cast<size_t>(6 + payloadLen));
    if (got != expected) {
        return false;
    }

    frame.addr = data[3];
    frame.seq = data[4];
    frame.cmd = data[5];
    frame.flags = data[6];
    frame.len = payloadLen;
    if (payloadLen > 0) {
        memcpy(frame.payload, &data[8], payloadLen);
    }
    return true;
}

bool FrameParser::feed(uint8_t byte, Frame& frame) {
    if (_len == 0 && byte != FRAME_MAGIC0) {
        return false;
    }
    if (_len == 1 && byte != FRAME_MAGIC1) {
        _len = (byte == FRAME_MAGIC0) ? 1 : 0;
        _buffer[0] = FRAME_MAGIC0;
        return false;
    }
    if (_len >= FRAME_MAX_SIZE) {
        reset();
        return false;
    }

    _buffer[_len++] = byte;
    if (_len >= 8) {
        const uint8_t payloadLen = _buffer[7];
        if (payloadLen > FRAME_MAX_PAYLOAD) {
            reset();
            return false;
        }
        const size_t expectedLen = static_cast<size_t>(10 + payloadLen);
        if (_len == expectedLen) {
            const bool ok = decodeFrame(_buffer, _len, frame);
            reset();
            return ok;
        }
    }
    return false;
}

void FrameParser::reset() {
    _len = 0;
}

uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}

void writeLe32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
    data[2] = static_cast<uint8_t>(value >> 16);
    data[3] = static_cast<uint8_t>(value >> 24);
}

void writeLe16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
}

}  // namespace Irrigation
