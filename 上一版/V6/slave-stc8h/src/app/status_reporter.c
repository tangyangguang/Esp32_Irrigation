#include "app/status_reporter.h"

#include "app/task_runner.h"
#include "drivers/input_monitor.h"
#include "comm/irrigation_frame.h"

static void write_le16(stc8h_u8 *out, stc8h_u16 value)
{
    out[0] = (stc8h_u8)value;
    out[1] = (stc8h_u8)(value >> 8);
}

static void write_le32(stc8h_u8 *out, stc8h_u32 value)
{
    out[0] = (stc8h_u8)value;
    out[1] = (stc8h_u8)(value >> 8);
    out[2] = (stc8h_u8)(value >> 16);
    out[3] = (stc8h_u8)(value >> 24);
}

void status_reporter_get_info(stc8h_u8 *payload, stc8h_u8 *len)
{
    if (payload == 0 || len == 0) {
        return;
    }
    payload[0] = 1u;
    payload[1] = 1u;
    payload[2] = 8u;
    payload[3] = 4u;
    payload[4] = 0x1Fu;
    payload[5] = 0x00u;
    payload[6] = 0u;
    payload[7] = 1u;
    payload[8] = 0u;
    *len = 9u;
}

void status_reporter_get_status(stc8h_u8 *payload, stc8h_u8 *len)
{
    if (payload == 0 || len == 0) {
        return;
    }
    payload[0] = task_runner_station_state();
    payload[1] = task_runner_task_state();
    payload[2] = task_runner_fault_code();
    write_le16(&payload[3], task_runner_active_task_id());
    payload[5] = task_runner_active_zone_id();
    payload[6] = task_runner_active_valve_no();
    write_le32(&payload[7], task_runner_elapsed_sec());
    write_le32(&payload[11], task_runner_remaining_sec());
    write_le32(&payload[15], input_monitor_flow_pulses());
    write_le16(&payload[19], input_monitor_flow_rate());
    payload[21] = input_monitor_bits();
    write_le16(&payload[22], input_monitor_voltage_mv());
    write_le16(&payload[24], task_runner_last_result()->result_seq);
    *len = 26u;
}

void status_reporter_get_inputs(stc8h_u8 *payload, stc8h_u8 *len)
{
    if (payload == 0 || len == 0) {
        return;
    }
    payload[0] = input_monitor_bits();
    write_le32(&payload[1], input_monitor_flow_pulses());
    write_le16(&payload[5], input_monitor_flow_rate());
    write_le16(&payload[7], input_monitor_voltage_mv());
    *len = 9u;
}

void status_reporter_get_last_result(stc8h_u8 *payload, stc8h_u8 *len)
{
    const irrigation_task_result_t *result;

    if (payload == 0 || len == 0) {
        return;
    }
    result = task_runner_last_result();
    write_le16(&payload[0], result->result_seq);
    write_le16(&payload[2], result->task_id);
    payload[4] = result->zone_id;
    payload[5] = result->result_code;
    payload[6] = result->fault_code;
    write_le32(&payload[7], result->planned_duration_sec);
    write_le32(&payload[11], result->actual_duration_sec);
    write_le32(&payload[15], result->flow_pulse_count);
    write_le16(&payload[19], result->stable_pulse_rate);
    payload[21] = result->input_bits_at_end;
    write_le16(&payload[22], result->voltage_mv_at_end);
    *len = 24u;
}
