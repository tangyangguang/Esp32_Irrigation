#ifndef IRRIGATION_BOARD_INIT_H
#define IRRIGATION_BOARD_INIT_H

#include "stc8h_config.h"

void board_init_safe_state(void);
void board_init_used_peripherals(void);
stc8h_u8 board_station_address(void);
stc8h_u32 board_uptime_sec(void);
stc8h_u32 board_millis(void);

#endif

