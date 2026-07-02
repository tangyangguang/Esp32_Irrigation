#include "app/task_runner.h"

#include "app/status_reporter.h"
#include "board/board_init.h"
#include "drivers/input_monitor.h"
#include "drivers/output_driver.h"

static STC8H_XDATA irrigation_task_request_t g_task;
static STC8H_XDATA irrigation_task_result_t g_last_result;
static stc8h_u8 g_running;
static stc8h_u8 g_closing;
static stc8h_u8 g_task_state;
static stc8h_u8 g_fault_code;
static stc8h_u32 g_started_ms;
static stc8h_u32 g_branch_opened_ms;
static stc8h_u32 g_finish_started_ms;
static stc8h_u32 g_low_active_since_ms;
static stc8h_u8 g_main_hold_applied;
static stc8h_u8 g_branch_hold_applied;
static stc8h_u8 g_branch_opened;
static stc8h_u8 g_pump_started;
static stc8h_u8 g_pending_final_state;

static stc8h_u16 read_le16(const stc8h_u8 *data)
{
    return (stc8h_u16)data[0] | (stc8h_u16)((stc8h_u16)data[1] << 8);
}

static stc8h_u32 read_le32(const stc8h_u8 *data)
{
    return (stc8h_u32)data[0] |
           ((stc8h_u32)data[1] << 8) |
           ((stc8h_u32)data[2] << 16) |
           ((stc8h_u32)data[3] << 24);
}

static void close_task_outputs(void)
{
    output_driver_set_pump(0u);
    output_driver_close_all_valves();
}

static void store_result(stc8h_u8 result_code, stc8h_u8 fault_code)
{
    ++g_last_result.result_seq;
    g_last_result.task_id = g_task.task_id;
    g_last_result.zone_id = g_task.zone_id;
    g_last_result.result_code = result_code;
    g_last_result.fault_code = fault_code;
    g_last_result.planned_duration_sec = g_task.duration_sec;
    g_last_result.actual_duration_sec = task_runner_elapsed_sec();
    g_last_result.flow_pulse_count = input_monitor_flow_pulses();
    g_last_result.stable_pulse_rate = input_monitor_flow_rate();
    g_last_result.input_bits_at_end = input_monitor_bits();
    g_last_result.voltage_mv_at_end = input_monitor_voltage_mv();
}

static stc8h_u32 task_elapsed_ms(void)
{
    if (g_running == 0u && g_closing == 0u) {
        return 0UL;
    }
    return board_millis() - g_started_ms;
}

static stc8h_u32 branch_open_delay_ms(void)
{
    if (g_task.main_valve_enabled == 0u) {
        return 0UL;
    }
    return (stc8h_u32)g_task.main_valve_open_lead_sec * 1000UL;
}

static stc8h_u32 pump_start_delay_ms(void)
{
    return branch_open_delay_ms() + ((stc8h_u32)g_task.pump_start_delay_sec * 1000UL);
}

static void apply_hold_duty_if_ready(void)
{
    const stc8h_u32 elapsed_ms = task_elapsed_ms();

    if (g_task.main_valve_enabled != 0u &&
        g_main_hold_applied == 0u &&
        elapsed_ms >= (stc8h_u32)g_task.main_valve_full_power_ms) {
        (void)output_driver_set_valve(g_task.main_valve_no, g_task.main_valve_hold_duty_permille);
        g_main_hold_applied = 1u;
    }
    if (g_branch_opened != 0u &&
        g_branch_hold_applied == 0u &&
        (board_millis() - g_branch_opened_ms) >= (stc8h_u32)g_task.branch_full_power_ms) {
        (void)output_driver_set_valve(g_task.branch_valve_no, g_task.branch_hold_duty_permille);
        g_branch_hold_applied = 1u;
    }
}

static void apply_start_sequence(void)
{
    const stc8h_u32 elapsed_ms = task_elapsed_ms();

    if (g_branch_opened == 0u && elapsed_ms >= branch_open_delay_ms()) {
        (void)output_driver_set_valve(g_task.branch_valve_no, 1000u);
        if (g_task.branch_full_power_ms == 0u || g_task.branch_hold_duty_permille >= 1000u) {
            (void)output_driver_set_valve(g_task.branch_valve_no, g_task.branch_hold_duty_permille);
            g_branch_hold_applied = 1u;
        }
        g_branch_opened = 1u;
        g_branch_opened_ms = board_millis();
    }
    if (g_task.pump_enabled != 0u &&
        g_pump_started == 0u &&
        elapsed_ms >= pump_start_delay_ms()) {
        output_driver_set_pump(1u);
        g_pump_started = 1u;
    }
}

static void apply_pump_stop_lead(void)
{
    const stc8h_u32 elapsed = task_runner_elapsed_sec();
    stc8h_u32 remaining;

    if (g_task.pump_enabled == 0u || g_pump_started == 0u || elapsed >= g_task.duration_sec) {
        return;
    }
    remaining = g_task.duration_sec - elapsed;
    if (remaining <= (stc8h_u32)g_task.pump_stop_lead_sec) {
        output_driver_set_pump(0u);
        g_pump_started = 0u;
    }
}

