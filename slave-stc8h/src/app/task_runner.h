#ifndef IRRIGATION_TASK_RUNNER_H
#define IRRIGATION_TASK_RUNNER_H

#include "stc8h_config.h"

#define IRR_START_TASK_PAYLOAD_LEN 37u

typedef struct {
    stc8h_u16 task_id;
    stc8h_u8 source_id;
    stc8h_u8 zone_id;
    stc8h_u8 branch_valve_no;
    stc8h_u32 duration_sec;
    stc8h_u8 main_valve_enabled;
    stc8h_u8 main_valve_no;
    stc8h_u8 pump_enabled;
    stc8h_u8 main_valve_open_lead_sec;
    stc8h_u8 main_valve_close_delay_sec;
    stc8h_u8 pump_start_delay_sec;
    stc8h_u8 pump_stop_lead_sec;
    stc8h_u8 flow_meter_enabled;
    stc8h_u8 flow_input_no;
    stc8h_u8 low_level_enabled;
    stc8h_u8 low_level_input_no;
    stc8h_u8 low_level_active_mode;
    stc8h_u16 low_level_debounce_ms;
    stc8h_u16 no_flow_grace_sec;
    stc8h_u16 no_flow_confirm_sec;
    stc8h_u8 low_flow_action;
    stc8h_u8 over_flow_action;
    stc8h_u16 main_valve_full_power_ms;
    stc8h_u16 main_valve_hold_duty_permille;
    stc8h_u16 branch_full_power_ms;
    stc8h_u16 branch_hold_duty_permille;
} irrigation_task_request_t;

typedef struct {
    stc8h_u16 result_seq;
    stc8h_u16 task_id;
    stc8h_u8 zone_id;
    stc8h_u8 result_code;
    stc8h_u8 fault_code;
    stc8h_u32 planned_duration_sec;
    stc8h_u32 actual_duration_sec;
    stc8h_u32 flow_pulse_count;
    stc8h_u16 stable_pulse_rate;
    stc8h_u8 input_bits_at_end;
    stc8h_u16 voltage_mv_at_end;
} irrigation_task_result_t;

void task_runner_init(void);
void task_runner_handle(void);
stc8h_u8 task_runner_start_from_payload(const stc8h_u8 *payload, stc8h_u8 len, stc8h_u16 *accepted_task_id);
stc8h_u8 task_runner_stop(stc8h_u16 task_id, stc8h_u8 stop_reason, stc8h_u16 *stopped_task_id, stc8h_u8 *task_state);
stc8h_u8 task_runner_station_state(void);
stc8h_u8 task_runner_task_state(void);
stc8h_u8 task_runner_fault_code(void);
stc8h_u16 task_runner_active_task_id(void);
stc8h_u8 task_runner_active_zone_id(void);
stc8h_u8 task_runner_active_valve_no(void);
stc8h_u32 task_runner_elapsed_sec(void);
stc8h_u32 task_runner_remaining_sec(void);
const irrigation_task_result_t *task_runner_last_result(void);

#endif
