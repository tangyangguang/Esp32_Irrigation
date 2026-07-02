#include "comm/command_handler.h"

#include "app/status_reporter.h"
#include "app/task_runner.h"
#include "board/board_init.h"
#include "board_pins.h"
#include "comm/irrigation_frame.h"
#include "drv_rs485_uart.h"

static STC8H_XDATA irrigation_frame_parser_t g_parser;

static void send_response(const irrigation_frame_t *request,
                          stc8h_u8 flags,
                          const stc8h_u8 *payload,
                          stc8h_u8 payload_len)
{
    stc8h_u8 tx[IRR_FRAME_MAX_SIZE];
    stc8h_u8 len;

    len = irrigation_frame_encode(request->addr,
                                  request->seq,
                                  request->cmd,
                                  (stc8h_u8)(flags | IRR_FLAG_RESPONSE),
                                  payload,
                                  payload_len,
                                  tx,
                                  (stc8h_u8)sizeof(tx));
    if (len != 0u) {
        (void)drv_rs485_uart_write(BOARD_RS485_UART, tx, len);
    }
}

static void send_error(const irrigation_frame_t *request, stc8h_u8 error_code)
{
    stc8h_u8 payload[1];
    payload[0] = error_code;
    send_response(request, IRR_FLAG_ERROR, payload, 1u);
}

static void handle_request(const irrigation_frame_t *frame)
{
    stc8h_u8 payload[IRR_FRAME_MAX_PAYLOAD];
    stc8h_u8 payload_len = 0u;
    stc8h_u8 status;
    stc8h_u16 task_id;
    stc8h_u8 task_state;

    if (frame->addr != board_station_address()) {
        return;
    }
    if ((frame->flags & IRR_FLAG_RESPONSE) != 0u) {
        return;
    }

    if (frame->cmd == IRR_CMD_PING) {
        irrigation_write_le32(payload, board_uptime_sec());
        send_response(frame, 0u, payload, 4u);
    } else if (frame->cmd == IRR_CMD_GET_INFO) {
        status_reporter_get_info(payload, &payload_len);
        send_response(frame, 0u, payload, payload_len);
    } else if (frame->cmd == IRR_CMD_GET_STATUS) {
        status_reporter_get_status(payload, &payload_len);
        send_response(frame, 0u, payload, payload_len);
    } else if (frame->cmd == IRR_CMD_START_TASK) {
        status = task_runner_start_from_payload(frame->payload, frame->len, &task_id);
        if (status == 0u) {
            payload[0] = (stc8h_u8)task_id;
            payload[1] = (stc8h_u8)(task_id >> 8);
            send_response(frame, 0u, payload, 2u);
        } else if (status == IRR_ERR_BUSY) {
            send_error(frame, IRR_ERR_BUSY);
        } else {
            send_error(frame, status);
        }
    } else if (frame->cmd == IRR_CMD_STOP_TASK) {
        if (frame->len != 3u) {
            send_error(frame, IRR_ERR_BAD_LENGTH);
            return;
        }
        task_id = (stc8h_u16)frame->payload[0] | (stc8h_u16)((stc8h_u16)frame->payload[1] << 8);
        status = task_runner_stop(task_id, frame->payload[2], &task_id, &task_state);
        if (status == 0u) {
            payload[0] = (stc8h_u8)task_id;
            payload[1] = (stc8h_u8)(task_id >> 8);
            payload[2] = task_state;
            send_response(frame, 0u, payload, 3u);
        } else {
            send_error(frame, status);
        }
    } else if (frame->cmd == IRR_CMD_GET_LAST_RESULT) {
        status_reporter_get_last_result(payload, &payload_len);
        send_response(frame, 0u, payload, payload_len);
    } else if (frame->cmd == IRR_CMD_GET_INPUTS) {
        status_reporter_get_inputs(payload, &payload_len);
        send_response(frame, 0u, payload, payload_len);
    } else {
        send_error(frame, IRR_ERR_BAD_COMMAND);
    }
}

void command_handler_init(void)
{
    irrigation_frame_parser_reset(&g_parser);
    (void)drv_rs485_uart_init(BOARD_RS485_UART);
}

void command_handler_handle(void)
{
    while (drv_rs485_uart_readable(BOARD_RS485_UART) != 0u) {
        irrigation_frame_t frame;
        const stc8h_u8 byte = drv_rs485_uart_getc(BOARD_RS485_UART);
        if (irrigation_frame_parser_feed(&g_parser, byte, &frame) != 0u) {
            handle_request(&frame);
        }
    }
}
