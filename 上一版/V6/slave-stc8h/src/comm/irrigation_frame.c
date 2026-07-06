#include "comm/irrigation_frame.h"

#include "util_crc.h"

static stc8h_u8 decode_frame(const STC8H_XDATA stc8h_u8 *data, stc8h_u8 len, irrigation_frame_t *frame)
{
    stc8h_u8 payload_len;
    stc8h_u16 got;
    stc8h_u16 expected;
    stc8h_u8 i;

    if (data == 0 || frame == 0 || len < 10u) {
        return 0u;
    }
    if (data[0] != IRR_FRAME_MAGIC0 || data[1] != IRR_FRAME_MAGIC1) {
        return 0u;
    }
    if (data[2] != IRR_PROTOCOL_VERSION) {
        return 0u;
    }
    payload_len = data[7];
    if (payload_len > IRR_FRAME_MAX_PAYLOAD || len != (stc8h_u8)(10u + payload_len)) {
        return 0u;
    }
    got = (stc8h_u16)data[8u + payload_len] | (stc8h_u16)((stc8h_u16)data[9u + payload_len] << 8);
    expected = util_crc16_modbus_xdata(&data[2], (stc8h_u16)(6u + payload_len));
    if (got != expected) {
        return 0u;
    }

    frame->addr = data[3];
    frame->seq = data[4];
    frame->cmd = data[5];
    frame->flags = data[6];
    frame->len = payload_len;
    for (i = 0u; i < payload_len; ++i) {
        frame->payload[i] = data[8u + i];
    }
    return 1u;
}

void irrigation_frame_parser_reset(STC8H_XDATA irrigation_frame_parser_t *parser)
{
    if (parser != 0) {
        parser->len = 0u;
    }
}

stc8h_u8 irrigation_frame_parser_feed(STC8H_XDATA irrigation_frame_parser_t *parser, stc8h_u8 byte, irrigation_frame_t *frame)
{
    stc8h_u8 payload_len;
    stc8h_u8 expected_len;

    if (parser == 0 || frame == 0) {
        return 0u;
    }
    if (parser->len == 0u && byte != IRR_FRAME_MAGIC0) {
        return 0u;
    }
    if (parser->len == 1u && byte != IRR_FRAME_MAGIC1) {
        parser->len = (byte == IRR_FRAME_MAGIC0) ? 1u : 0u;
        parser->buffer[0] = IRR_FRAME_MAGIC0;
        return 0u;
    }
    if (parser->len >= IRR_FRAME_MAX_SIZE) {
        irrigation_frame_parser_reset(parser);
        return 0u;
    }
    parser->buffer[parser->len++] = byte;
    if (parser->len >= 8u) {
        payload_len = parser->buffer[7];
        if (payload_len > IRR_FRAME_MAX_PAYLOAD) {
            irrigation_frame_parser_reset(parser);
            return 0u;
        }
        expected_len = (stc8h_u8)(10u + payload_len);
        if (parser->len == expected_len) {
            const stc8h_u8 ok = decode_frame(parser->buffer, parser->len, frame);
            irrigation_frame_parser_reset(parser);
            return ok;
        }
    }
    return 0u;
}

stc8h_u8 irrigation_frame_encode(stc8h_u8 addr,
                                 stc8h_u8 seq,
                                 stc8h_u8 cmd,
                                 stc8h_u8 flags,
                                 const stc8h_u8 *payload,
                                 stc8h_u8 payload_len,
                                 stc8h_u8 *out,
                                 stc8h_u8 out_len)
{
    stc8h_u16 crc;
    stc8h_u8 i;

    if (out == 0 || payload_len > IRR_FRAME_MAX_PAYLOAD || out_len < (stc8h_u8)(10u + payload_len)) {
        return 0u;
    }
    if (payload_len > 0u && payload == 0) {
        return 0u;
    }
    out[0] = IRR_FRAME_MAGIC0;
    out[1] = IRR_FRAME_MAGIC1;
    out[2] = IRR_PROTOCOL_VERSION;
    out[3] = addr;
    out[4] = seq;
    out[5] = cmd;
    out[6] = flags;
    out[7] = payload_len;
    for (i = 0u; i < payload_len; ++i) {
        out[8u + i] = payload[i];
    }
    crc = util_crc16_modbus(&out[2], (stc8h_u16)(6u + payload_len));
    out[8u + payload_len] = (stc8h_u8)crc;
    out[9u + payload_len] = (stc8h_u8)(crc >> 8);
    return (stc8h_u8)(10u + payload_len);
}

void irrigation_write_le32(stc8h_u8 *out, stc8h_u32 value)
{
    out[0] = (stc8h_u8)value;
    out[1] = (stc8h_u8)(value >> 8);
    out[2] = (stc8h_u8)(value >> 16);
    out[3] = (stc8h_u8)(value >> 24);
}
