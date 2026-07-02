#ifndef IRRIGATION_OUTPUT_DRIVER_H
#define IRRIGATION_OUTPUT_DRIVER_H

#include "stc8h_config.h"

void output_driver_safe_off(void);
void output_driver_init(void);
stc8h_status_t output_driver_set_valve(stc8h_u8 valve_no, stc8h_u16 duty_permille);
void output_driver_close_valve(stc8h_u8 valve_no);
void output_driver_close_all_valves(void);
void output_driver_set_pump(stc8h_u8 enabled);

#endif
