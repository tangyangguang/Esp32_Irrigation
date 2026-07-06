#include "drivers/input_monitor.h"

#include "board/board_init.h"
#include "board_pins.h"
#include "stc8h_adc.h"
#include "stc8h_gpio.h"

static stc8h_u8 g_flow_input_no;
static stc8h_u8 g_flow_last_level;
static stc8h_u32 g_flow_pulses;
static stc8h_u32 g_flow_window_pulses;
static stc8h_u32 g_flow_window_started_ms;
static stc8h_u16 g_flow_rate;

static stc8h_u8 read_input_level(stc8h_u8 input_no)
{
    switch (input_no) {
    case 1u:
        return stc8h_gpio_read(BOARD_INPUT1_PORT, BOARD_INPUT1_PIN);
    case 2u:
        return stc8h_gpio_read(BOARD_INPUT2_PORT, BOARD_INPUT2_PIN);
    case 3u:
        return stc8h_gpio_read(BOARD_INPUT3_PORT, BOARD_INPUT3_PIN);
    case 4u:
        return stc8h_gpio_read(BOARD_INPUT4_PORT, BOARD_INPUT4_PIN);
    default:
        return 0u;
    }
}

void input_monitor_init(void)
{
    stc8h_gpio_set_mode(BOARD_INPUT1_PORT, BOARD_INPUT1_PIN, STC8H_GPIO_MODE_INPUT_ONLY);
    stc8h_gpio_set_mode(BOARD_INPUT2_PORT, BOARD_INPUT2_PIN, STC8H_GPIO_MODE_INPUT_ONLY);
    stc8h_gpio_set_mode(BOARD_INPUT3_PORT, BOARD_INPUT3_PIN, STC8H_GPIO_MODE_INPUT_ONLY);
    stc8h_gpio_set_mode(BOARD_INPUT4_PORT, BOARD_INPUT4_PIN, STC8H_GPIO_MODE_INPUT_ONLY);
    stc8h_adc_init();
    input_monitor_set_flow_input(0u);
}

void input_monitor_handle(void)
{
    stc8h_u8 level;
    stc8h_u32 now_ms;
    stc8h_u32 elapsed_ms;

    if (g_flow_input_no == 0u) {
        return;
    }
    level = read_input_level(g_flow_input_no);
    if (level != 0u && g_flow_last_level == 0u) {
        ++g_flow_pulses;
        ++g_flow_window_pulses;
    }
    g_flow_last_level = level;

    now_ms = board_millis();
    elapsed_ms = now_ms - g_flow_window_started_ms;
    if (elapsed_ms >= 1000UL) {
        g_flow_rate = (stc8h_u16)g_flow_window_pulses;
        g_flow_window_pulses = 0UL;
        g_flow_window_started_ms = now_ms;
    }
}

void input_monitor_set_flow_input(stc8h_u8 input_no)
{
    if (input_no > 4u) {
        input_no = 0u;
    }
    g_flow_input_no = input_no;
    input_monitor_reset_flow();
}

void input_monitor_reset_flow(void)
{
    g_flow_pulses = 0UL;
    g_flow_window_pulses = 0UL;
    g_flow_window_started_ms = board_millis();
    g_flow_rate = 0u;
    g_flow_last_level = (g_flow_input_no == 0u) ? 0u : read_input_level(g_flow_input_no);
}

stc8h_u8 input_monitor_bits(void)
{
    stc8h_u8 bits = 0u;

    bits |= read_input_level(1u) ? 0x01u : 0u;
    bits |= read_input_level(2u) ? 0x02u : 0u;
    bits |= read_input_level(3u) ? 0x04u : 0u;
    bits |= read_input_level(4u) ? 0x08u : 0u;
    return bits;
}

stc8h_u8 input_monitor_is_active(stc8h_u8 input_no, stc8h_u8 active_mode)
{
    const stc8h_u8 level = read_input_level(input_no);
    return (active_mode == 0u) ? (level == 0u) : (level != 0u);
}

stc8h_u32 input_monitor_flow_pulses(void)
{
    return g_flow_pulses;
}

stc8h_u16 input_monitor_flow_rate(void)
{
    return g_flow_rate;
}

stc8h_u16 input_monitor_voltage_mv(void)
{
    return 0u;
}
