#ifndef IRRIGATION_FRAME_H
#define IRRIGATION_FRAME_H

#include "stc8h_config.h"

#define IRR_FRAME_MAGIC0 0xA5u
#define IRR_FRAME_MAGIC1 0x5Au
#define IRR_PROTOCOL_VERSION 1u
#define IRR_FRAME_MAX_PAYLOAD 64u
#define IRR_FRAME_MAX_SIZE 76u

#define IRR_FLAG_RESPONSE 0x01u
#define IRR_FLAG_ERROR    0x02u
#define IRR_FLAG_BUSY     0x04u

#define IRR_CMD_PING            0x01u
#define IRR_CMD_GET_STATUS      0x02u
#define IRR_CMD_START_TASK      0x03u
#define IRR_CMD_STOP_TASK       0x04u
#define IRR_CMD_GET_LAST_RESULT 0x05u
#define IRR_CMD_GET_INPUTS      0x06u
#define IRR_CMD_GET_INFO        0x07u

#define IRR_ERR_BAD_COMMAND        1u
#define IRR_ERR_BAD_LENGTH         2u
#define IRR_ERR_BAD_PAYLOAD        3u
#define IRR_ERR_BAD_CRC            4u
#define IRR_ERR_BUSY               5u
#define IRR_ERR_INVALID_STATE      6u
#define IRR_ERR_INVALID_TASK       7u
#define IRR_ERR_UNSUPPORTED_VERSION 8u
#define IRR_ERR_INTERNAL           9u

typedef struct {
    stc8h_u8 addr;
    stc8h_u8 seq;
    stc8h_u8 cmd;
    stc8h_u8 flags;
    stc8h_u8 len;
    stc8h_u8 payload[IRR_FRAME_MAX_PAYLOAD];
} irrigation_frame_t;

typedef struct {
    stc8h_u8 buffer[IRR_FRAME_MAX_SIZE];
    stc8h_u8 len;
} irrigation_frame_parser_t;

void irrigation_frame_parser_reset(STC8H_XDATA irrigation_frame_parser_t *parser);
stc8h_u8 irrigation_frame_parser_feed(STC8H_XDATA irrigation_frame_parser_t *parser, stc8h_u8 byte, irrigation_frame_t *frame);
stc8h_u8 irrigation_frame_encode(stc8h_u8 addr,
                                 stc8h_u8 seq,
                                 stc8h_u8 cmd,
                                 stc8h_u8 flags,
                                 const stc8h_u8 *payload,
                                 stc8h_u8 payload_len,
                                 stc8h_u8 *out,
                                 stc8h_u8 out_len);
void irrigation_write_le32(stc8h_u8 *out, stc8h_u32 value);

#endif
