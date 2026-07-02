#include "stc8h_sfr.h"

#define BOARD_RS485_TX_ENABLE() do { P4 |= BOARD_RS485_DIR_MASK; } while (0)
#define BOARD_RS485_RX_ENABLE() do { P4 &= (stc8h_u8)~BOARD_RS485_DIR_MASK; } while (0)

#include "/Users/tyg/dir/codex_dir/Stc8hBase/drivers/drv_rs485_uart.c"