static void finish_task(stc8h_u8 result_code, stc8h_u8 fault_code, stc8h_u8 final_state)
{
    output_driver_set_pump(0u);
    output_driver_close_valve(g_task.branch_valve_no);
    g_fault_code = fault_code;
    store_result(result_code, fault_code);
    input_monitor_set_flow_input(0u);
    g_running = 0u;
    g_pump_started = 0u;
    g_branch_opened = 0u;
    g_branch_hold_applied = 0u;
    if (g_task.main_valve_enabled != 0u && g_task.main_valve_close_delay_sec != 0u) {
        g_closing = 1u;
        g_finish_started_ms = board_millis();
        g_pending_final_state = final_state;
        g_task_state = IRR_TASK_STATE_STOPPING;
    } else {
        close_task_outputs();
        g_closing = 0u;
        g_task_state = final_state;
    }
}

static void handle_close_delay(void)
{
    if (g_closing == 0u) {
        return;
    }
    if ((board_millis() - g_finish_started_ms) >= ((stc8h_u32)g_task.main_valve_close_delay_sec * 1000UL)) {
        close_task_outputs();
        g_closing = 0u;
        g_main_hold_applied = 0u;
        g_task_state = g_pending_final_state;
    }
}

static stc8h_u8 decode_start_payload(const stc8h_u8 *payload, stc8h_u8 len, irrigation_task_request_t *out)
{
    if (payload == 0 || out == 0 || len != IRR_START_TASK_PAYLOAD_LEN) {
        return 0u;
    }
    out->task_id = read_le16(&payload[0]);
    out->source_id = payload[2];
    out->zone_id = payload[3];
    out->branch_valve_no = payload[4];
    out->duration_sec = read_le32(&payload[5]);
    out->main_valve_enabled = payload[9];
    out->main_valve_no = payload[10];
    out->pump_enabled = payload[11];
    out->main_valve_open_lead_sec = payload[12];
    out->main_valve_close_delay_sec = payload[13];
    out->pump_start_delay_sec = payload[14];
    out->pump_stop_lead_sec = payload[15];
    out->flow_meter_enabled = payload[16];
    out->flow_input_no = payload[17];
    out->low_level_enabled = payload[18];
    out->low_level_input_no = payload[19];
    out->low_level_active_mode = payload[20];
    out->low_level_debounce_ms = read_le16(&payload[21]);
    out->no_flow_grace_sec = read_le16(&payload[23]);
    out->no_flow_confirm_sec = read_le16(&payload[25]);
    out->low_flow_action = payload[27];
    out->over_flow_action = payload[28];
    out->main_valve_full_power_ms = read_le16(&payload[29]);
    out->main_valve_hold_duty_permille = read_le16(&payload[31]);
    out->branch_full_power_ms = read_le16(&payload[33]);
    out->branch_hold_duty_permille = read_le16(&payload[35]);
    return 1u;
}

static stc8h_u8 task_valid(const irrigation_task_request_t *task)
{
    if (task->task_id == 0u || task->duration_sec == 0UL || task->duration_sec > 86400UL) {
        return 0u;
    }
    if (task->branch_valve_no == 0u || task->branch_valve_no > 8u) {
        return 0u;
    }
    if (task->main_valve_enabled != 0u && (task->main_valve_no == 0u || task->main_valve_no > 8u)) {
        return 0u;
    }
    if (task->flow_meter_enabled != 0u && (task->flow_input_no == 0u || task->flow_input_no > 4u)) {
        return 0u;
    }
    if (task->low_level_enabled != 0u && (task->low_level_input_no == 0u || task->low_level_input_no > 4u)) {
        return 0u;
    }
    if (task->main_valve_hold_duty_permille > 1000u || task->branch_hold_duty_permille > 1000u) {
        return 0u;
    }
    return 1u;
}

void task_runner_init(void)
{
    g_running = 0u;
    g_task_state = IRR_TASK_STATE_NONE;
    g_fault_code = IRR_FAULT_NONE;
    g_last_result.result_seq = 0u;
    g_last_result.result_code = IRR_RESULT_NONE;
    close_task_outputs();
}

void task_runner_handle(void)
{
    if (g_running == 0u) {
        handle_close_delay();
        return;
    }
    apply_start_sequence();
    apply_hold_duty_if_ready();
    apply_pump_stop_lead();
    if (g_task.low_level_enabled != 0u) {
        if (input_monitor_is_active(g_task.low_level_input_no, g_task.low_level_active_mode) != 0u) {
            if (g_low_active_since_ms == 0UL) {
                g_low_active_since_ms = board_millis();
            }
            if ((board_millis() - g_low_active_since_ms) >= (stc8h_u32)g_task.low_level_debounce_ms) {
                finish_task(IRR_RESULT_STOPPED_BY_FAULT, IRR_FAULT_LOW_LEVEL, IRR_TASK_STATE_STOPPED_BY_FAULT);
                return;
            }
        } else {
            g_low_active_since_ms = 0UL;
        }
    }
    if (task_runner_elapsed_sec() >= g_task.duration_sec) {
        finish_task(IRR_RESULT_COMPLETED, IRR_FAULT_NONE, IRR_TASK_STATE_COMPLETED);
    }
}

