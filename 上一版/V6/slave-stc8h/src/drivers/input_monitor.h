#ifndef IRRIGATION_INPUT_MONITOR_H
#define IRRIGATION_INPUT_MONITOR_H

#include "stc8h_config.h"

void input_monitor_init(void);
void input_monitor_handle(void);
void input_monitor_set_flow_input(stc8h_u8 input_no);
void input_monitor_reset_flow(void);
stc8h_u8 input_monitor_bits(void);
stc8h_u8 input_monitor_is_active(stc8h_u8 input_no, stc8h_u8 active_mode);
stc8h_u32 input_monitor_flow_pulses(void);
stc8h_u16 input_monitor_flow_rate(void);
stc8h_u16 input_monitor_voltage_mv(void);

#endif