stc8h_u8 task_runner_start_from_payload(const stc8h_u8 *payload, stc8h_u8 len, stc8h_u16 *accepted_task_id)
{
    irrigation_task_request_t next_task;

    if (g_running != 0u) {
        return 5u;
    }
    if (g_closing != 0u) {
        return 5u;
    }
    if (decode_start_payload(payload, len, &next_task) == 0u || task_valid(&next_task) == 0u) {
        return 3u;
    }

    g_task = next_task;
    g_fault_code = IRR_FAULT_NONE;
    g_started_ms = board_millis();
    g_branch_opened_ms = 0UL;
    g_finish_started_ms = 0UL;
    g_low_active_since_ms = 0UL;
    g_closing = 0u;
    g_main_hold_applied = 0u;
    g_branch_hold_applied = 0u;
    g_branch_opened = 0u;
    g_pump_started = 0u;
    input_monitor_set_flow_input(g_task.flow_meter_enabled ? g_task.flow_input_no : 0u);

    if (g_task.main_valve_enabled != 0u) {
        (void)output_driver_set_valve(g_task.main_valve_no, 1000u);
        if (g_task.main_valve_full_power_ms == 0u || g_task.main_valve_hold_duty_permille >= 1000u) {
            (void)output_driver_set_valve(g_task.main_valve_no, g_task.main_valve_hold_duty_permille);
            g_main_hold_applied = 1u;
        }
    }
    apply_start_sequence();

    g_running = 1u;
    g_task_state = IRR_TASK_STATE_RUNNING;
    if (accepted_task_id != 0) {
        *accepted_task_id = g_task.task_id;
    }
    return 0u;
}

stc8h_u8 task_runner_stop(stc8h_u16 task_id, stc8h_u8 stop_reason, stc8h_u16 *stopped_task_id, stc8h_u8 *task_state)
{
    (void)stop_reason;

    if (g_running == 0u && g_closing == 0u) {
        if (stopped_task_id != 0) {
            *stopped_task_id = 0u;
        }
        if (task_state != 0) {
            *task_state = g_task_state;
        }
        return 0u;
    }
    if (g_closing != 0u) {
        close_task_outputs();
        g_closing = 0u;
        g_task_state = IRR_TASK_STATE_STOPPED_BY_USER;
        if (stopped_task_id != 0) {
            *stopped_task_id = g_task.task_id;
        }
        if (task_state != 0) {
            *task_state = g_task_state;
        }
        return 0u;
    }
    if (task_id != 0u && task_id != g_task.task_id) {
        return 7u;
    }
    finish_task(IRR_RESULT_STOPPED_BY_USER, IRR_FAULT_NONE, IRR_TASK_STATE_STOPPED_BY_USER);
    if (stopped_task_id != 0) {
        *stopped_task_id = g_task.task_id;
    }
    if (task_state != 0) {
        *task_state = g_task_state;
    }
    return 0u;
}

stc8h_u8 task_runner_station_state(void)
{
    if (g_running != 0u) {
        return IRR_STATION_STATE_RUNNING;
    }
    if (g_closing != 0u) {
        return IRR_STATION_STATE_STOPPING;
    }
    if (g_fault_code != IRR_FAULT_NONE) {
        return IRR_STATION_STATE_FAULT;
    }
    return IRR_STATION_STATE_IDLE;
}

stc8h_u8 task_runner_task_state(void)
{
    return g_task_state;
}

stc8h_u8 task_runner_fault_code(void)
{
    return g_fault_code;
}

stc8h_u16 task_runner_active_task_id(void)
{
    return (g_running || g_closing) ? g_task.task_id : 0u;
}

stc8h_u8 task_runner_active_zone_id(void)
{
    return (g_running || g_closing) ? g_task.zone_id : 0u;
}

stc8h_u8 task_runner_active_valve_no(void)
{
    return (g_running || g_closing) ? g_task.branch_valve_no : 0u;
}

stc8h_u32 task_runner_elapsed_sec(void)
{
    if (g_running == 0u && g_closing == 0u) {
        return 0UL;
    }
    return task_elapsed_ms() / 1000UL;
}

stc8h_u32 task_runner_remaining_sec(void)
{
    const stc8h_u32 elapsed = task_runner_elapsed_sec();
    if (g_running == 0u || elapsed >= g_task.duration_sec) {
        return 0UL;
    }
    return g_task.duration_sec - elapsed;
}

const irrigation_task_result_t *task_runner_last_result(void)
{
    return &g_last_result;
}
